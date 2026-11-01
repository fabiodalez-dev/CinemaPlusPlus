#include "daemon/memory_keeper.hpp"

#include "service/memory_keep.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace nativetv {
namespace daemon {
namespace {
std::string rooted(const std::string &root, const std::string &path) {
    if (root.empty() || root == "/") return path;
    return root + path;
}

bool exists(const std::string &path) { return access(path.c_str(), F_OK) == 0; }

Json readJson(const std::string &path) {
    std::ifstream in(path);
    if (!in) return Json();
    std::stringstream text;
    text << in.rdbuf();
    try {
        return Json::parse(text.str());
    } catch (...) {
        return Json();
    }
}

// Through a temporary file and a rename: memchute never reads half a file.
bool writeAtomically(const std::string &path, const std::string &text) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << text << '\n';
        if (!out) return false;
    }
    chmod(tmp.c_str(), 0644);
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}
} // namespace

std::string applyMemoryKeep(const std::string &systemRoot, const Json &keepInMemory,
                            const std::function<void(const std::string &)> &log) {
    std::vector<std::string> keep;
    if (keepInMemory.isArray())
        for (const Json &item : keepInMemory.items())
            if (item.isString()) keep.push_back(item.string());
    const std::string system = rooted(systemRoot, service::memorySystemPath());
    const std::string override = rooted(systemRoot, service::memoryOverridePath());
    const std::string mark = rooted(systemRoot, service::memoryMarkPath());
    const bool overrideExists = exists(override);
    const service::MemoryKeepPlan plan = service::planMemoryKeep(
        exists(system), readJson(system), overrideExists,
        overrideExists ? readJson(override) : Json(), exists(mark), keep);
    std::string done;
    switch (plan.action) {
    case service::MemoryKeepPlan::Nothing:
        return "";
    case service::MemoryKeepPlan::NotOurs:
        done = "memory: an override that is not ours exists, left alone";
        break;
    case service::MemoryKeepPlan::Remove:
        std::remove(override.c_str());
        std::remove(mark.c_str());
        done = "memory: back to LG's list from the next restart";
        break;
    case service::MemoryKeepPlan::Write:
        if (!writeAtomically(override, plan.content.dump())) {
            done = "memory: could not write the memory manager override";
            break;
        }
        { std::ofstream touch(mark); }
        done = "memory: kept in memory from the next restart: " + [&keep]() {
            std::string joined;
            for (const std::string &id : keep) joined += (joined.empty() ? "" : ",") + id;
            return joined;
        }();
        break;
    }
    if (log) log(done);
    return done;
}

} // namespace daemon
} // namespace nativetv
