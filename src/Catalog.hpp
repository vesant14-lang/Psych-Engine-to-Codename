#pragma once
#include "../../src/fml_formats/PsychCharacter.hpp"
#include "../../src/fml_formats/PsychSongAudio.hpp"
#include "../../src/fml_formats/ChartExchange.hpp"
#include "../../src/fml_formats/CodenameChart.hpp"
#include "../../src/fml_formats/SongMeta.hpp"
#include "../../src/fml_formats/SparrowAtlas.hpp"
#include "../../src/fml_formats/AnimateAtlas.hpp"
#include "Resources.hpp"
#include "../../src/fml_notelab/NoteStyleRead.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <set>

namespace pi {
namespace fs = std::filesystem;
using json = nlohmann::json;
inline std::string lower(std::string s) { for (auto& c:s) c=char(std::tolower((unsigned char)c)); return s; }
inline std::string read(const fs::path& p) { return fml::psych::read(p); }
inline json document(const fs::path& p) {
    std::error_code ec;
    if (fs::file_size(p,ec)>32u*1024u*1024u || ec) return json();
    return json::parse(read(p),nullptr,false,true);
}
inline std::string field(const json& j,const char* key) {
    return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : "";
}
struct Match { fs::path path; std::vector<fs::path> alternatives; };
struct Files {
    std::vector<fs::path> roots, paths;
    std::vector<std::string> warnings;
    // Lower-case generic form of `paths`, built once instead of per lookup:
    // each find() used to convert all 2,700 paths of a mod for every key.
    mutable std::vector<std::string> lowered;
    const std::vector<std::string>& loweredPaths() const {
        if(lowered.size()!=paths.size()) { lowered.clear(); lowered.reserve(paths.size()); for(const auto& p:paths) lowered.push_back(lower(p.generic_u8string())); }
        return lowered;
    }
    void addTree(const fs::path& root) {
        std::error_code ec;
        if (!fs::is_directory(root,ec)) { warnings.push_back("No se reconoce carpeta: "+root.u8string()+" · "+ec.message()); return; }
        auto normalized=fs::weakly_canonical(root,ec);
        if (ec) { warnings.push_back("No se pudo resolver carpeta: "+root.u8string()+" · "+ec.message()); return; }
        for (const auto& old:roots) if (lower(old.u8string())==lower(normalized.u8string())) return;
        roots.push_back(normalized);
        fs::recursive_directory_iterator it(normalized,fs::directory_options::skip_permission_denied,ec), end;
        for (;it!=end && paths.size()<100000;it.increment(ec)) {
            if (ec) { warnings.push_back("No se pudo revisar una carpeta: "+ec.message()); ec.clear(); continue; }
            if (it->is_symlink(ec)) { it.disable_recursion_pending(); continue; }
            if (it->is_directory(ec)) {
                auto name=lower(it->path().filename().u8string());
                if(it.depth()>=10 || name==".git" || name==".fml") it.disable_recursion_pending();
                continue;
            }
            if(it->is_regular_file(ec)) paths.push_back(it->path());
        }
        if(paths.size()>=100000) warnings.push_back("Límite de 100.000 archivos alcanzado; divide la carpeta.");
        std::sort(paths.begin(),paths.end());
        paths.erase(std::unique(paths.begin(),paths.end()),paths.end());
    }
    Match find(const std::vector<std::string>& keys,const fs::path& context={},bool basenameFallback=true) const {
        // Exact expected paths win. Broad lookup never picks an ambiguous basename.
        for(const auto& key:keys) {
            if(!fml::psych::relativeKey(key)) continue;
            if(!context.empty()) {
                auto probe=context/fs::u8path(key);
                if(fml::psych::file(probe)) return {probe,{}};
            }
            for(const auto& root:roots) {
                auto probe=root/fs::u8path(key);
                if(fml::psych::file(probe)) return {probe,{}};
            }
        }
        const auto& low=loweredPaths();
        for(const auto& key:keys) {
            if(!fml::psych::relativeKey(key)) continue;
            std::vector<fs::path> candidates;
            auto suffix=lower(fs::u8path(key).generic_u8string()), base=lower(fs::u8path(key).filename().u8string());
            for(size_t i=0;i<paths.size();++i) {
                const auto& full=low[i];
                if(full.size()>suffix.size() && full.compare(full.size()-suffix.size(),suffix.size(),suffix)==0 && full[full.size()-suffix.size()-1]=='/') candidates.push_back(paths[i]);
            }
            if(candidates.empty() && basenameFallback) for(size_t i=0;i<paths.size();++i) {
                const auto& full=low[i];
                if(full.size()>=base.size() && full.compare(full.size()-base.size(),base.size(),base)==0 && (full.size()==base.size() || full[full.size()-base.size()-1]=='/')) candidates.push_back(paths[i]);
            }
            if(candidates.size()==1) return {candidates.front(),{}};
            if(candidates.size()>1) return {{},candidates};
        }
        return {};
    }
};
inline Match icon(const Files& files,const std::string& id) {
    if(id.empty() || !fml::psych::relativeKey(id)) return {};
    const std::vector<std::string> keys{"images/icons/"+id+".png","images/icons/icon-"+id+".png","icons/"+id+".png","icons/icon-"+id+".png"};
    for(const auto& key:keys) for(const auto& root:files.roots) {
        auto candidate=root/fs::u8path(key); if(fml::psych::file(candidate)) return {candidate,{}};
    }
    std::vector<fs::path> candidates;
    auto qualified=lower("icon-"+id+".png"),bare=lower(id+".png");
    for(const auto& p:files.paths) {
        auto name=lower(p.filename().u8string()); bool iconFolder=false;
        for(const auto& component:p.parent_path()) {
            auto part=lower(component.u8string());
            if(part=="icons" || part=="icon" || part=="healthicons" || part=="health-icons" || part=="badges") iconFolder=true;
        }
        if(name==qualified || (name==bare && iconFolder)) candidates.push_back(p);
    }
    if(candidates.size()==1) return {candidates.front(),{}};
    if(candidates.size()>1) return {{},candidates};
    return {};
}
struct Character {
    fs::path source, image, atlas, iconPath;
    fml::PsychCharacterImport imported;
    std::vector<char> take;
    std::vector<std::string> warnings;
    std::vector<fs::path> imageChoices, atlasChoices, iconChoices;
    bool animate=false;
    bool rawAtlas=false;
    bool manualBinding=false;
    std::shared_ptr<AnimateResource> animateData;
    // Psych's `image` may list several Sparrow sheets ("a,b"): the pages
    // after the first, each image with its atlas (Paths.getMultiAtlas).
    std::vector<std::pair<fs::path,fs::path>> extraSheets;
};
// Frame names of a Packer atlas (FlxAtlasFrames.fromSpriteSheetPacker):
// one "name = x y w h" per line.
inline std::vector<std::string> packerFrameNames(const std::string& text) {
    std::vector<std::string> names; std::stringstream lines(text); std::string line;
    while(std::getline(lines,line)) {
        auto eq=line.find('='); if(eq==std::string::npos)continue;
        auto name=line.substr(0,eq); while(!name.empty() && std::isspace((unsigned char)name.back()))name.pop_back();
        while(!name.empty() && std::isspace((unsigned char)name.front()))name.erase(name.begin());
        if(!name.empty())names.push_back(name);
    }
    return names;
}
inline bool packerAtlas(const fs::path& atlas) { return lower(atlas.extension().u8string())==".txt"; }
// resolvedFrames of every animation across all pages of the atlas.
inline void countCharacterFrames(Character& c,fml::DiagnosticSink& sink) {
    auto& character=c.imported.character;
    if(packerAtlas(c.atlas)) {
        auto names=packerFrameNames(read(c.atlas));
        for(auto& anim:character.anims) {
            int found=0; for(const auto& n:names) if(!anim.atlasPrefix.empty() && n.rfind(anim.atlasPrefix,0)==0) ++found;
            anim.resolvedFrames=anim.indices.empty()?found:std::min(found,(int)anim.indices.size());
        }
        return;
    }
    // Frames of an animation can live on any page: count them all.
    std::vector<int> found(character.anims.size(),0);
    std::vector<fs::path> pages{c.atlas}; for(const auto& extra:c.extraSheets)pages.push_back(extra.second);
    for(const auto& page:pages) {
        auto copy=character; for(auto& a:copy.anims)a.indices.clear();
        fml::countPsychAnimationFrames(copy,read(page),page.u8string(),sink);
        for(size_t i=0;i<copy.anims.size();++i)found[i]+=copy.anims[i].resolvedFrames;
    }
    for(size_t i=0;i<character.anims.size();++i)
        character.anims[i].resolvedFrames=character.anims[i].indices.empty()?found[i]:std::min(found[i],(int)character.anims[i].indices.size());
}
struct Difficulty {
    std::string name;
    fs::path path;
    fml::UniversalChart chart;
    fml::psych::SongAudio audio;
    std::vector<std::string> warnings;
    bool take=true;
    fml::PsychEventReport events;
    bool edited=false;
    bool manualAudio=false;
    std::map<std::string,int> exportCharacters;
    int exportStage=-1;
    std::string arrowSkin;   // song.arrowSkin of a Psych chart, read once at load
};
struct Song {
    std::string id, title, iconId;
    std::string artist,charter;
    fs::path iconPath;
    bool manualIcon=false;
    std::vector<fs::path> iconChoices;
    std::vector<Difficulty> difficulties;
};
struct Icon { std::string id; fs::path source,atlas,data; bool manual=false; };
enum class LooseKind { Character,Stage,Chart,Animate,Sparrow,Icon,Image,Audio,Support,Unknown };
struct LooseFile {
    fs::path path;
    LooseKind kind=LooseKind::Unknown;
    std::string group,reason,role;
    bool associated=false;
};
struct Stage {
    fs::path source,iconPath;
    fml::PsychStageImport imported;
    std::vector<std::string> warnings;
    std::vector<std::vector<fs::path>> imageChoices,atlasChoices;
    std::map<size_t,std::shared_ptr<AnimateResource>> animateData;
    std::set<size_t> manualObjects;
};
struct Catalog {
    Files files;
    std::vector<Character> characters;
    std::vector<Song> songs;
    std::vector<Stage> stages;
    std::vector<Icon> icons;
    std::vector<std::string> warnings;
    std::string origin;
    bool completeMod=false;
    std::shared_ptr<fml::ModExplorerCatalog> explorer;
    std::vector<fml::notelab::NoteStyle> noteStyles;
    std::vector<fs::path> selectedFiles;
    std::vector<LooseFile> loose;
};
// Paths.getSparrowAtlas(key) reads images/<key>.png and images/<key>.xml from
// the same folder. Once the image is known its atlas is the sibling .xml: a
// file with the same name elsewhere belongs to another sprite (Whitty Erect's
// alley/background.png and alley-ballistic/background.xml).
inline Match pairedAtlas(const Files& files,const fs::path& image,const std::vector<std::string>& keys,const fs::path& context) {
    if(!image.empty()) {
        auto sibling=image; sibling.replace_extension(".xml");
        if(fml::psych::file(sibling)) return {sibling,{}};
        // Packer sheets (week 6's spirit) pair a .txt with the image.
        sibling.replace_extension(".txt");
        if(fml::psych::file(sibling)) return {sibling,{}};
        return {};
    }
    return files.find(keys,context,false);
}
inline void resolveCharacter(Character& c,const Files& files) {
    const auto& model=c.imported.character;
    auto context=c.source.parent_path();
    auto key=model.spriteAtlas;
    auto animation=files.find({"images/"+key+"/Animation.json",key+"/Animation.json"},context,false);
    if(!animation.path.empty()) {
        c.animate=true; c.atlas=animation.path; c.animateData=animateResource(c.atlas);
        if(!c.animateData->images.empty()) c.image=c.animateData->images.front();
        if(!c.animateData->error.empty()) c.warnings.push_back(c.animateData->error);
        if(!c.animateData->compatibilityError.empty()) c.warnings.push_back(c.animateData->compatibilityError);
    } else {
        auto image=files.find({"images/"+key+".png",key+".png"},context);
        auto atlas=image.path.empty() && !image.alternatives.empty()?files.find({"images/"+key+".xml",key+".xml"},context):pairedAtlas(files,image.path,{"images/"+key+".xml",key+".xml"},context);
        c.image=image.path; c.imageChoices=image.alternatives; c.atlas=atlas.path; c.atlasChoices=atlas.alternatives;
        if(c.image.empty()) c.warnings.push_back(image.alternatives.empty()?"Imagen no encontrada. Puedes elegirla manualmente.":"Varias imágenes coinciden. Elige el archivo correcto.");
        if(c.atlas.empty()) c.warnings.push_back(atlas.alternatives.empty()?"Atlas XML no encontrado. Puedes elegirlo manualmente.":"Varios atlas coinciden. Elige el archivo correcto.");
        for(const auto& extra:c.imported.extraImages) {
            auto page=files.find({"images/"+extra+".png",extra+".png"},context);
            auto pageAtlas=pairedAtlas(files,page.path,{"images/"+extra+".xml",extra+".xml"},context);
            if(page.path.empty() || pageAtlas.path.empty()) { c.warnings.push_back("Hoja adicional no encontrada: "+extra); c.extraSheets.clear(); break; }
            c.extraSheets.push_back({page.path,pageAtlas.path});
        }
    }
    auto found=icon(files,model.icon);
    if(found.path.empty() && found.alternatives.empty()) found=icon(files,"face");
    c.iconPath=found.path; c.iconChoices=found.alternatives;
    c.take.assign(model.anims.size(),1);
    if(c.animate && c.animateData && c.animateData->error.empty()) {
        for(size_t i=0;i<c.take.size();++i) {
            auto& anim=c.imported.character.anims[i]; int count=c.animateData->atlas.frameCount(anim.atlasPrefix);
            bool valid=count>0;
            for(int frame:anim.indices) if(frame<0 || frame>=count) valid=false;
            anim.resolvedFrames=valid?(anim.indices.empty()?count:(int)anim.indices.size()):0;
            c.take[i]=anim.resolvedFrames>0;
        }
    }
    if(!c.animate && !c.atlas.empty()) {
        fml::DiagnosticSink sink;
        countCharacterFrames(c,sink);
        auto& character=c.imported.character;
        for(size_t i=0;i<c.take.size();++i) if(character.anims[i].resolvedFrames==0) c.take[i]=0;
        for(const auto& d:sink.all()) c.warnings.push_back(d.message);
    }
    c.imported.character.resolvedImage=c.image.generic_u8string(); c.imported.character.resolvedAtlas=c.atlas.generic_u8string();
}
inline void resolveStage(Stage& stage,const Files& files,const fml::Vfs* explorerVfs=nullptr) {
    auto& objects=stage.imported.stage.objects;
    stage.imageChoices.resize(objects.size()); stage.atlasChoices.resize(objects.size());
    for(size_t i=0;i<objects.size();++i) {
        auto& object=objects[i]; if(object.kind!=fml::StageObject::Kind::Sprite) continue;
        auto physical=[&](const std::string& key) {
            auto path=fs::u8path(key);if(!key.empty() && path.is_absolute() && fml::psych::file(path))return path;
            return explorerVfs && !key.empty()?explorerVfs->resolve(key).value_or(fs::path()):fs::path();
        };
        auto image=physical(object.resolvedImage),atlas=physical(object.resolvedAtlas);
        if(image.empty()) {
            auto found=files.find({"images/"+object.spritePath+".png",object.spritePath+".png"},stage.source.parent_path());
            image=found.path; stage.imageChoices[i]=found.alternatives;
        }
        if(atlas.empty()) {
            auto found=files.find({"images/"+object.spritePath+"/Animation.json",object.spritePath+"/Animation.json"},stage.source.parent_path(),false);
            atlas=found.path;
            if(atlas.empty()) {
                found=pairedAtlas(files,image,{"images/"+object.spritePath+".xml",object.spritePath+".xml"},stage.source.parent_path());
                atlas=found.path; stage.atlasChoices[i]=found.alternatives;
            }
        }
        if(!atlas.empty() && lower(atlas.filename().u8string())=="animation.json") {
            auto resource=animateResource(atlas); stage.animateData[i]=resource;
            if(!resource->images.empty()) image=resource->images.front();
            if(object.anims.empty() && resource->error.empty()) {
                auto names=resource->atlas.symbolNames();
                if(!names.empty()) { fml::AnimationDef anim; anim.name="idle"; anim.atlasPrefix=names.front(); anim.loop=true; object.anims.push_back(anim); }
            }
        }
        object.resolvedImage=image.generic_u8string(); object.resolvedAtlas=atlas.generic_u8string();
        if(image.empty())stage.warnings.push_back("Imagen de escenario sin resolver: "+object.name+" · "+object.spritePath);
        if(stage.iconPath.empty() && !image.empty()) stage.iconPath=image;
    }
}
inline bool stageShape(const json& node) {
    return node.is_object() && (node.contains("defaultZoom") || node.contains("objects") ||
        (node.contains("boyfriend") && node.contains("opponent")));
}
inline fml::psych::SongAudio resolveAudio(const Files& files,const fs::path& chart,const json& header,const std::string& fallback,std::vector<std::string>& warnings,const std::string& difficulty="normal") {
    fml::psych::SongAudio result=fml::psych::resolveSongAudio(files.roots,chart,fallback);
    auto song=fml::psych::formatSongPath(field(header,"song"));
    if(song.empty()) song=fallback;
    if(result.songPath.empty()) result.songPath=song;
    // Variant banks are resolved as a unit. A legacy instrumental must never
    // silently inherit voices from the normal version.
    if(fml::psych::relativeKey(difficulty)) {
        struct Bank { std::vector<std::string> prefixes; std::string suffix; };
        std::vector<Bank> banks;
        std::vector<std::string> songIds{fallback,song};
        std::sort(songIds.begin(),songIds.end()); songIds.erase(std::unique(songIds.begin(),songIds.end()),songIds.end());
        std::vector<std::string> shared,variant,nested;
        for(const auto& id:songIds) if(fml::psych::relativeKey(id)) {
            shared.push_back("songs/"+id+"/"); shared.push_back(id+"/");
            shared.push_back("songs/"+id+"/song/");shared.push_back(id+"/song/");
            variant.push_back("songs/"+id+"-"+difficulty+"/"); variant.push_back(id+"-"+difficulty+"/");
            nested.push_back("songs/"+id+"/"+difficulty+"/"); nested.push_back(id+"/"+difficulty+"/");
        }
        banks.push_back({shared,"-"+difficulty}); banks.push_back({variant,""}); banks.push_back({nested,""});
        for(size_t bankIndex=0;bankIndex<banks.size();++bankIndex) {
            const auto& bank=banks[bankIndex];
            auto find=[&](const std::string& role) {
                std::vector<std::string> keys;
                for(const auto& prefix:bank.prefixes) keys.push_back(prefix+role+bank.suffix+".ogg");
                if(bankIndex==0) {
                    auto local=chart.parent_path()/fs::u8path(role+bank.suffix+".ogg");
                    if(fml::psych::file(local)) return Match{local,{}};
                    if(role=="Voices-Player" || role=="Voices-Opponent") {
                        auto split=role.substr(7);
                        for(const auto& prefix:bank.prefixes) keys.push_back(prefix+"Voices"+bank.suffix+"-"+split+".ogg");
                    }
                }
                return files.find(keys,chart.parent_path(),false);
            };
            auto inst=find("Inst");
            if(!inst.alternatives.empty()) { result={}; warnings.push_back("Audio ambiguo: Inst-"+difficulty+".ogg; selección manual requerida."); return result; }
            fml::psych::SongAudio variantAudio; variantAudio.songPath=song; variantAudio.instPath=inst.path.empty()?result.instPath:inst.path.u8string();
            auto role=[&](const std::string& key) {
                auto found=find(key); if(!found.alternatives.empty()) warnings.push_back("Audio ambiguo: "+key+"-"+difficulty+".ogg; selección manual requerida.");
                return found.path.u8string();
            };
            variantAudio.voicesPath=role("Voices");
            auto characterRole=[&](const char* key,const std::string& suffix) {
                auto who=field(header,key); auto definition=files.find({"characters/"+who+".json",who+".json"});
                auto name=field(document(definition.path),"vocals_file");
                return role("Voices-"+(name.empty()?suffix:name));
            };
            variantAudio.playerVoicesPath=characterRole("player1","Player"); variantAudio.opponentVoicesPath=characterRole("player2","Opponent");
            if(inst.path.empty() && !variantAudio.hasVoices())continue;
            if(variantAudio.playerVoicesPath.empty() && !variantAudio.opponentVoicesPath.empty()) variantAudio.playerVoicesPath=variantAudio.voicesPath;
            if(!variantAudio.playerVoicesPath.empty() || !variantAudio.opponentVoicesPath.empty()) variantAudio.voicesPath.clear();
            if(!variantAudio.hasVoices() && result.hasVoices()) warnings.push_back(difficulty+": instrumental de variante sin voces asociadas; elige sus voces manualmente si las necesita.");
            return variantAudio;
        }
    }
    auto lookup=[&](const std::string& name) {
        auto local=chart.parent_path()/fs::u8path(name+".ogg");
        if(fml::psych::file(local)) return local.u8string();
        auto m=files.find({"songs/"+song+"/"+name+".ogg",song+"/"+name+".ogg","songs/"+song+"/song/"+name+".ogg",song+"/song/"+name+".ogg"},chart.parent_path(),false);
        if(!m.alternatives.empty()) warnings.push_back("Audio ambiguo: "+name+".ogg; selección manual requerida.");
        return m.path.u8string();
    };
    if(result.instPath.empty()) result.instPath=lookup("Inst");
    if(result.voicesPath.empty()) result.voicesPath=lookup("Voices");
    auto voice=[&](const char* role,const std::string& defaultSuffix) {
        auto who=field(header,role);
        auto source=files.find({"characters/"+who+".json",who+".json"});
        std::string suffix=field(document(source.path),"vocals_file");
        if(suffix.empty()) suffix=defaultSuffix;
        return lookup("Voices-"+suffix);
    };
    if(result.playerVoicesPath.empty()) result.playerVoicesPath=voice("player1","Player");
    if(result.opponentVoicesPath.empty()) result.opponentVoicesPath=voice("player2","Opponent");
    if(result.playerVoicesPath.empty() && !result.opponentVoicesPath.empty()) result.playerVoicesPath=result.voicesPath;
    if(!result.playerVoicesPath.empty() || !result.opponentVoicesPath.empty()) result.voicesPath.clear();
    return result;
}
inline std::pair<std::string,std::string> songIdentity(const fs::path& p,const json& header) {
    auto stem=p.stem().u8string(), folder=p.parent_path().filename().u8string();
    auto declared=fml::psych::formatSongPath(field(header,"song"));
    std::string id=declared.empty()?folder:declared, diff="normal";
    if(stem==folder) return {folder,diff};
    if(stem.size()>folder.size()+1 && stem.compare(0,folder.size()+1,folder+"-")==0) return {folder,stem.substr(folder.size()+1)};
    if(!declared.empty() && lower(stem)==lower(declared)) return {declared,diff};
    if(!declared.empty() && lower(stem).rfind(lower(declared)+"-",0)==0) return {declared,stem.substr(declared.size()+1)};
    auto dash=stem.rfind('-');
    const std::set<std::string> known{"easy","normal","hard","erect","nightmare","insane","mania","canon","expert","legacy","old"};
    if(dash!=std::string::npos && known.count(lower(stem.substr(dash+1)))) return {stem.substr(0,dash),stem.substr(dash+1)};
    return {declared.empty()?stem:declared,stem==id?"normal":stem};
}
inline Catalog load(const fs::path& folder,const std::vector<fs::path>& selected={}) {
    fml::psych::SourceIndexSession sourceSession;
    Catalog out; out.completeMod=selected.empty(); out.origin=folder.u8string();
    out.selectedFiles=selected;
    if(selected.empty()) {
        out.files.addTree(folder);
        // Engine libraries and enabled packs augment the selected source.
        for(const auto& root:fml::psych::roots(folder)) {
            auto relative=root.lexically_relative(folder);
            if(!relative.empty() && *relative.begin()!=".." && !relative.is_absolute()) out.files.addTree(root);
        }
    } else for(const auto& p:selected) {
        auto root=fml::psych::ownerRoot(p);
        if(root.empty() && lower(p.parent_path().filename().u8string())=="charts")root=p.parent_path().parent_path();
        out.files.addTree(root.empty()?p.parent_path():root);
    }
    std::map<std::string,std::string> weekIcons;
    for(const auto& p:out.files.paths) {
        if(lower(p.extension().u8string())!=".json") continue;
        auto j=document(p);
        if(!j.is_object() || !j.contains("songs") || !j["songs"].is_array()) continue;
        for(const auto& entry:j["songs"]) if(entry.is_array() && entry.size()>1 && entry[0].is_string() && entry[1].is_string()) {
            auto key=lower(fml::psych::formatSongPath(entry[0].get<std::string>()));
            weekIcons.emplace(key,entry[1].get<std::string>());
        }
    }
    const auto& candidates=selected.empty()?out.files.paths:selected;
    std::map<std::string,size_t> songMap;
    std::vector<fs::path> rawAtlases;
    std::map<std::string,fs::path> stageSources;
    for(const auto& p:candidates) {
        auto ext=lower(p.extension().u8string());
        bool stageFolder=lower(p.parent_path().filename().u8string())=="stages";
        if(ext==".lua" || ext==".hx") {
            auto bytes=read(p);
            if(stageFolder || (!selected.empty() && (bytes.find("makeLuaSprite")!=std::string::npos || bytes.find("makeAnimatedLuaSprite")!=std::string::npos)))
                stageSources.emplace(lower((p.parent_path()/p.stem()).lexically_normal().generic_u8string()),p);
            continue;
        }
        if(ext!=".json") continue;
        try {
            auto node=document(p); if(!node.is_object()) continue;
            if(lower(p.filename().u8string())=="animation.json") { rawAtlases.push_back(p); continue; }
            if(stageFolder || stageShape(node)) { stageSources[lower((p.parent_path()/p.stem()).lexically_normal().generic_u8string())]=p; continue; }
            bool characterShape=false;
            if(node.contains("image") && node.contains("animations") && node["animations"].is_array())
                for(const auto& animation:node["animations"]) if(!field(animation,"anim").empty() && !field(animation,"name").empty()) characterShape=true;
            // Dialogue portraits also have image/animations, but use loop_name/idle_name.
            if(characterShape) {
                auto parsed=fml::importPsychCharacter(read(p),p.stem().u8string());
                if(!parsed) { out.warnings.push_back(p.u8string()+": "+parsed.error()); continue; }
                Character c; c.source=p; c.imported=std::move(parsed.value()); c.warnings=c.imported.warnings;
                resolveCharacter(c,out.files); out.characters.push_back(std::move(c)); continue;
            }
            const auto& header=node.contains("song") && node["song"].is_object()?node["song"]:node;
            bool native=node.contains("strumLines") && node["strumLines"].is_array();
            if(!native && (!header.contains("notes") || !header["notes"].is_array() || !header.contains("bpm"))) continue;
            // Psych reads events.json as the song's global events
            // (Song.loadFromJson('events', song)); it has a chart's shape but is
            // never a difficulty. Its events already join every chart beside it.
            if(!native && lower(p.filename().u8string())=="events.json") continue;
            fml::DiagnosticSink sink;
            auto parsed=native?fml::Result<fml::ChartImportBundle>::fail("chart inválido"):fml::importPsychEngineChart({p},sink);
            if(native) {
                auto metadataFolder=lower(p.parent_path().filename().u8string())=="charts"?p.parent_path().parent_path():p.parent_path();
                auto common=document(metadataFolder/"meta.json"),overrides=document(metadataFolder/fs::u8path("meta-"+p.stem().u8string()+".json"));if(!common.is_object())common=json::object();if(overrides.is_object())common.update(overrides);
                auto meta=fml::parseSongMeta(common.dump(),metadataFolder.filename().u8string(),(metadataFolder/"meta.json").u8string(),sink);
                fml::CodenameChartDefaults defaults;defaults.songId=meta.name;defaults.displayName=meta.displayName;defaults.bpm=meta.bpm;defaults.beatsPerMeasure=meta.beatsPerMeasure;defaults.stepsPerBeat=meta.stepsPerBeat;defaults.needsVoices=meta.needsVoices;
                auto chart=fml::parseCodenameChart(read(p),p.stem().u8string(),defaults,sink);
                if(chart) {
                    auto globals=p.parent_path()/"events.json"; if(lower(p.parent_path().filename().u8string())=="charts") globals=p.parent_path().parent_path()/"events.json";
                    if(fml::psych::file(globals)) { auto events=fml::parseCodenameEventsFile(read(globals),globals.u8string(),sink); if(events) { chart.value().events.insert(chart.value().events.end(),events.value().begin(),events.value().end()); fml::rebuildChartTiming(chart.value(),sink); } }
                    fml::ChartImportBundle bundle;bundle.songName=chart.value().songId; bundle.candidates.push_back({p.stem().u8string(),std::move(chart.value())}); parsed=fml::Result<fml::ChartImportBundle>::ok(std::move(bundle));
                }
            }
            if(!parsed || parsed.value().candidates.empty()) { out.warnings.push_back(p.u8string()+": chart inválido"); continue; }
            auto identity=songIdentity(p,header);
            if(native && lower(p.parent_path().filename().u8string())=="charts") identity={p.parent_path().parent_path().filename().u8string(),p.stem().u8string()};
            // Same names in different folders remain distinct sources.
            auto group=lower(p.parent_path().u8string())+"|"+lower(identity.first);
            if(!songMap.count(group)) {
                Song s; s.id=identity.first; s.title=field(header,"song"); if(s.title.empty())s.title=parsed.value().songName;if(s.title.empty()) s.title=s.id;
                s.artist=field(header,"artist"); if(s.artist.empty()) s.artist=field(header,"composer"); s.charter=field(header,"charter");
                s.iconId=field(header,"icon"); if(s.iconId.empty()) s.iconId=field(header,"healthicon");
                if(s.iconId.empty() && weekIcons.count(lower(s.id))) s.iconId=weekIcons[lower(s.id)];
                auto m=icon(out.files,s.iconId);
                if(m.path.empty() && m.alternatives.empty()) m=icon(out.files,"face");
                s.iconPath=m.path; s.iconChoices=m.alternatives;
                songMap[group]=out.songs.size(); out.songs.push_back(std::move(s));
            }
            Difficulty d; d.name=identity.second; d.path=p; if(!native)d.arrowSkin=field(header,"arrowSkin");
            d.events=parsed.value().events;
            d.chart=std::move(parsed.value().candidates.front().chart); d.warnings=parsed.value().warnings;
            if(!native && d.chart.stage.empty())d.chart.stage=fml::psych::vanillaStage(identity.first);
            d.audio=resolveAudio(out.files,p,header,identity.first,d.warnings,d.name);
            if(!native)for(auto& line:d.chart.strumLines){if(line.type==1 && !d.audio.playerVoicesPath.empty())line.vocalsSuffix="-Player";if(line.type==0 && !d.audio.opponentVoicesPath.empty())line.vocalsSuffix="-Opponent";}
            out.songs[songMap[group]].difficulties.push_back(std::move(d));
        } catch(const std::exception& e) { out.warnings.push_back(p.u8string()+": "+e.what()); }
    }
    // Mod Explorer provides the same stage reconstruction and source resolution
    // as FML's viewer. The standalone broad index adds nonstandard locations.
    if(selected.empty()) {
        out.explorer=std::make_shared<fml::ModExplorerCatalog>();
        if(out.explorer->scan(folder)) for(const auto& asset:out.explorer->assets()) {
            if(asset.kind!=fml::ModExplorerAsset::Kind::Stage || !asset.valid ||
               (asset.format!=fml::ModExplorerAsset::Format::PsychJson && asset.format!=fml::ModExplorerAsset::Format::PsychLua)) continue;
            auto source=out.explorer->vfs().resolve(asset.sourcePath);
            if(source) stageSources.emplace(lower((source->parent_path()/source->stem()).lexically_normal().generic_u8string()),*source);
        }
    }
    for(const auto& entry:stageSources) try {
        auto source=entry.second; auto id=source.stem().u8string();
        auto definition=source; definition.replace_extension(".json");
        auto script=source; script.replace_extension(".lua");
        auto parsed=fml::importPsychStage(fml::psych::file(definition)?read(definition):"{}",id,read(script));
        if(!parsed) { out.warnings.push_back(source.u8string()+": "+parsed.error()); continue; }
        Stage stage; stage.source=source; stage.imported=std::move(parsed.value());
        const fml::Vfs* vfs=nullptr;
        if(out.explorer) for(const auto& asset:out.explorer->assets()) {
            auto actual=out.explorer->vfs().resolve(asset.sourcePath);
            if(asset.kind==fml::ModExplorerAsset::Kind::Stage && asset.valid && actual &&
                lower((actual->parent_path()/actual->stem()).lexically_normal().generic_u8string())==entry.first) {
                stage.imported.stage=std::get<fml::UniversalStage>(asset.parsed);
                stage.imported.warnings.insert(stage.imported.warnings.end(),asset.warnings.begin(),asset.warnings.end());
                vfs=&out.explorer->vfs(); break;
            }
        }
        if(stage.imported.stage.objects.size()<=3 && lower(source.parent_path().filename().u8string())=="stages") {
            auto recovered=fml::psych::loadStage(source);
            if(recovered) stage.imported=std::move(recovered.value());
        }
        stage.warnings=stage.imported.warnings; resolveStage(stage,out.files,vfs); out.stages.push_back(std::move(stage));
    } catch(const std::exception& e) { out.warnings.push_back(entry.second.u8string()+": "+e.what()); }
    for(const auto& source:rawAtlases) try {
        bool referenced=false;
        for(const auto& c:out.characters) if(lower(c.atlas.u8string())==lower(source.u8string())) referenced=true;
        for(const auto& s:out.stages) for(const auto& o:s.imported.stage.objects) if(lower(o.resolvedAtlas)==lower(source.generic_u8string())) referenced=true;
        if(referenced) continue;
        // Use Mod Explorer's own raw-atlas symbol discovery, including loose files.
        fml::ModExplorerCatalog explorer;
        if(!explorer.scanSingle(source,true)) { out.warnings.push_back(source.u8string()+": "+explorer.error()); continue; }
        for(const auto& asset:explorer.assets()) if(asset.valid && asset.kind==fml::ModExplorerAsset::Kind::Character) {
            Character c; c.rawAtlas=true; c.animate=true; c.source=source; c.atlas=source;
            c.imported.character=std::get<fml::UniversalCharacter>(asset.parsed);
            c.imported.character.id=source.parent_path().filename().u8string();
            c.imported.sourceJson=read(source); c.animateData=animateResource(source);
            if(!c.animateData->images.empty()) c.image=c.animateData->images.front();
            for(auto& animation:c.imported.character.anims) animation.resolvedFrames=c.animateData->atlas.frameCount(animation.atlasPrefix);
            c.imported.character.resolvedImage=c.image.generic_u8string(); c.imported.character.resolvedAtlas=c.atlas.generic_u8string();
            c.iconPath=icon(out.files,"face").path; c.take.assign(c.imported.character.anims.size(),1);
            c.warnings=asset.warnings;
            if(!c.animateData->error.empty()) c.warnings.push_back(c.animateData->error);
            out.characters.push_back(std::move(c)); break;
        }
    } catch(const std::exception& e) { out.warnings.push_back(source.u8string()+": "+e.what()); }
    std::stable_sort(out.stages.begin(),out.stages.end(),[](const auto& a,const auto& b){ return lower(a.imported.stage.id)<lower(b.imported.stage.id); });
    for(const auto& source:out.files.paths) {
        if(lower(source.extension().u8string())!=".png") continue;
        bool iconFolder=false;
        for(const auto& part:source.parent_path()) { auto name=lower(part.u8string()); if(name=="icons" || name=="healthicons" || name=="health-icons" || name=="badges") iconFolder=true; }
        if(!iconFolder && lower(source.filename().u8string()).rfind("icon-",0)!=0) continue;
        Icon icon; icon.source=source; icon.id=source.stem().u8string(); if(lower(icon.id).rfind("icon-",0)==0) icon.id.erase(0,5);
        auto atlas=source; atlas.replace_extension(".xml");if(!fml::psych::file(atlas))atlas.replace_extension(".txt");if(fml::psych::file(atlas))icon.atlas=atlas;
        auto data=source.parent_path()/"data.xml";
        if(fml::psych::file(data))icon.data=data;
        else icon.data=out.files.find({"data/icons/"+icon.id+".xml"},source.parent_path(),false).path;
        out.icons.push_back(std::move(icon));
    }
    std::stable_sort(out.characters.begin(),out.characters.end(),[](const auto& a,const auto& b){
        const auto& x=a.imported.character.id; const auto& y=b.imported.character.id;
        if(x==y) return a.source<b.source; if(x=="bf") return true; if(y=="bf") return false; return lower(x)<lower(y);
    });
    for(auto& s:out.songs) std::stable_sort(s.difficulties.begin(),s.difficulties.end(),[](const auto& a,const auto& b){
        if(a.name==b.name) return a.path<b.path;
        if(a.name=="normal") return true; if(b.name=="normal") return false; return a.name<b.name;
    });
    std::stable_sort(out.songs.begin(),out.songs.end(),[](const auto& a,const auto& b){ return lower(a.id)<lower(b.id); });
    out.warnings.insert(out.warnings.end(),out.files.warnings.begin(),out.files.warnings.end());
    if(!selected.empty() && out.origin.empty()) out.origin="Archivos sueltos";
    fml::Vfs noteVfs;
    if(!folder.empty())noteVfs.pushRoot(folder,"notes");
    else for(const auto& root:out.files.roots)noteVfs.pushRoot(root,"loose-notes");
    auto noteCatalog=fml::notelab::scanNoteStyles(noteVfs);
    for(auto& style:noteCatalog.styles) {
        bool heads=false;for(const auto& part:style.parts)if(part.part==fml::notelab::Part::Note)heads=true;
        if(!heads)continue;
        auto physical=[&](std::string& path){if(!path.empty())path=noteVfs.resolve(path).value_or(fs::path()).generic_u8string();};
        physical(style.definition);
        for(auto& sheet:style.sheets){physical(sheet.image);physical(sheet.atlas);}
        out.noteStyles.push_back(std::move(style));
    }
    return out;
}
inline json inventory(const Catalog& c) {
    json j={{"origin",c.origin},{"filesScanned",c.files.paths.size()},{"roots",json::array()},{"characters",json::array()},{"songs",json::array()},{"stages",json::array()},{"icons",json::array()},{"warnings",c.warnings}};
    for(const auto& root:c.files.roots) j["roots"].push_back(root.u8string());
    for(const auto& ch:c.characters) j["characters"].push_back({{"id",ch.imported.character.id},{"source",ch.source.u8string()},
        {"icon",ch.iconPath.u8string()},{"image",ch.image.u8string()},{"atlas",ch.atlas.u8string()},{"animate",ch.animate},{"rawAtlas",ch.rawAtlas},{"warnings",ch.warnings}});
    for(const auto& s:c.songs) {
        json d=json::array(); for(const auto& x:s.difficulties) d.push_back({{"difficulty",x.name},{"chart",x.path.u8string()},
            {"notes",x.chart.notes.size()},{"inst",x.audio.instPath},{"voices",x.audio.voicesPath},{"warnings",x.warnings},
            {"events",{{"total",x.events.total},{"translated",x.events.translated},{"unresolved",x.events.unresolved}}}});
        j["songs"].push_back({{"id",s.id},{"iconId",s.iconId},{"icon",s.iconPath.u8string()},{"difficulties",d}});
    }
    for(const auto& stage:c.stages) j["stages"].push_back({{"id",stage.imported.stage.id},{"source",stage.source.u8string()},
        {"objects",stage.imported.stage.objects.size()},{"warnings",stage.warnings}});
    for(const auto& icon:c.icons) j["icons"].push_back({{"id",icon.id},{"source",icon.source.u8string()},{"atlas",icon.atlas.u8string()},{"data",icon.data.u8string()}});
    j["noteStyles"]=json::array();for(const auto& style:c.noteStyles)j["noteStyles"].push_back({{"id",style.id},{"name",style.name},{"definition",style.definition},{"pixel",style.pixel},{"scope",style.scope}});
    return j;
}
}
