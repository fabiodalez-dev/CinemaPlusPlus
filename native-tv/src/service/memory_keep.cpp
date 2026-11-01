#include "service/memory_keep.hpp"

#include <algorithm>
#include <cctype>

namespace nativetv {
namespace service {

const std::string &memorySystemPath() {
    static const std::string path = "/etc/palm/memorymanager-conf.json";
    return path;
}

const std::string &memoryOverridePath() {
    static const std::string path = "/home/root/memorymanager-conf.json";
    return path;
}

// The same mark Cinema's tvtools.py leaves: the two agree on whose file it is.
const std::string &memoryMarkPath() {
    static const std::string path = "/home/root/.cinema-memorymanager";
    return path;
}

bool memoryAppIdValid(const std::string &id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '.' && c != '_' && c != '-') return false;
    }
    return true;
}

Json memoryKeepTarget(const Json &system, const std::vector<std::string> &keep) {
    Json target = system.isObject() ? Json::parse(system.dump()) : Json::object();
    Json list = Json::array();
    std::vector<std::string> seen;
    const Json current = target["KeepOnLaunchEx"];
    if (current.isArray()) {
        for (const Json &item : current.items()) {
            list.add(item);
            if (item.isString()) seen.push_back(item.string());
        }
    }
    for (const std::string &id : keep) {
        if (!memoryAppIdValid(id)) continue;
        bool present = false;
        for (const std::string &s : seen) present = present || s == id;
        if (present) continue;
        list.add(Json::text(id));
        seen.push_back(id);
    }
    target.set("KeepOnLaunchEx", list);
    return target;
}

namespace {
// The same content whatever the order of the keys: Cinema's Python 2 wrote
// the file from a dict, in no particular order.
std::string canonical(const Json &value) {
    if (value.isObject()) {
        std::vector<std::string> keys = value.keys();
        std::sort(keys.begin(), keys.end());
        std::string out = "{";
        for (const std::string &key : keys)
            out += Json::text(key).dump() + ":" + canonical(value[key]) + ",";
        return out + "}";
    }
    if (value.isArray()) {
        std::string out = "[";
        for (const Json &item : value.items()) out += canonical(item) + ",";
        return out + "]";
    }
    return value.dump();
}
} // namespace

MemoryKeepPlan planMemoryKeep(bool systemExists, const Json &system,
                              bool overrideExists, const Json &current,
                              bool ours, const std::vector<std::string> &keep) {
    MemoryKeepPlan plan;
    if (!systemExists) return plan;
    bool any = false;
    for (const std::string &id : keep) any = any || memoryAppIdValid(id);
    if (!any) {
        if (ours) plan.action = MemoryKeepPlan::Remove;
        return plan;
    }
    if (overrideExists && !ours) {
        plan.action = MemoryKeepPlan::NotOurs;
        return plan;
    }
    const Json target = memoryKeepTarget(system, keep);
    if (overrideExists && canonical(current) == canonical(target)) return plan;
    plan.action = MemoryKeepPlan::Write;
    plan.content = target;
    return plan;
}

} // namespace service
} // namespace nativetv
