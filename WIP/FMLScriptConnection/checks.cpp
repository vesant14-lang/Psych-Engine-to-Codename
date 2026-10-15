#include "upstream/src/fml_formats/PsychBehaviorPlan.hpp"
#include "upstream/src/fml_formats/PsychBehaviorSource.hpp"
#include "upstream/src/fml_formats/PsychSourceBinding.hpp"
#include "upstream/src/fml_formats/PsychScriptRelations.hpp"
#include "upstream/src/fml_formats/PsychCompat.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

void require(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
int main() {
    try {
        using namespace fml;
        const std::string source="function onCreate()\nsetProperty('defaultCamZoom', 0.8)\nunknownWipCall('review')\nend\n";
        auto deps=psych::scriptDependencies("function onCreate()\naddLuaScript('scripts/helper')\nend", "data/demo/script.lua");
        require(deps.size()==1 && deps[0].key=="scripts/helper","literal dependency was not discovered");
        std::vector<std::pair<std::string,std::string>> files={{"memory/song", "data/demo/script.lua"},{"memory/global","scripts/unrelated.lua"},{"memory/broken","data/demo/broken.lua"}};
        psychplan::Options options;options.songId="demo";options.psychSongName="Demo";options.activate=false;
        options.sourceText={{"memory/song",source},{"memory/global","function onBeatHit()\nreturn 1\nend"},{"memory/broken","function onCreate(\n"}};
        const auto off=psychplan::planBehavior(files,{},options);
        require(off.active==0 && off.approximate==0,"inactive plan connected behavior");
        for(const auto& entry:off.scripts)require(!entry.active && entry.destination.empty(),"inactive plan has executable output");
        options.activate=true;
        const auto plan=psychplan::planBehavior(files,{},options);
        auto find=[&](const char* key)->const psychplan::Script*{for(const auto& entry:plan.scripts)if(entry.key==key)return &entry;return nullptr;};
        const auto* song=find("data/demo/script.lua");
        require(song && song->active && song->destination=="songs/demo/scripts/script.hx","song behavior has no native destination");
        require(!song->pendingCalls.empty() && song->hscript.find("psych.pending")!=std::string::npos,"unknown call lost its explicit pending record");
        const auto* global=find("scripts/unrelated.lua");
        require(global && !global->active && global->destination.empty(),"unrelated global was activated");
        const auto* broken=find("data/demo/broken.lua");
        require(broken && !broken->active && broken->status=="Bloqueado","malformed Lua was not blocked");
        require(!psychlib::safeKey("../escape.lua") && psychlib::safeKey("scripts/helper.lua"),"script-library key bounds failed");
        const auto emitted=psychcompat::emit({});const auto installed=psychcompat::inspect(emitted);
        require(installed.ours && !installed.edited,"generated compatibility layer failed its integrity check");
        require(psychcompat::inspect(emitted+"\n// user edit\n").edited,"edited compatibility layer was treated as unchanged");
        const auto path=std::filesystem::path("../upstream/src/fml_formats/psych-compat/fml-psych-compat.hx");
        std::ifstream input(path,std::ios::binary);require(input.good(),"compatibility source is missing");
        std::string body{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        body.erase(std::remove(body.begin(),body.end(),'\r'),body.end());
        require(body==psychcompat::body(),"embedded compatibility layer differs from its HScript source");
        std::cout<<"PASS: WIP source snapshot compiles; dependencies, behavior plans, unresolved calls and compatibility integrity verified. No generated script was executed.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
