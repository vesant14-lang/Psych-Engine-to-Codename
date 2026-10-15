#pragma once
// Named timings for --profile. Off by default: a scope costs one branch.
#include <algorithm>
#include <chrono>
#include <map>
#include <string>

namespace pi {
struct ProfileStats { double totalMs=0,maxMs=0; size_t count=0; };
inline bool& profilingEnabled() { static bool enabled=false; return enabled; }
inline std::map<std::string,ProfileStats>& profileSections() { static std::map<std::string,ProfileStats> sections; return sections; }
struct ProfileScope {
    const char* name; bool on; std::chrono::steady_clock::time_point start;
    explicit ProfileScope(const char* label):name(label),on(profilingEnabled()) { if(on)start=std::chrono::steady_clock::now(); }
    ~ProfileScope() {
        if(!on)return;
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        auto& s=profileSections()[name]; s.totalMs+=ms; s.maxMs=std::max(s.maxMs,ms); ++s.count;
    }
    ProfileScope(const ProfileScope&)=delete; ProfileScope& operator=(const ProfileScope&)=delete;
};
}
#define PI_PROFILE_JOIN2(a,b) a##b
#define PI_PROFILE_JOIN(a,b) PI_PROFILE_JOIN2(a,b)
#define PI_PROFILE(label) ::pi::ProfileScope PI_PROFILE_JOIN(piProfileScope,__LINE__)(label)
