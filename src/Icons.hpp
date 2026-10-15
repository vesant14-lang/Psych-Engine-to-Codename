#pragma once
#include "Import.hpp"
#include "../../src/fml_formats/HealthIconFrames.hpp"
#include "../../third_party/stb_image.h"
#include "../../third_party/stb_image_write.h"
#include <cstring>

namespace pi {
inline fml::HealthIconFrames iconFrames(const Icon& icon,int width,int height) {
    fml::AtlasStore atlases;atlases.readText=[](const std::string& path){return read(fs::u8path(path));};
    return fml::healthIconFrames(icon.atlas.empty()?nullptr:atlases.get(icon.atlas.u8string()),width,height,read(icon.data));
}
inline Plan iconPlan(const Catalog& catalog,const Icon& icon,const fs::path& destination,const std::string& id,bool bundle=true,bool singleFrame=false,float health=.5f,double seconds=0,const std::string& state={},const bool* loopOverride=nullptr) {
    Plan p; p.destination=destination; p.label=id;
    if(!validId(id)) { p.errors.push_back("ID inválido: "+id+". Usa letras, números, guion o guion bajo."); return p; }
    const auto iconKey="images/icons/"+id;
    if(fml::psych::file(destination/fs::u8path(iconKey+".png")) || fml::psych::directory(destination/fs::u8path(iconKey))) p.errors.push_back("Ya existe; usa otro ID: "+iconKey);
    std::error_code ec; auto size=fs::file_size(icon.source,ec);
    if(ec || size>128u*1024u*1024u) { p.errors.push_back("Imagen no legible o demasiado grande"); return p; }
    auto encoded=read(icon.source); int width=0,height=0,n=0;
    if(!stbi_info_from_memory((const stbi_uc*)encoded.data(),(int)encoded.size(),&width,&height,&n) || width<=0 || height<=0 || (uint64_t)width*height>64u*1024u*1024u) { p.errors.push_back("PNG inválido o excede el límite"); return p; }
    if(singleFrame) {
        auto frames=iconFrames(icon,width,height);const auto* chosen=state.empty()?frames.sequence(health):frames.animation(state);const auto* frame=chosen?chosen->sample(seconds,loopOverride):nullptr;
        if(!frame || !fml::frameInsideSheet(*frame,width,height)) { p.errors.push_back("PNG inválido o excede el límite"); return p; }
        std::unique_ptr<stbi_uc,void(*)(void*)> pixels(stbi_load_from_memory((const stbi_uc*)encoded.data(),(int)encoded.size(),&width,&height,&n,4),stbi_image_free);
        if(!pixels) { p.errors.push_back("No se pudo decodificar la imagen"); return p; }
        int cropW=frame->rotated?frame->h:frame->w,cropH=frame->rotated?frame->w:frame->h;
        int w=frame->frameW>0?frame->frameW:cropW,h=frame->frameH>0?frame->frameH:cropH;
        if(w<=0 || h<=0 || (uint64_t)w*h>64u*1024u*1024u){p.errors.push_back("PNG inválido o excede el límite");return p;}
        std::vector<unsigned char> rgba((size_t)w*h*4,0);
        for(int y=0;y<cropH;++y) for(int x=0;x<cropW;++x) {
            int sx=frame->rotated?frame->w-1-y:x,sy=frame->rotated?x:y;
            int64_t dx=(int64_t)x-frame->frameX,dy=(int64_t)y-frame->frameY;
            if(dx>=0 && dy>=0 && dx<w && dy<h)std::memcpy(rgba.data()+((size_t)dy*w+dx)*4,pixels.get()+((size_t)(frame->y+sy)*width+frame->x+sx)*4,4);
        }
        std::string bytes; auto append=[](void* out,void* data,int count) { ((std::string*)out)->append((const char*)data,count); };
        if(!stbi_write_png_to_func(append,&bytes,w,h,4,rgba.data(),w*4)) p.errors.push_back("No se pudo decodificar la imagen");
        else addText(p,"images/icons/"+id+".png",bytes);
    } else {
        iconFiles(p,icon,id,bundle);
    }
    destinationCheck(p,catalog); return p;
}
inline Plan definitionPlan(const Catalog& catalog,int tab,int selected,const fs::path& destination,const std::string& id) {
    Plan p; p.destination=destination; p.label=id;
    if(!validId(id)) { p.errors.push_back("ID inválido: "+id+". Usa letras, números, guion o guion bajo."); return p; }
    if(tab==0 && selected>=0 && (size_t)selected<catalog.characters.size()) {
        auto c=catalog.characters[selected]; c.imported.character.id=id;
        addText(p,"data/characters/"+id+".xml",fml::emitCodenameCharacter(c.imported.character,c.take));
    } else if(tab==2 && selected>=0 && (size_t)selected<catalog.stages.size()) {
        auto s=catalog.stages[selected]; s.imported.stage.id=id;
        addText(p,"data/stages/"+id+".xml",fml::emitCodenameStage(s.imported));
    }
    p.warnings.push_back("Solo definición: copia sus recursos y revisa las rutas antes de usarla."); destinationCheck(p,catalog); return p;
}
inline Plan chartsPlan(const Catalog& catalog,const Song& song,const fs::path& destination,const std::string& id,const std::string& stageOverride) {
    Plan p; p.destination=destination; p.label=id;
    if(!validId(id)) { p.errors.push_back("ID de canción inválido."); return p; }
    bool routedSingers=false,builtInTypes=false;
    for(const auto& source:song.difficulties) if(source.take) {
        auto d=source; if(!stageOverride.empty()) d.chart.stage=stageOverride;
        auto safety=fml::chartSafetyError(d.chart);if(!safety.empty()){p.errors.push_back(d.name+": "+safety);continue;}
        if(!validId(d.name)) { p.errors.push_back("ID inválido: "+d.name+". Usa letras, números, guion o guion bajo."); continue; }
        fml::NewSongRequest request; request.id=id; request.bpm=d.chart.bpm; request.strumLines=d.chart.strumLines;
        addText(p,"songs/"+id+"/charts/"+d.name+".json",chartDocument(d,request));
        addText(p,"songs/"+id+"/meta-"+d.name+".json",difficultyMetadata(song,d,id));
        auto archive="psych-scripts/charts/"+id+"/"+d.name+"/";addCopy(p,archive+"source-chart.json",d.path);
        auto events=d.path.parent_path()/"events.json";if(fml::psych::file(events))addCopy(p,archive+"source-events.json",events);
        int script=0;for(const auto& type:noteSupport(catalog,d))if(!type.script.path.empty())addCopy(p,archive+"note-types/"+std::to_string(++script)+"-"+type.script.path.filename().u8string(),type.script.path);
        for(const auto& note:d.chart.notes){if(note.singerStrumLine>=0)routedSingers=true;if(psychBuiltInNoteType(note.type))builtInTypes=true;}
        p.events.push_back(eventReport(d));
        p.noteTypes.push_back(noteReport(catalog,d));
    }
    if(p.files.empty()) p.errors.push_back("Selecciona al menos una dificultad.");
    if(routedSingers)addText(p,"songs/"+id+"/scripts/fml-psych-singers.hx",singerRoutingScript());
    if(builtInTypes)addText(p,"songs/"+id+"/scripts/fml-psych-notetypes.hx",psychNoteTypesScript());
    addText(p,"psych-scripts/songs/"+id+"/events-report.json",p.events.dump(2));
    addText(p,"psych-scripts/songs/"+id+"/note-types-report.json",p.noteTypes.dump(2));
    p.warnings.push_back("Solo charts: configura audio, personajes y la declaración stage posteriormente."); destinationCheck(p,catalog); return p;
}
inline Plan chartBundlePlan(const Catalog& catalog,const Song& song,const fs::path& destination,const fs::path& base,const std::string& id,bool characters,bool stages) {
    auto plan=chartsPlan(catalog,song,destination,id,""); std::map<std::string,int> copiedCharacters,copiedStages;
    json bindings={{"stageIncluded",stages},{"charactersIncluded",characters},{"difficulties",json::array()}};
    std::vector<std::string> offsetCharacters;
    if(characters || stages) {plan.warnings.erase(std::remove(plan.warnings.begin(),plan.warnings.end(),"Solo charts: configura audio, personajes y la declaración stage posteriormente."),plan.warnings.end());plan.warnings.push_back("Chart con dependencias seleccionadas: configura el audio en destino.");}
    for(const auto& d:song.difficulties) if(d.take) {
        json references={{"difficulty",d.name},{"stageKey",d.chart.stage},{"characters",json::array()}};
        if(stages && !d.chart.stage.empty()) {
            int selected=d.exportStage;
            if(selected<0) for(int i=0;i<(int)catalog.stages.size();++i) if(lower(catalog.stages[i].imported.stage.id)==lower(d.chart.stage)) {if(selected>=0){selected=-2;break;}selected=i;}
            auto key=lower(d.chart.stage);
            if(copiedStages.count(key) && copiedStages[key]!=selected)plan.errors.push_back("Una misma clave de escenario tiene recursos diferentes: "+d.chart.stage);
            else if(!copiedStages.count(key)) {
                copiedStages[key]=selected;
                if(selected>=0 && (size_t)selected<catalog.stages.size()) {
                    // Not folded: the player's offset goes in a song script
                    // (playerOffsetScript), so one stage serves every chart.
                    Plan staged;stageFiles(staged,catalog.stages[selected],d.chart.stage);
                    if(staged.errors.empty())mergePlan(plan,staged);
                    else plan.warnings.push_back(d.chart.stage+": no se puede exportar el escenario ("+joined(staged.errors)+"); el chart se jugará sin decorado.");
                }else if(!validId(d.chart.stage) || !available(plan,base,"data/stages/"+d.chart.stage+".xml"))plan.errors.push_back("Elige el recurso de escenario para la clave: "+d.chart.stage);
            }
            if(selected>=0 && (size_t)selected<catalog.stages.size())references["stageAsset"]=catalog.stages[selected].source.u8string();
        }
        for(const auto& line:d.chart.strumLines) for(const auto& key:line.characters) if(!key.empty()) {
            int selected=-1; auto override=d.exportCharacters.find(key); if(override!=d.exportCharacters.end()) selected=override->second;
            else for(int i=0;i<(int)catalog.characters.size();++i) if(lower(catalog.characters[i].imported.character.id)==lower(key)) {if(selected>=0){selected=-2;break;}selected=i;}
            json reference={{"key",key}};if(selected>=0 && (size_t)selected<catalog.characters.size())reference["asset"]=catalog.characters[selected].source.u8string();references["characters"].push_back(reference);
            auto normalized=lower(key);if(!characters)continue;
            if(copiedCharacters.count(normalized) && copiedCharacters[normalized]!=selected)plan.errors.push_back("Una misma clave de personaje tiene recursos diferentes: "+key);
            else if(!copiedCharacters.count(normalized)){copiedCharacters[normalized]=selected;if(selected>=0 && (size_t)selected<catalog.characters.size()){characterFiles(plan,catalog.characters[selected],key,&catalog);
                bool player=false;for(const auto& l:d.chart.strumLines)if(l.type==1)for(const auto& c:l.characters)if(c==key)player=true;
                if(player && catalog.characters[selected].imported.character.position.x!=0)offsetCharacters.push_back(key);}else if(!validId(key) || !available(plan,base,"data/characters/"+key+".xml"))plan.errors.push_back("Elige el recurso de personaje para la clave: "+key);}
        }
        bindings["difficulties"].push_back(references);
    }
    if(!offsetCharacters.empty())addText(plan,"songs/"+id+"/scripts/fml-psych-player-offset.hx",playerOffsetScript(offsetCharacters));
    addText(plan,"psych-scripts/songs/"+id+"/resource-bindings.json",bindings.dump(2));destinationCheck(plan,catalog);
    std::sort(plan.errors.begin(),plan.errors.end()); plan.errors.erase(std::unique(plan.errors.begin(),plan.errors.end()),plan.errors.end()); return plan;
}
}
