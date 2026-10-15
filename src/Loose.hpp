#pragma once
#include "Catalog.hpp"

namespace pi {
inline bool sameFile(const fs::path& a,const fs::path& b) {
    return !a.empty() && !b.empty() && lower(a.lexically_normal().generic_u8string())==lower(b.lexically_normal().generic_u8string());
}
inline std::string audioRole(const fs::path& path) {
    auto name=lower(path.stem().u8string());
    if(name=="inst" || name.rfind("inst-",0)==0) return "Inst";
    if(name.rfind("voices-player",0)==0 || name.find("-player")!=std::string::npos) return "Player";
    if(name.rfind("voices-opponent",0)==0 || name.find("-opponent")!=std::string::npos) return "Opponent";
    if(name=="voices" || name.rfind("voices-",0)==0) return "Voices";
    return "";
}
inline void refreshLoose(Catalog& catalog) {
    catalog.loose.clear();
    for(const auto& path:catalog.selectedFiles) {
        LooseFile file; file.path=path; auto ext=lower(path.extension().u8string());
        if(ext==".ogg") { file.kind=LooseKind::Audio; file.role=audioRole(path); file.reason="Elige canción, dificultad y función del audio."; }
        else if(ext==".png") { file.kind=LooseKind::Image; file.reason="Elige personaje, objeto de escenario o icono."; }
        else if(ext==".xml" && read(path).find("TextureAtlas")!=std::string::npos) { file.kind=LooseKind::Sparrow; file.reason="Elige a qué personaje u objeto pertenece el atlas."; }
        else if(lower(path.filename().u8string())=="animation.json") file.kind=LooseKind::Animate;
        else if(lower(path.filename().u8string()).rfind("spritemap",0)==0) { file.kind=LooseKind::Support; file.reason="Página de Animate; necesita su Animation.json."; }
        std::vector<std::string> groups;
        auto group=[&](const std::string& name) { if(std::find(groups.begin(),groups.end(),name)==groups.end()) groups.push_back(name); };
        for(const auto& c:catalog.characters) {
            if(sameFile(c.source,path)) { file.kind=c.rawAtlas?(c.animate?LooseKind::Animate:LooseKind::Sparrow):LooseKind::Character; group(c.imported.character.id); }
            if(sameFile(c.image,path) || sameFile(c.atlas,path) || sameFile(c.iconPath,path)) group(c.imported.character.id);
            if(c.animateData) {
                for(const auto& p:c.animateData->maps) if(sameFile(p,path)) group(c.imported.character.id);
                for(const auto& p:c.animateData->images) if(sameFile(p,path)) group(c.imported.character.id);
            }
        }
        for(const auto& s:catalog.stages) {
            auto companion=path; companion.replace_extension(s.source.extension());
            if(sameFile(s.source,path) || ((ext==".lua" || ext==".json" || ext==".hx") && sameFile(s.source,companion))) { file.kind=LooseKind::Stage; group(s.imported.stage.id); }
            for(const auto& o:s.imported.stage.objects) if(sameFile(fs::u8path(o.resolvedImage),path) || sameFile(fs::u8path(o.resolvedAtlas),path)) group(s.imported.stage.id+" / "+o.name);
        }
        for(const auto& s:catalog.songs) for(const auto& d:s.difficulties) {
            auto name=s.id+" / "+d.name;
            if(sameFile(d.path,path)) { file.kind=LooseKind::Chart; group(name); }
            for(const auto& p:{d.audio.instPath,d.audio.voicesPath,d.audio.playerVoicesPath,d.audio.opponentVoicesPath}) if(sameFile(fs::u8path(p),path)) group(name);
        }
        for(const auto& icon:catalog.icons) if(sameFile(icon.source,path) || sameFile(icon.atlas,path) || sameFile(icon.data,path)) { if(sameFile(icon.source,path)) file.kind=LooseKind::Icon; group("icon: "+icon.id); }
        for(const auto& name:groups) { if(!file.group.empty()) file.group+="; "; file.group+=name; }
        file.associated=!groups.empty();
        if(file.associated) file.reason="Asociación por declaración o ruta resuelta.";
        if(!file.associated && file.reason.empty()) file.reason="Formato no reconocido; conservado en la lista para revisión.";
        catalog.loose.push_back(std::move(file));
    }
    std::stable_sort(catalog.loose.begin(),catalog.loose.end(),[](const auto& a,const auto& b) {
        if(a.kind!=b.kind) return a.kind<b.kind;
        if(a.group!=b.group) return a.group<b.group;
        return a.path<b.path;
    });
}
inline Catalog loadLoose(const std::vector<fs::path>& files) {
    auto catalog=load({},files);
    // A selected Sparrow pair can be previewed without a character definition.
    // Let Mod Explorer infer its animation prefixes; keep generated definitions
    // explicitly marked as raw atlases.
    for(const auto& path:files) {
        auto ext=lower(path.extension().u8string()); if(ext!=".xml" && ext!=".png") continue;
        auto atlas=path; atlas.replace_extension(".xml");
        auto image=path; image.replace_extension(".png");
        if(!fml::psych::file(atlas) || !fml::psych::file(image) || read(atlas).find("TextureAtlas")==std::string::npos) continue;
        bool used=false;
        for(const auto& c:catalog.characters) if(sameFile(c.atlas,atlas) || sameFile(c.image,image)) used=true;
        for(const auto& s:catalog.stages) for(const auto& o:s.imported.stage.objects) if(sameFile(fs::u8path(o.resolvedImage),image) || sameFile(fs::u8path(o.resolvedAtlas),atlas)) used=true;
        for(const auto& i:catalog.icons) if(sameFile(i.source,image)) used=true;
        if(used) continue;
        fml::ModExplorerCatalog explorer;
        if(!explorer.scanSingle(image,true)) continue;
        std::string error;
        if(!explorer.configureManualCharacter(image,atlas,{}, {},error)) { catalog.warnings.push_back(error); continue; }
        for(const auto& asset:explorer.assets()) if(asset.valid && asset.kind==fml::ModExplorerAsset::Kind::Character) {
            Character c; c.rawAtlas=true; c.source=atlas; c.image=image; c.atlas=atlas;
            c.imported.character=std::get<fml::UniversalCharacter>(asset.parsed);
            c.imported.character.id=image.stem().u8string(); c.imported.sourceJson="{}";
            c.imported.character.resolvedImage=image.generic_u8string(); c.imported.character.resolvedAtlas=atlas.generic_u8string();
            c.take.assign(c.imported.character.anims.size(),1); catalog.characters.push_back(std::move(c)); break;
        }
    }
    refreshLoose(catalog); return catalog;
}
inline bool mergeSongGroups(Catalog& catalog,size_t from,size_t to,std::string& error) {
    if(from==to || from>=catalog.songs.size() || to>=catalog.songs.size()) return false;
    const auto& a=catalog.songs[from]; const auto& b=catalog.songs[to];
    if(lower(a.id)!=lower(b.id)) { error="Solo se pueden unir grupos con el mismo ID de canción."; return false; }
    for(const auto& x:a.difficulties) for(const auto& y:b.difficulties) if(lower(x.name)==lower(y.name)) { error="Hay dificultades con el mismo nombre; conserva los grupos separados."; return false; }
    auto& target=catalog.songs[to]; for(auto& difficulty:catalog.songs[from].difficulties) target.difficulties.push_back(std::move(difficulty));
    std::stable_sort(target.difficulties.begin(),target.difficulties.end(),[](const auto& a,const auto& b){return a.name<b.name;});
    catalog.songs.erase(catalog.songs.begin()+from); refreshLoose(catalog); return true;
}
inline void preserveSession(Catalog& next,const Catalog& previous) {
    // Keep explicit decisions when adding another drop; remap indices by source
    // identity because discovery can insert resources before the old selection.
    for(auto& c:next.characters)for(const auto& old:previous.characters)if(sameFile(c.source,old.source)) {
        if(old.manualBinding)c=old;
        else {
            c.imported.character.anims=old.imported.character.anims;c.take=old.take;
            if(c.animate && c.animateData)for(auto& anim:c.imported.character.anims)anim.resolvedFrames=anim.indices.empty()?c.animateData->atlas.frameCount(anim.atlasPrefix):(int)anim.indices.size();
            else if(!c.atlas.empty()){fml::DiagnosticSink sink;fml::countPsychAnimationFrames(c.imported.character,read(c.atlas),c.atlas.u8string(),sink);}
        }
        break;
    }
    for(auto& stage:next.stages)for(const auto& old:previous.stages)if(sameFile(stage.source,old.source))for(auto index:old.manualObjects)if(index<old.imported.stage.objects.size())for(size_t i=0;i<stage.imported.stage.objects.size();++i)if(stage.imported.stage.objects[i].name==old.imported.stage.objects[index].name) {
        auto& object=stage.imported.stage.objects[i];object.resolvedImage=old.imported.stage.objects[index].resolvedImage;object.resolvedAtlas=old.imported.stage.objects[index].resolvedAtlas;stage.manualObjects.insert(i);auto resource=old.animateData.find(index);if(resource!=old.animateData.end())stage.animateData[i]=resource->second;else stage.animateData.erase(i);
    }
    auto characterIndex=[&](int index){if(index<0 || (size_t)index>=previous.characters.size())return -1;for(int i=0;i<(int)next.characters.size();++i)if(sameFile(next.characters[i].source,previous.characters[index].source))return i;return -1;};
    auto stageIndex=[&](int index){if(index<0 || (size_t)index>=previous.stages.size())return -1;for(int i=0;i<(int)next.stages.size();++i)if(sameFile(next.stages[i].source,previous.stages[index].source))return i;return -1;};
    for(auto& song:next.songs)for(auto& d:song.difficulties)for(const auto& oldSong:previous.songs)for(const auto& old:oldSong.difficulties)if(sameFile(d.path,old.path)) {
        d.take=old.take;
        if(old.edited){d.chart=old.chart;d.edited=true;}if(old.manualAudio){d.audio=old.audio;d.manualAudio=true;}
        d.exportStage=stageIndex(old.exportStage);for(const auto& assignment:old.exportCharacters){auto remapped=characterIndex(assignment.second);if(remapped>=0)d.exportCharacters[assignment.first]=remapped;}
        if(oldSong.manualIcon){song.iconPath=oldSong.iconPath;song.manualIcon=true;}
    }
    for(const auto& icon:previous.icons)if(icon.manual) {
        bool present=false,selected=false;for(const auto& i:next.icons)if(sameFile(i.source,icon.source))present=true;for(const auto& file:next.files.paths)if(sameFile(file,icon.source))selected=true;if(!present && selected)next.icons.push_back(icon);
    }
    for(const auto& song:previous.songs)if(song.difficulties.size()>1) {
        bool again=true;while(again){again=false;std::vector<size_t> matches;for(size_t i=0;i<next.songs.size();++i){bool match=false;for(const auto& d:next.songs[i].difficulties)for(const auto& old:song.difficulties)if(sameFile(d.path,old.path))match=true;if(match)matches.push_back(i);}if(matches.size()>1){std::string error;again=mergeSongGroups(next,matches.back(),matches.front(),error);}}
    }
    refreshLoose(next);
}
}
