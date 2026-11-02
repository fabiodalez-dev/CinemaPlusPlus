// Quick Connect with a stand-in Jellyfin server.
#include "service/jellyfin_link.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace nativetv;
using namespace nativetv::service;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main() {
    check(JellyfinLink::normalizeServer(" jf.local:8096/ ") == "http://jf.local:8096", "address completed");
    check(JellyfinLink::normalizeServer("https://jf.example/") == "https://jf.example", "https kept");

    char pattern[] = "/tmp/jellyfin-link-XXXXXX";
    const std::string dir = mkdtemp(pattern);
    const std::string file = dir + "/jellyfin.json";
    bool approved = false, quickConnect = true;
    std::string signInBody;
    const auto request = [&](const std::string &method, const std::string &url, const std::string &auth,
                             const std::string &body, std::string &response, long &status) {
        status = 200;
        check(auth.find("Client=\"Cinema++\"") != std::string::npos, "client named");
        if (url == "http://jf.local:8096/System/Info/Public")
            response = "{\"Id\":\"abc\",\"ServerName\":\"Casa\"}";
        else if (url.find("http://other") == 0)
            status = 404;
        else if (url == "http://jf.local:8096/QuickConnect/Initiate" && method == "POST")
            quickConnect ? (void)(response = "{\"Secret\":\"s e\",\"Code\":\"123456\"}") : (void)(status = 401);
        else if (url == "http://jf.local:8096/QuickConnect/Connect?Secret=s%20e")
            response = approved ? "{\"Authenticated\":true}" : "{\"Authenticated\":false}";
        else if (url == "http://jf.local:8096/Users/AuthenticateWithQuickConnect" && method == "POST") {
            signInBody = body;
            response = "{\"AccessToken\":\"0123456789token\",\"User\":{\"Id\":\"u1\",\"Name\":\"Fabio\"}}";
        } else
            status = 404;
        return true;
    };
    int64_t now = 0;
    JellyfinLink link(file, request, [&now]() { return now; });
    std::string error;
    check(link.status()["state"].string() == "unlinked", "unlinked at first");
    check(!link.start("other:1", error) && !error.empty(), "not a Jellyfin server");
    quickConnect = false;
    check(!link.start("jf.local:8096", error) && error.find("Quick Connect") != std::string::npos,
          "Quick Connect off");
    quickConnect = true;
    check(link.start("jf.local:8096", error), "code asked");
    check(link.status()["state"].string() == "waiting" && link.status()["code"].string() == "123456", "code shown");
    check(!link.poll(error), "not approved yet");
    approved = true;
    now = 30000;
    check(link.poll(error), "approved");
    check(signInBody == "{ \"Secret\": \"s e\" }" || signInBody.find("\"s e\"") != std::string::npos, "secret sent");
    const Json status = link.status();
    check(status["state"].string() == "linked" && status["user"].string() == "Fabio" && !status.has("token"),
          "linked without the token");
    struct stat info = {};
    check(stat(file.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600, "file only for root");
    std::ifstream in(file.c_str());
    std::stringstream text;
    text << in.rdbuf();
    check(text.str().find("0123456789token") != std::string::npos, "token saved");

    // A code nobody approves runs out after ten minutes.
    approved = false;
    check(link.start("jf.local:8096", error), "second code");
    now += 11 * 60 * 1000;
    check(!link.poll(error) && error.find("expired") != std::string::npos, "expired");
    check(link.unlink(error) && link.status()["state"].string() == "unlinked", "unlinked");
    // Jellyfin's discovery replies.
    const Json found = service::JellyfinLink::parseDiscovery(
        R"({"Address":"http://192.168.1.10:8096","Id":"abc","Name":"Casa","EndpointAddress":null})", "192.168.1.10");
    check(found["name"].string() == "Casa" && found["address"].string() == "http://192.168.1.10:8096",
          "a discovery reply");
    check(service::JellyfinLink::parseDiscovery(R"({"Address":"https://evil.example:9000/jf","Name":"x"})",
                                                "192.168.1.20")["address"].string() == "http://192.168.1.20:9000/jf",
          "the host is where the reply came from");
    check(!service::JellyfinLink::parseDiscovery("not json", "192.168.1.10").valid() &&
              !service::JellyfinLink::parseDiscovery(R"({"Name":"x"})", "192.168.1.10").valid() &&
              !service::JellyfinLink::parseDiscovery(R"({"Address":"http://a:80/x?y"})", "192.168.1.10").valid(),
          "anything else is not a server");
    rmdir(dir.c_str());
    std::cout << "jellyfin link tests passed\n";
    return 0;
}
