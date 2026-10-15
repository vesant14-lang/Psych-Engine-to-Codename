#pragma once
// The resource manager's data: every file of the loaded mod, what uses it,
// and what each song, character and stage still lacks before export. No UI
// here, so the checks can exercise it.
#include "Batch.hpp"
#include "../../src/fml_io/AudioImport.hpp"
#include <atomic>
#include <cstdint>

namespace pi {
enum class ResourceKind { Audio, Image, Atlas, Chart, Script };
// Sounds split by use the way Note Lab does (soundUseOf, the same in the three
// engines): song audio (Inst and Voices, under songs/), menu music (music/)
// and effects (sounds/ and the rest). Images by what uses them.
enum class AudioUse { Song, Music, Effect };
enum class ImageUse { Character, Stage, Icon, Other };
// Where a resource is used; `tab` is the app's tab (0 characters, 1 songs,
// 2 stages, 3 icons) and `index` the entry in it, -1 when not navigable.
// `role` is the ImageUse of an image use.
struct ResourceUse { std::string label; int tab=-1, index=-1, difficulty=-1, role=-1; };
struct ResourceItem {
    fs::path path;
    ResourceKind kind=ResourceKind::Image;
    std::string name,folder;          // file name and the folder it sits in
    std::vector<ResourceUse> uses;
    std::vector<std::string> problems; // known without opening the file
    int use=-1;                       // AudioUse or ImageUse
    std::string group;                // song an audio file belongs to
    std::string relative;             // path inside the mod
};
struct ResourceIndex {
    std::vector<ResourceItem> items;
    std::map<std::string,size_t> byPath;
    size_t count(ResourceKind kind) const { size_t n=0; for(const auto& i:items) if(i.kind==kind) ++n; return n; }
};

inline std::string resourceKey(const fs::path& p) { return lower(p.lexically_normal().generic_u8string()); }
inline std::vector<std::string> resourceSegments(const std::string& path) {
    std::vector<std::string> parts; std::string part;
    for(char c:path) { if(c=='/' || c=='\\') { if(!part.empty()) parts.push_back(part); part.clear(); } else part+=c; }
    if(!part.empty()) parts.push_back(part);
    return parts;
}
// Inst.ogg, Voices.ogg, Voices-bf.ogg, Inst-erect.ogg, song-Inst.ogg...
inline bool songAudioName(const std::string& file) {
    auto name=lower(file);
    return name.rfind("inst",0)==0 || name.rfind("voices",0)==0 || name.find("-inst")!=std::string::npos ||
           name.find("-voices")!=std::string::npos || name.find("_inst")!=std::string::npos || name.find("_voices")!=std::string::npos;
}
// `relative` is the path inside the mod; `song` receives the song folder.
inline AudioUse audioUseOf(const std::string& relative,std::string* song=nullptr) {
    auto parts=resourceSegments(relative);
    for(size_t i=0;i+1<parts.size();++i) {
        auto folder=lower(parts[i]);
        if(folder=="songs") { if(song && i+2<parts.size()) *song=parts[i+1]; return AudioUse::Song; }
        if(folder=="music") return AudioUse::Music;
    }
    if(!parts.empty() && songAudioName(parts.back())) { if(song && parts.size()>=2) *song=parts[parts.size()-2]; return AudioUse::Song; }
    return AudioUse::Effect;
}
// An image nothing uses, by its folder: characters/, icons/, stages/.
inline ImageUse imageUseOf(const std::string& relative) {
    auto parts=resourceSegments(relative);
    for(size_t i=0;i+1<parts.size();++i) {
        auto folder=lower(parts[i]);
        if(folder=="characters") return ImageUse::Character;
        if(folder=="icons") return ImageUse::Icon;
        if(folder=="stages") return ImageUse::Stage;
    }
    return ImageUse::Other;
}

inline ResourceIndex buildResourceIndex(const Catalog& c) {
    ResourceIndex index;
    auto kindOf=[](const fs::path& p,ResourceKind& out) {
        auto ext=lower(p.extension().u8string());
        if(ext==".ogg" || ext==".mp3" || ext==".wav" || ext==".flac") { out=ResourceKind::Audio; return true; }
        if(ext==".png" || ext==".jpg" || ext==".jpeg") { out=ResourceKind::Image; return true; }
        if(ext==".xml" || ext==".txt") { out=ResourceKind::Atlas; return true; }
        if(ext==".lua" || ext==".hx" || ext==".hscript") { out=ResourceKind::Script; return true; }
        return false;
    };
    auto add=[&](const fs::path& p,ResourceKind kind)->ResourceItem* {
        if(p.empty()) return nullptr;
        auto key=resourceKey(p);
        auto found=index.byPath.find(key);
        if(found!=index.byPath.end()) return &index.items[found->second];
        ResourceItem item; item.path=p; item.kind=kind; item.name=p.filename().u8string(); item.folder=p.parent_path().filename().u8string();
        index.byPath[key]=index.items.size(); index.items.push_back(std::move(item));
        return &index.items.back();
    };
    for(const auto& p:c.files.paths) {
        ResourceKind kind; if(!kindOf(p,kind)) continue;
        // A .txt is only an atlas when an image of the same name sits beside it.
        if(lower(p.extension().u8string())==".txt") { auto png=p; png.replace_extension(".png"); if(!fml::psych::file(png)) continue; }
        add(p,kind);
    }
    auto use=[&](const fs::path& p,ResourceKind kind,ResourceUse u) { if(auto* item=add(p,kind)) item->uses.push_back(std::move(u)); };
    for(int s=0;s<(int)c.songs.size();++s) {
        const auto& song=c.songs[s];
        for(int d=0;d<(int)song.difficulties.size();++d) {
            const auto& diff=song.difficulties[d];
            const std::string where=song.title+" · "+diff.name;
            use(diff.path,ResourceKind::Chart,{where+" · chart",1,s,d});
            use(fs::u8path(diff.audio.instPath),ResourceKind::Audio,{where+" · Inst",1,s,d});
            use(fs::u8path(diff.audio.voicesPath),ResourceKind::Audio,{where+" · Voices",1,s,d});
            use(fs::u8path(diff.audio.playerVoicesPath),ResourceKind::Audio,{where+" · Voices-Player",1,s,d});
            use(fs::u8path(diff.audio.opponentVoicesPath),ResourceKind::Audio,{where+" · Voices-Opponent",1,s,d});
        }
        use(song.iconPath,ResourceKind::Image,{song.title+" · icono de Freeplay",1,s,-1,(int)ImageUse::Icon});
    }
    for(int i=0;i<(int)c.characters.size();++i) {
        const auto& ch=c.characters[i]; const auto& id=ch.imported.character.id;
        use(ch.image,ResourceKind::Image,{id+" · hoja",0,i,-1,(int)ImageUse::Character});
        use(ch.atlas,ch.animate?ResourceKind::Chart:ResourceKind::Atlas,{id+" · atlas",0,i});
        for(const auto& extra:ch.extraSheets) { use(extra.first,ResourceKind::Image,{id+" · hoja adicional",0,i,-1,(int)ImageUse::Character}); use(extra.second,ResourceKind::Atlas,{id+" · atlas adicional",0,i}); }
        use(ch.iconPath,ResourceKind::Image,{id+" · icono",0,i,-1,(int)ImageUse::Icon});
        use(ch.source,ResourceKind::Chart,{id+" · definición",0,i});
    }
    for(int i=0;i<(int)c.stages.size();++i) {
        const auto& st=c.stages[i]; const auto& id=st.imported.stage.id;
        for(const auto& o:st.imported.stage.objects) {
            if(o.kind!=fml::StageObject::Kind::Sprite) continue;
            use(fs::u8path(o.resolvedImage),ResourceKind::Image,{id+" · "+o.name,2,i,-1,(int)ImageUse::Stage});
            use(fs::u8path(o.resolvedAtlas),ResourceKind::Atlas,{id+" · "+o.name+" (atlas)",2,i});
        }
        use(st.source,ResourceKind::Script,{id+" · definición",2,i});
    }
    for(int i=0;i<(int)c.icons.size();++i) use(c.icons[i].source,ResourceKind::Image,{c.icons[i].id+" · icono",3,i,-1,(int)ImageUse::Icon});
    // What is visible from the name alone; contents are checked on selection.
    for(auto& item:index.items) {
        // Classified by the path inside the mod, never by folders above it.
        std::string relative=item.path.filename().u8string();
        for(const auto& root:c.files.roots) if(within(root,item.path)) { relative=item.path.lexically_relative(root).generic_u8string(); break; }
        item.relative=relative;
        if(item.kind==ResourceKind::Audio) {
            auto use=audioUseOf(relative,&item.group);
            for(const auto& u:item.uses) if(u.tab==1 && u.index>=0 && (size_t)u.index<c.songs.size()) { use=AudioUse::Song; item.group=c.songs[u.index].title; break; }
            item.use=(int)use;
        }
        if(item.kind==ResourceKind::Image) {
            int role=-1; for(const auto& u:item.uses) if(u.role>=0 && (role<0 || u.role<role)) role=u.role;
            item.use=role>=0?role:(int)imageUseOf(relative);
        }
        if(item.kind==ResourceKind::Audio) {
            auto ext=lower(item.path.extension().u8string());
            if(ext!=".ogg") item.problems.push_back("Codename necesita OGG Vorbis; este archivo es "+ext.substr(1)+".");
        }
        if(!fml::psych::file(item.path)) item.problems.push_back("El archivo no existe en el disco.");
    }
    std::stable_sort(index.items.begin(),index.items.end(),[](const ResourceItem& a,const ResourceItem& b){
        if(a.kind!=b.kind) return (int)a.kind<(int)b.kind;
        if(a.uses.empty()!=b.uses.empty()) return !a.uses.empty();
        return lower(a.path.generic_u8string())<lower(b.path.generic_u8string());
    });
    index.byPath.clear(); for(size_t i=0;i<index.items.size();++i) index.byPath[resourceKey(index.items[i].path)]=i;
    return index;
}

// --- What each resource still lacks before export ----------------------------
// The same plans the export uses, against a destination that does not exist
// yet: destination-only complaints are dropped, the rest is what the user
// would read after pressing export.
struct Readiness {
    int tab=-1,index=-1;              // 0 character, 1 song, 2 stage
    std::string name;
    std::vector<std::string> errors,warnings;
    size_t files=0;
    unsigned long long textureBytes=0;
};
// importantNotice (Import.hpp) separates what changes the game from notes.
inline int readinessLevel(const Readiness& r) {
    if(!r.errors.empty()) return 2;
    for(const auto& w:r.warnings) if(importantNotice(w)) return 1;
    return 0;
}
// Loose resources a bundle can carry besides songs, characters and stages:
// menu music, sound effects and images nothing else exports (menus, UI).
// Song audio goes with its song; charts and scripts are not resources here.
inline bool extraResource(const ResourceItem& item) {
    // Only what has a place in a Codename mod: under images/, music/ or sounds/.
    bool placed=false;
    for(const auto& part:resourceSegments(item.relative)) { auto folder=lower(part); if(folder=="images" || folder=="music" || folder=="sounds") placed=true; }
    if(!placed || !selectableLooseResource(item.path)) return false;
    if(item.kind==ResourceKind::Audio) return item.use!=(int)AudioUse::Song;
    return item.kind==ResourceKind::Image;
}
inline void retainMarkableResources(const ResourceIndex& index,std::set<std::string>& selected) {
    std::set<std::string> allowed;
    for(const auto& item:index.items)if(extraResource(item))allowed.insert(resourceKey(item.path));
    for(auto it=selected.begin();it!=selected.end();)if(!allowed.count(resourceKey(fs::u8path(*it))))it=selected.erase(it);else ++it;
}
// Extra resources the user can explicitly select: music, effects and images no
// character, stage or icon already exports.
inline std::vector<std::string> wholeModExtras(const ResourceIndex& index) {
    std::vector<std::string> out;
    for(const auto& item:index.items) if(extraResource(item) && (item.kind==ResourceKind::Audio || (item.uses.empty() && item.use==(int)ImageUse::Other)))
        out.push_back(item.path.generic_u8string());
    return out;
}
inline std::vector<std::string> withoutDestinationNoise(std::vector<std::string> list) {
    list.erase(std::remove_if(list.begin(),list.end(),[](const std::string& e){
        return e.rfind("Ya existe",0)==0 || e=="Elige una carpeta de destino existente." || e.rfind("Ya existe esa canción",0)==0 ||
               e.rfind("El destino debe estar separado",0)==0;
    }),list.end());
    std::sort(list.begin(),list.end()); list.erase(std::unique(list.begin(),list.end()),list.end());
    return list;
}
inline std::vector<Readiness> exportReadiness(const Catalog& c,const fs::path& base,const std::atomic<bool>* cancel=nullptr) {
    std::vector<Readiness> out;
    const auto projected=fs::temp_directory_path()/"psych-importer-readiness-not-written";
    auto finish=[&](Readiness r,const Plan& p) {
        r.errors=withoutDestinationNoise(p.errors); r.warnings=withoutDestinationNoise(p.warnings);
        r.files=p.files.size(); r.textureBytes=decodedTextureBytes(p);
        out.push_back(std::move(r));
    };
    for(int i=0;i<(int)c.songs.size();++i) {
        if(cancel && *cancel) return out;
        Readiness r; r.tab=1; r.index=i; r.name=c.songs[i].title;
        finish(r,songPlan(c,c.songs[i],projected,base,c.songs[i].id,"",true,true));
    }
    for(int i=0;i<(int)c.characters.size();++i) {
        if(cancel && *cancel) return out;
        Readiness r; r.tab=0; r.index=i; r.name=c.characters[i].imported.character.id;
        finish(r,characterPlan(c,c.characters[i],projected,c.characters[i].imported.character.id));
    }
    for(int i=0;i<(int)c.stages.size();++i) {
        if(cancel && *cancel) return out;
        Readiness r; r.tab=2; r.index=i; r.name=c.stages[i].imported.stage.id;
        finish(r,stagePlan(c,c.stages[i],projected,c.stages[i].imported.stage.id));
    }
    return out;
}
}
