#include "service/memory_keep.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace nativetv;
using namespace nativetv::service;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

Json lg() {
    return Json::parse("{\"KeepOnLaunchEx\":[\"com.webos.app.home\"],\"Threshold\":{\"Low\":50}}");
}

void testTheAppsAreAddedOnceAfterLgsOwn() {
    const Json target = memoryKeepTarget(lg(), {"org.bloomee.webos", "com.webos.app.home",
                                                "org.bloomee.webos"});
    check(target["KeepOnLaunchEx"].size() == 2, "LG's app first, ours once");
    check(target["KeepOnLaunchEx"][0].string() == "com.webos.app.home", "LG's app first");
    check(target["KeepOnLaunchEx"][1].string() == "org.bloomee.webos", "ours after LG's");
    check(target["Threshold"]["Low"].number() == 50, "the rest of LG's file kept");
}

void testInvalidIdsAreIgnored() {
    const Json target = memoryKeepTarget(lg(), {"../etc", "a b", ""});
    check(target["KeepOnLaunchEx"].size() == 1, "nothing invalid added");
    check(memoryAppIdValid("org.webosbrew.lounge.launcher"), "a normal id is valid");
}

void testPlan() {
    const std::vector<std::string> keep = {"org.bloomee.webos"};
    check(planMemoryKeep(false, Json(), false, Json(), false, keep).action ==
              MemoryKeepPlan::Nothing, "no LG file, nothing to do");
    MemoryKeepPlan write = planMemoryKeep(true, lg(), false, Json(), false, keep);
    check(write.action == MemoryKeepPlan::Write, "no override yet: written");
    check(planMemoryKeep(true, lg(), true, write.content, true, keep).action ==
              MemoryKeepPlan::Nothing, "already as wanted: left");
    const Json reordered = Json::parse("{\"Threshold\":{\"Low\":50},\"KeepOnLaunchEx\":[\"com.webos.app.home\",\"org.bloomee.webos\"]}");
    check(planMemoryKeep(true, lg(), true, reordered, true, keep).action ==
              MemoryKeepPlan::Nothing, "the same content in another key order: left");
    check(planMemoryKeep(true, lg(), true, lg(), false, keep).action ==
              MemoryKeepPlan::NotOurs, "someone else's override: left alone");
    check(planMemoryKeep(true, lg(), true, write.content, true, {}).action ==
              MemoryKeepPlan::Remove, "empty list: ours removed");
    check(planMemoryKeep(true, lg(), true, lg(), false, {}).action ==
              MemoryKeepPlan::Nothing, "empty list, not ours: left");
}
} // namespace

int main() {
    testTheAppsAreAddedOnceAfterLgsOwn();
    testInvalidIdsAreIgnored();
    testPlan();
    std::cout << "memory keep tests passed\n";
    return 0;
}
