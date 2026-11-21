#include "daemon/home_takeover_hooks.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace nativetv {
namespace daemon {
namespace {
// The Home key opens com.tvweb.home, the webOS wrapper app that Cinema++
// registers as its Home replacement. Opening it in turn opens the native
// client com.tvweb.dashboard, which draws the real launcher UI (SDL2 + GL).
// The HBC-installed id (io.github.ihtisham1211.nativetv) is the dashboard web
// page, not a launcher, so pointing the Home key at it opens Settings.
const char *const kAppId = "com.tvweb.home";
const char *const kBootHookName = "55-cinemapp-home";
const char *const kBackupName = "home-webos22-keybinds.json.bak";
// The HBC init.d script that opens Cinema++ after SAM has settled. Keeps the
// path out of the final launcher so the TV never shows LG's Home on power-on.
// luna-send with `-i` and an open stdin because LG's build is silent at EOF
// (see lg-tv-luna-stdin). All in the background so boot never waits on us.
const char *const kBootHookScript =
    "#!/bin/sh\n"
    "# Native Tv Home (W22 family): bring Cinema++ to the front once SAM has\n"
    "# published com.webos.applicationManager. Added by takeoverViaHooks.\n"
    "APP=com.tvweb.home\n"
    "(\n"
    "  n=0\n"
    "  while [ $n -lt 60 ]; do\n"
    "    (sleep 2) | /usr/bin/luna-send -i -n 1 "
    "luna://com.webos.service.applicationmanager/getAppInfo "
    "\"{\\\"id\\\":\\\"$APP\\\"}\" 2>/dev/null | grep -q '\"returnValue\":true' && break\n"
    "    sleep 2\n"
    "    n=$((n + 1))\n"
    "  done\n"
    "  (sleep 3) | /usr/bin/luna-send -i -n 1 "
    "luna://com.webos.applicationManager/launch "
    "\"{\\\"id\\\":\\\"$APP\\\"}\" "
    ">/dev/null 2>&1\n"
    ") </dev/null >/dev/null 2>&1 &\n"
    "exit 0\n";

std::string rooted(const std::string &root, const std::string &path) {
    if (root.empty() || root == "/") return path;
    size_t end = root.size();
    while (end > 1 && root[end - 1] == '/') --end;
    return root.substr(0, end) + path;
}

std::string keybindsPath(const std::string &systemRoot) {
    return rooted(systemRoot, "/home/root/.config/lginputhook/keybinds.json");
}

std::string bootHookPath(const std::string &systemRoot) {
    return rooted(systemRoot, std::string("/var/lib/webosbrew/init.d/") + kBootHookName);
}

std::string backupPath(const std::string &dataRoot) {
    return (dataRoot.empty() ? std::string("/var/lib/tvweb") : dataRoot) +
           std::string("/") + kBackupName;
}

bool readFile(const std::string &path, std::string &contents) {
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input) return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    contents = buffer.str();
    return input.good() || input.eof();
}

bool writeFileMode(const std::string &path, const std::string &contents,
                   mode_t mode) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream output(temporary.c_str(),
                             std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!output.good()) {
            output.close();
            (void)unlink(temporary.c_str());
            return false;
        }
    }
    (void)chmod(temporary.c_str(), mode);
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        (void)unlink(temporary.c_str());
        return false;
    }
    return true;
}

bool ensureDirectory(const std::string &path) {
    if (path.empty() || path == "/") return true;
    struct stat info;
    if (stat(path.c_str(), &info) == 0) return S_ISDIR(info.st_mode);
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0 &&
        !ensureDirectory(path.substr(0, slash))) return false;
    if (mkdir(path.c_str(), 0755) == 0) return true;
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool fileExists(const std::string &path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

const char *const kTakeoverMark = "com.tvweb.home";

bool keybindClaimsHome(const std::string &contents) {
    // The exact string match is intentional: any keybind that routes 773 to
    // Native Tv counts as active, no matter the surrounding formatting.
    return contents.find("\"773\"") != std::string::npos &&
           contents.find(kTakeoverMark) != std::string::npos;
}

std::string renderKeybinds() {
    // One line keeps diffs minimal when a user later edits by hand. com.tvweb.home
    // takes no params; it is the LG-registered Home wrapper.
    return std::string("{\"773\":{\"action\":\"launch\",\"id\":\"") + kAppId + "\"}}\n";
}

Json failure(const std::string &message) {
    Json result = Json::object();
    result.set("ok", Json::flag(false));
    result.set("error", Json::text(message));
    return result;
}

Json success(bool changed) {
    Json result = Json::object();
    result.set("ok", Json::flag(true));
    result.set("changed", Json::flag(changed));
    return result;
}
} // namespace

bool activeViaHooks(const std::string &systemRoot,
                    const std::string &dataRoot) {
    (void)dataRoot;
    std::string keybinds;
    if (!readFile(keybindsPath(systemRoot), keybinds) ||
        !keybindClaimsHome(keybinds))
        return false;
    return fileExists(bootHookPath(systemRoot));
}

Json takeoverViaHooks(const std::string &systemRoot,
                      const std::string &dataRoot) {
    const std::string keybinds = keybindsPath(systemRoot);
    const std::string boot = bootHookPath(systemRoot);
    const std::string backup = backupPath(dataRoot);
    const std::string wanted = renderKeybinds();

    std::string current;
    const bool hadKeybinds = readFile(keybinds, current);
    const bool alreadyClaimed = hadKeybinds && keybindClaimsHome(current);

    // Back up the previous keybind once, so release can restore it exactly.
    // A second takeover must not overwrite a good backup with our own content.
    if (!alreadyClaimed) {
        const std::string dataDir = dataRoot.empty() ?
            std::string("/var/lib/tvweb") : dataRoot;
        if (!ensureDirectory(dataDir))
            return failure(std::string("could not create ") + dataDir);
        // Store "" for the "no previous file" case, distinguishable from an
        // existing empty file because we always write at least "{}\n" there.
        const std::string payload = hadKeybinds ? current : std::string();
        if (!writeFileMode(backup, payload, 0600))
            return failure(std::string("could not write ") + backup);
    }

    const std::string keybindsDir =
        keybinds.substr(0, keybinds.find_last_of('/'));
    if (!ensureDirectory(keybindsDir))
        return failure(std::string("could not create ") + keybindsDir);
    if (!hadKeybinds || current != wanted) {
        if (!writeFileMode(keybinds, wanted, 0644))
            return failure(std::string("could not write ") + keybinds);
    }

    const std::string bootDir = boot.substr(0, boot.find_last_of('/'));
    if (!ensureDirectory(bootDir))
        return failure(std::string("could not create ") + bootDir);
    std::string existingBoot;
    const bool hadBoot = readFile(boot, existingBoot);
    if (!hadBoot || existingBoot != kBootHookScript) {
        if (!writeFileMode(boot, kBootHookScript, 0755))
            return failure(std::string("could not write ") + boot);
    }

    const bool changed = !alreadyClaimed || !hadBoot ||
                         existingBoot != kBootHookScript;
    return success(changed);
}

Json releaseViaHooks(const std::string &systemRoot,
                     const std::string &dataRoot) {
    const std::string keybinds = keybindsPath(systemRoot);
    const std::string boot = bootHookPath(systemRoot);
    const std::string backup = backupPath(dataRoot);
    bool changed = false;

    // Remove the boot hook first: if the keybind restore fails below, this
    // one is still gone so the next boot does not relaunch Native Tv.
    if (unlink(boot.c_str()) == 0) changed = true;
    else if (errno != ENOENT)
        return failure(std::string("could not remove ") + boot);

    std::string previous;
    const bool hadBackup = readFile(backup, previous);
    if (hadBackup) {
        if (previous.empty()) {
            // There was no keybind file before our takeover: remove ours.
            if (unlink(keybinds.c_str()) == 0) changed = true;
            else if (errno != ENOENT)
                return failure(std::string("could not remove ") + keybinds);
        } else {
            std::string existing;
            const bool readOk = readFile(keybinds, existing);
            if (!readOk || existing != previous) {
                if (!writeFileMode(keybinds, previous, 0644))
                    return failure(std::string("could not restore ") + keybinds);
                changed = true;
            }
        }
        if (unlink(backup.c_str()) != 0 && errno != ENOENT)
            return failure(std::string("could not remove ") + backup);
    } else {
        // No backup: leave the keybind file alone so a user-authored config
        // is never lost to a stale release. We only clear if we see our mark.
        std::string existing;
        if (readFile(keybinds, existing) && keybindClaimsHome(existing)) {
            if (!writeFileMode(keybinds, std::string("{}\n"), 0644))
                return failure(std::string("could not clear ") + keybinds);
            changed = true;
        }
    }
    return success(changed);
}
} // namespace daemon
} // namespace nativetv
