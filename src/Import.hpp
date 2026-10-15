#pragma once
#include "Catalog.hpp"
#include "Log.hpp"
#include "Modchart.hpp"
#include "NoteSupport.hpp"
#include "../../src/fml_adapters/CodenameSongAuthor.hpp"
#include "../../src/fml_formats/CodenameChart.hpp"
#include <array>
#include "../../src/fml_core/ChartSafety.hpp"
#include "../../src/fml_formats/HealthIconFrames.hpp"
#include "../../third_party/stb_image.h"
#include <chrono>
#include <sstream>

namespace pi {
struct SourceStamp {std::uintmax_t size;fs::file_time_type modified;};
struct Output { std::string path, text; fs::path source; std::vector<std::string> appendLines; std::optional<SourceStamp> sourceStamp; };
inline std::optional<SourceStamp> stampSource(const fs::path& path) {
    std::error_code ec;auto size=fs::file_size(path,ec);if(ec)return std::nullopt;
    auto modified=fs::last_write_time(path,ec);if(ec)return std::nullopt;return SourceStamp{size,modified};
}
inline bool unchangedSource(const Output& file) {
    if(file.source.empty())return true;
    const auto current=stampSource(file.source);
    return file.sourceStamp && current && file.sourceStamp->size==current->size && file.sourceStamp->modified==current->modified;
}
struct Plan {
    fs::path destination;
    std::vector<Output> files;
    std::vector<std::string> warnings, errors;
    std::string label;
    std::map<std::string,std::string> iconBindings;
    json events=json::array();
    json noteTypes=json::array();
    // The destination is a mod folder (single exports). A batch writes into
    // the mods folder itself and adds one config per bundle instead.
    bool modRoot=true;
    // A batch carries a text file on how to install it (refreshInstallGuide).
    std::string guidePath; bool guideZip=false,guideInPlace=false;
    bool ready() const { return errors.empty() && !files.empty(); }
};
// data/config/modpack.ini identifies a Codename mod. In developer mode
// Codename asks to generate one for every mod without it (MainState.hx,
// ModConfigWarning.hx). Same sections as Codename's template; API_VERSION is
// left out so the engine loading the mod uses its own (1.0.1 writes 1).
inline std::string modConfigText(std::string name) {
    name.erase(std::remove_if(name.begin(),name.end(),[](char c){return c=='"' || c=='\n' || c=='\r';}),name.end());
    return "[Common] # This section applies the 'MOD_' prefix to the flags so you don't have to.\n"
           "NAME=\""+name+"\"\n"
           "DESCRIPTION=\"Exported from Psych Engine with Psych Importer (Funkin Mod Lab).\"\n"
           "AUTHOR=\"\"\n"
           "VERSION=\"1.0.0\"\n\n"
           "DOWNLOAD_LINK=\"\"\n\n"
           "[Flags] # This section doesn't apply any prefix.\n"
           "DISABLE_WARNING_SCREEN=true\n"
           "DISABLE_LANGUAGES=true\n";
}
inline bool validId(const std::string& id) {
    if(id.empty() || id.size()>100 || id=="." || id=="..") return false;
    for(unsigned char c:id) if(!std::isalnum(c) && c!='_' && c!='-') return false;
    static const std::set<std::string> reserved{"con","prn","aux","nul","com1","com2","com3","com4","com5","com6","com7","com8","com9","lpt1","lpt2","lpt3","lpt4","lpt5","lpt6","lpt7","lpt8","lpt9"};
    return !reserved.count(lower(id));
}
inline bool within(const fs::path& root,const fs::path& child) {
    std::error_code ec; auto a=fs::weakly_canonical(root,ec); if(ec) return false;
    auto b=fs::weakly_canonical(child,ec); if(ec) return false;
    auto rel=b.lexically_relative(a);
    return !rel.empty() && *rel.begin()!=".." && !rel.is_absolute();
}
inline bool available(const Plan& p,const fs::path& base,const std::string& path) {
    for(const auto& file:p.files)if(lower(file.path)==lower(path))return true;
    return fml::psych::file(p.destination/fs::u8path(path)) || (!base.empty() && fml::psych::file(base/fs::u8path(path)));
}
inline void addText(Plan& p,const std::string& path,const std::string& text) { p.files.push_back({path,text,{}}); }
// A Sparrow atlas is copied as text without stray BOMs before "<?xml": some
// mods ship two (Mario's Madness exe-land), which strict XML parsers reject.
inline std::string cleanAtlasText(std::string text) {
    while(text.size()>=3 && (unsigned char)text[0]==0xEF && (unsigned char)text[1]==0xBB && (unsigned char)text[2]==0xBF) text.erase(0,3);
    return text;
}
// Merge an isolated sub-plan (one character or stage) into a song plan.
inline void mergePlan(Plan& into,const Plan& from) {
    into.files.insert(into.files.end(),from.files.begin(),from.files.end());
    into.warnings.insert(into.warnings.end(),from.warnings.begin(),from.warnings.end());
    for(const auto& binding:from.iconBindings) {
        auto previous=into.iconBindings.find(binding.first);
        if(previous!=into.iconBindings.end() && previous->second!=binding.second)into.errors.push_back("Dos recursos escribirían la misma ruta: "+binding.first);
        else into.iconBindings[binding.first]=binding.second;
    }
}
inline std::string joined(const std::vector<std::string>& list) {
    std::string out; for(const auto& item:list) out+=(out.empty()?"":"; ")+item; return out;
}
// Psych adds a character's x position on either side; Codename subtracts it
// on the player side unless the XML says isPlayer="true" (Character.playAnim:
// offset = globalOffset.x when isPlayer != playerOffsets). The exported Psych
// characters listed here are moved once so they stand where Psych put them.
// Folding it into the stage instead made every song write its own version of
// a shared stage (Wii Funkin': 3hotnew, fisticuffs, unknownnew...).
inline std::string playerOffsetScript(const std::vector<std::string>& ids) {
    std::string list; for(const auto& id:ids) list+=(list.empty()?"":", ")+std::string("\"")+id+"\"";
    return "// Psych position offsets on the player side, written by Psych Importer.\n"
           "var psychCharacters = ["+list+"];\n"
           "function postCreate() {\n"
           "    for (line in strumLines.members)\n"
           "        for (character in line.characters)\n"
           "            if (character != null && psychCharacters.contains(character.curCharacter) && character.isPlayer != character.playerOffsets)\n"
           "                character.x += 2 * character.globalOffset.x;\n"
           "}\n";
}
inline bool validPng(const fs::path& path,int* width=nullptr,int* height=nullptr,bool fresh=false) {
    std::error_code ec;auto size=fs::file_size(path,ec);
    if(ec || size==0 || size>128u*1024u*1024u)return false;
    const auto modified=fs::last_write_time(path,ec);if(ec)return false;
    struct Cached { uintmax_t size;fs::file_time_type modified;int width,height; };
    static std::mutex mutex;static std::map<std::string,Cached> cache;
    const auto key=path.lexically_normal().generic_u8string();
    if(!fresh) {
        std::lock_guard<std::mutex> lock(mutex);auto hit=cache.find(key);
        if(hit!=cache.end() && hit->second.size==size && hit->second.modified==modified) {
            if(width)*width=hit->second.width;if(height)*height=hit->second.height;return true;
        }
    }
    auto bytes=read(path);int w=0,h=0,n=0;
    if(bytes.size()<8 || bytes.compare(0,8,"\x89PNG\r\n\x1a\n",8)!=0)return false;
    if(!stbi_info_from_memory((const stbi_uc*)bytes.data(),(int)bytes.size(),&w,&h,&n) || w<=0 || h<=0 || (uint64_t)w*h>64u*1024u*1024u)return false;
    auto* pixels=stbi_load_from_memory((const stbi_uc*)bytes.data(),(int)bytes.size(),&w,&h,&n,4);
    if(!pixels)return false;
    stbi_image_free(pixels);
    {std::lock_guard<std::mutex> lock(mutex);if(cache.size()>=512)cache.clear();cache[key]={size,modified,w,h};}
    if(width)*width=w;if(height)*height=h;return true;
}
inline void addCopy(Plan& p,const std::string& path,const fs::path& source) {
    const auto stamp=stampSource(source);
    if(lower(fs::u8path(path).extension().u8string())==".png" && !validPng(source))p.errors.push_back("PNG inválido o excede el límite: "+source.u8string());
    p.files.push_back({path,{},source,{},stamp});
}
inline void addAtlas(Plan& p,const fs::path& image,const fs::path& atlas,const std::string& target) {
    fml::DiagnosticSink sink;auto packed=lower(atlas.extension().u8string())==".txt";
    auto text=cleanAtlasText(read(atlas));
    auto parsed=packed?fml::parsePackerAtlas(text,atlas.u8string(),sink):fml::parseSparrowAtlas(text,atlas.u8string(),sink);
    int width=0,height=0;
    if(!parsed || !fml::psych::pngSize(image,width,height)) {p.errors.push_back("Atlas o PNG inválido: "+atlas.u8string());return;}
    for(const auto& frame:parsed.value().frames)if(!fml::frameInsideSheet(frame,width,height)) {p.errors.push_back("Frame fuera de la imagen: "+atlas.u8string());return;}
    if(packed && lower(fs::u8path(target).extension().u8string())!=".txt")addText(p,target,fml::emitSparrowAtlas(parsed.value(),fs::u8path(target).stem().u8string()+".png"));
    else addText(p,target,text);
}
inline Icon iconSource(const fs::path& path,const Catalog* catalog=nullptr) {
    if(catalog)for(const auto& icon:catalog->icons)if(icon.source==path)return icon;
    Icon icon;icon.source=path;auto atlas=path;atlas.replace_extension(".xml");if(!fml::psych::file(atlas))atlas.replace_extension(".txt");if(fml::psych::file(atlas))icon.atlas=atlas;
    auto data=path.parent_path()/"data.xml";if(fml::psych::file(data))icon.data=data;return icon;
}
inline void iconFiles(Plan& p,const Icon& icon,const std::string& id,bool bundle=true) {
    const auto prefix="images/icons/"+id;
    const auto identity=icon.source.generic_u8string()+"|"+icon.atlas.generic_u8string()+"|"+icon.data.generic_u8string();
    auto bound=p.iconBindings.find(prefix);
    if(bound!=p.iconBindings.end()){if(bound->second!=identity)p.errors.push_back("Dos recursos escribirían la misma ruta: "+prefix);return;}
    p.iconBindings[prefix]=identity;
    if(fml::psych::file(p.destination/fs::u8path(prefix+".png")) || fml::psych::directory(p.destination/fs::u8path(prefix))) p.errors.push_back("Ya existe; usa otro ID: "+prefix);
    if(bundle && !icon.atlas.empty()) {
        addCopy(p,prefix+"/icon.png",icon.source);addAtlas(p,icon.source,icon.atlas,prefix+"/icon.xml");
        std::string data=read(icon.data);
        if(!icon.data.empty()) {
            pugi::xml_document doc;if(!doc.load_buffer(data.data(),data.size()) || !doc.document_element()){p.errors.push_back("Datos de icono inválidos: "+icon.data.u8string());return;}
        } else {
            fml::AtlasStore store;store.readText=[](const std::string& path){return read(fs::u8path(path));};int width=0,height=0;
            if(!fml::psych::pngSize(icon.source,width,height))return;
            auto frames=fml::healthIconFrames(store.get(icon.atlas.u8string()),width,height);
            data=fml::emitHealthIconData(frames);
        }
        addText(p,prefix+"/data.xml",data);
    } else {
        addCopy(p,prefix+".png",icon.source);
        if(bundle && !icon.data.empty())addCopy(p,prefix+"/data.xml",icon.data);
    }
}
inline void animateFiles(Plan& p,const std::shared_ptr<AnimateResource>& resource,const std::string& prefix,const std::string& archive,const std::string& id) {
    if(!resource || !resource->error.empty()) { p.errors.push_back(id+": Animate no válido: "+(resource?resource->error:"sin atlas")); return; }
    if(resource->compatible.empty()) { p.errors.push_back(id+": Animate contiene efectos no convertibles: "+resource->compatibilityError); return; }
    addText(p,archive+"/Animation.json",resource->original);
    addText(p,prefix+"/Animation.json",resource->compatible);
    for(const auto& map:resource->maps) addCopy(p,prefix+"/"+map.filename().u8string(),map);
    for(const auto& image:resource->images) {
        auto relative=image.lexically_relative(resource->source.parent_path()).generic_u8string();
        if(!fml::psych::relativeKey(relative)) { p.errors.push_back("Ruta destino inválida: "+relative); continue; }
        addCopy(p,prefix+"/"+relative,image);
    }
}
inline bool archiveCopy(const std::string& path) {
    return path.rfind("psych-scripts/",0)==0 || path.find("/psych-scripts/")!=std::string::npos;
}
inline void destinationCheck(Plan& p,const Catalog& catalog) {
    if(p.modRoot && !p.destination.empty() && fml::psych::directory(p.destination) &&
       !fml::psych::file(p.destination/"data/config/modpack.ini") &&
       std::none_of(p.files.begin(),p.files.end(),[](const Output& f){return f.path=="data/config/modpack.ini";}))
        p.files.push_back({"data/config/modpack.ini",modConfigText(p.label),{}});
    // psych-scripts/ only keeps reference copies of the originals. An empty
    // script there is skipped with a notice; it must not block the bundle.
    p.files.erase(std::remove_if(p.files.begin(),p.files.end(),[&](const Output& file){
        if(file.source.empty() || !archiveCopy(file.path)) return false;
        std::error_code ec; auto size=fs::file_size(file.source,ec);
        if(ec || size!=0) return false;
        p.warnings.push_back("Archivo de origen vacío, no se archiva: "+file.source.u8string()); return true;
    }),p.files.end());
    if(p.destination.empty() || !fml::psych::directory(p.destination)) p.errors.push_back("Elige una carpeta de destino existente.");
    // What must never happen is writing inside a source folder. A destination
    // above the mod (Downloads holding the mod's own folder) is fine as long
    // as no planned file lands inside it.
    for(const auto& root:catalog.files.roots) {
        if(p.destination.empty()) break;
        bool inside=within(root,p.destination);
        if(!inside && within(p.destination,root)) for(const auto& file:p.files) if(within(root,p.destination/fs::u8path(file.path))) { inside=true; break; }
        if(inside) { p.errors.push_back("El destino debe estar separado de las carpetas de origen."); break; }
    }
    std::set<std::string> seen; unsigned long long total=0;
    for(auto& file:p.files) {
        if(!fml::psych::relativeKey(file.path) || !within(p.destination,p.destination/fs::u8path(file.path))) p.errors.push_back("Ruta destino inválida: "+file.path);
        if(!seen.insert(lower(file.path)).second) p.errors.push_back("Dos recursos escribirían la misma ruta: "+file.path);
        if(fml::psych::file(p.destination/fs::u8path(file.path))) p.errors.push_back("Ya existe; usa otro ID: "+file.path);
        if(!file.source.empty()) {
            if(!file.sourceStamp)file.sourceStamp=stampSource(file.source);
            std::error_code ec; auto size=fs::file_size(file.source,ec);
            if(ec || size==0 || size>512ull*1024ull*1024ull) p.errors.push_back("Recurso ausente, vacío o demasiado grande: "+file.source.u8string());
            else total+=size;
            if(lower(fs::u8path(file.path).extension().u8string())==".png" && !validPng(file.source))p.errors.push_back("PNG inválido o excede el límite: "+file.source.u8string());
        } else total+=file.text.size();
    }
    // Limits that keep one export from taking the machine down: a ceiling on
    // files and size, and free disk space with a margin.
    constexpr size_t maxFiles=100000; constexpr unsigned long long maxBytes=16ull<<30,margin=256ull<<20;
    if(p.files.size()>maxFiles) p.errors.push_back("La exportación supera el límite de "+std::to_string(maxFiles)+" archivos; divídela en varias.");
    if(total>maxBytes) p.errors.push_back("La exportación supera el límite de 16 GB; divídela en varias.");
    if(!p.destination.empty() && fml::psych::directory(p.destination)) {
        std::error_code ec; auto space=fs::space(p.destination,ec);
        if(!ec && space.available<total+margin)
            p.errors.push_back("No hay espacio suficiente en el disco de destino: hacen falta "+std::to_string((total+margin)>>20)+" MB y quedan "+std::to_string(space.available>>20)+" MB.");
    }
}
inline void characterFiles(Plan& plan,Character c,const std::string& id,const Catalog* catalog=nullptr) {
    if(!validId(id)) { plan.errors.push_back("ID inválido: "+id+". Usa letras, números, guion o guion bajo."); return; }
    if(!c.imported.extraImages.empty() && c.extraSheets.size()!=c.imported.extraImages.size()) { plan.errors.push_back(id+": no se encontraron todas las hojas de su atlas ("+std::to_string(c.imported.extraImages.size()+1)+")."); return; }
    if(c.image.empty() || c.atlas.empty()) {
        // Psych cannot load it either: the JSON names a sheet the mod lacks.
        plan.errors.push_back(id+": la imagen que declara ("+c.imported.character.spriteAtlas+") no está en el mod; tampoco carga en Psych. Elígela a mano si existe con otro nombre.");
        return;
    }
    fml::DiagnosticSink sink;
    if(!c.animate && !packerAtlas(c.atlas)) {
        auto parsed=fml::parseSparrowAtlas(read(c.atlas),c.atlas.u8string(),sink);
        if(!parsed) { plan.errors.push_back(id+": atlas inválido."); return; }
        countCharacterFrames(c,sink);
    } else if(packerAtlas(c.atlas)) {
        countCharacterFrames(c,sink);
    } else if(!c.animateData || !c.animateData->error.empty()) { plan.errors.push_back(id+": atlas inválido."); return; }
    int taken=0;
    for(size_t i=0;i<c.take.size();++i) {
        if(c.take[i] && c.imported.character.anims[i].resolvedFrames<=0) plan.errors.push_back(id+": animación sin fotogramas: "+c.imported.character.anims[i].name);
        if(c.take[i]) ++taken;
    }
    if(!taken) { plan.errors.push_back(id+": selecciona al menos una animación válida."); return; }
    auto model=c.imported.character; model.id=id; model.spriteAtlas="characters/"+id+"-import";
    if(!c.iconPath.empty()) { model.icon=id+"-import"; iconFiles(plan,iconSource(c.iconPath,catalog),model.icon); }
    else { model.icon="face"; plan.warnings.push_back(id+": no hay icono de origen; requiere face en el destino."); }
    if(c.animate) animateFiles(plan,c.animateData,"images/"+model.spriteAtlas,"psych-scripts/characters/"+id+"/animate",id);
    else if(!c.extraSheets.empty()) {
        // Several sheets: images/<key>/1.png+1.xml, 2.png+2.xml... is what
        // Paths.loadFrames reads as one MultiFramesCollection.
        addCopy(plan,"images/"+model.spriteAtlas+"/1.png",c.image);
        addAtlas(plan,c.image,c.atlas,"images/"+model.spriteAtlas+"/1.xml");
        int page=1;
        for(const auto& extra:c.extraSheets) {
            ++page;
            addCopy(plan,"images/"+model.spriteAtlas+"/"+std::to_string(page)+".png",extra.first);
            addAtlas(plan,extra.first,extra.second,"images/"+model.spriteAtlas+"/"+std::to_string(page)+".xml");
        }
    } else if(packerAtlas(c.atlas)) {
        addCopy(plan,"images/"+model.spriteAtlas+".png",c.image);
        addAtlas(plan,c.image,c.atlas,"images/"+model.spriteAtlas+".txt");
    } else {
        addCopy(plan,"images/"+model.spriteAtlas+".png",c.image);
        addAtlas(plan,c.image,c.atlas,"images/"+model.spriteAtlas+".xml");
    }
    addText(plan,"psych-scripts/characters/"+id+"/source.json",c.imported.sourceJson);
    if(model.camOffset.x!=0) addText(plan,"data/characters/"+id+".hx",
        "// Psych camera offset adaptation.\nfunction onGetCamPos(event) {\n    if (isPlayer) event.x -= 2 * cameraOffset.x;\n}\n");
    addText(plan,"data/characters/"+id+".xml",fml::emitCodenameCharacter(model,c.take));
    for(const auto& w:c.warnings) plan.warnings.push_back(w.rfind(id+":",0)==0?w:id+": "+w);
    auto script=c.source; script.replace_extension(".lua");
    if(fml::psych::file(script)) { addCopy(plan,"psych-scripts/characters/"+id+"/source.lua",script); plan.warnings.push_back(id+": Lua conservado sin activar.");plan.warnings.push_back(id+": el comportamiento Lua del personaje no se conecta; requiere porteo."); }
}
inline Plan characterPlan(const Catalog& catalog,const Character& c,const fs::path& destination,const std::string& id) {
    Plan p; p.destination=destination; p.label=id; characterFiles(p,c,id,&catalog); destinationCheck(p,catalog); return p;
}
inline void stageFiles(Plan& plan,Stage stage,const std::string& id) {
    if(!validId(id)) { plan.errors.push_back("ID inválido: "+id+". Usa letras, números, guion o guion bajo."); return; }
    auto& model=stage.imported.stage; model.id=id; model.assetFolder.clear();
    int scenery=0; std::map<std::string,std::string> copied; std::vector<size_t> absent;
    for(size_t i=0;i<model.objects.size();++i) {
        auto& object=model.objects[i];
        if(object.kind==fml::StageObject::Kind::Box) { ++scenery; continue; }
        if(object.kind!=fml::StageObject::Kind::Sprite) continue;
        // A sprite whose image is not in the mod is also blank in Psych
        // (makeLuaSprite with a missing file). It is left out with a notice
        // instead of blocking the stage and every song that uses it.
        if(object.resolvedImage.empty()) { plan.warnings.push_back(id+": imagen ausente en el mod, se omite: "+object.spritePath); absent.push_back(i); continue; }
        if(!stage.animateData.count(i) && object.resolvedAtlas.empty() && !object.anims.empty()) { plan.warnings.push_back(id+": atlas ausente en el mod, se omite: "+object.spritePath); absent.push_back(i); continue; }
        ++scenery;
        auto identity=object.resolvedImage+"|"+object.resolvedAtlas;
        if(copied.count(identity)) { object.spritePath=copied[identity]; continue; }
        auto key="stages/"+id+"-import/asset-"+std::to_string(i+1); copied[identity]=key;
        object.spritePath=key;
        if(stage.animateData.count(i)) animateFiles(plan,stage.animateData[i],"images/"+key,"psych-scripts/stages/"+id+"/animate-"+std::to_string(i+1),id);
        else {
            addCopy(plan,"images/"+key+".png",fs::u8path(object.resolvedImage));
            if(!object.resolvedAtlas.empty()) addAtlas(plan,fs::u8path(object.resolvedImage),fs::u8path(object.resolvedAtlas),"images/"+key+".xml");
        }
    }
    for(auto at=absent.rbegin();at!=absent.rend();++at) model.objects.erase(model.objects.begin()+(std::ptrdiff_t)*at);
    if(!scenery) plan.errors.push_back(id+": escenario vacío; no se puede importar como completo.");
    addText(plan,"data/stages/"+id+".xml",fml::emitCodenameStage(stage.imported));
    for(const auto& ext:{".json",".lua",".hx"}) {
        auto path=stage.source; path.replace_extension(ext);
        if(fml::psych::file(path)) addCopy(plan,"psych-scripts/stages/"+id+"/source"+ext,path);
    }
    // Each notice names its stage. ModExplorer's English count of script
    // calls repeats FML's "ajustes del script" line and is left out.
    for(const auto& w:stage.warnings) {
        if(w.find(" stage script calls were not reconstructed")!=std::string::npos) continue;
        plan.warnings.push_back(w.rfind(id+":",0)==0?w:id+": "+w);
    }
    if(stage.imported.scriptCallsIgnored || stage.imported.branchesUnknown || stage.imported.loopsSkipped || stage.imported.scriptHadBranches)
        plan.warnings.push_back(id+": lógica dinámica pendiente; se importa el decorado declarativo.");
}
// FML rebuilds a stage that the source engine compiled into its executable
// from the image keys it stores (PsychStageSource.hpp). The pieces come out
// centred, without positions, scales or scroll: a starting point for the stage
// editor, not the stage the song uses.
inline bool reconstructedFromEngine(const Stage& stage) {
    for(const auto& w:stage.imported.warnings)if(w.rfind("RECONSTRUIDO",0)==0)return true;
    return false;
}
// Decoded size of the PNGs a plan writes (IHDR width * height * 4).
inline unsigned long long decodedTextureBytes(const Plan& plan) {
    unsigned long long total=0;
    for(const auto& file:plan.files) {
        if(file.source.empty() || lower(fs::u8path(file.path).extension().u8string())!=".png")continue;
        std::ifstream in(file.source,std::ios::binary);unsigned char h[24]{};
        if(!in.read((char*)h,24) || h[1]!='P' || h[2]!='N' || h[3]!='G')continue;
        auto be=[&](int at){return (unsigned long long)h[at]<<24|(unsigned long long)h[at+1]<<16|(unsigned long long)h[at+2]<<8|h[at+3];};
        total+=be(16)*be(20)*4ull;
    }
    return total;
}
inline Plan stagePlan(const Catalog& catalog,const Stage& stage,const fs::path& destination,const std::string& id) {
    Plan p; p.destination=destination; p.label=id; stageFiles(p,stage,id); destinationCheck(p,catalog); return p;
}
inline json eventReport(const Difficulty& d) {
    json result={{"difficulty",d.name},{"total",d.events.total},{"converted",json::array()},{"unresolved",json::array()}};
    for(const auto& item:d.events.translated) {
        std::string equivalent; for(const auto& port:fml::psychEventPorts()) if(port.psych==item.first) equivalent=port.codename;
        result["converted"].push_back({{"psych",item.first},{"codename",equivalent},{"count",item.second}});
    }
    for(const auto& item:d.events.unresolved) {
        std::string hint="Custom event needs a Codename script or manual port.";
        for(const auto& port:fml::psychEventPorts()) if(port.psych==item.first) hint=port.hint;
        result["unresolved"].push_back({{"psych",item.first},{"count",item.second},{"hint",hint},{"preserved",true},{"active",false}});
    } return result;
}
inline std::string chartDocument(const Difficulty& difficulty,const fml::NewSongRequest& request) {
    auto chart=difficulty.chart;
    for(const auto& note:chart.notes) if(!note.type.empty() && std::find(chart.noteTypes.begin(),chart.noteTypes.end(),note.type)==chart.noteTypes.end()) chart.noteTypes.push_back(note.type);
    // Embed each difficulty's resolved globals locally. Separate difficulty
    // groups can have different events.json files or BPM-dependent conversions.
    // This prevents one difficulty from inheriting another group's events.
    for(auto& event:chart.events) if(event.foreignSource.empty()) event.global=false;
    auto original=read(difficulty.path);
    auto baseline=fml::isCodenameChartDocument(original)?original:fml::buildEmptyChart(request);
    // Codename Chart.loadChart overlays inline meta onto meta(-difficulty).json.
    // Keep the initial tempo/voice declaration valid even for a loose chart.
    auto native=json::parse(baseline);if(!native.contains("meta") || !native["meta"].is_object())native["meta"]=json::object();
    auto& meta=native["meta"];meta["name"]=request.id;meta["displayName"]=request.displayName.empty()?chart.songId:request.displayName;meta["bpm"]=chart.bpm;meta["beatsPerMeasure"]=chart.beatsPerMeasure;meta["stepsPerBeat"]=chart.stepsPerBeat;meta["needsVoices"]=chart.needsVoices;baseline=native.dump(1,'\t');
    return fml::writeCodenameChart(baseline,chart);
}
inline std::string difficultyMetadata(const Song& song,const Difficulty& d,const std::string& id) {
    fml::SongMeta meta;meta.name=id;meta.displayName=song.title;meta.bpm=d.chart.bpm;meta.beatsPerMeasure=d.chart.beatsPerMeasure;meta.stepsPerBeat=d.chart.stepsPerBeat;meta.needsVoices=d.chart.needsVoices;
    for(const auto& diff:song.difficulties)if(diff.take)meta.difficulties.push_back(diff.name);meta.difficultiesInferred=false;
    return fml::serializeSongMeta(meta,{});
}
// Characters by note type in Codename: each is placed like a stage character
// (Stage.applyCharStuff) beside its line's own, 230 px apart as in the
// preview, and takes the notes of its type in onNoteHit; with "-duet" the
// line's characters keep singing too. Rows: character, type, line, place.
inline std::string typeSingersScript(const std::vector<std::array<std::string,4>>& rows) {
    std::string data;
    for(const auto& r:rows) data+=std::string(data.empty()?"":", ")+"[\""+r[0]+"\", \""+r[1]+"\", "+r[2]+", \""+r[3]+"\"]";
    return "// Characters that sing the notes of one type, from the Psych song's own\n"
           "// scripts. Written by Psych Importer.\n"
           "import funkin.game.Character;\nimport haxe.ds.StringMap;\n\n"
           "var singers = ["+data+"];\n"
           "var made = [];\n\n"
           "function postCreate() {\n"
           "\tvar beside = new StringMap();\n"
           "\tfor (s in singers) {\n"
           "\t\tvar c = new Character(0, 0, s[0], s[3] == \"boyfriend\");\n"
           "\t\tstage.applyCharStuff(c, s[3], 0);\n"
           "\t\tvar n = beside.exists(s[3]) ? beside.get(s[3]) + 1 : 1;\n"
           "\t\tbeside.set(s[3], n);\n"
           "\t\tc.x += (s[3] == \"boyfriend\" ? 230 : -230) * n;\n"
           "\t\tmade.push(c);\n"
           "\t}\n"
           "}\n\n"
           "function singerOf(type) {\n"
           "\tif (type == null) return -1;\n"
           "\tfor (i in 0...singers.length) if (type.indexOf(singers[i][1]) >= 0) return i;\n"
           "\treturn -1;\n"
           "}\n\n"
           "function sing(event) {\n"
           "\tvar i = singerOf(event.noteType);\n"
           "\tif (i < 0 || i >= made.length) return;\n"
           "\tevent.characters = event.noteType.indexOf(\"-duet\") >= 0 ? event.characters.concat([made[i]]) : [made[i]];\n"
           "}\n\n"
           "function onNoteHit(event) { sing(event); }\n"
           "function onPlayerMiss(event) { sing(event); }\n";
}
inline std::string singerRoutingScript() {return R"HX(// Basic singer routing, same convention as FML.
function fmlRouteSinger(event) {
    if (event.note == null || !event.note.extra.exists("fmlSingerLine")) return;
    var target = event.note.extra.get("fmlSingerLine");
    if (target >= 0 && target < strumLines.members.length) {
        var line = strumLines.members[target];
        if (line != null && line.characters != null) event.characters = line.characters;
    }
}
function onNoteHit(event) { fmlRouteSinger(event); }
function onPlayerMiss(event) { fmlRouteSinger(event); }
)HX";}
// Psych's hardcoded note types (Note.hx:221-226; PlayState.hx:2985 and 3069)
// are plain names to Codename, which would sing them as normal notes.
inline bool psychBuiltInNoteType(const std::string& type) {
    return type=="Alt Animation" || type=="No Animation" || type=="Hey!";
}
inline std::string psychNoteTypesScript() {return R"HX(// Psych Engine built-in note types, written by Psych Importer so they keep
// their behaviour in Codename (Psych Note.hx:221-226, PlayState.hx:2985/3069).
function psychHey(event) {
    for (character in event.characters) {
        if (character == null) continue;
        var anim = character.hasAnim("hey") ? "hey" : (character.hasAnim("cheer") ? "cheer" : null);
        if (anim == null) continue;
        event.preventAnim();
        character.playAnim(anim, true);
    }
}
function onNoteHit(event) {
    if (event.noteType == "Alt Animation") event.animSuffix = "-alt";
    else if (event.noteType == "No Animation") event.preventAnim();
    else if (event.noteType == "Hey!") psychHey(event);
}
function onPlayerMiss(event) {
    if (event.noteType == "Alt Animation") event.animSuffix = "miss-alt";
    else if (event.noteType == "No Animation") event.preventAnim();
}
)HX";}
// fitToGame: a character or stage the chart names and the game lacks is
// swapped in the chart for Codename's own (dad, bf or gf by the line; stage),
// so nothing falls back at load time. Only with a base to know what it has.
inline Plan songPlan(const Catalog& catalog,const Song& song,const fs::path& destination,const fs::path& base,
                     const std::string& id,const std::string& stageOverride,bool bringCharacters,bool bringStages=false,bool modcharts=true,bool fitToGame=false,const std::set<std::string>& supplied={}) {
    Plan p; p.destination=destination; p.label=id;
    if(!validId(id)) { p.errors.push_back("ID de canción inválido."); return p; }
    if(fml::psych::directory(destination/"songs"/fs::u8path(id))) { p.errors.push_back("Ya existe esa canción. Usa otro ID."); return p; }
    std::vector<Difficulty> selected;
    for(const auto& d:song.difficulties) if(d.take) {
        // A stray file whose name cannot be a Codename difficulty is left out
        // with a notice; the song's real difficulties still export.
        if(!validId(d.name)) { p.warnings.push_back(id+": se omite la dificultad con nombre inválido: "+d.name); continue; }
        // Codename cannot play a chart without its Inst (Paths.inst). A
        // difficulty the mod ships without audio is left out, not the song.
        if(d.audio.instPath.empty()) { p.warnings.push_back(id+": se omite la dificultad "+d.name+": el mod no trae su instrumental."); continue; }
        auto inst=fml::probeAudioFile(fs::u8path(d.audio.instPath));
        if(!inst.readable || inst.container!=fml::AudioContainer::OggVorbis) { p.warnings.push_back(id+": se omite la dificultad "+d.name+": su instrumental no es un OGG Vorbis válido."); continue; }
        selected.push_back(d);
    }
    if(selected.empty()) { p.errors.push_back(id+": ninguna dificultad exportable (sin instrumental o con nombre inválido)."); return p; }
    for(auto& difficulty:selected) {
        auto keepVoice=[&](std::string& path) {
            if(path.empty())return;
            const auto probe=fml::probeAudioFile(fs::u8path(path));
            if(!probe.readable || probe.container!=fml::AudioContainer::OggVorbis) {
                p.warnings.push_back(id+"/"+difficulty.name+": se omite una pista que no es OGG Vorbis válido: "+path);
                path.clear();
            }
        };
        keepVoice(difficulty.audio.voicesPath);keepVoice(difficulty.audio.playerVoicesPath);keepVoice(difficulty.audio.opponentVoicesPath);
    }
    std::map<std::string,std::string> renamed;
    std::set<std::string> needs;
    std::set<std::string> placedStages;
    std::map<std::string,int> boundCharacters,boundStages;
    auto bind=[&](std::map<std::string,int>& bindings,const std::string& key,int index,size_t count,const char* kind) {
        if(index<0)return;
        if((size_t)index>=count) {p.errors.push_back(std::string("Recurso de ")+kind+" no disponible para la clave: "+key);return;}
        const auto normalized=lower(key);auto previous=bindings.find(normalized);
        if(previous!=bindings.end() && previous->second!=index)p.errors.push_back(std::string("Una misma clave de ")+kind+" tiene recursos diferentes: "+key);
        else bindings[normalized]=index;
    };
    for(const auto& d:selected) {
        const auto stageKey=!stageOverride.empty()?stageOverride:d.chart.stage.empty()?fml::psych::vanillaStage(song.id):d.chart.stage;
        bind(boundStages,stageKey,d.exportStage,catalog.stages.size(),"escenario");
        for(const auto& line:d.chart.strumLines)for(const auto& key:line.characters) {
            auto chosen=d.exportCharacters.find(key);
            if(chosen!=d.exportCharacters.end())bind(boundCharacters,key,chosen->second,catalog.characters.size(),"personaje");
        }
    }
    for(auto& d:selected) {
        auto safety=fml::chartSafetyError(d.chart);if(!safety.empty())p.errors.push_back(d.name+": "+safety);
        if(!stageOverride.empty()) d.chart.stage=stageOverride;
        if(d.chart.stage.empty()) d.chart.stage=fml::psych::vanillaStage(song.id);
        if(!fml::psych::relativeKey(d.chart.stage)) p.warnings.push_back(id+"/"+d.name+": la clave de escenario no es una ruta válida ("+d.chart.stage+"); la canción se jugará sin decorado.");
        else if((bringStages && boundStages.count(lower(d.chart.stage))) || (!available(p,base,"data/stages/"+d.chart.stage+".xml") && !supplied.count(lower("data/stages/"+d.chart.stage+".xml")))) {
            if(bringStages) {
                if(!placedStages.count(d.chart.stage)) {
                    std::vector<const Stage*> matches;
                    auto bound=boundStages.find(lower(d.chart.stage));
                    if(bound!=boundStages.end())matches.push_back(&catalog.stages[bound->second]);
                    else for(const auto& s:catalog.stages) if(lower(s.imported.stage.id)==lower(d.chart.stage)) matches.push_back(&s);
                    // A stage compiled into the source engine is rebuilt from the
                    // image keys in its executable. It still gives the song its
                    // backdrop (Day Out, Alone), unless it is so heavy that
                    // Codename runs out of memory loading it (Demise: 1.4 GB).
                    bool tooHeavy=false;
                    if(matches.size()==1 && reconstructedFromEngine(*matches.front())) {
                        Plan probe;stageFiles(probe,*matches.front(),d.chart.stage);
                        tooHeavy=decodedTextureBytes(probe)>768ull*1024ull*1024ull;
                    }
                    if(tooHeavy) {
                        p.warnings.push_back(d.chart.stage+": el escenario va compilado en el motor de origen y su reconstrucción pesa más de 768 MB de texturas; no se incluye para que Codename no se quede sin memoria. Expórtalo aparte desde Escenarios si quieres partir de ella.");
                        placedStages.insert(d.chart.stage);
                    } else if(matches.size()==1) {
                        // Isolated: a stage that cannot be exported (empty,
                        // Animate effects) leaves the song without backdrop,
                        // as Codename does with a missing stage.
                        Plan staged;stageFiles(staged,*matches.front(),d.chart.stage);
                        if(staged.errors.empty()) mergePlan(p,staged);
                        else p.warnings.push_back(d.chart.stage+": no se puede exportar el escenario ("+joined(staged.errors)+"); la canción se jugará sin decorado.");
                        placedStages.insert(d.chart.stage);
                    } else {
                        p.warnings.push_back(d.chart.stage+": "+(matches.empty()?std::string("el escenario no está en el mod ni en el destino"):"hay "+std::to_string(matches.size())+" escenarios con esa clave")+"; la canción se jugará sin decorado hasta que lo añadas.");
                        placedStages.insert(d.chart.stage);
                    }
                }
            } else if(fitToGame && !base.empty() && available(p,base,"data/stages/stage.xml")) {
                p.warnings.push_back(id+"/"+d.name+": el escenario "+d.chart.stage+" no está en el juego; el chart usa stage.");
                d.chart.stage="stage";
            } else p.warnings.push_back(id+"/"+d.name+": no se importará el escenario "+d.chart.stage+". Tendrás que modificar posteriormente la declaración stage del chart a un escenario existente en el destino.");
        }
        for(const auto& line:d.chart.strumLines) for(const auto& who:line.characters) if(!who.empty()) needs.insert(who);
    }
    // Characters the song's scripts make sing by note type (PlayerChars.lua):
    // brought like the chart's own, and placed and routed by a script.
    std::vector<std::array<std::string,4>> typeSingers;
    for(const auto& singer:scriptedTypeSingers(catalog,selected.front())) {
        const auto& who=catalog.characters[singer.character].imported.character.id;
        int line=-1; for(const auto& d:selected) { for(const auto& n:d.chart.notes) if(n.type.find(singer.type)!=std::string::npos) { line=n.strumLine; break; } if(line>=0) break; }
        if(line<0 || (size_t)line>=selected.front().chart.strumLines.size()) continue;
        const int type=selected.front().chart.strumLines[line].type;
        // Without characters in the bundle, one the game lacks would be a bf.
        if(!bringCharacters && !available(p,base,"data/characters/"+who+".xml")) continue;
        needs.insert(who);
        typeSingers.push_back({who,singer.type,std::to_string(line),type==1?"boyfriend":type==2?"girlfriend":"dad"});
    }
    if(!typeSingers.empty()) {
        addText(p,"songs/"+id+"/scripts/fml-psych-type-singers.hx",typeSingersScript(typeSingers));
        std::string names; for(const auto& t:typeSingers) names+=(names.empty()?"":", ")+t[0]+" ("+t[1]+")";
        p.warnings.push_back(id+": cantan por tipo de nota "+names+"; aparecen al lado de su línea, sin la coreografía del script original.");
    }
    std::set<std::string> playerSide;
    for(const auto& d:selected) for(const auto& line:d.chart.strumLines) if(line.type==1) for(const auto& who:line.characters) playerSide.insert(who);
    if(fitToGame && !base.empty()) {
        // What would be missing: not in the game, and not coming in the bundle.
        auto coming=[&](const std::string& who) {
            if(!bringCharacters || !validId(who)) return false;
            if(boundCharacters.count(lower(who)))return true;
            int n=0; for(const auto& c:catalog.characters) if(lower(c.imported.character.id)==lower(who)) ++n;
            return n>0;
        };
        std::map<std::string,std::string> swap;
        for(auto& d:selected) for(auto& line:d.chart.strumLines) for(auto& who:line.characters) {
            if(who.empty() || available(p,base,"data/characters/"+who+".xml") || supplied.count(lower("data/characters/"+who+".xml")) || coming(who)) continue;
            const std::string own=line.type==1?"bf":line.type==2?"gf":"dad";
            if(!available(p,base,"data/characters/"+own+".xml")) continue;
            if(swap.emplace(who,own).second) p.warnings.push_back(id+": el personaje "+who+" no está en el juego; el chart usa "+own+".");
            who=own;
        }
        if(!swap.empty()) {
            needs.clear();
            for(const auto& d:selected) for(const auto& line:d.chart.strumLines) for(const auto& who:line.characters) if(!who.empty()) needs.insert(who);
            for(const auto& t:typeSingers) needs.insert(t[0]);
        }
    }
    std::vector<std::string> offsetCharacters;
    for(const auto& who:needs) {
        // Psych and Codename load their fallback character for one they
        // cannot find (Character.hx:591): every case below is a notice.
        const std::string fallback="; Codename usará su personaje de reserva (bf) hasta que lo añadas.";
        if(!fml::psych::relativeKey(who)) { p.warnings.push_back("La clave de personaje "+who+" no es una ruta válida"+fallback); continue; }
        if(!(bringCharacters && boundCharacters.count(lower(who))) && (available(p,base,"data/characters/"+who+".xml") || supplied.count(lower("data/characters/"+who+".xml")))) continue;
        std::vector<const Character*> matches;
        auto bound=boundCharacters.find(lower(who));
        if(bound!=boundCharacters.end())matches.push_back(&catalog.characters[bound->second]);
        else for(const auto& c:catalog.characters) if(lower(c.imported.character.id)==lower(who)) matches.push_back(&c);
        if(matches.empty()) { p.warnings.push_back("El personaje "+who+" no está en el mod ni en el destino"+fallback); continue; }
        if(!bringCharacters) { p.warnings.push_back("El personaje "+who+" no se incluye en el paquete: el destino debe tenerlo"+fallback); continue; }
        if(!validId(who)) { p.warnings.push_back("El personaje "+who+" tiene una clave que Codename no admite como archivo"+fallback); continue; }
        if(matches.size()>1) p.warnings.push_back("Hay "+std::to_string(matches.size())+" definiciones de "+who+"; se usa "+matches.front()->source.u8string()+". Elige otra en Charts si no es la buena.");
        Plan character;characterFiles(character,*matches.front(),who,&catalog);
        if(!character.errors.empty()) { p.warnings.push_back("No se puede exportar el personaje "+who+" ("+joined(character.errors)+")"+fallback); continue; }
        mergePlan(p,character);renamed[who]=who;
        if(playerSide.count(who) && matches.front()->imported.character.position.x!=0)offsetCharacters.push_back(who);
    }
    if(!offsetCharacters.empty()) addText(p,"songs/"+id+"/scripts/fml-psych-player-offset.hx",playerOffsetScript(offsetCharacters));
    fml::NewSongRequest request; const auto& first=selected.front().chart;
    request.id=id; request.displayName=song.title; request.bpm=first.bpm;
    request.beatsPerMeasure=first.beatsPerMeasure; request.stepsPerBeat=first.stepsPerBeat;
    request.stage=first.stage; request.scrollSpeed=first.speed; request.strumLines=first.strumLines;
    // "song-" keeps a song and a character with the same id from writing the
    // same images/icons/<id>-import.png (Pixel Funk: song and character pixelman).
    request.icon=song.iconPath.empty()?"face":"song-"+id+"-import";
    if(!song.iconPath.empty()) iconFiles(p,iconSource(song.iconPath,&catalog),request.icon);
    fml::SongMeta meta; meta.name=id; meta.displayName=song.title; meta.bpm=first.bpm;
    meta.beatsPerMeasure=first.beatsPerMeasure; meta.stepsPerBeat=first.stepsPerBeat; meta.icon=request.icon; meta.needsVoices=false;
    bool actors=false;
    // Paths.inst/voices try <name>-<difficulty>.ogg and then <name>.ogg. The
    // file most difficulties share is written once without suffix; only a
    // difficulty with different audio gets its own (Psych 1.0's base songs
    // used to carry three identical copies of the same Inst).
    std::map<fml::AudioRole,std::string> shared;
    {
        // Ties go to "normal" (Flags.DEFAULT_DIFFICULTY), then to the first.
        std::map<fml::AudioRole,std::vector<std::pair<std::string,double>>> uses;
        auto use=[&](fml::AudioRole role,const std::string& path,const Difficulty& d) {
            if(path.empty())return; double weight=1+(lower(d.name)=="normal"?.5:0);
            auto& list=uses[role]; for(auto& entry:list)if(entry.first==path){entry.second+=weight;return;}
            list.push_back({path,weight});
        };
        for(const auto& d:selected) {
            use(fml::AudioRole::Instrumental,d.audio.instPath,d);use(fml::AudioRole::Voices,d.audio.voicesPath,d);
            use(fml::AudioRole::VoicesPlayer,d.audio.playerVoicesPath,d);use(fml::AudioRole::VoicesOpponent,d.audio.opponentVoicesPath,d);
        }
        for(const auto& role:uses) {
            double best=0; for(const auto& path:role.second) if(path.second>best) { best=path.second; shared[role.first]=path.first; }
        }
    }
    std::set<std::string> audioWritten;
    for(auto& d:selected) {
        p.events.push_back(eventReport(d));
        p.noteTypes.push_back(noteReport(catalog,d));
        int noteScript=0;
        for(const auto& type:noteSupport(catalog,d)) if(!type.script.path.empty()) addCopy(p,"psych-scripts/songs/"+id+"/note-types/"+d.name+"/"+std::to_string(++noteScript)+"-"+type.script.path.filename().u8string(),type.script.path);
        for(const auto& item:d.events.unresolved) p.warnings.push_back(id+"/"+d.name+": evento sin equivalente: "+item.first+" ("+std::to_string(item.second)+")");
        if(!validId(d.name)) { p.errors.push_back("Dificultad con nombre inválido: "+d.name); continue; }
        auto copyAudio=[&](const std::string& path,fml::AudioRole role) {
            if(path.empty()) return;
            auto probe=fml::probeAudioFile(fs::u8path(path));
            if(!probe.readable || probe.container!=fml::AudioContainer::OggVorbis) { p.warnings.push_back(id+"/"+d.name+": se omite una pista que no es OGG Vorbis válido: "+path); return; }
            auto name=fml::audioFileName(role,meta,shared[role]==path?std::string():d.name);
            if(audioWritten.insert(name).second) addCopy(p,"songs/"+id+"/song/"+name,fs::u8path(path));
        };
        copyAudio(d.audio.instPath,fml::AudioRole::Instrumental);
        copyAudio(d.audio.voicesPath,fml::AudioRole::Voices);
        copyAudio(d.audio.playerVoicesPath,fml::AudioRole::VoicesPlayer);
        copyAudio(d.audio.opponentVoicesPath,fml::AudioRole::VoicesOpponent);
        meta.needsVoices=meta.needsVoices || d.audio.hasVoices();
        d.chart.needsVoices=d.audio.hasVoices();
        for(auto& line:d.chart.strumLines) {
            if(line.type==1 && !d.audio.playerVoicesPath.empty()) line.vocalsSuffix="-Player";
            if(line.type==0 && !d.audio.opponentVoicesPath.empty()) line.vocalsSuffix="-Opponent";
        }
        meta.difficulties.push_back(d.name);
        // The chart's inline meta overrides meta.json (Chart.loadChart): it
        // must say whether voices were really exported, not what Psych said.
        auto exported=d;exported.chart.needsVoices=d.audio.hasVoices();
        addText(p,"songs/"+id+"/charts/"+d.name+".json",chartDocument(exported,request));
        auto metadata=d;metadata.chart.needsVoices=d.audio.hasVoices();
        auto exportedSong=song;for(auto& other:exportedSong.difficulties)other.take=std::any_of(selected.begin(),selected.end(),[&](const Difficulty& kept){return kept.path==other.path && kept.name==other.name;});
        addText(p,"songs/"+id+"/meta-"+d.name+".json",difficultyMetadata(exportedSong,metadata,id));
        addCopy(p,"psych-scripts/songs/"+id+"/source/"+d.name+".json",d.path);
        for(const auto& n:d.chart.notes) if(n.singerStrumLine>=0) actors=true;
        // Song and difficulty named: in a batch "easy: ..." alone says nothing.
        for(const auto& w:d.warnings) p.warnings.push_back(id+"/"+d.name+": "+w);
        if(!d.chart.noteTypes.empty()) p.warnings.push_back(id+"/"+d.name+": los tipos de nota custom requieren revisar su comportamiento.");
    }
    meta.difficultiesInferred=false;
    addText(p,"songs/"+id+"/meta.json",fml::serializeSongMeta(meta,{}));
    addText(p,"psych-scripts/songs/"+id+"/events-report.json",p.events.dump(2));
    addText(p,"psych-scripts/songs/"+id+"/note-types-report.json",p.noteTypes.dump(2));
    if(actors) addText(p,"songs/"+id+"/scripts/fml-psych-singers.hx",singerRoutingScript());
    bool builtInTypes=false;
    for(const auto& d:selected) for(const auto& n:d.chart.notes) if(psychBuiltInNoteType(n.type)) builtInTypes=true;
    if(builtInTypes) addText(p,"songs/"+id+"/scripts/fml-psych-notetypes.hx",psychNoteTypesScript());
    std::set<fs::path> scripts;
    for(const auto& d:selected) {
        std::error_code ec;
        for(const auto& entry:fs::directory_iterator(d.path.parent_path(),ec)) {
            auto ext=lower(entry.path().extension().u8string());
            if(entry.is_regular_file(ec) && (ext==".lua" || ext==".hx")) scripts.insert(entry.path());
        }
    }
    int scriptIndex=0;
    for(const auto& source:scripts) addCopy(p,"psych-scripts/songs/"+id+"/source/scripts/"+std::to_string(++scriptIndex)+"-"+source.filename().u8string(),source);
    if(!scripts.empty()) {
        p.warnings.push_back("Scripts originales archivados como referencia.");
        p.warnings.push_back(id+": pueden quedar acciones de scripts sin convertir; revisa el comportamiento en Codename.");
    }
    // The song's modchart (modchart.xml, its Lua replayed over the chart) as
    // a Codename HScript, with the shaders it puts on the cameras.
    if(modcharts) {
        const auto modchart=cachedSongModchart(catalog.files,selected.front());
        if(!modchart->empty()) {
            addText(p,"songs/"+id+"/scripts/psych-modchart.hx",codenameModchartScript(*modchart,song.title));
            std::set<std::string> effects;
            for(const auto& s:modchart->shaders) if(!s.frag.empty() && effects.insert(lower(s.effect)).second) {
                addCopy(p,"shaders/"+s.effect+".frag",s.frag); if(!s.vert.empty()) addCopy(p,"shaders/"+s.effect+".vert",s.vert);
            }
            p.warnings.push_back(id+": modchart convertido a Codename ("+modchartSummary(*modchart)+").");
            for(const auto& n:modchart->notes) p.warnings.push_back(id+": modchart: "+n);
        }
    }
    auto events=selected.front().path.parent_path()/"events.json";
    if(fml::psych::file(events)) addCopy(p,"psych-scripts/songs/"+id+"/source/events.json",events);
    // If a destination/base has an explicit Freeplay list, preserve it and add the new song.
    for(const auto& key:{"data/config/freeplaySonglist.txt","data/freeplaySonglist.txt"}) {
        fs::path source;
        if(fml::psych::file(destination/key)) source=destination/key;
        else if(!base.empty() && fml::psych::file(base/key)) source=base/key;
        if(source.empty()) continue;
        auto contents=read(source); if(!contents.empty() && contents.back()!='\n') contents+='\n'; contents+=id+"\n";
        // This explicit update is the only intentional overwrite; SafeWriter backs it up.
        addText(p,"data/config/freeplaySonglist.txt",contents); break;
    }
    // Codename keeps every texture of the stage and characters loaded at once;
    // Mario's Madness' Demise needed ~1.7 GB and ended in "Memory Exhausted!".
    if(auto bytes=decodedTextureBytes(p);bytes>1536ull*1024ull*1024ull)
        p.warnings.push_back(id+": sus texturas ocupan unos "+std::to_string(bytes/(1024ull*1024ull))+" MB decodificadas; Codename puede quedarse sin memoria al cargar la canción.");
    for(auto& file:p.files)if(file.path=="data/config/freeplaySonglist.txt")file.appendLines={id};
    destinationCheck(p,catalog);
    // A Freeplay list update is expected and backed up; do not block it as a collision.
    p.errors.erase(std::remove(p.errors.begin(),p.errors.end(),"Ya existe; usa otro ID: data/config/freeplaySonglist.txt"),p.errors.end());
    return p;
}
// A notice that changes what Codename shows or plays (a character falling
// back to bf, a song without its stage, a difficulty or file left out, an
// event that will not happen) as opposed to bookkeeping: scripts archived,
// stages rebuilt from their script, Animate adapted, events kept for a port.
inline bool importantNotice(const std::string& text) {
    if(text=="Scripts originales archivados como referencia." || text=="events.json was imported as global events" || text.rfind("Archivo de origen vacío, no se archiva: ",0)==0)return false;
    const auto separator=text.find(": ");const auto body=separator==std::string::npos?text:text.substr(separator+2);
    if(body=="Lua conservado sin activar." || body=="events.json was imported as global events")return false;
    if(body.rfind("modchart convertido a Codename (",0)==0 && body.size()>2 && body.compare(body.size()-2,2,").")==0)return false;
    if(body.rfind("este stage no trae `objects`: sus ",0)==0 && body.find("sprites salen de su script, con su orden y su scroll")!=std::string::npos)return false;
    return true;
}
// The plan as text: blocks, then what Codename will lack or do differently,
// then (when asked) the bookkeeping notes; events only where something was
// converted or lost. Capped: a review of thousands of files must stay
// responsive; the full lists go to the export manifest and the log.
inline std::string describe(const Plan& p,bool info=true) {
    std::string text=p.label+"\nDestino: "+p.destination.u8string()+"\n";
    constexpr size_t shown=1500;
    if(!p.errors.empty())text+="\nBloqueos ("+std::to_string(p.errors.size())+"): impiden generar este paquete.";
    for(size_t i=0;i<p.errors.size() && i<shown;++i)text+="\nBLOQUEADO: "+p.errors[i];
    if(p.errors.size()>shown)text+="\nBLOQUEADO: hay más problemas; revisa los recursos por separado.";
    std::vector<const std::string*> review,notes;
    for(const auto& w:p.warnings) (importantNotice(w)?review:notes).push_back(&w);
    if(!review.empty()) {
        text+="\n\nPuede afectar al juego ("+std::to_string(review.size())+"): cambios o problemas que revisar antes de exportar.";
        for(size_t i=0;i<review.size() && i<shown;++i) text+="\nAviso: "+*review[i];
        if(review.size()>shown) text+="\nAviso: ... y "+std::to_string(review.size()-shown)+" avisos más (lista completa en el manifiesto).";
    }
    if(!notes.empty() && info) {
        text+="\n\nInformativo ("+std::to_string(notes.size())+"): archivos de referencia y conversiones realizadas.";
        for(size_t i=0;i<notes.size() && i<shown;++i) text+="\nNota: "+*notes[i];
        if(notes.size()>shown) text+="\nNota: ... y "+std::to_string(notes.size()-shown)+" notas más.";
    } else if(!notes.empty()) text+="\n\nInformativo: "+std::to_string(notes.size())+" notas ocultas.";
    size_t eventSections=0;
    for(const auto& d:p.events) {
        if(d["unresolved"].empty() && (!info || d["converted"].empty())) continue;
        if(++eventSections>200) { text+="\n\nEventos: ... y más dificultades."; break; }
        text+="\n\nEventos: "+(d.contains("song")?d["song"].get<std::string>()+"/":std::string())+d["difficulty"].get<std::string>();
        if(info) for(const auto& e:d["converted"]) text+="\nConvertido: "+e["psych"].get<std::string>()+" → "+e["codename"].get<std::string>()+" ("+std::to_string(e["count"].get<int>())+")";
        for(const auto& e:d["unresolved"]) text+="\nSin equivalente: "+e["psych"].get<std::string>()+" ("+std::to_string(e["count"].get<int>())+") · "+e["hint"].get<std::string>();
    }
    text+="\n\nArchivos ("+std::to_string(p.files.size())+"):\n";
    for(size_t i=0;i<p.files.size() && i<shown;++i) text+=p.files[i].path+"\n";
    if(p.files.size()>shown) text+="... y "+std::to_string(p.files.size()-shown)+" archivos más.\n";
    return text;
}
inline std::string execute(const Plan& p,bool* completed=nullptr) {
    if(completed)*completed=false;
    if(!p.ready()) return describe(p);
    const auto started=std::chrono::steady_clock::now();
    logs::info("Exportación: "+p.label+" -> "+p.destination.u8string());
    auto stamp=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    fs::path root=p.destination;
    if(!p.modRoot && !p.label.empty() && !p.files.empty() &&
       std::all_of(p.files.begin(),p.files.end(),[&](const Output& f){return f.path.rfind(p.label+"/",0)==0;}))
        root=p.destination/fs::u8path(p.label);
    fml::SafeWriteTransaction transaction(root,stamp);
    fml::DiagnosticSink sink;
    json result={{"format","psych-importer-basic-2"},{"id",p.label},{"warnings",p.warnings},{"events",p.events},{"noteTypes",p.noteTypes},{"files",json::array()}};
    std::set<std::string> destinations;
    std::set<std::string> checkedImages;
    // Validate the whole batch before staging or changing any destination.
    for(const auto& file:p.files) {
        std::error_code ec;
        const auto target=p.destination/fs::u8path(file.path);
        const bool update=!file.appendLines.empty() && file.path=="data/config/freeplaySonglist.txt";
        if(!fml::psych::relativeKey(file.path) || !within(p.destination,target) ||
           !destinations.insert(lower(file.path)).second || (fs::exists(target,ec) && !update) || ec) {
            return "Importación cancelada antes de escribir: "+file.path+"\n"+describe(p);
        }
        if(!file.source.empty()) {
            if(!unchangedSource(file))return "Importación cancelada: la fuente cambió desde la revisión del plan: "+file.source.u8string();
            auto size=fs::file_size(file.source,ec);
            if(ec || size==0 || size>512ull*1024ull*1024ull ||
               (lower(fs::u8path(file.path).extension().u8string())==".png" && checkedImages.insert(lower(file.source.lexically_normal().generic_u8string())).second && !validPng(file.source,nullptr,nullptr,true)))
                return "Importación cancelada antes de escribir: "+file.source.u8string()+"\n"+describe(p);
        }
        if(!update)transaction.expectUnchanged(target,std::nullopt);
    }
    for(const auto& file:p.files) {
        const auto target=p.destination/fs::u8path(file.path);
        if(!file.source.empty()) {
            transaction.expectSourceUnchanged(file.source,file.sourceStamp->size,file.sourceStamp->modified);
            transaction.addCopy(target,file.source,"Psych Importer: "+p.label);
        }
        else {
            auto bytes=file.text;
            if(!file.appendLines.empty() && file.path=="data/config/freeplaySonglist.txt") {
                std::optional<std::string> previous;
                std::error_code ec;
                if(fs::exists(target,ec)) {
                    auto size=fs::file_size(target,ec);
                    if(ec || size>4u*1024u*1024u)return "Importación cancelada antes de escribir: "+file.path;
                    std::ifstream in(target,std::ios::binary);if(!in)return "Importación cancelada antes de escribir: "+file.path;
                    bytes.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
                    if(in.bad())return "Importación cancelada antes de escribir: "+file.path;
                    previous=bytes;
                }
                transaction.expectUnchanged(target,std::move(previous));
                for(const auto& song:file.appendLines) {
                    bool present=false;std::istringstream lines(bytes);std::string line;
                    while(std::getline(lines,line)){if(!line.empty() && line.back()=='\r')line.pop_back();if(line==song || line.rfind(song+":",0)==0)present=true;}
                    if(!present){if(!bytes.empty() && bytes.back()!='\n')bytes+='\n';bytes+=song+"\n";}
                }
            }
            transaction.add(target,std::move(bytes),"Psych Importer: "+p.label);
        }
        result["files"].push_back({{"path",file.path},{"source",file.source.u8string()},{"status","written"}});
    }
    result["status"]="complete";
    transaction.add(root/".psych-importer"/fs::u8path(stamp+".json"),result.dump(2),"Psych Importer manifest");
    bool ok=transaction.commit(sink);
    if(completed)*completed=ok;
    logs::write(ok?"INFO":"WARN",std::string(ok?"Exportación completa: ":"Exportación cancelada: ")+p.label+" · "+std::to_string(logs::since(started))+" ms");
    for(const auto& diagnostic:sink.all())logs::warn("Escritura: "+diagnostic.message);
    std::string report=std::string(ok?"Importación básica completada":"Importación cancelada")+": "+std::to_string(ok?p.files.size():0)+" archivos.\n"+describe(p);
    for(const auto& d:sink.all()) report+="\n"+d.message;
    return report;
}
}
