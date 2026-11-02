#include "daemon/cinema_update.hpp"
#include "core/sha256.hpp"
#include <cctype>
#include <cstdio>
#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cstdlib>
#include <unistd.h>

namespace nativetv {
namespace daemon {
namespace {
const char *kReleases = "https://github.com/fabiodalez-dev/CinemaPlusPlus/releases/";
constexpr size_t kManifestBytes = 4096;
constexpr size_t kPackageBytes = 96 * 1024 * 1024;

struct Sink {
    std::string *body;
    FILE *file;
    size_t written, max;
};

size_t collect(char *data, size_t size, size_t count, void *user) {
    Sink *sink = static_cast<Sink *>(user);
    const size_t n = size * count;
    if (sink->written + n > sink->max) return 0;
    sink->written += n;
    if (sink->file) return std::fwrite(data, 1, n, sink->file);
    sink->body->append(data, n);
    return n;
}

bool curlFetch(const std::string &url, std::string &body, const std::string &toFile, size_t maxBytes,
               std::string &error) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        error = "no HTTP client";
        return false;
    }
    Sink sink = {&body, toFile.empty() ? nullptr : std::fopen(toFile.c_str(), "wb"), 0, maxBytes};
    if (!toFile.empty() && !sink.file) {
        curl_easy_cleanup(curl);
        error = "could not write the download";
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 8000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, toFile.empty() ? 15000L : 600000L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "CinemaPlusPlus-update");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (sink.file) std::fclose(sink.file);
    if (code != CURLE_OK || status != 200) {
        error = status == 404 ? "no release published yet" : "GitHub did not answer";
        return false;
    }
    return true;
}

bool detached(const std::string &command) {
    const pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        // A grandchild in a session of its own: the install restarts this service.
        setsid();
        if (fork() != 0) _exit(0);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    int status = 0;
    (void)waitpid(child, &status, 0);
    return true;
}

std::string readTrimmed(const std::string &path) {
    std::ifstream file(path.c_str());
    std::string text;
    std::getline(file, text);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    return text;
}

bool version(const std::string &text, int parts[3]) {
    return std::sscanf(text.c_str(), "%d.%d.%d", &parts[0], &parts[1], &parts[2]) == 3;
}

std::string fileSha256(const std::string &path) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) return "";
    crypto::Sha256 hash;
    char buffer[65536];
    while (file) {
        file.read(buffer, sizeof buffer);
        if (file.gcount() > 0) hash.update(buffer, static_cast<size_t>(file.gcount()));
    }
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    for (uint8_t byte : hash.finish()) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 15]);
    }
    return hex;
}
} // namespace

bool CinemaUpdate::newer(const std::string &candidate, const std::string &current) {
    int a[3], b[3];
    if (!version(candidate, a)) return false;
    if (!version(current, b)) return true;
    for (int i = 0; i < 3; ++i)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

bool CinemaUpdate::parseManifest(const std::string &body, Json &manifest) {
    manifest = Json::parse(body);
    int parts[3];
    const std::string sha = manifest["sha256"].string();
    const std::string bundle = manifest["bundle"].string();
    if (manifest["id"].string() != "cinemapp" || !version(manifest["version"].string(), parts) ||
        sha.size() != 64 || bundle != "cinemapp-" + manifest["version"].string() + ".tar")
        return false;
    for (char c : sha)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

CinemaUpdate::CinemaUpdate(std::string installRoot, Fetch fetch, Launch launch)
    : root_(std::move(installRoot)), fetch_(fetch ? fetch : Fetch(curlFetch)),
      launch_(launch ? launch : Launch(detached)) {}

CinemaUpdate::~CinemaUpdate() {
    wait();
}

void CinemaUpdate::wait() {
    if (worker_.joinable()) worker_.join();
}

void CinemaUpdate::run(std::function<void()> job) {
    wait();
    worker_ = std::thread(std::move(job));
}

Json CinemaUpdate::status() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string current = readTrimmed(root_ + "/CINEMAPP_VERSION");
    Json result = Json::object();
    result.set("current", Json::text(current));
    result.set("state", Json::text(state_));
    if (!error_.empty()) result.set("error", Json::text(error_));
    if (manifest_.isObject()) {
        result.set("latest", manifest_["version"]);
        result.set("available", Json::flag(newer(manifest_["version"].string(), current)));
    }
    result.set("releasesUrl", Json::text(std::string(kReleases)));
    return result;
}

void CinemaUpdate::check() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == "checking" || state_ == "downloading" || state_ == "installing") return;
        state_ = "checking";
        error_.clear();
    }
    run([this]() {
        std::string body, error;
        Json manifest;
        const bool ok = fetch_(std::string(kReleases) + "latest/download/cinemapp.manifest.json", body, "",
                               kManifestBytes, error) &&
                        parseManifest(body, manifest);
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ok ? "idle" : "failed";
        if (ok) manifest_ = manifest;
        else error_ = error.empty() ? "the release's manifest is not one this version understands" : error;
    });
}

bool CinemaUpdate::install(std::string &error) {
    Json manifest;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == "checking" || state_ == "downloading" || state_ == "installing") {
            error = "an update is already running";
            return false;
        }
        if (!manifest_.isObject() ||
            !newer(manifest_["version"].string(), readTrimmed(root_ + "/CINEMAPP_VERSION"))) {
            error = "nothing newer to install";
            return false;
        }
        manifest = manifest_;
        state_ = "downloading";
        error_.clear();
    }
    run([this, manifest]() {
        const std::string stage = root_ + "/.update";
        const std::string package = stage + "/release.tar";
        (void)std::system(("rm -rf '" + stage + "'").c_str());
        (void)mkdir(stage.c_str(), 0700);
        std::string body, error;
        const std::string url = std::string(kReleases) + "download/v" + manifest["version"].string() + "/" +
                                manifest["bundle"].string();
        bool ok = fetch_(url, body, package, kPackageBytes, error);
        if (ok && fileSha256(package) != manifest["sha256"].string()) {
            ok = false;
            error = "the download does not match its SHA-256";
        }
        if (ok && std::system(("cd '" + stage + "' && tar -xf release.tar && rm -f release.tar && "
                               "[ -f install.sh ] && [ -f bundle.tar ]").c_str()) != 0) {
            ok = false;
            error = "the package could not be unpacked";
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = ok ? "installing" : "failed";
            if (!ok) error_ = error;
        }
        if (ok && !launch_("sh '" + stage + "/install.sh' > '" + root_ + "/update.log' 2>&1")) {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = "failed";
            error_ = "the install could not start";
        }
    });
    return true;
}

} // namespace daemon
} // namespace nativetv
