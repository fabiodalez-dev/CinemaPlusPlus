#pragma once

#include "core/json.hpp"
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>

namespace nativetv {
namespace ambilight {

// The webOS bus through one long-lived client: a small Node.js process using
// the bus library webOS ships (palmbus.js), fed JSON lines. LG's Bluetooth
// service leaks an open file for every new client that talks to it, and a
// luna-send per colour hung it in about ten minutes; a native client of our
// own cannot be given the Bluetooth permission groups from the volatile
// role folders, while node already has them.
class NodeBus {
  public:
    NodeBus(std::string runDirectory, std::string palmbus);
    ~NodeBus();
    // The first reply, or an invalid Json after the timeout or a broken pipe.
    Json call(const std::string &uri, const Json &payload, int timeoutMs);
    // Replies until cancelled; onEnd when the service ends it or the pipe dies.
    int subscribe(const std::string &uri, const Json &payload,
                  std::function<void(const Json &)> onMessage, std::function<void()> onEnd);
    void cancel(int id);

  private:
    bool ensure();
    bool send(const Json &message);
    void read(int fd, pid_t pid);
    void broken();

    struct Waiter {
        bool done = false;
        Json reply;
    };
    struct Sub {
        std::function<void(const Json &)> onMessage;
        std::function<void()> onEnd;
    };

    std::string runDirectory_;
    std::string palmbus_;
    std::mutex mutex_;
    std::condition_variable replied_;
    pid_t pid_ = -1;
    int in_ = -1;
    int nextId_ = 0;
    std::map<int, Waiter *> waiting_;
    std::map<int, Sub> subs_;
    std::thread reader_;
};

} // namespace ambilight
} // namespace nativetv
