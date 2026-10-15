#pragma once
// Session log with rotation and hard limits: a long session, a crash loop or
// an export of thousands of files never fills the disk nor slows the program.
// psych-importer.log is the current file; .1 ... .5 are the previous ones.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace pi::logs {
namespace fs=std::filesystem;
struct Limits {
    std::uintmax_t fileBytes=2u*1024u*1024u;   // per file before rotating
    int keep=5;                                // rotated files kept (12 MB at most)
    size_t lineChars=4000;                     // longer lines are cut
    int perSecond=200;                         // beyond this, lines are counted, not written
};
inline Limits& limits() { static Limits value; return value; }
struct State {
    std::mutex mutex; fs::path folder,file; std::ofstream out; std::uintmax_t size=0;
    long long second=-1; int inSecond=0; size_t dropped=0;
};
inline State& state() { static State value; return value; }
inline fs::path rotated(const State& s,int n) { return s.folder/("psych-importer."+std::to_string(n)+".log"); }
inline void rotateLocked(State& s) {
    s.out.close(); std::error_code ec;
    fs::remove(rotated(s,limits().keep),ec);
    for(int n=limits().keep-1;n>=1;--n) if(fs::exists(rotated(s,n),ec)) fs::rename(rotated(s,n),rotated(s,n+1),ec);
    fs::rename(s.file,rotated(s,1),ec);
    s.out.open(s.file,std::ios::binary|std::ios::app); s.size=0;
}
inline std::string stamp() {
    const auto now=std::chrono::system_clock::now(); const auto time=std::chrono::system_clock::to_time_t(now);
    const int ms=(int)(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()%1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm,&time);
#else
    localtime_r(&time,&tm);
#endif
    char buffer[40]; std::snprintf(buffer,sizeof buffer,"%04d-%02d-%02d %02d:%02d:%02d.%03d",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec,ms);
    return buffer;
}
inline void writeLocked(State& s,const char* level,std::string text) {
    if(!s.out.is_open()) return;
    for(auto& c:text) if(c=='\n' || c=='\r') c=' ';
    if(text.size()>limits().lineChars) text=text.substr(0,limits().lineChars)+" ... ("+std::to_string(text.size())+" caracteres)";
    const std::string line=stamp()+" ["+level+"] "+text+"\n";
    if(s.size+line.size()>limits().fileBytes) rotateLocked(s);
    s.out<<line; s.out.flush(); s.size+=line.size();
}
inline void write(const char* level,const std::string& text) {
    auto& s=state(); std::lock_guard<std::mutex> lock(s.mutex);
    if(!s.out.is_open()) return;
    const long long second=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(second!=s.second) {
        if(s.dropped) writeLocked(s,"WARN","Límite de ritmo: "+std::to_string(s.dropped)+" líneas omitidas en un segundo.");
        s.second=second; s.inSecond=0; s.dropped=0;
    }
    if(++s.inSecond>limits().perSecond) { ++s.dropped; return; }
    writeLocked(s,level,text);
}
inline void info(const std::string& text) { write("INFO",text); }
inline void warn(const std::string& text) { write("WARN",text); }
inline void error(const std::string& text) { write("ERROR",text); }
// A list cut to `limit` entries; the rest is counted.
inline void list(const char* level,const std::string& title,const std::vector<std::string>& items,size_t limit=40) {
    if(items.empty()) return;
    write(level,title+" ("+std::to_string(items.size())+"):");
    for(size_t i=0;i<items.size() && i<limit;++i) write(level,"  "+items[i]);
    if(items.size()>limit) write(level,"  ... y "+std::to_string(items.size()-limit)+" más.");
}
// Opens (or continues) the log in `folder`; false if it cannot be written.
inline bool open(const fs::path& folder) {
    auto& s=state(); std::lock_guard<std::mutex> lock(s.mutex);
    if(s.out.is_open()) s.out.close();
    std::error_code ec; fs::create_directories(folder,ec);
    s.folder=folder; s.file=folder/"psych-importer.log";
    s.out.open(s.file,std::ios::binary|std::ios::app); if(!s.out.is_open()) return false;
    s.size=fs::file_size(s.file,ec); if(ec) s.size=0;
    if(s.size>=limits().fileBytes) rotateLocked(s);
    return s.out.is_open();
}
inline void close() {
    auto& s=state(); std::lock_guard<std::mutex> lock(s.mutex);
    if(s.dropped) writeLocked(s,"WARN","Límite de ritmo: "+std::to_string(s.dropped)+" líneas omitidas en un segundo.");
    s.dropped=0; if(s.out.is_open()) s.out.close();
}
inline fs::path folder() { return state().folder; }
// Written while the program is crashing: never waits for the lock.
inline void fatal(const std::string& text) {
    auto& s=state(); if(!s.mutex.try_lock()) return;
    writeLocked(s,"FATAL",text); s.mutex.unlock();
}
// Milliseconds since `start`, for timing loads and exports.
inline long long since(std::chrono::steady_clock::time_point start) {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
}
}
