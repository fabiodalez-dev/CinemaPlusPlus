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
    if (appsWorker_.joinable()) appsWorker_.join();
}

bool CinemaUpdate::pickRelease(const Json &releases, const std::string &appId, const std::string &tagPrefix,
                               Json &release) {
    bool found = false;
    for (size_t i = 0; i < releases.size(); ++i) {
        const Json &candidate = releases[i];
        if (candidate["draft"].boolean() || candidate["prerelease"].boolean()) continue;
        const std::string tag = candidate["tag_name"].string();
        int parts[3];
        if (tag.compare(0, tagPrefix.size(), tagPrefix) != 0 || !version(tag.substr(tagPrefix.size()), parts))
            continue;
        const std::string versionText = tag.substr(tagPrefix.size());
        const std::string ipk = appId + "_" + versionText + "_all.ipk";
        std::string ipkUrl, sumsUrl;
        const Json &assets = candidate["assets"];
        for (size_t a = 0; a < assets.size(); ++a) {
            const std::string name = assets[a]["name"].string();
            const std::string url = assets[a]["browser_download_url"].string();
            if (url.compare(0, 19, "https://github.com/") != 0) continue;
            if (name == ipk) ipkUrl = url;
            if (name == "SHA256SUMS.txt") sumsUrl = url;
        }
        if (ipkUrl.empty() || sumsUrl.empty()) continue;
        if (found && !newer(versionText, release["latest"].string())) continue;
        release = Json::object();
        release.set("latest", Json::text(versionText));
        release.set("ipk", Json::text(ipk));
        release.set("ipkUrl", Json::text(ipkUrl));
        release.set("sumsUrl", Json::text(sumsUrl));
        release.set("notes", Json::text(candidate["body"].string().substr(0, 1500)));
        release.set("url", candidate["html_url"]);
        found = true;
    }
    return found;
}

std::string CinemaUpdate::sumFor(const std::string &sums, const std::string &name) {
    std::istringstream lines(sums);
    std::string line, found;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string hash, file, extra;
        if (!(words >> hash >> file) || (words >> extra)) continue;
        if (!file.empty() && file[0] == '*') file.erase(0, 1);
        if (file != name || hash.size() != 64) continue;
        bool hex = true;
        for (char &c : hash) {
            hex = hex && std::isxdigit(static_cast<unsigned char>(c));
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (hex) found = hash;
    }
    return found;
}

Json CinemaUpdate::appsStatus() {
    std::lock_guard<std::mutex> lock(mutex_);
    Json result = Json::object();
    result.set("state", Json::text(appsState_));
    result.set("apps", apps_.isArray() ? apps_ : Json::array());
    return result;
}

void CinemaUpdate::checkApps() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (appsState_ == "checking") return;
        appsState_ = "checking";
    }
    if (appsWorker_.joinable()) appsWorker_.join();
    appsWorker_ = std::thread([this]() {
        struct App {
            const char *id, *name, *repo, *tag;
        };
        static const App kApps[] = {
            {"org.bloomee.webos", "Bloomee", "fabiodalez-dev/BloomeeWebOS", "webos-v"},
            {"org.webosbrew.lounge.launcher", "Cinema", "fabiodalez-dev/Cinema", "v"},
        };
        Json apps = Json::array();
        for (const App &app : kApps) {
            Json entry = Json::object();
            entry.set("id", Json::text(app.id));
            entry.set("name", Json::text(app.name));
            std::ifstream info((appsDir_ + "/" + app.id + "/appinfo.json").c_str());
            std::stringstream infoText;
            infoText << info.rdbuf();
            const std::string installed = Json::parse(infoText.str())["version"].string();
            if (installed.empty()) continue; // only apps on this TV
            entry.set("installed", Json::text(installed));
            std::string body, error;
            Json release;
            if (!fetch_(std::string("https://api.github.com/repos/") + app.repo + "/releases?per_page=15", body, "",
                        2 * 1024 * 1024, error)) {
                entry.set("error", Json::text(error));
            } else if (!pickRelease(Json::parse(body), app.id, app.tag, release)) {
                entry.set("error", Json::text("no release with an IPK and SHA256SUMS.txt"));
            } else {
                std::string sums;
                const std::string sha =
                    fetch_(release["sumsUrl"].string(), sums, "", 65536, error) ? sumFor(sums, release["ipk"].string()) : "";
                entry.set("latest", release["latest"]);
                entry.set("notes", release["notes"]);
                entry.set("url", release["url"]);
                entry.set("available", Json::flag(newer(release["latest"].string(), installed)));
                if (sha.empty()) {
                    entry.set("available", Json::flag(false));
                    entry.set("error", Json::text("the IPK is not in SHA256SUMS.txt"));
                } else {
                    entry.set("ipkUrl", release["ipkUrl"]);
                    entry.set("sha256", Json::text(sha));
                }
            }
            apps.add(entry);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        apps_ = apps;
        appsState_ = "idle";
    });
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
