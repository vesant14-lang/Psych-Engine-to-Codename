#pragma once
#include "Catalog.hpp"
#include "../../src/fml_runtime/Gameplay.hpp"
#include "../../src/fml_formats/HealthIconFrames.hpp"
#include "../../src/fml_notelab/NoteImage.hpp"
#include "../../src/fml_notelab/NoteGameplay.hpp"
#include "../../third_party/stb_image.h"

namespace pi {
// Resolve presentation assets only. The native FML renderer owns geometry,
// trimming, rotated frames, sustain pieces, health and rating layout.
struct PreviewHud {
    fml::NoteSkin notes;
    fml::HudImage healthBar,timeBar,combo;
    std::map<std::string,fml::HudImage> ratings;
    std::array<fml::HudImage,10> digits;
    fs::path font;
    std::string declaredSkin;
    std::string selectedStyle;
    std::shared_ptr<fml::notelab::GameplayNotes> preparedNotes;
    bool notesValid=false;
    std::shared_ptr<fml::notelab::Image> paletteSheet;
    std::vector<std::string> diagnostics;
    bool setStyle(const fml::notelab::NoteStyle& style) {
        auto prepared=std::make_shared<fml::notelab::GameplayNotes>(fml::notelab::prepareGameplayNotes(style,[](const std::string& path){
            std::error_code ec;auto source=fs::u8path(path);if(fs::file_size(source,ec)>64u*1024u*1024u || ec)return std::vector<unsigned char>{};
            auto bytes=read(source);return std::vector<unsigned char>(bytes.begin(),bytes.end());
        }));
        if(!prepared->ok){diagnostics.insert(diagnostics.end(),prepared->diagnostics.begin(),prepared->diagnostics.end());return false;}
        notes=prepared->skin;notesValid=true;selectedStyle=style.id;paletteSheet.reset();preparedNotes=std::move(prepared);return true;
    }

    static fml::HudImage image(const fs::path& path) {
        fml::HudImage out; std::error_code ec;
        if(path.empty() || fs::file_size(path,ec)>64u*1024u*1024u || ec)return out;
        auto bytes=read(path);int w=0,h=0,n=0;
        if(!stbi_info_from_memory((const stbi_uc*)bytes.data(),(int)bytes.size(),&w,&h,&n) || w<=0 || h<=0 || (uint64_t)w*h>64u*1024u*1024u)return out;
        out.image=path.u8string();out.width=(float)w;out.height=(float)h;return out;
    }
    void setNotes(const fs::path& png,const fs::path& xml) {
        notes={};notesValid=false;paletteSheet.reset();preparedNotes.reset();selectedStyle.clear();
        auto info=image(png);if(info.image.empty() || xml.empty())return;
        std::error_code ec;if(fs::file_size(xml,ec)>16u*1024u*1024u || ec)return;
        fml::AtlasStore atlases;atlases.readText=[](const std::string& p){return read(fs::u8path(p));};
        const auto* atlas=atlases.get(xml.u8string());if(!atlas)return;
        // Reject UVs outside the source sheet instead of silently sampling
        // another sprite. Partial/unknown layouts use the generic renderer.
        for(const auto& f:atlas->frames)if(!fml::frameInsideSheet(f,(int)info.width,(int)info.height))return;
        notes.image=png.u8string();notes.atlas=xml.u8string();
        for(int i=0;i<4;++i) {
            auto color=std::string(fml::noteColorName(i));
            // Psych's scroll prefix excludes the similarly named hold frames.
            if(!atlas->framesFor(color+"0").empty())notes.scrollPrefix[i]=color+"0";
        }
        auto metrics=fml::noteSkinMetricsOf(atlases,notes);notesValid=metrics.ok;
        for(int i=0;i<4;++i)notesValid=notesValid && metrics.haveNote[i] && metrics.haveHold[i] && metrics.haveHoldEnd[i];
        if(!notesValid)diagnostics.push_back("Note atlas has missing receptor, head or sustain frames.");
        // The same 0.7+ noteSkins convention used by FML NoteStyleRead.
        // Keep static receptors intact and bake only RGB-controlled frames.
        auto location=lower(png.generic_u8string());
        if(notesValid && location.find("/noteskins/")!=std::string::npos) {
            if(auto cached=paletteCache().find(location);cached!=paletteCache().end()){paletteSheet=cached->second;if(!paletteSheet)notesValid=false;return;}
            auto raw=read(png);auto sheet=std::make_shared<fml::notelab::Image>();
            if((uint64_t)info.width*info.height<=16u*1024u*1024u && fml::notelab::decodePng(std::vector<unsigned char>(raw.begin(),raw.end()),*sheet)) {
                for(const auto& frame:atlas->frames)for(int lane=0;lane<4;++lane) {
                    const auto color=std::string(fml::noteColorName(lane)),direction=std::string(fml::strumAnimPrefix(lane));
                    const auto name=lower(frame.name);
                    if(name.rfind(color,0)==0 || (lane==0 && name.rfind("pruple",0)==0) || name.rfind(direction+" press",0)==0 || name.rfind(direction+" confirm",0)==0) {
                        auto crop=fml::notelab::cropRect(*sheet,frame.x,frame.y,frame.w,frame.h);
                        fml::notelab::applyPsychPalette(crop,fml::notelab::psychDefaultPalette(lane,false));fml::notelab::blit(*sheet,crop,frame.x,frame.y);break;
                    }
                }
                paletteSheet=std::move(sheet);
            }else notesValid=false;
            paletteCache()[location]=paletteSheet;
        }
    }
    // Same catalog, chart folder and arrowSkin give the same HUD: switching
    // songs reused to re-parse the whole chart and rebuild the RGB note sheet
    // every time (90-130 ms per switch with Whitty and Wii Funkin').
    static std::map<std::string,PreviewHud>& resolvedCache() { static std::map<std::string,PreviewHud> cache; return cache; }
    static std::map<std::string,std::shared_ptr<fml::notelab::Image>>& paletteCache() { static std::map<std::string,std::shared_ptr<fml::notelab::Image>> cache; return cache; }
    static void clearCaches() { resolvedCache().clear(); paletteCache().clear(); }
    void resolve(const Catalog& c,const fs::path& chartPath={}) {
        std::string skin;bool known=false;
        for(const auto& s:c.songs)for(const auto& d:s.difficulties)if(!chartPath.empty() && d.path==chartPath){skin=d.arrowSkin;known=true;}
        if(!known && !chartPath.empty()) {
            auto root=document(chartPath);
            const auto& song=root.is_object() && root.contains("song") && root["song"].is_object()?root["song"]:root;
            skin=field(song,"arrowSkin");
        }
        const auto key=c.origin+"|"+std::to_string(c.files.paths.size())+"|"+chartPath.parent_path().u8string()+"|"+skin;
        if(auto hit=resolvedCache().find(key);hit!=resolvedCache().end()){*this=hit->second;return;}
        resolveUncached(c,chartPath,skin);
        if(resolvedCache().size()>256)resolvedCache().clear();
        resolvedCache()[key]=*this;
    }
    void resolveUncached(const Catalog& c,const fs::path& chartPath,const std::string& skin) {
        *this={};
        declaredSkin=skin;
        std::vector<std::string> keys;
        if(!declaredSkin.empty())keys={declaredSkin};
        else keys={"noteSkins/NOTE_assets","NOTE_assets","game/notes/default"};
        for(const auto& key:keys) {
            if(!fml::psych::relativeKey(key)){diagnostics.push_back("Invalid arrowSkin path.");break;}
            auto xml=c.files.find({"images/"+key+".xml",key+".xml"},chartPath.parent_path());
            if(!xml.alternatives.empty()){diagnostics.push_back("More than one note atlas matches: "+key);break;}
            if(xml.path.empty())continue;
            // PNG/XML must come from the same sheet, not two basename matches.
            auto png=xml.path;png.replace_extension(".png");
            if(!fml::psych::file(png)) {
                fml::DiagnosticSink sink;auto atlas=fml::parseSparrowAtlas(read(xml.path),xml.path.u8string(),sink);
                if(atlas && fml::psych::relativeKey(atlas.value().imagePath))png=xml.path.parent_path()/fs::u8path(atlas.value().imagePath);
            }
            setNotes(png,xml.path);break;
        }
        auto art=[&](const std::string& key){
            auto exact=c.files.find({"images/"+key+".png"},chartPath.parent_path(),false);
            auto nearby=image(exact.path.empty()?c.files.find({"images/"+key+".png",key+".png"},chartPath.parent_path()).path:exact.path);
            auto name=fs::u8path(key).filename().u8string();
            if((name=="healthBar" || name=="timeBar") && !nearby.image.empty() && (nearby.height>48 || nearby.width>1280 || nearby.width<nearby.height*6)) {
                diagnostics.push_back("Bar artwork needs explicit geometry; generic bar retained: "+nearby.image);return fml::HudImage{};
            }
            return nearby;
        };
        healthBar=art("healthBar");if(healthBar.image.empty())healthBar=art("game/healthBar");
        timeBar=art("timeBar");combo=art("combo");
        for(const auto* name:{"sick","good","bad","shit"})ratings[name]=art(name);
        for(int i=0;i<10;++i)digits[i]=art("num"+std::to_string(i));
        font=c.files.find({"fonts/vcr.ttf","vcr.ttf"},chartPath.parent_path()).path;
    }
    static void bindIcon(const Catalog& c,int index,bool player,fml::HealthHud& hud) {
        if(index<0 || (size_t)index>=c.characters.size())return;
        const auto& character=c.characters[index];auto info=image(character.iconPath);if(info.image.empty())return;
        auto atlas=character.iconPath;atlas.replace_extension(".xml");if(!fml::psych::file(atlas))atlas.replace_extension(".txt");
        auto data=character.iconPath.parent_path()/"data.xml";
        if(!fml::psych::file(data))data=c.files.find({"data/icons/"+character.imported.character.icon+".xml"},character.source.parent_path(),false).path;
        for(const auto& icon:c.icons)if(icon.source==character.iconPath){atlas=icon.atlas;data=icon.data;break;}
        fml::AtlasStore store;store.readText=[](const std::string& path){return read(fs::u8path(path));};
        auto frames=fml::healthIconFrames(atlas.empty()?nullptr:store.get(atlas.u8string()),(int)info.width,(int)info.height,read(data));
        uint32_t color=player?0xFF66FF33u:0xFFFF0000u;
        auto hex=character.imported.character.healthColor;if(!hex.empty() && hex[0]=='#')try{color=0xFF000000u|(uint32_t)std::stoul(hex.substr(1),nullptr,16);}catch(...){}
        if(player){hud.playerIcon=info.image;hud.playerIconW=info.width;hud.playerIconH=info.height;hud.playerFrames=frames;hud.playerColor=color;}
        else{hud.opponentIcon=info.image;hud.opponentIconW=info.width;hud.opponentIconH=info.height;hud.opponentFrames=frames;hud.opponentColor=color;}
    }
};
}
