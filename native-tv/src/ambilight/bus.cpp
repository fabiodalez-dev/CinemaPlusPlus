#include "ambilight/bus.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace nativetv {
namespace ambilight {
namespace {
const char *kScript = R"JS(
var pb = require(process.argv[2]);
var h = new pb.Handle('', false);
var subs = {};
function out(o) { process.stdout.write(JSON.stringify(o) + '\n'); }
require('readline').createInterface({input: process.stdin}).on('line', function (line) {
  var m;
  try { m = JSON.parse(line); } catch (e) { return; }
  if (m.op === 'cancel') {
    if (subs[m.id]) { subs[m.id].cancel(); delete subs[m.id]; }
    return;
  }
  var payload = JSON.stringify(m.payload || {});
  if (m.op === 'sub') {
    var s = h.subscribe(m.uri, payload);
    subs[m.id] = s;
    s.addListener('response', function (r) {
      var reply; try { reply = JSON.parse(r.payload()); } catch (e) { reply = {}; }
      out({id: m.id, reply: reply});
    });
    s.addListener('cancel', function () { delete subs[m.id]; out({id: m.id, end: true}); });
    return;
  }
  var c = h.call(m.uri, payload), done = false;
  c.addListener('response', function (r) {
    if (done) return;
    done = true;
    var reply; try { reply = JSON.parse(r.payload()); } catch (e) { reply = {}; }
    out({id: m.id, reply: reply});
    c.cancel();
  });
}).on('close', function () { process.exit(0); });
)JS";
} // namespace

NodeBus::NodeBus(std::string runDirectory, std::string palmbus)
    : runDirectory_(std::move(runDirectory)), palmbus_(std::move(palmbus)) {}

NodeBus::~NodeBus() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (in_ >= 0) close(in_);
        in_ = -1;
        if (pid_ > 0) kill(pid_, SIGTERM);
    }
    if (reader_.joinable()) reader_.join();
}

bool NodeBus::ensure() {
    if (pid_ > 0 && in_ >= 0) return true;
    if (reader_.joinable()) {
        // The old reader ended with its process.
        mutex_.unlock();
        reader_.join();
        mutex_.lock();
    }
    const std::string script = runDirectory_ + "/bus.js";
    {
        FILE *f = std::fopen(script.c_str(), "w");
        if (!f) return false;
        std::fputs(kScript, f);
        std::fclose(f);
        chmod(script.c_str(), 0600);
    }
    int toChild[2], fromChild[2];
    if (pipe(toChild) != 0) return false;
    if (pipe(fromChild) != 0) {
        close(toChild[0]);
        close(toChild[1]);
        return false;
    }
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        dup2(toChild[0], 0);
        dup2(fromChild[1], 1);
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, 2);
        for (int fd = 3; fd < 256; ++fd) close(fd);
        execl("/usr/bin/node", "node", script.c_str(), palmbus_.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    close(toChild[0]);
    close(fromChild[1]);
    fcntl(toChild[1], F_SETFD, FD_CLOEXEC);
    fcntl(fromChild[0], F_SETFD, FD_CLOEXEC);
    pid_ = pid;
    in_ = toChild[1];
    reader_ = std::thread(&NodeBus::read, this, fromChild[0], pid);
    return true;
}

bool NodeBus::send(const Json &message) {
    const std::string line = message.dump(false) + "\n";
    size_t done = 0;
    while (done < line.size()) {
        const ssize_t n = write(in_, line.data() + done, line.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += size_t(n);
    }
    return true;
}

void NodeBus::read(int fd, pid_t pid) {
    std::string buffer;
    char chunk[4096];
    for (;;) {
        const ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        buffer.append(chunk, size_t(n));
        size_t newline;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            const std::string line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            Json message;
            try {
                message = Json::parse(line);
            } catch (...) {
                continue;
            }
            if (!message["id"].isNumber()) continue;
            const int id = int(message["id"].number());
            std::function<void(const Json &)> onMessage;
            std::function<void()> onEnd;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto w = waiting_.find(id);
                if (w != waiting_.end()) {
                    w->second->done = true;
                    w->second->reply = message["reply"];
                    waiting_.erase(w);
                    replied_.notify_all();
                    continue;
                }
                auto s = subs_.find(id);
                if (s == subs_.end()) continue;
                if (message["end"].boolean()) {
                    onEnd = s->second.onEnd;
                    subs_.erase(s);
                } else {
                    onMessage = s->second.onMessage;
                }
            }
            // Outside the lock: a handler may call the bus.
            if (onMessage) onMessage(message["reply"]);
            if (onEnd) onEnd();
        }
    }
    close(fd);
    int status = 0;
    waitpid(pid, &status, 0);
    std::vector<std::function<void()>> ends;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pid_ == pid) {
            pid_ = -1;
            if (in_ >= 0) close(in_);
            in_ = -1;
        }
        for (auto &w : waiting_) w.second->done = true;
        waiting_.clear();
        for (auto &s : subs_)
            if (s.second.onEnd) ends.push_back(s.second.onEnd);
        subs_.clear();
        replied_.notify_all();
    }
    for (auto &end : ends) end();
}

Json NodeBus::call(const std::string &uri, const Json &payload, int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!ensure()) return Json();
    Waiter waiter;
    const int id = ++nextId_;
    waiting_[id] = &waiter;
    Json message = Json::object();
    message.set("op", Json::text("call"));
    message.set("id", Json::numeric(id));
    message.set("uri", Json::text(uri));
    message.set("payload", payload);
    if (!send(message)) {
        waiting_.erase(id);
        return Json();
    }
    replied_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&waiter]() { return waiter.done; });
    waiting_.erase(id);
    return waiter.reply;
}

int NodeBus::subscribe(const std::string &uri, const Json &payload,
                       std::function<void(const Json &)> onMessage, std::function<void()> onEnd) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!ensure()) return 0;
    const int id = ++nextId_;
    subs_[id] = Sub{std::move(onMessage), std::move(onEnd)};
    Json message = Json::object();
    message.set("op", Json::text("sub"));
    message.set("id", Json::numeric(id));
    message.set("uri", Json::text(uri));
    message.set("payload", payload);
    if (!send(message)) {
        subs_.erase(id);
        return 0;
    }
    return id;
}

void NodeBus::cancel(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!subs_.erase(id) || in_ < 0) return;
    Json message = Json::object();
    message.set("op", Json::text("cancel"));
    message.set("id", Json::numeric(id));
    (void)send(message);
}

} // namespace ambilight
} // namespace nativetv
