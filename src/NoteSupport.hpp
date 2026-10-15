#pragma once
#include "Catalog.hpp"
namespace pi {
struct NoteSupport { std::string type; size_t count=0; Match script,visual; };
inline std::vector<NoteSupport> noteSupport(const Catalog& catalog,const Difficulty& difficulty) {
    std::map<std::string,size_t> counts;
    for(const auto& note:difficulty.chart.notes) if(!note.type.empty()) ++counts[note.type];
    std::vector<NoteSupport> result;
    for(const auto& entry:counts) {
        NoteSupport item; item.type=entry.first; item.count=entry.second;
        item.script=catalog.files.find({"custom_notetypes/"+item.type+".lua","custom_notetypes/"+item.type+".hx","data/notes/"+item.type+".hx",item.type+".lua",item.type+".hx"},difficulty.path.parent_path());
        item.visual=catalog.files.find({"images/"+item.type+".png","images/notes/"+item.type+".png","images/game/notes/"+item.type+".xml","images/notes/"+item.type+"/Animation.json"},difficulty.path.parent_path(),false);
        result.push_back(std::move(item));
    } return result;
}
inline json noteReport(const Catalog& catalog,const Difficulty& difficulty) {
    json report={{"difficulty",difficulty.name},{"types",json::array()},{"preview","selected base graphics; custom behavior pending"}};
    for(const auto& type:noteSupport(catalog,difficulty)) {
        json ambiguity=json::array(); for(const auto& path:type.script.alternatives) ambiguity.push_back(path.u8string());
        report["types"].push_back({{"name",type.type},{"notes",type.count},{"status","behavior-review-required"},{"script",type.script.path.u8string()},{"scriptAlternatives",ambiguity},{"visual",type.visual.path.u8string()},{"preserved",true}});
    } return report;
}
}
