#pragma once
#include "Icons.hpp"
#include "I18n.hpp"
#include "Version.hpp"
#include "../../third_party/miniz/miniz.h"
#include <cstring>
#include <ctime>
#include <sstream>

namespace pi {
inline bool selectableLooseResource(const fs::path& path) {
    const auto ext=lower(path.extension().u8string());
    return ext==".png" || ext==".jpg" || ext==".jpeg" || ext==".ogg" || ext==".mp3" || ext==".wav" || ext==".flac";
}
struct BatchOptions {
    std::set<int> characters,songs,stages,icons;
    std::set<std::pair<int,int>> charts;
    bool songsUsingCharacters=false,bringCharacters=true,bringStages=true,separate=false;
    bool chartCharacters=false,chartStages=false;
    std::set<std::string> resources;   // loose files from Resources (generic u8 paths)
    bool modcharts=true;               // songs carry their modchart as Codename HScript
    bool zip=false;                    // only changes what the install guide says
    bool fitChartToGame=false;         // swap in the chart what the game lacks (songPlan fitToGame)
    std::string name="psych-bundle";
};
inline std::set<int> relatedSongs(const Catalog& catalog,const std::set<int>& characters) {
    std::set<std::string> ids; for(int index:characters) if(index>=0 && (size_t)index<catalog.characters.size()) ids.insert(catalog.characters[index].imported.character.id);
    std::set<int> songs;
    for(size_t i=0;i<catalog.songs.size();++i) for(const auto& d:catalog.songs[i].difficulties)
        for(const auto& line:d.chart.strumLines) for(const auto& id:line.characters) if(ids.count(id)) songs.insert((int)i);
    return songs;
}
// What the marked songs bring along with "Traer personajes" / "Incluir
// escenarios": the catalog entries their charts name, first match first as
// songPlan resolves them. Entries already marked are not counted again.
struct BatchDependencies { std::set<int> characters,stages,songs; };
inline BatchDependencies batchDependencies(const Catalog& c,const BatchOptions& o) {
    BatchDependencies out; auto songs=o.songs;
    if(o.songsUsingCharacters) for(int s:relatedSongs(c,o.characters)) if(songs.insert(s).second) out.songs.insert(s);
    std::map<std::string,int> characters,stages;
    for(int i=0;i<(int)c.characters.size();++i) characters.emplace(lower(c.characters[i].imported.character.id),i);
    for(int i=0;i<(int)c.stages.size();++i) stages.emplace(lower(c.stages[i].imported.stage.id),i);
    for(int s:songs) if(s>=0 && (size_t)s<c.songs.size()) for(const auto& d:c.songs[s].difficulties) {
        if(!d.take) continue;
        if(o.bringCharacters) for(const auto& line:d.chart.strumLines) for(const auto& who:line.characters) {
            auto found=characters.find(lower(who)); if(found!=characters.end() && !o.characters.count(found->second)) out.characters.insert(found->second);
        }
        if(o.bringStages) {
            auto key=d.chart.stage.empty()?fml::psych::vanillaStage(c.songs[s].id):d.chart.stage;
            auto found=stages.find(lower(key)); if(found!=stages.end() && !o.stages.count(found->second)) out.stages.insert(found->second);
        }
    }
    return out;
}
// --- Install guide -----------------------------------------------------------
// A batch leaves a text file that says how to put it into Codename. It is
// written from the final plan, so a partial export describes what it wrote.
// Codename plays one mod from mods/, picked with TAB in the main menu
// (MainMenuState.hx:146, Options.hx:127), and always loads addons/ on top
// (MainState.hx:81-110).
inline std::string installGuideText(const Plan& plan) {
    const bool en=language()==Language::English;
    std::set<std::string> mods,songs,characters,stages,modcharts; size_t icons=0; bool archive=false;
    auto ends=[](const std::string& s,const std::string& tail){ return s.size()>=tail.size() && s.compare(s.size()-tail.size(),tail.size(),tail)==0; };
    for(const auto& f:plan.files) {
        const auto& path=f.path; auto low=lower(path);
        if(ends(low,"data/config/modpack.ini")) mods.insert(path.substr(0,path.size()-std::string("data/config/modpack.ini").size()));
        std::vector<std::string> parts; std::string part;
        for(char ch:path) { if(ch=='/') { parts.push_back(part); part.clear(); } else part+=ch; }
        parts.push_back(part);
        for(size_t i=0;i+2<parts.size();++i) {
            if(lower(parts[i])=="songs" && lower(parts[i+2])=="charts") songs.insert(parts[i+1]);
            if(lower(parts[i])=="songs" && i+3<parts.size() && lower(parts[i+2])=="scripts" && lower(parts[i+3])=="psych-modchart.hx") modcharts.insert(parts[i+1]);
            if(lower(parts[i])=="data" && lower(parts[i+1])=="characters" && ends(low,".xml")) characters.insert(parts[i+2]);
            if(lower(parts[i])=="data" && lower(parts[i+1])=="stages" && ends(low,".xml")) stages.insert(parts[i+2]);
            if(lower(parts[i])=="images" && lower(parts[i+1])=="icons" && ends(low,".png")) ++icons;
        }
        if(low.find("psych-scripts/")!=std::string::npos) archive=true;
    }
    auto list=[en](const std::set<std::string>& names,size_t most) {
        std::string out; size_t n=0;
        for(const auto& name:names) { if(n==most) break; out+=(out.empty()?"":", ")+name; ++n; }
        if(names.size()>most) out+=(en?" and ":" y ")+std::to_string(names.size()-most)+(en?" more":" más");
        return out;
    };
    char when[32]{}; std::time_t now=std::time(nullptr); std::tm local{}; localtime_s(&local,&now); std::strftime(when,sizeof when,"%d/%m/%Y %H:%M",&local);
    std::string t="Psych Importer " PSYCH_IMPORTER_VERSION " - "+plan.label+" - "+when+"\n\n";
    t+=en?"WHAT IS IN IT\n":"QUÉ HAY\n";
    auto count=[&](size_t n,const char* es1,const char* esN,const char* en1,const char* enN) {
        return std::to_string(n)+" "+(en?(n==1?en1:enN):(n==1?es1:esN));
    };
    t+="  "+count(songs.size(),"canción","canciones","song","songs")+", "+count(characters.size(),"personaje","personajes","character","characters")+", "+
       count(stages.size(),"escenario","escenarios","stage","stages")+", "+count(icons,"icono","iconos","icon","icons")+" ("+
       count(plan.files.size()+1,"archivo","archivos","file","files")+").\n";
    if(!modcharts.empty()) t+=std::string(en?"  Converted modchart in: ":"  Con modchart convertido: ")+list(modcharts,12)+".\n";
    t+=en?"\nHOW TO INSTALL IT\n":"\nCÓMO INSTALARLO\n";
    if(mods.size()==1) {
        auto folder=*mods.begin(); if(!folder.empty() && folder.back()=='/') folder.pop_back();
        if(plan.guideInPlace) {
            t+=en?"  It is already in Codename's mods folder"+std::string(plan.guideZip?" as a ZIP, which Codename loads without unpacking":"")+". Open Codename, press TAB in the main menu and pick \""+folder+"\".\n"
                 :"  Ya está en la carpeta mods de Codename"+std::string(plan.guideZip?", en ZIP, que Codename carga sin descomprimir":"")+". Abre Codename, pulsa TAB en el menú principal y elige \""+folder+"\".\n";
        } else if(plan.guideZip) {
            // The ZIP holds the mod at its root: Codename loads it as it is.
            t+=en?"  1. Put "+folder+".zip into \"mods\", the folder next to CodenameEngine.exe, as it is: Codename loads it without unpacking.\n"
                   "     To unpack it instead, do it into a new folder, mods/"+folder+"/ (it must end up as mods/"+folder+"/data/config/modpack.ini).\n"
                 :"  1. Pon "+folder+".zip dentro de \"mods\", la carpeta que está junto a CodenameEngine.exe, tal cual: Codename lo carga sin descomprimir.\n"
                   "     Si prefieres descomprimirlo, hazlo en una carpeta nueva, mods/"+folder+"/ (tiene que quedar mods/"+folder+"/data/config/modpack.ini).\n";
        } else {
            t+=en?"  1. Copy the folder \""+folder+"\" into \"mods\", the folder next to CodenameEngine.exe.\n"
                 :"  1. Copia la carpeta \""+folder+"\" dentro de \"mods\", la carpeta que está junto a CodenameEngine.exe.\n";
            t+=(en?"     It must end up as mods/":"     Tiene que quedar así: mods/")+folder+"/data/config/modpack.ini\n";
        }
        if(!plan.guideInPlace)
            t+=en?"  2. Open Codename, press TAB in the main menu and pick \""+folder+"\".\n"
                 :"  2. Abre Codename, pulsa TAB en el menú principal y elige \""+folder+"\".\n";
        if(!songs.empty()) t+=en?"  The songs are in Freeplay.\n":"  Las canciones salen en Freeplay.\n";
    } else {
        t+=en?"  Every folder holding data/config/modpack.ini is a mod of its own:\n":"  Cada carpeta que tiene data/config/modpack.ini es un mod aparte:\n";
        size_t n=0; for(const auto& m:mods) { if(n++==60) { t+="    ...\n"; break; } t+="    "+m+"\n"; }
        if(plan.guideZip) t+=en?"  Unpack the ZIP somewhere else first: dropped into mods as it is, Codename would read it as one empty mod.\n"
                                :"  Descomprime antes el ZIP en otra carpeta: tal cual en mods, Codename lo leería como un solo mod vacío.\n";
        t+=en?"  1. Copy the ones you want into \"mods\", next to CodenameEngine.exe (from a ZIP, extract them first).\n"
               "     Codename plays one mod at a time: press TAB in the main menu to pick it.\n"
               "  2. A character, stage or icon you want in every mod can go into \"addons\" (next to \"mods\") instead.\n"
             :"  1. Copia las que quieras dentro de \"mods\", junto a CodenameEngine.exe (si vienen en ZIP, descomprímelas antes).\n"
               "     Codename juega un mod cada vez: con TAB en el menú principal eliges cuál.\n"
               "  2. Un personaje, escenario o icono que quieras en todos los mods puede ir en \"addons\" (junto a \"mods\").\n";
    }
    std::vector<std::string> review; for(const auto& w:plan.warnings) if(importantNotice(w)) review.push_back(message(w));
    if(!review.empty()) {
        t+=en?"\nCHECK BEFORE PLAYING\n":"\nANTES DE JUGAR, REVISA\n";
        for(size_t i=0;i<review.size() && i<40;++i) t+="  - "+review[i]+"\n";
        if(review.size()>40) t+=en?"  ...and "+std::to_string(review.size()-40)+" more in the report Psych Importer showed when exporting.\n"
                                  :"  ...y "+std::to_string(review.size()-40)+" más en el informe que mostró Psych Importer al exportar.\n";
    }
    t+=en?"\nOTHER FILES\n":"\nOTROS ARCHIVOS\n";
    if(!modcharts.empty()) t+=en?"  songs/<song>/scripts/psych-modchart.hx is the converted modchart. If a song breaks because of it, delete that file and it plays without one.\n"
                                :"  songs/<canción>/scripts/psych-modchart.hx es el modchart convertido. Si una canción falla por él, borra ese archivo y se juega sin modchart.\n";
    if(archive) t+=en?"  psych-scripts/ keeps the original Psych scripts and reports, for reference only: Codename does not run them.\n"
                     :"  psych-scripts/ guarda los scripts e informes originales de Psych, solo de referencia: Codename no los ejecuta.\n";
    t+=en?"  This file can be deleted.\n":"  Este archivo se puede borrar.\n";
    std::string crlf; crlf.reserve(t.size()+t.size()/20);
    for(char ch:t) { if(ch=='\n') crlf+='\r'; crlf+=ch; }
    return crlf;
}
inline void refreshInstallGuide(Plan& plan) {
    if(plan.guidePath.empty()) return;
    plan.files.erase(std::remove_if(plan.files.begin(),plan.files.end(),[&](const Output& f){ return f.path==plan.guidePath; }),plan.files.end());
    if(plan.files.empty()) return;
    plan.files.push_back({plan.guidePath,installGuideText(plan),{}});
}
// Exporting straight into Codename's mods folder: its assets/ is the base
// even when none was chosen, so what Codename already has is not copied.
inline fs::path codenameAssetsBeside(const fs::path& destination) {
    if(lower(destination.filename().u8string())!="mods") return {};
    auto root=destination.parent_path();
    if(!fml::psych::file(root/"CodenameEngine.exe") || !fml::psych::directory(root/"assets")) return {};
    return root/"assets";
}
inline Plan batchPlan(const Catalog& catalog,const fs::path& destination,const fs::path& chosenBase,const BatchOptions& options) {
    Plan result; result.destination=destination; result.label=options.name;
    const fs::path base=chosenBase.empty()?codenameAssetsBeside(destination):chosenBase;
    // Separate bundles sit in songs/<id>/, characters/<id>/...: straight in
    // Codename's mods/ they would be found as mods called "songs" and
    // "characters", empty, and the real ones would not be found at all.
    if(options.separate && !codenameAssetsBeside(destination).empty())
        result.errors.push_back("Los mods por separado no van directamente en la carpeta mods de Codename: expórtalos a otra carpeta y copia a mods los que quieras.");
    if(!validId(options.name)) { result.errors.push_back("ID de canción inválido."); return result; }
    auto songSelection=options.songs;
    if(options.songsUsingCharacters) { auto related=relatedSongs(catalog,options.characters); songSelection.insert(related.begin(),related.end()); }
    std::map<std::string,size_t> paths;
    auto append=[&](Plan plan,const std::string& category,const std::string& id) {
        auto prefix=options.separate?category+"/"+id+"/":options.name+"/";
        for(const auto& binding:plan.iconBindings){auto key=prefix+binding.first;auto old=result.iconBindings.find(key);if(old!=result.iconBindings.end() && old->second!=binding.second)result.errors.push_back("Dos recursos escribirían la misma ruta: "+key);else result.iconBindings[key]=binding.second;}
        result.errors.insert(result.errors.end(),plan.errors.begin(),plan.errors.end());
        result.warnings.insert(result.warnings.end(),plan.warnings.begin(),plan.warnings.end());
        for(const auto& event:plan.events) { auto copy=event; copy["song"]=id; result.events.push_back(copy); }
        for(const auto& notes:plan.noteTypes) { auto copy=notes; copy["song"]=id; result.noteTypes.push_back(copy); }
        for(auto file:plan.files) {
            // A batch has its own Freeplay list assembled once below.
            if(file.path=="data/config/freeplaySonglist.txt" || file.path=="data/config/modpack.ini") continue;
            file.path=prefix+file.path; auto key=lower(file.path);
            if(paths.count(key)) {
                const auto& old=result.files[paths[key]];
                if(old.source!=file.source || old.text!=file.text) result.errors.push_back("Dos recursos escribirían la misma ruta: "+file.path);
            } else { paths[key]=result.files.size(); result.files.push_back(std::move(file)); }
        }
        if(options.separate && category=="songs") addText(result,prefix+"data/config/freeplaySonglist.txt",id+"\n");
        if(options.separate && !paths.count(lower(prefix+"data/config/modpack.ini"))) {
            paths[lower(prefix+"data/config/modpack.ini")]=result.files.size();addText(result,prefix+"data/config/modpack.ini",modConfigText(id));
        }
    };
    // Plan individual bundles against an empty destination view. Collision
    // checks apply to final prefixed paths, not an unrelated installed mod.
    auto projected=destination/fs::u8path(options.separate?".psych-plan-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()):options.name);
    auto prepare=[&](Plan p) {
        p.errors.erase(std::remove_if(p.errors.begin(),p.errors.end(),[](const std::string& e){
            return e=="Elige una carpeta de destino existente.";
        }),p.errors.end()); return p;
    };
    // One item that cannot be exported is left out with its reason; the rest
    // of the bundle is still written.
    auto appendOrSkip=[&](Plan plan,const std::string& category,const std::string& id,const std::string& what) {
        plan=prepare(plan);
        if(!plan.errors.empty()) {
            // "Se omite el personaje x: <reason>", without repeating "x: ".
            for(auto& e:plan.errors) if(e.rfind(id+": ",0)==0) e=e.substr(id.size()+2);
            result.warnings.push_back("Se omite "+what+" "+id+": "+joined(plan.errors)); return false;
        }
        append(plan,category,id); return true;
    };
    // Two definitions with the same id (Rev-Mixed ships two bf) would write
    // the same files: the later one gets -2, -3... like songs below.
    auto uniqueId=[&](const std::string& id,std::set<std::string>& used,const char* what) {
        std::string out=id; for(int n=2;used.count(lower(out));++n) out=id+"-"+std::to_string(n);
        if(out!=id) result.warnings.push_back(std::string("Hay más de un ")+what+" "+id+": este se exporta como "+out+".");
        used.insert(lower(out)); return out;
    };
    std::set<std::string> characterIds,stageIds;
    for(int i:options.characters) if(i>=0 && (size_t)i<catalog.characters.size()) {
        const auto& c=catalog.characters[i]; auto id=uniqueId(c.imported.character.id,characterIds,"personaje");
        appendOrSkip(characterPlan(catalog,c,projected,id),"characters",id,"el personaje");
    }
    for(int i:options.stages) if(i>=0 && (size_t)i<catalog.stages.size()) {
        const auto& s=catalog.stages[i];
        // A stage the source engine compiled in can only be rebuilt from its
        // image keys, without layout. When Codename ships that stage, its
        // own stays: the rebuilt spooky would replace the one with lightning.
        if(reconstructedFromEngine(s) && !base.empty() && fml::psych::file(base/fs::u8path("data/stages/"+s.imported.stage.id+".xml"))) {
            result.warnings.push_back("Se usa el escenario "+s.imported.stage.id+" de Codename: el de origen va compilado en su motor y solo se reconstruiría sin su disposición.");
            continue;
        }
        auto id=uniqueId(s.imported.stage.id,stageIds,"escenario");
        appendOrSkip(stagePlan(catalog,s,projected,id),"stages",id,"el escenario");
    }
    std::string freeplay; std::set<std::string> songIds;
    std::set<std::string> supplied;
    if(!options.separate)for(const auto& file:result.files) {
        const auto prefix=options.name+"/";
        if(file.path.rfind(prefix,0)==0)supplied.insert(lower(file.path.substr(prefix.size())));
    }
    for(int i:songSelection) if(i>=0 && (size_t)i<catalog.songs.size()) {
        const auto& s=catalog.songs[i];
        // Two song groups with the same id (Wii Funkin': boxing-match) would
        // write the same songs/<id>/ folder: the later one gets -2, -3...
        std::string id=s.id; for(int n=2;songIds.count(lower(id));++n) id=s.id+"-"+std::to_string(n);
        if(id!=s.id) result.warnings.push_back("Hay más de una canción "+s.id+": esta se exporta como "+id+".");
        songIds.insert(lower(id));
        if(appendOrSkip(songPlan(catalog,s,projected,base,id,"",options.bringCharacters,options.bringStages,options.modcharts,options.fitChartToGame,supplied),"songs",id,"la canción")) freeplay+=id+"\n";
    }
    if(!options.separate && !freeplay.empty()) addText(result,options.name+"/data/config/freeplaySonglist.txt",freeplay);
    if(!options.separate) addText(result,options.name+"/data/config/modpack.ini",modConfigText(options.name));
    result.modRoot=false;
    std::set<std::string> iconIds;
    for(int i:options.icons) if(i>=0 && (size_t)i<catalog.icons.size()) {
        // Two icon files with one id (icon-bf.png in two folders) get -2.
        const auto& icon=catalog.icons[i]; auto id=uniqueId(icon.id,iconIds,"icono");
        appendOrSkip(iconPlan(catalog,icon,projected,id),"icons",id,"el icono");
    }
    for(const auto& index:options.charts) if(index.first>=0 && (size_t)index.first<catalog.songs.size() && index.second>=0 && (size_t)index.second<catalog.songs[index.first].difficulties.size()) {
        auto song=catalog.songs[index.first]; auto d=song.difficulties[index.second]; d.take=true; song.difficulties={d};
        auto p=chartBundlePlan(catalog,song,projected,base,song.id,options.chartCharacters,options.chartStages);
        for(auto& file:p.files) {
            if(file.path=="psych-scripts/songs/"+song.id+"/events-report.json") file.path="psych-scripts/charts/"+song.id+"/"+d.name+"/events-report.json";
            if(file.path=="psych-scripts/songs/"+song.id+"/note-types-report.json") file.path="psych-scripts/charts/"+song.id+"/"+d.name+"/note-types-report.json";
            if(file.path=="psych-scripts/songs/"+song.id+"/resource-bindings.json")file.path="psych-scripts/charts/"+song.id+"/"+d.name+"/resource-bindings.json";
        }
        appendOrSkip(p,"charts",song.id+"-"+d.name,"el chart");
    }
    // Loose resources marked in Resources go where a Codename mod keeps them
    // (images/, music/, sounds/), from that folder on: Psych keeps them under
    // assets/shared, assets/<week> or mods/<mod>. An image brings the atlas
    // beside it. Whatever a song, character or stage already writes wins.
    if(!options.resources.empty()) {
        const auto prefix=options.separate?"resources/"+options.name+"-resources/":options.name+"/";
        int added=0;
        auto place=[&](const fs::path& path)->std::string {
            auto relative=path.generic_u8string();
            for(const auto& root:catalog.files.roots) if(within(root,path)) { relative=path.lexically_relative(root).generic_u8string(); break; }
            std::vector<std::string> parts; std::string part;
            for(char ch:relative) { if(ch=='/') { if(!part.empty()) parts.push_back(part); part.clear(); } else part+=ch; }
            if(!part.empty()) parts.push_back(part);
            for(size_t i=0;i+1<parts.size();++i) {
                auto folder=lower(parts[i]);
                if(folder=="images" || folder=="music" || folder=="sounds") { std::string out; for(size_t k=i;k<parts.size();++k) out+=(k>i?"/":"")+parts[k]; return out; }
            }
            return {};
        };
        auto add=[&](const fs::path& path) {
            auto inside=place(path);
            if(inside.empty() || !fml::psych::relativeKey(inside)) { result.warnings.push_back("Se omite el recurso "+path.filename().u8string()+": no está en images/, music/ ni sounds/ del mod."); return; }
            auto ext=lower(path.extension().u8string());
            if((ext==".mp3" || ext==".wav" || ext==".flac")) { result.warnings.push_back("Se omite el recurso "+inside+": Codename necesita OGG Vorbis."); return; }
            auto key=lower(prefix+inside);
            if(paths.count(key)) return;   // already written by its song, character or stage
            paths[key]=result.files.size(); result.files.push_back({prefix+inside,{},path}); ++added;
        };
        for(const auto& key:options.resources) {
            auto path=fs::u8path(key);
            if(!selectableLooseResource(path)) {result.warnings.push_back("Se omite el recurso "+path.filename().u8string()+": su tipo no se puede marcar en Recursos.");continue;}
            if(!fml::psych::file(path)) { result.warnings.push_back("Se omite el recurso "+path.filename().u8string()+": ya no está en el disco."); continue; }
            add(path);
            auto ext=lower(path.extension().u8string());
            if(ext==".png") for(const char* atlas:{".xml",".txt"}) { auto sibling=path; sibling.replace_extension(atlas); if(fml::psych::file(sibling)) add(sibling); }
        }
        if(added && options.separate && !paths.count(lower(prefix+"data/config/modpack.ini"))) {
            paths[lower(prefix+"data/config/modpack.ini")]=result.files.size(); addText(result,prefix+"data/config/modpack.ini",modConfigText(options.name+"-resources"));
        }
        logs::info("Recursos sueltos en "+options.name+": "+std::to_string(added)+" archivos de "+std::to_string(options.resources.size())+" marcados");
    }
    const bool hasResource=std::any_of(result.files.begin(),result.files.end(),[](const Output& f) {
        const auto path=lower(f.path);
        return !archiveCopy(path) && path.find("freeplaysonglist.txt")==std::string::npos &&
               (path.size()<std::string("data/config/modpack.ini").size() || path.compare(path.size()-std::string("data/config/modpack.ini").size(),std::string("data/config/modpack.ini").size(),"data/config/modpack.ini")!=0);
    });
    if(!hasResource) result.errors.push_back("Selecciona al menos un recurso.");
    std::sort(result.warnings.begin(),result.warnings.end()); result.warnings.erase(std::unique(result.warnings.begin(),result.warnings.end()),result.warnings.end());
    // Inside the bundle folder, or beside the separate bundles.
    result.guidePath=options.separate?options.name+"-"+ui("COMO-INSTALAR.txt","HOW-TO-INSTALL.txt"):options.name+"/"+ui("COMO-INSTALAR.txt","HOW-TO-INSTALL.txt");
    result.guideZip=options.zip;
    result.guideInPlace=!options.separate && !codenameAssetsBeside(destination).empty();
    refreshInstallGuide(result);
    destinationCheck(result,catalog); return result;
}
// --- "Mark the whole mod" -------------------------------------------------------
// What the mod adds, not everything the catalog sees. A Psych install also
// carries the base game in assets/ (Bopeebo, bf, the week stages, the menus);
// Codename has its own, and marking them replaced Codename's bf and filled the
// bundle with Psych's menus. So: the mod's songs (a base-game song only when
// Codename does not have it), and the characters, stages and icons of the
// mod's own folders; the songs bring the rest they use. Nothing that cannot
// be exported is marked, and loose resources stay a choice of Resources.
inline bool engineBasePath(const fs::path& path) {
    bool assets=false;
    for(const auto& part:path) { auto p=lower(part.u8string()); if(p=="mods") return false; if(p=="assets") assets=true; }
    return assets;
}
inline bool modFolderPath(const fs::path& path) { for(const auto& part:path) if(lower(part.u8string())=="mods") return true; return false; }
inline std::string songKey(const std::string& id) { std::string k; for(char c:lower(id)) if(std::isalnum((unsigned char)c)) k+=c; return k; }
inline std::set<std::string> baseGameSongs(const fs::path& base) {
    std::set<std::string> out; std::error_code ec;
    if(!base.empty()) for(const auto& entry:fs::directory_iterator(base/"songs",ec)) if(entry.is_directory(ec)) out.insert(songKey(entry.path().filename().u8string()));
    // Without a Codename install at hand, the weeks every engine ships.
    if(out.empty()) for(const char* id:{"tutorial","bopeebo","fresh","dadbattle","spookeez","south","monster","pico","phillynice","blammed","satinpanties","high","milf",
                                       "cocoa","eggnog","winterhorrorland","senpai","roses","thorns","ugh","guns","stress","test"}) out.insert(id);
    return out;
}
inline bool playableSong(const Song& song) {
    if(!validId(song.id))return false;
    for(const auto& d:song.difficulties) {
        if(!d.take || !validId(d.name) || d.audio.instPath.empty() || !fml::chartSafetyError(d.chart).empty()) continue;
        auto inst=fml::probeAudioFile(fs::u8path(d.audio.instPath));
        if(inst.readable && inst.container==fml::AudioContainer::OggVorbis) return true;
    }
    return false;
}
struct WholeMod { BatchOptions options; size_t baseSkipped=0,unexportable=0; };
inline WholeMod wholeModSelection(const Catalog& catalog,const fs::path& base,const BatchOptions& current) {
    WholeMod out; out.options=current;
    out.options.characters.clear();out.options.songs.clear();out.options.stages.clear();out.options.icons.clear();
    out.options.songsUsingCharacters=false;
    for(auto it=out.options.resources.begin();it!=out.options.resources.end();)if(!selectableLooseResource(fs::u8path(*it)))it=out.options.resources.erase(it);else ++it;
    const auto probe=fs::temp_directory_path()/fs::u8path("psych-importer-probe-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto clean=[](Plan plan) {
        plan.errors.erase(std::remove_if(plan.errors.begin(),plan.errors.end(),[](const std::string& e){
            return e.rfind("Ya existe; usa otro ID: ",0)==0 || e=="Elige una carpeta de destino existente." || e.rfind("El destino debe estar separado",0)==0; }),plan.errors.end());
        return plan.errors.empty();
    };
    const auto vanilla=baseGameSongs(base);
    for(int i=0;i<(int)catalog.songs.size();++i) {
        const auto& song=catalog.songs[i];
        const bool fromBase=!song.difficulties.empty() && engineBasePath(song.difficulties.front().path);
        if(fromBase && vanilla.count(songKey(song.id))) { ++out.baseSkipped; continue; }
        if(!playableSong(song)) { ++out.unexportable; continue; }
        out.options.songs.insert(i);
    }
    for(int i=0;i<(int)catalog.characters.size();++i) {
        const auto& c=catalog.characters[i];
        // Outside mods/, one Codename already ships is the base game's
        // (Psych 0.7's own tree has no assets/ folder to tell).
        if(engineBasePath(c.source) || (!modFolderPath(c.source) && !base.empty() && fml::psych::file(base/fs::u8path("data/characters/"+c.imported.character.id+".xml")))) { ++out.baseSkipped; continue; }
        if(!clean(characterPlan(catalog,c,probe,c.imported.character.id))) { ++out.unexportable; continue; }
        out.options.characters.insert(i);
    }
    for(int i=0;i<(int)catalog.stages.size();++i) {
        const auto& s=catalog.stages[i];
        if(engineBasePath(s.source) || (!modFolderPath(s.source) && !base.empty() && fml::psych::file(base/fs::u8path("data/stages/"+s.imported.stage.id+".xml")))) { ++out.baseSkipped; continue; }
        if(reconstructedFromEngine(s) && !base.empty() && fml::psych::file(base/fs::u8path("data/stages/"+s.imported.stage.id+".xml"))) { ++out.baseSkipped; continue; }
        if(!clean(stagePlan(catalog,s,probe,s.imported.stage.id))) { ++out.unexportable; continue; }
        out.options.stages.insert(i);
    }
    for(int i=0;i<(int)catalog.icons.size();++i) {
        const auto& icon=catalog.icons[i];
        if(engineBasePath(icon.source)) { ++out.baseSkipped; continue; }
        if(!clean(iconPlan(catalog,icon,probe,icon.id))) { ++out.unexportable; continue; }
        out.options.icons.insert(i);
    }
    for(auto it=out.options.charts.begin();it!=out.options.charts.end();) {
        if(it->first<0 || (size_t)it->first>=catalog.songs.size() || it->second<0 || (size_t)it->second>=catalog.songs[it->first].difficulties.size() || out.options.songs.count(it->first))it=out.options.charts.erase(it);else ++it;
    }
    return out;
}
// --- Partial export ------------------------------------------------------------
// Not everything can be fixed from the importer. A partial export writes what
// is sound and leaves out what blocks, each with its reason, the way a batch
// already leaves out an item that cannot be exported. Only a plan without
// destination, inside its own sources or with nothing left stays blocked.
inline bool structuralError(const std::string& e) {
    return e=="Elige una carpeta de destino existente." || e.rfind("El destino debe estar separado",0)==0 ||
           e=="Selecciona al menos un recurso." || e=="ID de canción inválido." || e=="Ya existe esa canción. Usa otro ID." ||
           e.rfind("La exportación supera el límite",0)==0 || e.rfind("No hay espacio suficiente",0)==0 ||
           e.rfind("Los mods por separado no van directamente",0)==0;
}
// Every place where `head` starts a path segment (at 0 or after a '/').
inline std::vector<size_t> segmentStarts(const std::string& path,const std::string& head) {
    std::vector<size_t> out;
    for(auto at=path.find(head);at!=std::string::npos;at=path.find(head,at+1)) if(at==0 || path[at-1]=='/') out.push_back(at);
    return out;
}
// Files that only work together. A character XML without its sprite breaks
// where a missing character falls back to bf (Character.hx:591), and a song
// without its Inst or a chart cannot be played (Paths.inst). Losing one part
// leaves the whole unit out. `prefix` is the bundle folder in a batch.
struct PartialUnit { int kind=0; std::string id,prefix; };   // 0 character, 1 stage, 2 song
inline bool partialUnit(const std::string& raw,PartialUnit& unit) {
    const auto path=lower(raw);
    auto take=[&](int kind,size_t at,size_t from,size_t length) { unit.kind=kind; unit.prefix=raw.substr(0,at); unit.id=raw.substr(from,length); return true; };
    for(auto at:segmentStarts(path,"images/characters/")) {
        auto from=at+18; auto cut=path.find("-import",from);
        if(cut==std::string::npos || cut==from || path.find('/',from)<cut) continue;
        auto next=cut+7; if(next<path.size() && path[next]!='.' && path[next]!='/') continue;
        return take(0,at,from,cut-from);
    }
    for(auto at:segmentStarts(path,"data/characters/")) {
        auto from=at+16; auto dot=path.rfind('.');
        if(dot==std::string::npos || dot<=from || path.find('/',from)!=std::string::npos) continue;
        return take(0,at,from,dot-from);
    }
    for(auto at:segmentStarts(path,"images/stages/")) {
        auto from=at+14; auto cut=path.find("-import/",from);
        if(cut==std::string::npos || cut==from || path.find('/',from)<cut) continue;
        return take(1,at,from,cut-from);
    }
    for(auto at:segmentStarts(path,"data/stages/")) {
        auto from=at+12; auto dot=path.rfind('.');
        if(dot==std::string::npos || dot<=from || path.find('/',from)!=std::string::npos) continue;
        return take(1,at,from,dot-from);
    }
    for(auto at:segmentStarts(path,"songs/")) {
        auto from=at+6; auto slash=path.find('/',from); if(slash==std::string::npos || slash==from) continue;
        auto tail=path.substr(slash+1);
        if(tail.rfind("song/inst",0)==0 || tail.rfind("charts/",0)==0 || tail.rfind("meta",0)==0) return take(2,at,from,slash-from);
    }
    return false;
}
inline std::vector<std::string> partialHeads(const PartialUnit& u) {
    const auto& p=u.prefix; const auto& id=u.id;
    if(u.kind==0) return {p+"images/characters/"+id+"-import.",p+"images/characters/"+id+"-import/",p+"data/characters/"+id+".",
                          p+"images/icons/"+id+"-import.png",p+"psych-scripts/characters/"+id+"/"};
    if(u.kind==1) return {p+"images/stages/"+id+"-import/",p+"data/stages/"+id+".",p+"psych-scripts/stages/"+id+"/"};
    return {p+"songs/"+id+"/",p+"psych-scripts/songs/"+id+"/",p+"images/icons/song-"+id+"-import.png"};
}
// Bookkeeping that is not worth writing on its own.
inline bool partialFiller(const std::string& path) {
    auto name=lower(path); const std::string config="data/config/modpack.ini";
    bool modConfig=name.size()>=config.size() && name.compare(name.size()-config.size(),config.size(),config)==0;
    return archiveCopy(name) || modConfig || name.find("freeplaysonglist.txt")!=std::string::npos;
}
inline Plan partialPlan(const Plan& p,std::vector<std::string>* omitted=nullptr) {
    if(p.ready()) { if(omitted) omitted->clear(); return p; }
    std::vector<std::string> blocking,notices;
    std::set<size_t> drop;
    std::vector<PartialUnit> units;
    std::map<std::string,std::vector<size_t>> byPath,bySource;
    for(size_t i=0;i<p.files.size();++i) {
        byPath[lower(p.files[i].path)].push_back(i);
        if(!p.files[i].source.empty()) bySource[p.files[i].source.u8string()].push_back(i);
    }
    auto unitOf=[&](size_t i) { PartialUnit u; if(partialUnit(p.files[i].path,u)) units.push_back(u); };
    for(const auto& e:p.errors) {
        std::string rest;
        auto after=[&](const std::string& head) { if(e.compare(0,head.size(),head)!=0) return false; rest=e.substr(head.size()); return true; };
        if(structuralError(e)) blocking.push_back(e);
        // What the destination already has stays; the plan only adds.
        else if(after("Ya existe; usa otro ID: ")) { const auto found=byPath.find(lower(rest));if(found==byPath.end() || found->second.empty()){blocking.push_back(e);continue;}for(auto i:found->second) drop.insert(i); notices.push_back("Se omite (ya existe en el destino y no se sobrescribe): "+rest); }
        else if(after("Dos recursos escribirían la misma ruta: ")) {
            const auto found=byPath.find(lower(rest));if(found==byPath.end() || found->second.empty()){blocking.push_back(e);continue;}
            const auto& list=found->second; for(size_t k=1;k<list.size();++k) { drop.insert(list[k]); unitOf(list[k]); }
            notices.push_back("Se omite (otro recurso ya escribe esa ruta; se conserva el primero): "+rest);
        }
        else if(after("Ruta destino inválida: ")) { for(auto i:byPath[lower(rest)]) { drop.insert(i); unitOf(i); } notices.push_back("Se omite (ruta destino inválida): "+rest); }
        else if(after("Recurso ausente, vacío o demasiado grande: ")) { for(auto i:bySource[rest]) { drop.insert(i); unitOf(i); } notices.push_back("Se omite (recurso ausente, vacío o demasiado grande): "+rest); }
        else if(after("PNG inválido o excede el límite: ")) { for(auto i:bySource[rest]) { drop.insert(i); unitOf(i); } notices.push_back("Se omite (imagen dañada o demasiado grande): "+rest); }
        else if(e.find(": Animate no válido")!=std::string::npos || e.find(": Animate contiene efectos")!=std::string::npos) {
            auto id=e.substr(0,e.find(": Animate"));
            for(int kind:{0,1}) { PartialUnit u; u.kind=kind; u.id=id; units.push_back(u); }
            notices.push_back("No se exporta: "+e);
        }
        // Written anyway: Codename plays the rest of the character or stage.
        else if(e.find(": animación sin fotogramas: ")!=std::string::npos || e.find(": escenario vacío;")!=std::string::npos) notices.push_back("Se exporta con este problema: "+e);
        else blocking.push_back(e);
    }
    std::set<std::string> seenUnits;
    for(const auto& u:units) {
        if(!seenUnits.insert(std::to_string(u.kind)+"|"+lower(u.prefix)+"|"+lower(u.id)).second) continue;
        auto heads=partialHeads(u); for(auto& h:heads) h=lower(h);
        for(size_t i=0;i<p.files.size();++i) { auto path=lower(p.files[i].path); for(const auto& h:heads) if(path.rfind(h,0)==0) { drop.insert(i); break; } }
        if(u.kind==0) notices.push_back("Se omite el personaje "+u.id+" porque le falta una parte; Codename usará su personaje de reserva (bf) donde aparezca.");
        else if(u.kind==1) notices.push_back("Se omite el escenario "+u.id+" porque le falta una parte; las canciones que lo usan se jugarán sin decorado.");
        else notices.push_back("Se omite la canción "+u.id+" porque le falta su instrumental, un chart o sus metadatos.");
    }
    Plan out=p; out.files.clear(); out.errors=blocking; bool guideKept=false;
    for(size_t i=0;i<p.files.size();++i) {
        if(!p.guidePath.empty() && p.files[i].path==p.guidePath) { guideKept=!drop.count(i); continue; }
        if(drop.count(i)) continue;
        auto file=p.files[i];
        // A left-out song also leaves its Freeplay list.
        if(lower(file.path).find("freeplaysonglist.txt")!=std::string::npos && file.source.empty()) {
            for(const auto& u:units) if(u.kind==2 && lower(file.path).rfind(lower(u.prefix),0)==0) {
                std::stringstream in(file.text); std::string line,kept;
                while(std::getline(in,line)) { auto clean=line; if(!clean.empty() && clean.back()=='\r') clean.pop_back(); if(lower(clean)!=lower(u.id)) kept+=line+"\n"; }
                file.text=kept;
            }
            if(file.text.find_first_not_of(" \t\r\n")==std::string::npos) continue;
            if(fml::psych::file(p.destination/fs::u8path(file.path)) && read(p.destination/fs::u8path(file.path))==file.text) continue;
        }
        out.files.push_back(std::move(file));
    }
    if(std::all_of(out.files.begin(),out.files.end(),[](const Output& f){return partialFiller(f.path);})) {
        out.files.clear(); if(out.errors.empty()) out.errors.push_back("No queda nada que exportar sin lo bloqueado.");
    }
    std::sort(notices.begin(),notices.end()); notices.erase(std::unique(notices.begin(),notices.end()),notices.end());
    out.warnings=notices; out.warnings.insert(out.warnings.end(),p.warnings.begin(),p.warnings.end());
    if(guideKept) refreshInstallGuide(out); else out.guidePath.clear();
    if(omitted) *omitted=notices;
    return out;
}
// Entries go in with their sizes in the local header and no data descriptor:
// Codename 1.0.1 reads a ZIP of mods/ by walking those headers (SysZip on
// haxe.zip.Reader) and miniz's descriptors left it at "Invalid Zip Data"
// before the title screen. A single bundle sits at the ZIP's root, which is
// where Codename looks for data/, songs/... in a ZIP mod (ZipFolderLibrary);
// separate bundles keep their folders and are meant to be unpacked.
struct ZipSource { FILE* file=nullptr; const std::string* text=nullptr; };
inline size_t zipRead(void* opaque,mz_uint64 offset,void* buffer,size_t n) {
    auto* source=static_cast<ZipSource*>(opaque);
    if(source->text) { if(offset>=source->text->size()) return 0; n=std::min<size_t>(n,(size_t)(source->text->size()-offset)); std::memcpy(buffer,source->text->data()+offset,n); return n; }
    if(_fseeki64(source->file,(long long)offset,SEEK_SET)!=0) return 0;
    return std::fread(buffer,1,n,source->file);
}
inline bool zipAdd(mz_zip_archive& zip,const std::string& name,const std::string* text,const fs::path& path) {
    // MZ_DEFAULT_LEVEL, not MZ_DEFAULT_COMPRESSION: that one is -1 and, or-ed
    // with the flags, makes miniz drop them all.
    mz_uint flags=MZ_DEFAULT_LEVEL|MZ_ZIP_FLAG_WRITE_HEADER_SET_SIZE;
    if(std::all_of(name.begin(),name.end(),[](char c){ return (unsigned char)c<0x80; })) flags|=MZ_ZIP_FLAG_ASCII_FILENAME;
    ZipSource source; mz_uint64 size=0;
    if(text) { source.text=text; size=text->size(); }
    else {
        std::error_code ec; size=fs::file_size(path,ec); if(ec || !size) return false;
        source.file=_wfopen(path.c_str(),L"rb"); if(!source.file) return false;
    }
    const bool ok=mz_zip_writer_add_read_buf_callback(&zip,name.c_str(),zipRead,&source,size,nullptr,nullptr,0,flags,nullptr,0,nullptr,0)!=0;
    if(source.file) std::fclose(source.file);
    return ok;
}
inline std::string executeZip(const Plan& plan,bool* completed=nullptr) {
    if(completed)*completed=false;
    if(!plan.ready()) { logs::warn("ZIP bloqueado: "+plan.label); logs::list("WARN","Bloqueos",plan.errors); return describe(plan); }
    if(!validId(plan.label)) return "ID de canción inválido.";
    for(const auto& file:plan.files)if(!unchangedSource(file))return "Importación cancelada: la fuente cambió desde la revisión del plan: "+file.source.u8string();
    std::set<std::string> checkedImages;
    for(const auto& file:plan.files)if(!file.source.empty() && lower(fs::u8path(file.path).extension().u8string())==".png" && checkedImages.insert(lower(file.source.lexically_normal().generic_u8string())).second && !validPng(file.source,nullptr,nullptr,true))
        return "Importación cancelada antes de escribir: "+file.source.u8string();
    // miniz writes this ZIP without ZIP64: past 3.5 GB it would fail halfway.
    {
        unsigned long long total=0; for(const auto& file:plan.files) { std::error_code ec; total+=file.source.empty()?file.text.size():fs::file_size(file.source,ec); }
        if(total>3500ull*1024ull*1024ull) { logs::warn("ZIP demasiado grande: "+plan.label+" · "+std::to_string(total>>20)+" MB"); return "El ZIP superaría 3,5 GB; exporta a carpeta o divide la selección.\n"+describe(plan); }
    }
    const auto started=std::chrono::steady_clock::now();
    logs::info("ZIP: "+plan.label+" -> "+plan.destination.u8string()+" · "+std::to_string(plan.files.size())+" archivos");
    auto target=plan.destination/fs::u8path(plan.label+".zip");
    std::error_code ec;
    if(fs::exists(target,ec) || !within(plan.destination,target)) return "Ya existe; usa otro ID: "+target.u8string();
    // In Codename's mods/ a folder and a ZIP with one name are one mod twice.
    if(fml::psych::directory(plan.destination/fs::u8path(plan.label))) return "Ya existe; usa otro ID: "+(plan.destination/fs::u8path(plan.label)).u8string();
    const std::string prefix=plan.label+"/";
    const bool single=!plan.files.empty() && std::all_of(plan.files.begin(),plan.files.end(),[&](const Output& f){ return f.path.rfind(prefix,0)==0; });
    auto stamp=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    auto temporary=plan.destination/fs::u8path(".psych-zip-"+stamp+".tmp");
    mz_zip_archive zip{};
    FILE* handle=_wfopen(temporary.c_str(),L"wb+");
    if(!handle) return "No se pudo crear el ZIP.";
    if(!mz_zip_writer_init_cfile(&zip,handle,0)) { std::fclose(handle); fs::remove(temporary,ec); return "No se pudo crear el ZIP."; }
    bool ok=true; size_t count=0;
    for(const auto& file:plan.files) {
        if(!fml::psych::relativeKey(file.path)) { ok=false; break; }
        const std::string name=single?file.path.substr(prefix.size()):file.path;
        if(!zipAdd(zip,name,file.source.empty()?&file.text:nullptr,file.source)) { ok=false; break; }
        ++count;
    }
    json manifest={{"format","psych-importer-bundle-1"},{"id",plan.label},{"events",plan.events},{"noteTypes",plan.noteTypes},{"warnings",plan.warnings},{"files",count}};
    auto metadata=manifest.dump(2);
    if(ok) ok=zipAdd(zip,".psych-importer/bundle-report.json",&metadata,{});
    if(ok) ok=mz_zip_writer_finalize_archive(&zip)!=0;
    mz_zip_writer_end(&zip);
    std::fclose(handle);
    if(ok)for(const auto& file:plan.files)if(!unchangedSource(file)){ok=false;break;}
    if(ok && !fs::exists(target,ec)) { fs::rename(temporary,target,ec); ok=!ec; } else ok=false;
    if(!ok) { fs::remove(temporary,ec); logs::error("No se pudo crear el ZIP "+target.u8string()+" (tras "+std::to_string(count)+" archivos)"); return "No se pudo crear el ZIP.\n"+describe(plan); }
    logs::info("ZIP creado: "+target.u8string()+" · "+std::to_string(count)+" archivos · "+std::to_string(logs::since(started))+" ms");
    if(completed)*completed=true;
    return "ZIP creado: "+target.u8string()+"\n"+describe(plan);
}
}
