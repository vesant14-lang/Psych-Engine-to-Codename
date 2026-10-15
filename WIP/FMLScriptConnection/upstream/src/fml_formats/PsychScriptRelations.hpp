#pragma once
#include "PsychScriptLibrary.hpp"
#include <map>
#include <set>

namespace fml::psychlib {
struct Relation {
    std::string caller, target, call, requested, status;
    int line = 0;
};
struct RelationReview {
    std::vector<Relation> links;
    std::set<std::string> linkedStoredOriginals;
    int selected = 0, stored = 0, skipped = 0;
    bool truncated = false;
};
inline RelationReview reviewRelations(const std::vector<std::pair<std::string,std::string>>& selected,
                                     const std::vector<Entry>& library, const Read& reader) {
    struct Candidate {
        std::string key, origin, source, original;
        bool selected = false;
    };
    RelationReview out;
    std::vector<Candidate> files;
    std::set<std::string> origins, selectedKeys;
    constexpr std::size_t maxFiles=256, maxBytes=4u*1024u*1024u, maxTotalBytes=32u*1024u*1024u;
    std::size_t totalBytes=0;
    for (const auto& item : selected) {
        if (files.size() >= maxFiles) { out.truncated=true; break; }
        const auto path=std::filesystem::u8path(item.first);
        const auto ext=psych::extensionLower(path);
        if ((ext != ".lua" && ext != ".hx") || !safeKey(item.second)) continue;
        std::error_code ec;
        const auto size=std::filesystem::file_size(path,ec);
        if (ec || size>maxBytes || size>maxTotalBytes-totalBytes) { ++out.skipped; continue; }
        const auto owner=origin(path);
        if (!selectedKeys.insert(owner+"|"+item.second).second) continue;
        origins.insert(owner);
        files.push_back({item.second,owner,psych::read(path),{},true});
        totalBytes+=size;
        ++out.selected;
    }
    for (const auto& entry : library) {
        if (files.size() >= maxFiles) { out.truncated=true; break; }
        if (!origins.count(entry.origin) || selectedKeys.count(entry.origin+"|"+entry.key)) continue;
        const auto bytes=verifiedSource(entry,reader);
        if (!bytes || bytes->size()>maxBytes || bytes->size()>maxTotalBytes-totalBytes) { ++out.skipped; continue; }
        files.push_back({entry.key,entry.origin,*bytes,entry.original,false});
        totalBytes+=bytes->size();
        ++out.stored;
    }
    std::map<std::pair<std::string,std::string>,bool> functionCache;
    for (const auto& caller : files) {
        for (const auto& dep : psych::scriptDependencies(caller.source,caller.key)) {
            if (out.links.size() >= 4096) { out.truncated=true; return out; }
            Relation link{caller.key,{},dep.call,dep.key,dep.status,dep.line};
            if (dep.status == "external-isolated" || dep.status == "engine-method-pending") {
                if (caller.selected) out.links.push_back(std::move(link));
                continue;
            }
            if (dep.key.empty()) {
                if (caller.selected) { link.status="dynamic-pending"; out.links.push_back(std::move(link)); }
                continue;
            }
            std::vector<const Candidate*> matches;
            std::string requested=dep.key;
            if (dep.call == "require") std::replace(requested.begin(),requested.end(),'.','/');
            for (const auto& target : files) {
                if (target.origin != caller.origin) continue;
                const auto ext=psych::extensionLower(std::filesystem::u8path(target.key));
                if (dep.call == "addLuaScript" && ext != ".lua") continue;
                if (dep.call == "addHScript" && ext != ".hx") continue;
                bool match=false;
                if (dep.status == "callback-reference") {
                    const auto cacheKey=std::make_pair(target.origin+"|"+target.key,requested);
                    const auto found=functionCache.find(cacheKey);
                    match=found==functionCache.end()
                        ? functionCache.emplace(cacheKey,psych::providesScriptFunction(target.source,target.key,requested)).first->second
                        : found->second;
                }
                else {
                    const auto key=withoutExtension(target.key);
                    const auto wanted=withoutExtension(requested);
                    match=key == wanted || key == "scripts/"+wanted ||
                        (wanted.find('/')==std::string::npos && std::filesystem::u8path(key).filename().u8string()==wanted);
                }
                if (match) matches.push_back(&target);
            }
            if (!caller.selected && std::none_of(matches.begin(),matches.end(),[](const Candidate* p) { return p->selected; })) continue;
            if (matches.size() == 1) {
                link.target=matches.front()->key;
                link.status=matches.front()->selected ? "selected" : "stored";
                if (!matches.front()->selected) out.linkedStoredOriginals.insert(matches.front()->original);
                if (!caller.selected) out.linkedStoredOriginals.insert(caller.original);
            } else link.status=matches.empty() ? "missing" : "ambiguous";
            out.links.push_back(std::move(link));
        }
    }
    return out;
}
} // namespace fml::psychlib
