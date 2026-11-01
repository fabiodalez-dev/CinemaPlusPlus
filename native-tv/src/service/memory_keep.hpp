#pragma once

#include "core/json.hpp"
#include <string>
#include <vector>

namespace nativetv {
namespace service {

// Apps LG's memory manager (memchute) keeps when another app is opened: its
// list KeepOnLaunchEx in /etc/palm/memorymanager-conf.json, read-only, which
// memchute takes from /home/root/memorymanager-conf.json when that exists.
// Ported from Cinema's tvtools.py: Bloomee kept open so the music does not
// stop when the TV opens another app. memchute reads the file when it starts,
// and restarting it closes the apps it tracks, so a change counts from the
// next restart of the TV.

const std::string &memorySystemPath();    // /etc/palm/memorymanager-conf.json
const std::string &memoryOverridePath();  // /home/root/memorymanager-conf.json
const std::string &memoryMarkPath();      // /home/root/.cinema-memorymanager

bool memoryAppIdValid(const std::string &id);

// LG's configuration with the apps added to KeepOnLaunchEx, in order, once.
Json memoryKeepTarget(const Json &system, const std::vector<std::string> &keep);

struct MemoryKeepPlan {
    enum Action { Nothing, Write, Remove, NotOurs };
    Action action = Nothing;
    Json content;  // for Write
};

// What to do with the override. An override that exists without our mark
// belongs to someone else and is left alone; with an empty list ours goes,
// so memchute is back to LG's list.
MemoryKeepPlan planMemoryKeep(bool systemExists, const Json &system,
                              bool overrideExists, const Json &current,
                              bool ours, const std::vector<std::string> &keep);

// The apps offered in the dashboard: the installed ones LG shows, with
// whether each is kept, the kept ones first and then by title.
Json memoryApps(const Json &installed, const Json &keepInMemory);

} // namespace service
} // namespace nativetv
