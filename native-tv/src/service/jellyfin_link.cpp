#include "service/jellyfin_link.hpp"
#include "domain/say.hpp"
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include <fcntl.h>
#include <fstream>
#include <random>
#include <sstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

namespace nativetv {
namespace service {
namespace {

// A code stays valid on the server for ten minutes; the TV gives up then.
constexpr int64_t kCodeLifetimeMs = 10 * 60 * 1000;

size_t collect(char *data, size_t size, size_t count, void *user) {
    auto *body = static_cast<std::string *>(user);
    const size_t n = size * count;
    if (body->size() + n > 1024 * 1024) return 0;
    body->append(data, n);
    return n;
}

bool curlRequest(const std::string &method, const std::string &url, const std::string &authorization,
                 const std::string &body, std::string &response, long &status) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    curl_slist *headers = curl_slist_append(nullptr, ("Authorization: " + authorization).c_str());
    headers = curl_slist_append(headers, "Accept: application/json");
    if (method == "POST") {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return code == CURLE_OK;
}

int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string randomHex(size_t length) {
    std::random_device device;
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < length; ++i) out.push_back(digits[device() % 16]);
    return out;
}

std::string quoted(const std::string &value) {
    std::string out;
    for (char c : value)
        if (c != '"' && c != '\r' && c != '\n') out.push_back(c);
    return out;
}

std::string urlEncode(const std::string &value) {
    std::ostringstream out;
    static const char digits[] = "0123456789ABCDEF";
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else
            out << '%' << digits[c >> 4] << digits[c & 15];
    }
    return out.str();
}
} // namespace

JellyfinLink::JellyfinLink(std::string connectionPath, Request request, Clock clock)
    : connectionPath_(std::move(connectionPath)), request_(request ? request : Request(curlRequest)),
      clock_(clock ? clock : Clock(steadyMs)), deviceId_("cinemapp-" + randomHex(16)) {}

std::string JellyfinLink::normalizeServer(const std::string &server) {
    std::string value;
    for (char c : server)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') value.push_back(c);
    if (value.empty()) return "";
    if (value.compare(0, 7, "http://") != 0 && value.compare(0, 8, "https://") != 0)
        value = "http://" + value;
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

std::string JellyfinLink::authorization(const std::string &token) const {
    std::string header = "MediaBrowser Client=\"Cinema++\", Device=\"LG TV\", DeviceId=\"" + deviceId_ +
                         "\", Version=\"0.1\"";
    if (!token.empty()) header += ", Token=\"" + quoted(token) + "\"";
    return header;
}

bool JellyfinLink::start(const std::string &rawServer, std::string &error) {
    const std::string server = normalizeServer(rawServer);
    if (server.empty() || server.size() > 200) {
        error = say::msg("jellyfin.link.noServer", "Enter the address of the Jellyfin server");
        return false;
    }
    std::string response;
    long status = 0;
    if (!request_("GET", server + "/System/Info/Public", authorization(), "", response, status) ||
        status != 200 || !Json::parse(response)["Id"].isString()) {
        error = say::msg("jellyfin.link.notJellyfin", "No Jellyfin server answers at that address");
        return false;
    }
    response.clear();
    if (!request_("POST", server + "/QuickConnect/Initiate", authorization(), "", response, status) ||
        status != 200) {
        error = status == 401 || status == 403 ? say::msg("jellyfin.link.quickConnectOff", "Quick Connect is off on the server")
                                               : say::msg("jellyfin.link.noCode", "The server did not give a code");
        return false;
    }
    const Json answer = Json::parse(response);
    if (!answer["Secret"].isString() || !answer["Code"].isString()) {
        error = say::msg("jellyfin.link.noCode", "The server did not give a code");
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    server_ = server;
    secret_ = answer["Secret"].string();
    code_ = answer["Code"].string();
    startedMs_ = clock_();
    return true;
}

bool JellyfinLink::poll(std::string &error) {
    std::string server, secret;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (secret_.empty()) return false;
        if (clock_() - startedMs_ > kCodeLifetimeMs) {
            secret_.clear();
            code_.clear();
            error = say::msg("jellyfin.link.expired", "The code expired");
            return false;
        }
        server = server_;
        secret = secret_;
    }
    std::string response;
    long status = 0;
    if (!request_("GET", server + "/QuickConnect/Connect?Secret=" + urlEncode(secret), authorization(), "",
                  response, status) ||
        status != 200 || !Json::parse(response)["Authenticated"].boolean())
        return false;
    Json body = Json::object();
    body.set("Secret", Json::text(secret));
    response.clear();
    if (!request_("POST", server + "/Users/AuthenticateWithQuickConnect", authorization(), body.dump(),
                  response, status) ||
        status != 200) {
        error = say::msg("jellyfin.link.refused", "The server refused the sign-in");
        return false;
    }
    const Json signedIn = Json::parse(response);
    const std::string token = signedIn["AccessToken"].string();
    const std::string userId = signedIn["User"]["Id"].string();
    if (token.size() < 8 || userId.empty()) {
        error = say::msg("jellyfin.link.refused", "The server refused the sign-in");
        return false;
    }
    if (!writeConnection(server, userId, token, signedIn["User"]["Name"].string(), error)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    secret_.clear();
    code_.clear();
    return true;
}

void JellyfinLink::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    secret_.clear();
    code_.clear();
}

bool JellyfinLink::unlink(std::string &error) {
    cancel();
    if (std::remove(connectionPath_.c_str()) != 0 && errno != ENOENT) {
        error = std::strerror(errno);
        return false;
    }
    return true;
}

bool JellyfinLink::writeConnection(const std::string &server, const std::string &userId,
                                   const std::string &token, const std::string &userName,
                                   std::string &error) {
    Json document = Json::object();
    document.set("server", Json::text(server));
    document.set("userId", Json::text(userId));
    document.set("token", Json::text(token));
    document.set("userName", Json::text(userName));
    const std::string bytes = document.dump();
    const std::string staging = connectionPath_ + ".part";
    (void)std::remove(staging.c_str());
    const int descriptor = open(staging.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0) {
        error = std::strerror(errno);
        return false;
    }
    const bool written = write(descriptor, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()) &&
                         fsync(descriptor) == 0;
    close(descriptor);
    if (!written || std::rename(staging.c_str(), connectionPath_.c_str()) != 0) {
        error = std::strerror(errno);
        (void)std::remove(staging.c_str());
        return false;
    }
    return true;
}

Json JellyfinLink::status() const {
    Json result = Json::object();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!secret_.empty()) {
        result.set("state", Json::text("waiting"));
        result.set("code", Json::text(code_));
        result.set("server", Json::text(server_));
        const int64_t left = (kCodeLifetimeMs - (clock_() - startedMs_)) / 1000;
        result.set("expiresInS", Json::numeric(left < 0 ? 0 : static_cast<double>(left)));
        return result;
    }
    std::ifstream file(connectionPath_.c_str());
    std::stringstream text;
    text << file.rdbuf();
    const Json connection = Json::parse(text.str());
    if (connection["token"].string().size() > 8) {
        result.set("state", Json::text("linked"));
        result.set("server", Json::text(connection["server"].string()));
        result.set("user", Json::text(connection["userName"].string()));
    } else {
        result.set("state", Json::text("unlinked"));
    }
    return result;
}

Json JellyfinLink::parseDiscovery(const std::string &reply, const std::string &sourceIp) {
    const Json data = Json::parse(reply);
    const std::string advertised = normalizeServer(data["Address"].string());
    in_addr parsed = {};
    if (advertised.empty() || inet_pton(AF_INET, sourceIp.c_str(), &parsed) != 1) return Json();
    // The port, and a path such as /jellyfin, from the reply; the host is where it came from.
    const size_t hostStart = advertised.find("://") + 3;
    const size_t pathStart = advertised.find('/', hostStart);
    const std::string hostPort = advertised.substr(hostStart, pathStart == std::string::npos ? std::string::npos
                                                                                             : pathStart - hostStart);
    const size_t colon = hostPort.rfind(':');
    std::string port = colon == std::string::npos ? "8096" : hostPort.substr(colon + 1);
    if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos) return Json();
    std::string path = pathStart == std::string::npos ? "" : advertised.substr(pathStart);
    for (char c : path)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '/' && c != '-' && c != '_' && c != '.') return Json();
    Json server = Json::object();
    const std::string address = "http://" + sourceIp + ":" + port + path;
    server.set("name", Json::text(data["Name"].string(address).substr(0, 60)));
    server.set("address", Json::text(address));
    server.set("id", Json::text(data["Id"].string().substr(0, 64)));
    return server;
}

Json JellyfinLink::discover(int waitMs) {
    Json servers = Json::array();
    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return servers;
    const int yes = 1;
    (void)setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(7359);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    const char question[] = "Who is JellyfinServer?";
    if (sendto(sock, question, sizeof question - 1, 0, reinterpret_cast<sockaddr *>(&to), sizeof to) < 0) {
        close(sock);
        return servers;
    }
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::microseconds>(until - std::chrono::steady_clock::now());
        if (left.count() <= 0) break;
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(sock, &ready);
        timeval wait = {static_cast<long>(left.count() / 1000000), static_cast<int>(left.count() % 1000000)};
        if (select(sock + 1, &ready, nullptr, nullptr, &wait) <= 0) break;
        char buffer[2048];
        sockaddr_in from = {};
        socklen_t fromSize = sizeof from;
        const ssize_t count = recvfrom(sock, buffer, sizeof buffer, 0, reinterpret_cast<sockaddr *>(&from), &fromSize);
        if (count <= 0 || from.sin_family != AF_INET) continue;
        char ip[INET_ADDRSTRLEN] = {};
        if (!inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip)) continue;
        const Json server = parseDiscovery(std::string(buffer, static_cast<size_t>(count)), ip);
        bool seen = !server.valid();
        for (const Json &known : servers.items())
            seen = seen || known["address"].string() == server["address"].string();
        if (!seen && servers.size() < 8) servers.add(server);
    }
    close(sock);
    return servers;
}

} // namespace service
} // namespace nativetv
