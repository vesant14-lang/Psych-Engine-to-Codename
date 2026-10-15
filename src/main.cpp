#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "Version.hpp"
#include "Import.hpp"
#include "Batch.hpp"
#include "ResourceIndex.hpp"
#include "Playback.hpp"
#include "I18n.hpp"
#include "Loose.hpp"
#include "UiLayout.hpp"
#include "MiniChart.hpp"
#include "Gameplay.hpp"
#include "PreviewView.hpp"
#include "Profile.hpp"
#include "../../src/fml_formats/HealthIconFrames.hpp"
#include "../../src/fml_formats/FontValidation.hpp"
#include "../../src/fml_audio/AudioEngine.hpp"
#include "../../src/fml_app/PreviewPlacement.hpp"
#include "../../src/fml_render/GlRenderer.hpp"
#include "../../src/fml_runtime/PreviewFraming.hpp"
#include "../../third_party/imgui/imgui.h"
#include "../../third_party/imgui/imgui_internal.h"
#include "../../third_party/imgui/imgui_impl_sdl3.h"
#include "../../third_party/imgui/imgui_impl_opengl3.h"
#include "../../third_party/stb_image.h"
#include "../../third_party/stb_image_write.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <future>
#include <sstream>
#include <mutex>
#include <memory>
#include <cstdio>
#include <cmath>
#include <array>
#include <windows.h>
#include <shellapi.h>
#include <psapi.h>
#pragma comment(lib,"psapi.lib")

using namespace pi;
namespace {
struct Texture { GLuint id=0; int w=0,h=0; std::string error; };
struct Textures : fml::IImageInfo {
    fml::GlRenderer renderer;
    std::map<std::string,Texture> cache;
    // The viewers are at most a window wide: sheets above 4096 px are kept at
    // half size or less (Whitty Erect's 5894x3254 stage sheets went from 77 to
    // 19 MB each). Decoding in the background keeps a 6000 px PNG from
    // freezing the window for a second. Captures and checks stay synchronous.
    int sizeCap=4096; bool async=false; size_t vramBudget=0;
    bool init(std::string& error) {
        if(!renderer.init([](const std::string& path){return path;},&error))return false;
        renderer.setTextureSizeCap(sizeCap); if(vramBudget)renderer.setVramBudget(vramBudget); if(async)renderer.setAsyncTextureDecode(true);
        return true;
    }
    Texture& get(const fs::path& path) {
        const auto key=path.u8string();auto& t=cache[key];
        auto native=renderer.previewImage(key);t.id=native.texture;t.w=native.width;t.h=native.height;
        t.error=native.ok?std::string():"Imagen no encontrada o no legible";
        return t;
    }
    void clear() { renderer.shutdown();cache.clear();std::string error;init(error); }
    Texture& memory(const std::string& key,const fml::notelab::Image& pixels) {
        if(!renderer.dynamicFrameReady(key) && !pixels.empty()) {
            auto bgra=pixels.rgba;
            for(size_t i=0;i+3<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
            renderer.uploadDynamicFrame(key,bgra.data(),pixels.w,pixels.h);
        }
        return get(fs::u8path(key));
    }
    fml::ImageInfo imageInfo(const std::string& path) override { return renderer.imageInfo(path); }
};
enum class Action { Mod,Files,Destination,Base,Image,Atlas,CharacterIcon,SongIcon,Inst,Voices,PlayerVoices,OpponentVoices,StageImage,StageAtlas,PreviewNoteAtlas };
struct Request { Action action; int character=-1,song=-1,difficulty=-1,stage=-1,object=-1; };
struct DialogResult { Request request; std::vector<fs::path> paths; std::string error; };
std::mutex dialogMutex;
std::unique_ptr<DialogResult> dialogResult;
void selected(void* userdata,const char* const* list,int) {
    std::unique_ptr<Request> req((Request*)userdata); auto result=std::make_unique<DialogResult>(); result->request=*req;
    if(!list) result->error=SDL_GetError();
    else for(int i=0;list[i];++i) result->paths.push_back(fs::u8path(list[i]));
    std::lock_guard<std::mutex> lock(dialogMutex); dialogResult=std::move(result);
}
// How the camera frame is drawn; it has to stand out on light and dark stages
// alike. Shared by Preview and Stages and kept between sessions.
struct CameraFrameStyle { ImVec4 color{1,1,1,.6f}; float thickness=2,dim=0; bool corners=true; };
struct App {
    Catalog catalog;
    Textures textures;
    fml::AudioEngine audio;
    fml::AudioEngine gameAudio;
    // Resources has its own player: browsing files never cuts the song.
    fml::AudioEngine auditionAudio;
    bool auditionLoop=false,audioWasPlaying[3]{};
    std::string audioKey;                 // tracks app.audio holds
    CameraFrameStyle cameraFrame; bool cameraFrameDirty=false; fs::path cameraFramePath;
    Gameplay game;
    std::array<int,3> stageActors{{-1,-1,-1}};
    fs::path destination,base,languagePath;
    fml::AtlasStore atlases;
    std::set<int> previewSongsSeen;
    fml::StageAnimator stageAnimator;
    std::future<Catalog> loading;
    std::future<std::string> writing;
    bool busy=false,writeBusy=false,dialogBusy=false,playingPreview=true,bringCharacters=true,bringStages=true;
    bool loopPreview=true,viewFlipX=false,viewFlipY=false,showActors=false,autoFrameStage=true,audioLoop=false,zipPlan=false;
    bool chartCharacters=true,chartStages=true;
    bool previewPlayAfterLoad=false;
    // Playback handed between Songs/Charts (app.audio) and Preview
    // (gameAudio): where to resume once the other player's tracks are ready.
    double previewStartMs=0,audioStartMs=0; bool audioPlayAfterLoad=false;
    bool showCameraFrame=true;
    fml::ShaderLibrary modShaders;     // modchart camera shaders for the preview
    BatchDependencies deps;
    std::map<std::string,ImFont*> hudFonts;
    std::map<std::string,fml::HealthIconFrames> iconModels;
    int tab=0,character=-1,song=-1,difficulty=0,animation=0,stageIndex=-1,stageObject=0,icon=-1;
    float zoom=1,clock=0,previewRate=1,panX=0,panY=0,iconHealth=.5f,stageDuration=10,stageBpm=120;
    BatchOptions batch;
    std::array<char,160> bundleName{};
    ImVec2 languageCenters[2]{},tabCenters[7]{};
    ImVec2 viewCanvasCenters[7]{},viewCenterButtons[7]{};
    // Resource manager (tab 6).
    ResourceIndex resources; bool resourcesDirty=true; int resourceFilter=0,resource=-1,resourceAudioUse=(int)AudioUse::Song,resourceImageUse=-1;
    std::array<char,160> resourceSearch{}; std::vector<size_t> resourceFiltered;
    std::vector<Readiness> readiness; std::future<std::vector<Readiness>> readinessJob;
    std::shared_ptr<std::atomic<bool>> readinessCancel; bool readinessBusy=false,readinessDirty=true,readinessOnlyIssues=true;
    std::string auditionKey,resourceText,resourceTextKey; bool auditionWithSong=false,auditionRange=false;
    std::vector<float> auditionPeaks; float auditionPan=0,auditionA=0,auditionB=0;
    MiniChart mini;
    std::set<int> chartSelection;
    std::set<fs::path> auditionFiles;
    bool reviewVisible=false;
    bool reviewRequested=false,batchRequested=false;
    bool looseRequested=false;
    int looseIndex=0,bindSong=0,bindDifficulty=0,bindCharacter=0,bindStage=0,bindObject=0,bindRole=0,bindTarget=0;
    std::array<char,160> filter{},targetId{},stage{};
    std::string status="Abre un mod o archivos JSON sueltos para empezar.", report;
    std::string loggedStatus; std::chrono::steady_clock::time_point loadStarted;
    // The report translated once per change, not every frame: thousands of
    // lines through the message table cost more than the whole frame.
    bool markAllVisible=false;
    bool reportInfo=false; std::string reportLocalized; size_t reportHash=0; Language reportLanguage=Language::Spanish;
    const std::string& localizedReport() {
        const auto hash=std::hash<std::string>{}(report);
        if(hash!=reportHash || language()!=reportLanguage || (reportLocalized.empty() && !report.empty())) { reportHash=hash; reportLanguage=language(); reportLocalized=reportText(report); }
        return reportLocalized;
    }
    std::string initialSelection;
    bool reportOpen=false;
    Plan plan;
    fml::SparrowAtlas previewAtlas;
    fml::PreviewBounds previewBounds;
    std::string previewKey;
    std::string iconState;
    bool previewValid=false;
    // Sprite sheet beside the character (optional): which page, and whether
    // the view follows the current frame or shows the whole sheet.
    bool showSheet=false,sheetFollow=true; int sheetPage=0,sheetPages=0;
    // Its own view: 1 fits the region, the wheel zooms toward the cursor and
    // dragging moves it (left or middle button), double click fits again.
    float sheetZoom=1,sheetPanX=0,sheetPanY=0; ImVec2 sheetCenter{};
    App() {
        std::snprintf(bundleName.data(),bundleName.size(),"psych-bundle");
        atlases.readText=[](const std::string& path) { std::error_code ec; auto p=fs::u8path(path); return fs::file_size(p,ec)>32u*1024u*1024u || ec?std::string():pi::read(p); };
    }
    void resetView() { clock=0; zoom=1; panX=panY=0; viewFlipX=viewFlipY=false; previewKey.clear(); }
    void loadSource(const fs::path& folder,const std::vector<fs::path>& files={}) {
        if(busy || writeBusy) return;
        busy=true; status="Buscando JSON y recursos en varias rutas..."; loadStarted=std::chrono::steady_clock::now();
        logs::info("Cargando "+(files.empty()?folder.u8string():std::to_string(files.size())+" archivos sueltos"));
        loading=std::async(std::launch::async,[folder,files]{ return files.empty()?pi::load(folder):pi::loadLoose(files); });
    }
    void addLoose(std::vector<fs::path> paths) {
        paths.insert(paths.end(),catalog.selectedFiles.begin(),catalog.selectedFiles.end());
        for(const auto& c:catalog.characters) paths.push_back(c.source);
        for(const auto& s:catalog.songs) for(const auto& d:s.difficulties) paths.push_back(d.path);
        for(const auto& s:catalog.stages) paths.push_back(s.source);
        std::vector<fs::path> expanded;
        for(const auto& path:paths) {
            if(fml::psych::directory(path)) { Files index; index.addTree(path); expanded.insert(expanded.end(),index.paths.begin(),index.paths.end()); }
            else expanded.push_back(path);
        }
        std::sort(expanded.begin(),expanded.end()); expanded.erase(std::unique(expanded.begin(),expanded.end()),expanded.end());
        loadSource({},expanded);
    }
    void selectCharacter(int index) {
        character=index; animation=0; resetView(); sheetZoom=1; sheetPanX=sheetPanY=0;
        std::snprintf(targetId.data(),targetId.size(),"%s",catalog.characters[index].imported.character.id.c_str());
    }
    void selectSong(int index) {
        if(index!=song) difficulty=0;
        song=index;
        std::snprintf(targetId.data(),targetId.size(),"%s",catalog.songs[index].id.c_str()); loadAudio();
    }
    void selectStage(int index) { stageIndex=index; stageObject=0; resetView(); stageAnimator.reset(catalog.stages[index].imported.stage); std::snprintf(targetId.data(),targetId.size(),"%s",catalog.stages[index].imported.stage.id.c_str()); }
    void selectIcon(int index) { icon=index;iconState.clear();iconHealth=.5f; resetView(); std::snprintf(targetId.data(),targetId.size(),"%s",catalog.icons[index].id.c_str()); }
    std::vector<std::pair<int,int>> chartRefs() const { std::vector<std::pair<int,int>> refs; for(int s=0;s<(int)catalog.songs.size();++s) for(int d=0;d<(int)catalog.songs[s].difficulties.size();++d) refs.emplace_back(s,d); return refs; }
    void select(int index) { if(tab==0) selectCharacter(index); else if(tab==1) selectSong(index); else if(tab==2) selectStage(index); else if(tab==3) selectIcon(index); else if(tab==4) { auto refs=chartRefs(); if(index>=0 && (size_t)index<refs.size()) { song=refs[index].first; difficulty=refs[index].second; resetView(); std::snprintf(targetId.data(),targetId.size(),"%s",catalog.songs[song].id.c_str()); loadAudio(); } } }
    int selectedIndex() const { if(tab>=5)return -1; if(tab==4) { auto refs=chartRefs(); for(int i=0;i<(int)refs.size();++i) if(refs[i]==std::make_pair(song,difficulty)) return i; return -1; } return tab==0?character:tab==1?song:tab==2?stageIndex:icon; }
    int count() const { if(tab>=5)return 0;return tab==0?(int)catalog.characters.size():tab==1?(int)catalog.songs.size():tab==2?(int)catalog.stages.size():tab==3?(int)catalog.icons.size():(int)chartRefs().size(); }
    std::set<int>& selection() { return tab==0?batch.characters:tab==1?batch.songs:tab==2?batch.stages:tab==3?batch.icons:chartSelection; }
    size_t selectionCount() const { return batch.characters.size()+batch.songs.size()+batch.stages.size()+batch.icons.size()+chartSelection.size()+batch.resources.size(); }
    void prepareBatch(bool zip) { batch.name=bundleName.data(); batch.charts.clear(); auto refs=chartRefs(); for(int i:chartSelection) if(i>=0 && (size_t)i<refs.size()) batch.charts.insert(refs[i]); batch.zip=zip; plan=batchPlan(catalog,destination,base,batch); zipPlan=zip; showPlan(true); }
    // The same tracks keep playing: changing tab re-selects the song, and
    // that must not cut what is being heard.
    void loadAudio(bool force=false) {
        if(song<0) return;
        std::vector<std::string> paths; std::string key;
        const auto& a=catalog.songs[song].difficulties[difficulty].audio;
        for(const auto& p:{a.instPath,a.voicesPath,a.playerVoicesPath,a.opponentVoicesPath}) if(!p.empty()) { paths.push_back(p); key+=p+"|"; }
        if(!force && key==audioKey) return;
        audioKey=key; audio.pause(); audio.clearLoop(); audioLoop=false;
        audio.loadTracksAsync(paths);
    }
    void loadGameAudio(bool autoplay=false) {
        game.playing=false; gameAudio.pause(); gameAudio.clearLoop(); gameAudio.setRate(game.rate); game.positionMs=0;
        previewPlayAfterLoad=autoplay;if(autoplay)audio.pause();
        std::vector<std::string> paths; for(const auto& p:{game.audio.instPath,game.audio.voicesPath,game.audio.playerVoicesPath,game.audio.opponentVoicesPath}) if(!p.empty()) paths.push_back(p);
        {PI_PROFILE("switch.audioQueue");gameAudio.loadTracksAsync(paths);} {PI_PROFILE("switch.rebuildAgain");game.rebuild(catalog);} game.seek(0,atlases);
    }
    void poll() {
        if(busy && loading.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try {
                auto result=loading.get();
                preserveSession(result,catalog);
                catalog=std::move(result);textures.clear();iconModels.clear();PreviewHud::clearCaches();
                resources=buildResourceIndex(catalog);resourcesDirty=false;resource=-1;resourceFiltered.clear();retainMarkableResources(resources,batch.resources);readinessDirty=true;readiness.clear();if(readinessCancel)*readinessCancel=true;auditionKey.clear();audioKey.clear();auditionAudio.pause();character=song=stageIndex=icon=-1;
                auto reader=atlases.readText; atlases=fml::AtlasStore{}; atlases.readText=reader;
                batch.characters.clear(); batch.songs.clear(); batch.stages.clear(); batch.icons.clear();
                chartSelection.clear(); batch.charts.clear();
                if(!catalog.characters.empty()) {
                    int preferred=0; for(int i=0;i<(int)catalog.characters.size();++i) if(catalog.characters[i].imported.character.id=="bf") preferred=i;
                    if(tab==0) for(int i=0;i<(int)catalog.characters.size();++i) if(lower(catalog.characters[i].imported.character.id)==lower(initialSelection)) preferred=i;
                    selectCharacter(preferred);
                }
                if(!catalog.songs.empty()) {
                    int keep=tab,preferred=0;
                    for(int i=0;i<(int)catalog.songs.size();++i) if(lower(catalog.songs[i].id)==lower(initialSelection)) preferred=i;
                    selectSong(preferred); if(keep==0 && character>=0) selectCharacter(character);
                }
                if(!catalog.stages.empty()) stageIndex=0;
                if(!catalog.icons.empty()) icon=0;
                stageActors={Gameplay::uniqueCharacter(catalog,"dad"),Gameplay::uniqueCharacter(catalog,"bf"),Gameplay::uniqueCharacter(catalog,"gf")};
                for(int i=0;i<(int)catalog.stages.size();++i) if(tab==2 && lower(catalog.stages[i].imported.stage.id)==lower(initialSelection)) stageIndex=i;
                for(int i=0;i<(int)catalog.icons.size();++i) if(tab==3 && lower(catalog.icons[i].id)==lower(initialSelection)) icon=i;
                if(!catalog.loose.empty()) { looseIndex=0; looseRequested=true; }
                status=std::to_string(catalog.characters.size())+" personajes · "+std::to_string(catalog.songs.size())+" canciones · "+std::to_string(catalog.stages.size())+" escenarios · "+std::to_string(catalog.files.paths.size())+" archivos revisados";
                if(tab!=5) {
                    if(character<0 && song>=0) tab=1;
                    if(character<0 && song<0 && stageIndex>=0) tab=2;
                    if(character<0 && song<0 && stageIndex<0 && icon>=0) tab=3;
                }
                if(tab==2 && stageIndex>=0) selectStage(stageIndex);
                if(tab==3 && icon>=0) selectIcon(icon);
                game.provisionalNotes=!catalog.completeMod;game.showHud=PreviewHudAvailable;
                game.chooseChart(catalog,song,difficulty);if(tab==5)loadGameAudio();else gameAudio.loadTracksAsync({});
                if(catalog.characters.empty() && catalog.songs.empty() && catalog.stages.empty() && catalog.icons.empty()) status="No se encontraron personajes ni charts Psych. Revisa el informe.";
                logs::info("Cargado en "+std::to_string(logs::since(loadStarted))+" ms: "+std::to_string(catalog.songs.size())+" canciones, "+std::to_string(catalog.characters.size())+" personajes, "+
                           std::to_string(catalog.stages.size())+" escenarios, "+std::to_string(catalog.icons.size())+" iconos, "+std::to_string(catalog.files.paths.size())+" archivos");
            } catch(const std::exception& e) { status=e.what(); logs::error(std::string("Carga fallida: ")+e.what()); }
            busy=false;
        }
        if(writeBusy && writing.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try { report=writing.get(); status=report.substr(0,report.find('\n')); reportOpen=true; }
            catch(const std::exception& e) { report=e.what(); reportOpen=true; logs::error(std::string("Exportación interrumpida: ")+e.what()); }
            writeBusy=false;
        }
        int count=0; std::string error;
        if(audio.pollTrackLoad(&count,&error)) {
            if(!error.empty()) status="Audio: "+error;
            if(audioPlayAfterLoad) { audioPlayAfterLoad=false; audio.seekMs(audioStartMs); if(audio.trackCount()) audio.play(); }
        }
        error.clear(); if(gameAudio.pollTrackLoad(&count,&error)) {
            if(!error.empty())status="Preview audio: "+error;
            if(previewPlayAfterLoad){
                previewPlayAfterLoad=false;
                if(previewStartMs>0){gameAudio.seekMs(previewStartMs);game.seek(previewStartMs,atlases);previewStartMs=0;}
                game.playing=true;if(gameAudio.trackCount())gameAudio.play();
            }
        }
        // Every engine finishes its own track loads here; one left out stays
        // "Loading audio..." forever.
        error.clear(); if(auditionAudio.pollTrackLoad(&count,&error) && !error.empty()) status="Resources audio: "+error;
    }
    void choose(SDL_Window* window,Action action) {
        if(dialogBusy || busy || writeBusy) return;
        dialogBusy=true; auto* req=new Request{action,character,song,difficulty,stageIndex,stageObject};
        if(action==Action::Mod || action==Action::Destination || action==Action::Base) SDL_ShowOpenFolderDialog(selected,req,window,nullptr,false);
        else {
            static SDL_DialogFileFilter jsonFilter[]={{"","json;lua;hx;png;xml;ogg"}};
            static SDL_DialogFileFilter pngFilter[]={{"","png"}};
            static SDL_DialogFileFilter xmlFilter[]={{"","xml;json"}};
            static SDL_DialogFileFilter audioFilter[]={{"","ogg"}};
            jsonFilter[0].name=ui("Recursos Psych / Animate","Psych / Animate resources"); pngFilter[0].name=ui("Imagen PNG","PNG image"); xmlFilter[0].name=ui("Sparrow XML / Animate JSON","Sparrow XML / Animate JSON"); audioFilter[0].name=ui("Audio OGG","OGG audio");
            auto* filter=action==Action::Files?jsonFilter:action==Action::Atlas || action==Action::StageAtlas || action==Action::PreviewNoteAtlas?xmlFilter:
                action==Action::Inst || action==Action::Voices || action==Action::PlayerVoices || action==Action::OpponentVoices?audioFilter:pngFilter;
            SDL_ShowOpenFileDialog(selected,req,window,filter,1,nullptr,action==Action::Files);
        }
    }
    void acceptDialog() {
        std::unique_ptr<DialogResult> r;
        { std::lock_guard<std::mutex> lock(dialogMutex); r=std::move(dialogResult); }
        if(!r) return; dialogBusy=false;
        if(!r->error.empty()) { status=r->error; return; }
        if(r->paths.empty()) return;
        const auto p=r->paths.front(); auto a=r->request.action;
        if(a==Action::Mod) loadSource(p);
        else if(a==Action::Files) {
            addLoose(r->paths);
        } else if(a==Action::Destination) destination=p;
        else if(a==Action::Base) base=fml::psych::directory(p/"assets")?p/"assets":p;
        else if(a==Action::PreviewNoteAtlas) {auto png=p;png.replace_extension(".png");game.hud.setNotes(png,p);game.provisionalNotes=!game.hud.notesValid;}
        else if(a==Action::Image || a==Action::Atlas || a==Action::CharacterIcon) {
            if(r->request.character<0 || (size_t)r->request.character>=catalog.characters.size()) return;
            auto& c=catalog.characters[r->request.character];
            c.manualBinding=true;
            if(a==Action::Image) c.image=p; else if(a==Action::Atlas) {
                c.atlas=p; c.animate=lower(p.filename().u8string())=="animation.json"; fml::DiagnosticSink sink;
                if(c.animate) {
                    c.animateData=animateResource(p); if(!c.animateData->images.empty()) c.image=c.animateData->images.front();
                    for(auto& anim:c.imported.character.anims) anim.resolvedFrames=c.animateData->atlas.frameCount(anim.atlasPrefix);
                } else fml::countPsychAnimationFrames(c.imported.character,pi::read(p),p.u8string(),sink);
                for(size_t i=0;i<c.take.size();++i) c.take[i]=c.imported.character.anims[i].resolvedFrames>0;
            } else c.iconPath=p;
            c.imported.character.resolvedImage=c.image.generic_u8string(); c.imported.character.resolvedAtlas=c.atlas.generic_u8string();
            textures.clear(); previewKey.clear();
            atlases.invalidate(p.generic_u8string());
        } else if(a==Action::SongIcon) {
            if(r->request.song>=0 && (size_t)r->request.song<catalog.songs.size()) {catalog.songs[r->request.song].iconPath=p;catalog.songs[r->request.song].manualIcon=true;}
        }
        else if(a==Action::StageImage || a==Action::StageAtlas) {
            if(r->request.stage<0 || (size_t)r->request.stage>=catalog.stages.size()) return;
            auto& s=catalog.stages[r->request.stage]; if(r->request.object<0 || (size_t)r->request.object>=s.imported.stage.objects.size()) return;
            auto& o=s.imported.stage.objects[r->request.object];
            s.manualObjects.insert(r->request.object);
            if(a==Action::StageImage) o.resolvedImage=p.generic_u8string();
            else { o.resolvedAtlas=p.generic_u8string(); s.animateData.erase(r->request.object);
                if(lower(p.filename().u8string())=="animation.json") { auto resource=animateResource(p); s.animateData[r->request.object]=resource; if(!resource->images.empty()) o.resolvedImage=resource->images.front().generic_u8string(); }
            }
            atlases.invalidate(o.resolvedAtlas); textures.clear(); stageAnimator.reset(s.imported.stage);
        } else {
            if(r->request.song<0 || (size_t)r->request.song>=catalog.songs.size() || r->request.difficulty<0 || (size_t)r->request.difficulty>=catalog.songs[r->request.song].difficulties.size()) return;
            auto& audioSource=catalog.songs[r->request.song].difficulties[r->request.difficulty].audio;
            catalog.songs[r->request.song].difficulties[r->request.difficulty].manualAudio=true;
            if(a==Action::Inst) audioSource.instPath=p.u8string();
            if(a==Action::Voices) { audioSource.voicesPath=p.u8string(); audioSource.playerVoicesPath.clear(); audioSource.opponentVoicesPath.clear(); }
            if(a==Action::PlayerVoices) { audioSource.playerVoicesPath=p.u8string(); audioSource.voicesPath.clear(); }
            if(a==Action::OpponentVoices) { audioSource.opponentVoicesPath=p.u8string(); audioSource.voicesPath.clear(); }
            loadAudio();
        }
    }
    // What a partial export would write; computed once per review.
    Plan partial; std::vector<std::string> partialOmitted;
    void showPlan(bool import) {
        report=describe(plan,reportInfo || !import); partial=partialPlan(plan,&partialOmitted); reportOpen=!import; reviewRequested=import;
        if(!import) return;
        logs::info("Revisión: "+plan.label+(zipPlan?" (ZIP)":"")+" · "+std::to_string(plan.files.size())+" archivos · "+std::to_string(plan.warnings.size())+" avisos · "+std::to_string(plan.errors.size())+" bloqueos");
        logs::list("WARN","Bloqueos",plan.errors,20);
        if(!plan.ready()) logs::info(partial.ready()?"Exportable a medias: "+std::to_string(partial.files.size())+" archivos, "+std::to_string(partialOmitted.size())+" omisiones":std::string("Sin exportación a medias posible"));
    }
    void review(bool import,bool onlyDefinition=false) {
        zipPlan=false;
        if(tab==4 && song>=0) { auto copy=catalog.songs[song]; auto d=copy.difficulties[difficulty]; d.take=true; copy.difficulties={d}; plan=chartBundlePlan(catalog,copy,destination,base,targetId.data(),!onlyDefinition && chartCharacters,!onlyDefinition && chartStages); showPlan(import); return; }
        if(onlyDefinition) {
            if(tab==1 && song>=0) plan=chartsPlan(catalog,catalog.songs[song],destination,targetId.data(),stage.data());
            else if(tab==3 && icon>=0) plan=iconPlan(catalog,catalog.icons[icon],destination,targetId.data(),false);
            else plan=definitionPlan(catalog,tab,selectedIndex(),destination,targetId.data());
        }
        else if(tab==0 && character>=0) plan=characterPlan(catalog,catalog.characters[character],destination,targetId.data());
        else if(tab==1 && song>=0) plan=songPlan(catalog,catalog.songs[song],destination,base,targetId.data(),stage.data(),bringCharacters,bringStages);
        else if(tab==2 && stageIndex>=0) plan=stagePlan(catalog,catalog.stages[stageIndex],destination,targetId.data());
        else if(tab==3 && icon>=0) plan=iconPlan(catalog,catalog.icons[icon],destination,targetId.data());
        else { report=inventory(catalog).dump(2); reportOpen=true; return; }
        showPlan(import);
    }
};
void tooltip(const std::string& text) { if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",text.c_str()); }
// --- Toolbar -------------------------------------------------------------------
// Windows' own icon font (Segoe Fluent Icons on 11, Segoe MDL2 Assets on 10)
// is merged into the UI font; without it the labels go alone.
bool iconFontLoaded=false;
std::string iconGlyph(unsigned code) {
    if(!iconFontLoaded) return {};
    std::string out; out+=char(0xE0|(code>>12)); out+=char(0x80|((code>>6)&0x3F)); out+=char(0x80|(code&0x3F)); return out;
}
std::string iconLabel(unsigned code,const std::string& text) { auto glyph=iconGlyph(code); return glyph.empty()?text:glyph+"  "+text; }
// The last two parts of a path: "Codename.Engine-Windows / mods".
std::string shortPath(fs::path p) {
    while(!p.empty() && p.filename().empty()) p=p.parent_path();
    auto name=p.filename().u8string(),parent=p.parent_path().filename().u8string();
    return parent.empty()?name:parent+" / "+name;
}
void pathRow(const char* label,const fs::path& path) { ImGui::TextDisabled("%s",label); ImGui::SameLine(); ImGui::TextWrapped("%s",path.empty()?ui("Sin resolver","Unresolved"):path.filename().u8string().c_str()); tooltip(path.u8string()); }
void drawScenePreview(App& app,const fml::RenderList& render,ImVec2 at,ImVec2 size,
                      const fml::ViewportTransform& view,bool flipX=false,bool flipY=false,
                      const std::vector<fml::GlRenderer::ShaderPass>* passes=nullptr) {
    auto& renderer=app.textures.renderer;
    int width=std::max(1,(int)std::round(size.x)),height=std::max(1,(int)std::round(size.y));
    if(!renderer.beginOffscreenFrame(width,height))return;
    renderer.draw(render,view,width,height);
    // Camera shaders (a modchart's): the scene again into FML's capture, the
    // chain over the game frame, written back into this offscreen image. The
    // first draw stays around the frame, as the wide preview shows it.
    if(passes && !passes->empty() && renderer.beginSceneCapture(width,height,0,0,0,1)) {
        renderer.draw(render,view,width,height);
        renderer.setPresentFramebuffer(renderer.offscreenFramebuffer());
        if(!renderer.applyShaderChain(*passes,view,width,height)) renderer.presentSceneCapture(width,height);
        renderer.setPresentFramebuffer(0);
    }
    auto image=renderer.finishOffscreenPreview();
    if(image.ok)ImGui::GetWindowDrawList()->AddImage(ImTextureRef((ImTextureID)image.texture),at,{at.x+size.x,at.y+size.y},
                                                   {flipX?1.0f:0.0f,flipY?0.0f:1.0f},{flipX?0.0f:1.0f,flipY?1.0f:0.0f});
}
// FML's engine camera frame (GlRenderer::drawGameBounds): the 1280x720 the
// game shows, same transform as the scene, visible once the view zooms out
// or pans.
void drawCameraFrame(ImVec2 at,ImVec2 size,const fml::ViewportTransform& view,const CameraFrameStyle& style,bool flipX=false,bool flipY=false) {
    const float fit=std::min(size.x/1280.0f,size.y/720.0f)*view.zoom,w=1280*fit,h=720*fit;
    float x=size.x*.5f-640*fit+view.panX,y=size.y*.5f-360*fit+view.panY;
    if(flipX)x=size.x-x-w; if(flipY)y=size.y-y-h;
    auto* draw=ImGui::GetWindowDrawList(); const ImVec2 a{at.x+x,at.y+y},b{a.x+w,a.y+h},end{at.x+size.x,at.y+size.y};
    // What the game does not show, darkened around the frame.
    if(style.dim>0) {
        const ImU32 shade=IM_COL32(0,0,0,(int)(std::clamp(style.dim,0.0f,1.0f)*255));
        draw->AddRectFilled(at,{end.x,std::max(at.y,a.y)},shade); draw->AddRectFilled({at.x,std::min(end.y,b.y)},end,shade);
        draw->AddRectFilled({at.x,std::max(at.y,a.y)},{std::max(at.x,a.x),std::min(end.y,b.y)},shade);
        draw->AddRectFilled({std::min(end.x,b.x),std::max(at.y,a.y)},{end.x,std::min(end.y,b.y)},shade);
    }
    const ImU32 color=ImGui::ColorConvertFloat4ToU32(style.color); const float t=std::max(1.0f,style.thickness);
    draw->AddRect(a,b,color,0.0f,ImDrawFlags_None,t);
    if(style.corners) {
        // Solid corners, three times thicker: visible even when the line is faint.
        ImVec4 solid=style.color; solid.w=1; const ImU32 mark=ImGui::ColorConvertFloat4ToU32(solid);
        const float l=std::min(w,h)*.08f,k=t*3;
        for(int cx=0;cx<2;++cx) for(int cy=0;cy<2;++cy) {
            const ImVec2 p{cx?b.x:a.x,cy?b.y:a.y}; const float sx=cx?-1.0f:1.0f,sy=cy?-1.0f:1.0f;
            draw->AddLine(p,{p.x+sx*l,p.y},mark,k); draw->AddLine(p,{p.x,p.y+sy*l},mark,k);
        }
    }
}
void saveCameraFrame(const App& app) {
    if(app.cameraFramePath.empty()) return;
    std::error_code ec; fs::create_directories(app.cameraFramePath.parent_path(),ec);
    std::ofstream out(app.cameraFramePath,std::ios::binary); const auto& s=app.cameraFrame;
    out<<s.color.x<<' '<<s.color.y<<' '<<s.color.z<<' '<<s.color.w<<' '<<s.thickness<<' '<<s.dim<<' '<<(s.corners?1:0)<<'\n';
}
void loadCameraFrame(App& app) {
    std::ifstream in(app.cameraFramePath,std::ios::binary); CameraFrameStyle s; int corners=1;
    if(in>>s.color.x>>s.color.y>>s.color.z>>s.color.w>>s.thickness>>s.dim>>corners) {
        for(float* v:{&s.color.x,&s.color.y,&s.color.z,&s.color.w}) *v=std::clamp(*v,0.0f,1.0f);
        s.thickness=std::clamp(s.thickness,1.0f,8.0f); s.dim=std::clamp(s.dim,0.0f,.9f); s.corners=corners!=0; app.cameraFrame=s;
    }
}
// The toggle with its style popup, in Preview and in Stages.
void cameraFrameToggle(App& app,const char* hint) {
    ImGui::Checkbox(uiId("Cuadro de cámara","Camera frame","camera-frame").c_str(),&app.showCameraFrame); tooltip(hint);
    ImGui::SameLine(); if(ImGui::SmallButton((iconLabel(0xE790,ui("Estilo","Style"))+"###camera-frame-style").c_str())) ImGui::OpenPopup("##camera-frame-style");
    tooltip(ui("Color, grosor y oscurecer lo que queda fuera.","Color, thickness and dimming what lies outside."));
    if(ImGui::BeginPopup("##camera-frame-style")) {
        auto& s=app.cameraFrame; bool changed=false;
        ImGui::TextUnformatted(ui("Cuadro de cámara","Camera frame")); ImGui::Separator();
        // Quick colors that read on most stages.
        const ImVec4 swatches[]={{1,1,1,1},{1,.85f,.2f,1},{.25f,.9f,1,1},{1,.3f,.75f,1},{.4f,1,.45f,1},{1,.35f,.3f,1}};
        for(int i=0;i<6;++i) {
            if(i) ImGui::SameLine(); ImGui::PushID(i);
            if(ImGui::ColorButton("##swatch",swatches[i],ImGuiColorEditFlags_NoTooltip,{24,24})) { float alpha=s.color.w; s.color=swatches[i]; s.color.w=alpha; changed=true; }
            ImGui::PopID();
        }
        ImGui::SetNextItemWidth(220); changed|=ImGui::ColorEdit4(uiId("Color","Color","frame-color").c_str(),&s.color.x,ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_AlphaPreviewHalf);
        ImGui::SetNextItemWidth(220); changed|=ImGui::SliderFloat(uiId("Grosor","Thickness","frame-thickness").c_str(),&s.thickness,1,8,"%.0f px");
        ImGui::SetNextItemWidth(220); changed|=ImGui::SliderFloat(uiId("Oscurecer fuera","Dim outside","frame-dim").c_str(),&s.dim,0,.9f,"%.2f");
        changed|=ImGui::Checkbox(uiId("Esquinas marcadas","Corner marks","frame-corners").c_str(),&s.corners);
        if(ImGui::Button(uiId("Restaurar","Reset","frame-reset").c_str())) { s=CameraFrameStyle{}; changed=true; }
        if(changed) { app.cameraFrameDirty=true; app.showCameraFrame=true; }
        ImGui::EndPopup();
    } else if(app.cameraFrameDirty) { saveCameraFrame(app); app.cameraFrameDirty=false; }
}
// One player at a time: the one that just started pauses the others.
// Changing tab never stops what is playing.
void soloAudio(App& app) {
    fml::AudioEngine* engines[]={&app.audio,&app.gameAudio,&app.auditionAudio};
    int started=-1; for(int i=0;i<3;++i) if(engines[i]->playing() && !app.audioWasPlaying[i]) started=i;
    if(started>=0) for(int i=0;i<3;++i) if(i!=started && engines[i]->playing()) { engines[i]->pause(); if(i==1) app.game.playing=false; }
    for(int i=0;i<3;++i) app.audioWasPlaying[i]=engines[i]->playing();
}
void iconAt(App& app,const fs::path& path,ImVec2 at,float size,bool health=true) {
    if(!ImGui::IsRectVisible(at,{at.x+size,at.y+size}))return;
    auto& tex=app.textures.get(path); auto* draw=ImGui::GetWindowDrawList();
    if(tex.id) {
        if(health) {
            auto key=path.generic_u8string();auto found=app.iconModels.find(key);
            if(found==app.iconModels.end()) {
                Icon definition;definition.source=path;
                for(const auto& icon:app.catalog.icons)if(icon.source==path){definition=icon;break;}
                found=app.iconModels.emplace(key,iconFrames(definition,tex.w,tex.h)).first;
            }
            auto frame=found->second.sample(.5f);if(frame) {
                fml::PreviewBounds bounds;bounds.include(0,0);bounds.include(frame->frameW>0?frame->frameW:fml::frameDrawW(*frame),frame->frameH>0?frame->frameH:fml::frameDrawH(*frame));
                auto place=fml::fitPreviewBounds(at,{size,size},bounds,0,0,false,1,{},0);
                auto piece=fml::placePreviewFrame(*frame,place,found->second.facingLeft,false,true,tex.w,tex.h);
                draw->AddImageQuad(ImTextureRef((ImTextureID)tex.id),piece.p0,{piece.p1.x,piece.p0.y},piece.p1,{piece.p0.x,piece.p1.y},piece.uv[0],piece.uv[1],piece.uv[2],piece.uv[3]);
            }
        } else {
            const float scale=std::min(size/tex.w,size/tex.h);ImVec2 p{at.x+(size-tex.w*scale)*.5f,at.y+(size-tex.h*scale)*.5f};
            draw->AddImage(ImTextureRef((ImTextureID)tex.id),p,{p.x+tex.w*scale,p.y+tex.h*scale});
        }
    } else {
        draw->AddRect(at,ImVec2(at.x+size,at.y+size),IM_COL32(72,83,95,255),4);
        draw->AddText(ImVec2(at.x+size*.4f,at.y+size*.25f),IM_COL32(154,162,176,255),"-");
    }
}
void list(App& app) {
    const char* names[]={ui("Personajes","Characters"),ui("Canciones","Songs"),ui("Escenarios","Stages"),ui("Iconos","Icons"),"Charts"};
    ImGui::TextUnformatted(names[app.tab]); ImGui::Separator();
    if(ImGui::SmallButton(ui("Todos","All"))) for(int i=0;i<app.count();++i) app.selection().insert(i);
    ImGui::SameLine(); if(ImGui::SmallButton(ui("Limpiar","Clear"))) app.selection().clear();
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##search",ui("Buscar...","Search..."),app.filter.data(),app.filter.size());
    auto match=lower(app.filter.data());
    int count=app.count();
    auto chartRefs=app.chartRefs();
    for(int i=0;i<count;++i) {
        auto label=app.tab==0?app.catalog.characters[i].imported.character.id:app.tab==1?app.catalog.songs[i].title:app.tab==2?app.catalog.stages[i].imported.stage.id:app.tab==3?app.catalog.icons[i].id:app.catalog.songs[chartRefs[i].first].id+" / "+app.catalog.songs[chartRefs[i].first].difficulties[chartRefs[i].second].name;
        if(lower(label).find(match)==std::string::npos) continue;
        ImGui::PushID(i); bool take=app.selection().count(i)!=0;
        // Brought along by the marked songs: shown, not marked.
        const bool implied=!take && ((app.tab==0 && app.deps.characters.count(i)) || (app.tab==1 && app.deps.songs.count(i)) || (app.tab==2 && app.deps.stages.count(i)));
        if(implied) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,true);
        if(ImGui::Checkbox("##multiple",&take)) { if(take) app.selection().insert(i); else app.selection().erase(i); }
        if(implied) { ImGui::PopItemFlag(); tooltip(app.tab==1?ui("Va con los personajes marcados.","Goes with the marked characters."):ui("Va con las canciones marcadas.","Goes with the marked songs.")); }
        ImGui::SameLine(); auto at=ImGui::GetCursorScreenPos();
        bool chosen=i==app.selectedIndex();
        if(ImGui::Selectable("##row",chosen,0,ImVec2(0,58))) app.select(i);
        if(ImGui::BeginPopupContextItem("##actions")) {
            if(!chosen) app.select(i);
            if(ImGui::MenuItem(ui("Ver informe","View report"))) app.review(false);
            if(ImGui::MenuItem(ui("Exportar bundle...","Export bundle..."))) app.review(true);
            if(ImGui::MenuItem(app.tab==1 || app.tab==4?ui("Solo charts...","Charts only..."):app.tab==3?ui("Solo PNG...","PNG only..."):ui("Solo definición...","Definition only..."))) app.review(true,true);
            if(ImGui::MenuItem(take?ui("Quitar de selección","Remove from selection"):ui("Añadir a selección","Add to selection"))) { if(take) app.selection().erase(i); else app.selection().insert(i); }
            if(ImGui::MenuItem(ui("Exportar selección múltiple...","Export multiple selected resources..."))) { app.selection().insert(i); app.batchRequested=true; }
            ImGui::EndPopup();
        }
        auto iconPath=app.tab==0?app.catalog.characters[i].iconPath:app.tab==1?app.catalog.songs[i].iconPath:app.tab==2?app.catalog.stages[i].iconPath:app.tab==3?app.catalog.icons[i].source:app.catalog.songs[chartRefs[i].first].iconPath;
        tooltip(label+"\n"+iconPath.u8string());
        iconAt(app,iconPath,ImVec2(at.x+4,at.y+5),46,app.tab!=2);
        auto* draw=ImGui::GetWindowDrawList(); draw->PushClipRect(at,ImVec2(at.x+ImGui::GetContentRegionAvail().x,at.y+58),true);
        draw->AddText(ImVec2(at.x+60,at.y+18),ImGui::GetColorU32(ImGuiCol_Text),label.c_str()); draw->PopClipRect();
        ImGui::PopID();
    }
    if(!count) ImGui::TextWrapped(ui("Abre un mod o JSON sueltos. Se detecta el tipo por su contenido.","Open a mod or loose files. Their content determines their type."));
}
// Everything a mod offers in one go; charts already travel with their songs.
void markWholeMod(App& app) {
    const fs::path base=app.base.empty()?codenameAssetsBeside(app.destination):app.base;
    auto current=app.batch;retainMarkableResources(buildResourceIndex(app.catalog),current.resources);
    auto whole=wholeModSelection(app.catalog,base,current);
    app.batch=whole.options;
    auto refs=app.chartRefs();
    for(auto it=app.chartSelection.begin();it!=app.chartSelection.end();)if(*it<0 || (size_t)*it>=refs.size() || app.batch.songs.count(refs[*it].first))it=app.chartSelection.erase(it);else ++it;
    app.status=std::string(ui("Marcado el mod: ","Marked the mod: "))+std::to_string(app.batch.songs.size())+" "+ui("canciones","songs")+", "+
               std::to_string(app.batch.characters.size())+" "+ui("personajes","characters")+", "+std::to_string(app.batch.stages.size())+" "+ui("escenarios","stages")+", "+std::to_string(app.batch.icons.size())+" "+ui("iconos","icons")+". "+
               ui("Sin marcar: ","Not marked: ")+std::to_string(whole.baseSkipped)+" "+ui("del juego base","from the base game")+", "+
               std::to_string(whole.unexportable)+" "+ui("que no se pueden exportar","that cannot be exported")+".";
}
std::string countText(size_t n,const char* one,const char* many) { return std::to_string(n)+" "+(n==1?one:many); }
void joinCount(std::string& out,size_t n,const char* one,const char* many) { if(!n) return; if(!out.empty()) out+=", "; out+=countText(n,one,many); }
std::string dependencyText(const App& app) {
    std::string out; const auto& d=app.deps;
    joinCount(out,d.characters.size(),ui("personaje","character"),ui("personajes","characters"));
    joinCount(out,d.stages.size(),ui("escenario","stage"),ui("escenarios","stages"));
    joinCount(out,d.songs.size(),ui("canción","song"),ui("canciones","songs"));
    return out;
}
// One card per end of the conversion: what it is, what is in it, how to
// change it.
template<class Body> void toolbarCard(const char* id,float width,float height,Body body) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{10,7}); ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.094f,.125f,.165f,1));
    ImGui::BeginChild(id,{width,height},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{6,4}); body(); ImGui::PopStyleVar();
    ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopStyleVar();
}
// A two-line accent button: the action and what it will take.
bool exportButton(const char* id,const std::string& title,const std::string& detail,ImVec2 size,bool enabled) {
    auto at=ImGui::GetCursorScreenPos(); bool pressed=ImGui::InvisibleButton(id,size);
    bool hovered=enabled && ImGui::IsItemHovered(),held=enabled && ImGui::IsItemActive();
    auto* draw=ImGui::GetWindowDrawList();
    ImVec4 fill=!enabled?ImVec4(.15f,.20f,.25f,1):held?ImVec4(.06f,.33f,.72f,1):hovered?ImVec4(.12f,.47f,.95f,1):ImVec4(.08f,.40f,.85f,1);
    draw->AddRectFilled(at,{at.x+size.x,at.y+size.y},ImGui::ColorConvertFloat4ToU32(fill),6);
    auto* font=ImGui::GetFont(); const float small=ImGui::GetFontSize()*.82f;
    auto a=ImGui::CalcTextSize(title.c_str()); auto b=font->CalcTextSizeA(small,FLT_MAX,0,detail.c_str());
    const float top=at.y+(size.y-(a.y+b.y+2))*.5f;
    draw->AddText({at.x+(size.x-a.x)*.5f,top},enabled?IM_COL32_WHITE:IM_COL32(150,160,172,255),title.c_str());
    draw->AddText(font,small,{at.x+(size.x-b.x)*.5f,top+a.y+2},enabled?IM_COL32(207,224,247,255):IM_COL32(130,140,152,255),detail.c_str());
    return pressed && enabled;
}
void toolbar(App& app,SDL_Window* window) {
    const auto& c=app.catalog; const ImVec4 amber(1,.78f,.35f,1);
    const bool busy=app.busy || app.writeBusy || app.dialogBusy;
    const bool empty=c.characters.empty() && c.songs.empty() && c.stages.empty() && c.icons.empty();
    const float spacing=ImGui::GetStyle().ItemSpacing.x,total=ImGui::GetContentRegionAvail().x,side=176,arrow=20;
    const float cardW=std::max(230.0f,(total-side-arrow-spacing*3)*.5f);
    const float cardH=ImGui::GetTextLineHeight()*3+ImGui::GetFrameHeight()+4*3+14+2;
    ImGui::BeginDisabled(busy);
    toolbarCard("##source-card",cardW,cardH,[&]{
        ImGui::TextDisabled("%s",iconLabel(0xE838,ui("Origen · Psych Engine","Source · Psych Engine")).c_str());
        if(c.origin.empty()) ImGui::TextColored(amber,"%s",ui("Sin cargar","Not loaded"));
        else { ImGui::TextUnformatted(shortPath(fs::u8path(c.origin)).c_str()); tooltip(c.origin); }
        if(empty) ImGui::TextDisabled("%s",ui("Abre la carpeta de un mod o archivos sueltos.","Open a mod folder or loose files."));
        else ImGui::TextDisabled("%s · %s · %s",countText(c.songs.size(),ui("canción","song"),ui("canciones","songs")).c_str(),countText(c.characters.size(),ui("personaje","character"),ui("personajes","characters")).c_str(),countText(c.stages.size(),ui("escenario","stage"),ui("escenarios","stages")).c_str());
        if(ImGui::Button((iconLabel(0xE8B7,ui("Abrir mod","Open mod"))+"###open-mod").c_str())) app.choose(window,Action::Mod); ImGui::SameLine();
        if(ImGui::Button((iconLabel(0xE8E5,ui("Archivos sueltos","Loose files"))+"###loose-files").c_str())) app.choose(window,Action::Files);
        if(!c.loose.empty()) { ImGui::SameLine(); if(ImGui::Button(uiId("Revisar sueltos","Review loose files","review-loose").c_str())) app.looseRequested=true; }
    });
    ImGui::SameLine(); {
        auto at=ImGui::GetCursorScreenPos(); ImGui::Dummy({arrow,cardH}); auto* draw=ImGui::GetWindowDrawList();
        ImVec2 m{at.x+arrow*.5f,at.y+cardH*.5f}; const ImU32 color=IM_COL32(91,104,118,255);
        draw->AddLine({m.x-7,m.y},{m.x+6,m.y},color,2); draw->AddLine({m.x+1,m.y-5},{m.x+6,m.y},color,2); draw->AddLine({m.x+1,m.y+5},{m.x+6,m.y},color,2);
    }
    ImGui::SameLine();
    toolbarCard("##destination-card",cardW,cardH,[&]{
        ImGui::TextDisabled("%s",iconLabel(0xE896,ui("Destino · Codename Engine","Destination · Codename Engine")).c_str());
        if(app.destination.empty()) ImGui::TextColored(amber,"%s",ui("Sin elegir","Not chosen"));
        else { ImGui::TextUnformatted(shortPath(app.destination).c_str()); tooltip(app.destination.u8string()); }
        if(app.base.empty()) ImGui::TextDisabled("%s",ui("Sin base de lectura (opcional)","No read-only base (optional)"));
        else { ImGui::TextDisabled("%s %s",ui("Base de lectura:","Read-only base:"),shortPath(app.base).c_str()); tooltip(app.base.u8string()); }
        if(ImGui::Button((iconLabel(0xE8B7,ui("Elegir destino","Choose destination"))+"###choose-destination").c_str())) app.choose(window,Action::Destination); ImGui::SameLine();
        if(ImGui::Button((iconLabel(0xE72E,ui("Base","Base"))+"###choose-base").c_str())) app.choose(window,Action::Base);
        tooltip(ui("Una instalación de Codename de solo lectura: lo que ya tenga no se vuelve a exportar.","A read-only Codename install: what it already has is not exported again."));
    });
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::BeginGroup();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+side-2*43-spacing);
    for(int i=0;i<2;++i) {
        if(i) ImGui::SameLine(); bool active=language()==(i?Language::English:Language::Spanish);
        if(active) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.08f,.40f,.85f,1));
        if(ImGui::Button(i?"EN###language-en":"ES###language-es",ImVec2(43,0))) {
            setLanguage(i?Language::English:Language::Spanish);
            if(!saveLanguage(app.languagePath)) app.status="No se pudo guardar el idioma; el cambio se mantiene en esta sesión.";
        }
        auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax(); app.languageCenters[i]=ImVec2((a.x+b.x)/2,(a.y+b.y)/2);
        if(active) ImGui::PopStyleColor();
    }
    const size_t marked=app.selectionCount(),extra=app.deps.characters.size()+app.deps.stages.size()+app.deps.songs.size();
    std::string detail=!marked?ui("Nada marcado","Nothing marked"):std::to_string(marked)+" "+ui("marcados","marked")+(extra?" · +"+std::to_string(extra):"");
    const float buttonH=cardH-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemSpacing.y;
    if(exportButton("##export-marked",iconLabel(0xEDE1,ui("Exportar","Export")),detail,{side,buttonH},!busy && !empty)) app.batchRequested=true;
    if(!marked) tooltip(ui("Marca canciones, personajes, escenarios o iconos con sus casillas, o usa «Marcar todo el mod».","Tick songs, characters, stages or icons, or use «Mark the whole mod»."));
    else tooltip(std::string(ui("Abre la exportación múltiple.","Opens the multiple export."))+(extra?std::string(" +")+std::to_string(extra)+": "+dependencyText(app)+ui(" que van con lo marcado."," that go with what is marked."):std::string()));
    ImGui::EndGroup();
}
// What will be exported, kept in view, and the shortcut that saves ticking
// "All" in every tab.
void selectionBar(App& app) {
    const auto& b=app.batch;const size_t marked=app.selectionCount();std::string summary;
    if(!marked)summary=ui("Nada marcado. Usa las casillas de las listas.","Nothing marked. Use the list checkboxes.");
    else {
        joinCount(summary,b.songs.size(),ui("canción","song"),ui("canciones","songs"));joinCount(summary,b.characters.size(),ui("personaje","character"),ui("personajes","characters"));
        joinCount(summary,b.stages.size(),ui("escenario","stage"),ui("escenarios","stages"));joinCount(summary,b.icons.size(),ui("icono","icon"),ui("iconos","icons"));
        joinCount(summary,app.chartSelection.size(),"chart","charts");joinCount(summary,b.resources.size(),ui("recurso","resource"),ui("recursos","resources"));
        summary=std::string(ui("Marcado: ","Marked: "))+summary;const auto extra=dependencyText(app);
        if(!extra.empty())summary+=" · "+std::string(ui("van con ello: ","along with it: "))+extra;
    }
    const float wrap=std::max(100.0f,ImGui::GetContentRegionAvail().x-24);
    const float height=ImGui::GetFrameHeight()+ImGui::CalcTextSize(summary.c_str(),nullptr,false,wrap).y+ImGui::GetStyle().ItemSpacing.y+12;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{10,5});ImGui::PushStyleColor(ImGuiCol_ChildBg,marked?ImVec4(.10f,.17f,.25f,1):ImVec4(.08f,.11f,.14f,1));
    ImGui::BeginChild("##selection-bar",{0,height},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BeginDisabled(app.busy || app.writeBusy || app.dialogBusy);
    const auto label=iconLabel(0xE8B3,ui("Marcar todo el mod","Mark the whole mod"))+"###mark-whole-mod";
    if(ImGui::Button(label.c_str()))markWholeMod(app);
    const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax(),pos=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    app.markAllVisible=ImGui::IsItemVisible() && min.x>=pos.x && max.x<=pos.x+size.x && max.y<=pos.y+size.y;
    tooltip(ui("Marca los personajes, canciones, escenarios e iconos exportables de sus listas. Los charts y el audio van con la canción. No marca scripts, ejecutables ni archivos internos. Los recursos sueltos se eligen a mano en Recursos.","Marks exportable characters, songs, stages and icons from their lists. Charts and audio travel with the song. Scripts, executables and internal files are not marked. Loose resources are selected manually in Resources."));
    if(marked){ImGui::SameLine();if(ImGui::Button(uiId("Limpiar","Clear","clear-marks").c_str())){app.batch.characters.clear();app.batch.songs.clear();app.batch.stages.clear();app.batch.icons.clear();app.chartSelection.clear();app.batch.charts.clear();app.batch.resources.clear();}}
    ImGui::EndDisabled();ImGui::TextWrapped("%s",summary.c_str());
    ImGui::EndChild();ImGui::PopStyleColor();ImGui::PopStyleVar();
}
void viewControls(App& app) {
    previewZoomControls(app.zoom);
    wrappedCheckbox(uiId("Voltear X","Flip X","view-flip-x").c_str(),&app.viewFlipX); ImGui::SameLine(); wrappedCheckbox(uiId("Voltear Y","Flip Y","view-flip-y").c_str(),&app.viewFlipY);
    nextControl(buttonWidth(ui("Centrar","Center")));if(ImGui::Button(uiId("Centrar","Center","view-center").c_str())) { app.zoom=1; app.panX=app.panY=0; }
    auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();app.viewCenterButtons[app.tab]={(a.x+b.x)*.5f,(a.y+b.y)*.5f};
    ImGui::TextWrapped("%s",ui("Arrastra con izq./central · rueda para zoom","Drag left/middle · wheel to zoom"));
}
void interactCanvas(App& app) {
    auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();auto center=ImVec2((a.x+b.x)*.5f,(a.y+b.y)*.5f);app.viewCanvasCenters[app.tab]=center;
    interactPreviewView(app.zoom,app.panX,app.panY,center);
}
void animationControls(App& app,int count,int fps,bool showView=true) {
    fps=std::max(1,fps);
    if(ImGui::Button(uiId(app.playingPreview?"Pausar":"Reproducir",app.playingPreview?"Pause":"Play","preview-play").c_str())) app.playingPreview=!app.playingPreview;
    nextControl(buttonWidth(ui("Inicio","Restart")));if(ImGui::Button(uiId("Inicio","Restart","preview-start").c_str())) app.clock=0;
    ImGui::SameLine(); if(ImGui::Button("<##frame-prev")) { app.clock=(float)std::max(0,playbackFrame(app.clock,count,fps,app.loopPreview)-1)/fps; app.playingPreview=false; }
    ImGui::SameLine(); if(ImGui::Button(">##frame-next")) { app.clock=(float)std::min(std::max(0,count-1),playbackFrame(app.clock,count,fps,app.loopPreview)+1)/fps; app.playingPreview=false; }
    nextControl(buttonWidth(ui("Repetir","Loop"))+ImGui::GetFrameHeight());wrappedCheckbox(uiId("Repetir","Loop","preview-loop").c_str(),&app.loopPreview);
    float width=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.5f;
    int frame=playbackFrame(app.clock,count,fps,app.loopPreview);
    ImGui::BeginGroup();ImGui::TextUnformatted(ui("Fotograma","Frame"));ImGui::SetNextItemWidth(width);
    if(ImGui::SliderInt("##preview-frame",&frame,0,std::max(0,count-1))) { app.clock=(float)frame/fps; app.playingPreview=false; }
    ImGui::EndGroup();ImGui::SameLine();
    ImGui::BeginGroup();ImGui::TextUnformatted(ui("Velocidad","Speed"));ImGui::SetNextItemWidth(width);
    ImGui::SliderFloat("##preview-speed",&app.previewRate,.1f,3.0f,"%.2fx");ImGui::EndGroup();
    if(!app.loopPreview && count>0 && app.clock*fps>=count) { app.clock=(float)(count-1)/fps; app.playingPreview=false; }
    if(showView)viewControls(app);
}
// One rectangle of a sheet the current frame is drawn from.
struct SheetPiece { fs::path image; int x=0,y=0,w=0,h=0; std::string name; };
// The sheet beside the character: the current frame's rectangle on it, or
// for Animate every piece the frame is assembled from, numbered. Several
// pages (multi-sheet, several spritemaps): one at a time.
void drawSheet(App& app,const std::vector<SheetPiece>& pieces,ImVec2 at,ImVec2 size,bool animate) {
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(at,{at.x+size.x,at.y+size.y},IM_COL32(28,34,42,255),4);
    std::vector<fs::path> pages; for(const auto& p:pieces) if(std::find(pages.begin(),pages.end(),p.image)==pages.end()) pages.push_back(p.image);
    app.sheetPages=(int)pages.size();
    if(pages.empty()) { draw->AddText({at.x+10,at.y+10},IM_COL32(154,162,176,255),ui("Sin fotograma que mostrar.","No frame to show.")); return; }
    app.sheetPage=std::clamp(app.sheetPage,0,(int)pages.size()-1);
    const auto& page=pages[app.sheetPage]; auto& tex=app.textures.get(page);
    if(!tex.id) { draw->AddText({at.x+10,at.y+10},IM_COL32(234,204,126,255),message(tex.error).c_str()); return; }
    std::vector<const SheetPiece*> shown; for(const auto& p:pieces) if(p.image==page) shown.push_back(&p);
    // Region of the sheet: around the pieces (following the frame) or whole.
    float rx=0,ry=0,rw=(float)tex.w,rh=(float)tex.h;
    if(app.sheetFollow && !shown.empty()) {
        float x0=1e9f,y0=1e9f,x1=-1e9f,y1=-1e9f;
        for(const auto* p:shown) { x0=std::min(x0,(float)p->x); y0=std::min(y0,(float)p->y); x1=std::max(x1,(float)(p->x+p->w)); y1=std::max(y1,(float)(p->y+p->h)); }
        const float margin=std::max(x1-x0,y1-y0)*.35f+16;
        rx=std::max(0.0f,x0-margin); ry=std::max(0.0f,y0-margin); rw=std::min((float)tex.w,x1+margin)-rx; rh=std::min((float)tex.h,y1+margin)-ry;
    }
    // The region fits the panel at zoom 1; zoom and pan move over the whole
    // sheet. Up to 8 screen pixels per sheet pixel, whatever its size.
    const float top=26,fit=std::min((size.x-16)/std::max(1.0f,rw),(size.y-top-10)/std::max(1.0f,rh));
    const float maxZoom=std::max(5.0f,8.0f/std::max(1e-4f,fit));
    const ImVec2 center{at.x+size.x*.5f,at.y+top+(size.y-top-10)*.5f};
    if(ImGui::IsItemHovered()) {
        auto& io=ImGui::GetIO();
        if(io.MouseWheel!=0) {
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
            const float next=std::clamp(app.sheetZoom*std::pow(1.2f,io.MouseWheel),.25f,maxZoom),ratio=next/app.sheetZoom;
            app.sheetPanX=(io.MousePos.x-center.x)-((io.MousePos.x-center.x)-app.sheetPanX)*ratio;
            app.sheetPanY=(io.MousePos.y-center.y)-((io.MousePos.y-center.y)-app.sheetPanY)*ratio;
            app.sheetZoom=next;
        }
        if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { app.sheetZoom=1; app.sheetPanX=app.sheetPanY=0; }
    }
    if(ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) { app.sheetPanX+=ImGui::GetIO().MouseDelta.x; app.sheetPanY+=ImGui::GetIO().MouseDelta.y; }
    const float scale=fit*app.sheetZoom,cx=rx+rw*.5f,cy=ry+rh*.5f;
    auto toScreen=[&](float x,float y){ return ImVec2(center.x+app.sheetPanX+(x-cx)*scale,center.y+app.sheetPanY+(y-cy)*scale); };
    const ImVec2 origin=toScreen(0,0),end=toScreen((float)tex.w,(float)tex.h);
    draw->PushClipRect({at.x,at.y+top},{at.x+size.x,at.y+size.y},true);
    // Checkerboard only where the sheet is visible: zoomed in, the sheet is
    // far larger than the panel.
    const ImVec2 v0{std::max(origin.x,at.x),std::max(origin.y,at.y+top)},v1{std::min(end.x,at.x+size.x),std::min(end.y,at.y+size.y)};
    for(float y=v0.y;y<v1.y;y+=12) for(float x=v0.x;x<v1.x;x+=12)
        draw->AddRectFilled({x,y},{std::min(v1.x,x+12),std::min(v1.y,y+12)},(((int)((x-v0.x)/12)+(int)((y-v0.y)/12))%2)?IM_COL32(52,59,67,255):IM_COL32(43,49,57,255));
    draw->AddImage(ImTextureRef((ImTextureID)tex.id),origin,end);
    static const ImU32 colors[]={IM_COL32(255,214,64,255),IM_COL32(64,214,255,255),IM_COL32(255,96,180,255),IM_COL32(120,255,120,255),IM_COL32(255,150,60,255),IM_COL32(170,140,255,255)};
    for(size_t k=0;k<shown.size();++k) {
        const auto* p=shown[k]; const ImU32 color=animate?colors[k%6]:colors[0];
        const ImVec2 a=toScreen((float)p->x,(float)p->y),b=toScreen((float)(p->x+p->w),(float)(p->y+p->h));
        draw->AddRect(a,b,color,0.0f,ImDrawFlags_None,2.0f);
        if(animate && b.x-a.x>14 && b.y-a.y>12) draw->AddText({a.x+3,a.y+2},color,std::to_string(k+1).c_str());
        if(ImGui::IsMouseHoveringRect(a,b) && ImGui::IsWindowHovered()) ImGui::SetTooltip("%s\n%d, %d · %d×%d",p->name.c_str(),p->x,p->y,p->w,p->h);
    }
    draw->PopClipRect();
    std::string header=page.filename().u8string()+" · "+std::to_string(tex.w)+"×"+std::to_string(tex.h);
    if(pages.size()>1) header=std::to_string(app.sheetPage+1)+"/"+std::to_string(pages.size())+" · "+header;
    if(animate) header+=" · "+std::to_string(shown.size())+" "+ui("piezas armadas","assembled pieces");
    else if(!shown.empty()) header+=" · "+shown.front()->name;
    // Sheet pixels to screen pixels, so the zoom reads as a real size; in the
    // corner, where a long header cannot push it out.
    char zoomText[24]; std::snprintf(zoomText,sizeof zoomText,"%.0f%%",scale*100);
    const ImVec2 zoomSize=ImGui::CalcTextSize(zoomText),zoomAt{at.x+size.x-zoomSize.x-10,at.y+size.y-zoomSize.y-8};
    draw->AddRectFilled({zoomAt.x-5,zoomAt.y-2},{zoomAt.x+zoomSize.x+5,zoomAt.y+zoomSize.y+2},IM_COL32(20,26,34,220),4);
    draw->AddText(zoomAt,IM_COL32(200,211,225,255),zoomText);
    draw->PushClipRect(at,{at.x+size.x,at.y+top},true); draw->AddText({at.x+8,at.y+6},IM_COL32(200,211,225,255),header.c_str()); draw->PopClipRect();
}
void characterPreview(App& app) {
    ImGui::TextUnformatted(ui("Vista previa","Preview")); ImGui::SameLine();
    ImGui::Checkbox(uiId("Hoja de sprites","Sprite sheet","show-sheet").c_str(),&app.showSheet);
    tooltip(ui("La hoja de la que sale el fotograma, al lado. En Animate, las piezas con las que se arma.","The sheet the frame comes from, alongside. For Animate, the pieces it is assembled from."));
    if(app.showSheet) {
        ImGui::SameLine(); if(ImGui::Checkbox(uiId("Seguir el fotograma","Follow the frame","sheet-follow").c_str(),&app.sheetFollow)) { app.sheetZoom=1; app.sheetPanX=app.sheetPanY=0; }
        ImGui::SameLine(); if(ImGui::SmallButton(uiId("Encajar","Fit","sheet-fit").c_str())) { app.sheetZoom=1; app.sheetPanX=app.sheetPanY=0; }
        tooltip(ui("Rueda: zoom hacia el cursor · arrastrar: mover · doble clic: encajar.","Wheel: zoom toward the cursor · drag: move · double click: fit."));
        if(app.sheetPages>1) {
            ImGui::SameLine(); if(ImGui::SmallButton("<##sheet-prev")) app.sheetPage=(app.sheetPage+app.sheetPages-1)%app.sheetPages;
            ImGui::SameLine(); ImGui::Text("%d/%d",app.sheetPage+1,app.sheetPages);
            ImGui::SameLine(); if(ImGui::SmallButton(">##sheet-next")) app.sheetPage=(app.sheetPage+1)%app.sheetPages;
        }
    }
    ImGui::Separator();
    if(app.character<0) { ImGui::TextWrapped(ui("Selecciona un personaje.","Select a character.")); return; }
    auto& c=app.catalog.characters[app.character];
    auto& model=c.imported.character;
    if(!model.anims.empty()) app.animation=std::clamp(app.animation,0,(int)model.anims.size()-1);
    auto at=ImGui::GetCursorScreenPos(); auto size=ImGui::GetContentRegionAvail(); size.y=std::max(80.0f,size.y-265);
    // With the sheet open the character keeps a bit more than half.
    const float full=size.x; if(app.showSheet) size.x=std::floor(full*.56f);
    std::vector<SheetPiece> pieces;
    ImGui::InvisibleButton("##canvas",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle); interactCanvas(app); auto* draw=ImGui::GetWindowDrawList();
    draw->PushClipRect(at,ImVec2(at.x+size.x,at.y+size.y),true);
    for(int y=0;y<(int)size.y;y+=20) for(int x=0;x<(int)size.x;x+=20) draw->AddRectFilled(ImVec2(at.x+x,at.y+y),ImVec2(at.x+x+20,at.y+y+20),((x/20+y/20)%2)?IM_COL32(52,59,67,255):IM_COL32(43,49,57,255));
    std::string error;
    if(c.animate) {
        if(!c.animateData || !c.animateData->error.empty()) error=c.animateData?c.animateData->error:"Animate: atlas no resuelto";
        else if(model.anims.empty()) error=ui("Sin animaciones","No animations");
        else {
            const auto& atlas=c.animateData->atlas; const auto& animation=model.anims[app.animation];
            int available=atlas.frameCount(animation.atlasPrefix),count=animation.indices.empty()?available:(int)animation.indices.size();
            int logical=playbackFrame(app.clock,count,animation.fps,app.loopPreview);
            int frame=animation.indices.empty()?logical:animation.indices[logical];
            if(available<=0 || frame<0 || frame>=available) error=ui("Esta animación no tiene fotogramas","This animation has no frames");
            else {
                fml::PreviewBounds bounds;
                for(const auto& candidate:model.anims) {
                    auto b=atlas.bounds(candidate.atlasPrefix); if(!b.valid) continue;
                    bool fx=(model.flipX!=candidate.flipX)!=app.viewFlipX,fy=candidate.flipY!=app.viewFlipY;
                    for(int corner=0;corner<4;++corner) { float x=corner&1?b.right:b.left,y=corner&2?b.bottom:b.top; bounds.include((fx?-x:x)-candidate.offset.x,(fy?-y:y)-candidate.offset.y); }
                }
                auto placement=fml::fitPreviewBounds(at,size,bounds,0,0,!model.antialiasing,app.zoom,ImVec2(app.panX,app.panY));
                bool fx=(model.flipX!=animation.flipX)!=app.viewFlipX,fy=animation.flipY!=app.viewFlipY;
                auto point=[&](float x,float y) { return ImVec2(placement.anchor.x+((fx?-x:x)-animation.offset.x)*placement.scale,placement.anchor.y+((fy?-y:y)-animation.offset.y)*placement.scale); };
                std::vector<fml::AnimateElement> elements; atlas.flatten(animation.atlasPrefix,frame,elements);
                for(const auto& element:elements) {
                    auto* sprite=element.sprite; if(!sprite) continue;
                    auto& texture=app.textures.get(sprite->sourceImage.empty()?c.image:fs::u8path(sprite->sourceImage)); if(!texture.id) { error=texture.error; continue; }
                    float w=(float)sprite->width(),h=(float)sprite->height();
                    auto p0=point(element.tx,element.ty),p1=point(element.tx+element.a*w,element.ty+element.b*w);
                    auto p2=point(element.tx+element.a*w+element.c*h,element.ty+element.b*w+element.d*h),p3=point(element.tx+element.c*h,element.ty+element.d*h);
                    float u0=(float)sprite->x/texture.w,v0=(float)sprite->y/texture.h,u1=(float)(sprite->x+sprite->w)/texture.w,v1=(float)(sprite->y+sprite->h)/texture.h;
                    ImVec2 uv0(u0,v0),uv1(u1,v0),uv2(u1,v1),uv3(u0,v1);
                    if(sprite->rotated) { uv0=ImVec2(u1,v0); uv1=ImVec2(u1,v1); uv2=ImVec2(u0,v1); uv3=ImVec2(u0,v0); }
                    draw->AddImageQuad(ImTextureRef((ImTextureID)texture.id),p0,p1,p2,p3,uv0,uv1,uv2,uv3);
                    pieces.push_back({sprite->sourceImage.empty()?c.image:fs::u8path(sprite->sourceImage),sprite->x,sprite->y,sprite->w,sprite->h,sprite->name});
                }
            }
        }
    }
    else {
        auto& tex=app.textures.get(c.image);
        if(app.previewKey!=c.atlas.u8string()+"|"+c.source.u8string()) {
            app.previewKey=c.atlas.u8string()+"|"+c.source.u8string(); fml::DiagnosticSink sink;
            auto parsed=fml::parseSparrowAtlas(pi::read(c.atlas),c.atlas.u8string(),sink);
            app.previewValid=(bool)parsed; app.previewBounds={};
            if(parsed) {
                app.previewAtlas=std::move(parsed.value());
                for(const auto& a:model.anims) for(auto index:app.previewAtlas.framesFor(a.atlasPrefix)) {
                    const auto& f=app.previewAtlas.frames[index];
                    app.previewBounds.include(-a.offset.x,-a.offset.y);
                    app.previewBounds.include(-a.offset.x+std::max(1,f.frameW>0?f.frameW:f.w),-a.offset.y+std::max(1,f.frameH>0?f.frameH:f.h));
                }
            }
        }
        if(!tex.id) error=tex.error;
        else if(!app.previewValid || model.anims.empty()) error=ui("Selecciona un atlas XML válido","Choose a valid XML atlas");
        else {
            app.animation=std::clamp(app.animation,0,(int)model.anims.size()-1); const auto& anim=model.anims[app.animation];
            auto frames=app.previewAtlas.framesFor(anim.atlasPrefix);
            if(!anim.indices.empty()) { std::vector<size_t> selectedFrames; for(int index:anim.indices) if(index>=0 && (size_t)index<frames.size()) selectedFrames.push_back(frames[index]); frames=std::move(selectedFrames); }
            if(frames.empty()) error=ui("Esta animación no tiene fotogramas","This animation has no frames");
            else {
                auto frameIndex=(size_t)playbackFrame(app.clock,(int)frames.size(),anim.fps,app.loopPreview);
                const auto& f=app.previewAtlas.frames[frames[frameIndex]];
                // FML's stable envelope and placement retain authored trim/offsets.
                auto placement=fml::fitPreviewBounds(at,size,app.previewBounds,-anim.offset.x,-anim.offset.y,!model.antialiasing,app.zoom,ImVec2(app.panX,app.panY));
                auto placed=fml::placePreviewFrame(f,placement,(model.flipX!=anim.flipX)!=app.viewFlipX,anim.flipY!=app.viewFlipY,true,tex.w,tex.h);
                auto ref=ImTextureRef((ImTextureID)tex.id);
                draw->AddImageQuad(ref,placed.p0,ImVec2(placed.p1.x,placed.p0.y),placed.p1,ImVec2(placed.p0.x,placed.p1.y),placed.uv[0],placed.uv[1],placed.uv[2],placed.uv[3]);
                pieces.push_back({c.image,f.x,f.y,f.w,f.h,f.name});
            }
        }
    }
    if(!error.empty()) draw->AddText(ImVec2(at.x+12,at.y+20),IM_COL32(234,204,126,255),message(error).c_str());
    draw->PopClipRect();
    if(app.showSheet) {
        ImGui::SameLine(); const ImVec2 sheetAt=ImGui::GetCursorScreenPos(),sheetSize{full-size.x-ImGui::GetStyle().ItemSpacing.x,size.y};
        ImGui::InvisibleButton("##sheet",sheetSize,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle); drawSheet(app,pieces,sheetAt,sheetSize,c.animate);
        app.sheetCenter={sheetAt.x+sheetSize.x*.5f,sheetAt.y+sheetSize.y*.5f};
    }
    ImGui::SetNextItemWidth(-1);
    const char* current=model.anims.empty()?ui("Sin animaciones","No animations"):model.anims[app.animation].name.c_str();
    if(ImGui::BeginCombo("##animation",current)) {
        for(int i=0;i<(int)model.anims.size();++i) if(ImGui::Selectable(model.anims[i].name.c_str(),i==app.animation)) { app.animation=i; app.clock=0; }
        ImGui::EndCombo();
    }
    int count=model.anims.empty()?0:model.anims[app.animation].resolvedFrames,fps=model.anims.empty()?24:model.anims[app.animation].fps;
    animationControls(app,count,fps);
}
void fileChoice(App& app,SDL_Window* window,const char* label,const std::string& path,Action action) {
    ImGui::PushID(label); ImGui::TextDisabled("%s",label);
    float chooseWidth=buttonWidth(ui("Elegir","Choose"));
    bool beside=ImGui::GetContentRegionAvail().x>=chooseWidth+ImGui::GetStyle().ItemSpacing.x+100;
    ImGui::SetNextItemWidth(beside?ImGui::GetContentRegionAvail().x-chooseWidth-ImGui::GetStyle().ItemSpacing.x:-1);
    std::vector<char> value(path.begin(),path.end()); value.push_back(0);
    ImGui::InputText("##path",value.data(),value.size(),ImGuiInputTextFlags_ReadOnly); tooltip(path.empty()?ui("No encontrado. Elige un archivo.","Not found. Choose a file."):path);
    if(beside)ImGui::SameLine();if(ImGui::Button(ui("Elegir","Choose"))) app.choose(window,action); ImGui::PopID();
}
void characterOptions(App& app,SDL_Window* window) {
    ImGui::TextUnformatted(ui("Importación","Import")); ImGui::Separator(); if(app.character<0) return;
    auto& c=app.catalog.characters[app.character];
    ImGui::TextDisabled("ID"); ImGui::SetNextItemWidth(-1); ImGui::InputText("##ID",app.targetId.data(),app.targetId.size());
    fileChoice(app,window,ui("Imagen","Image"),c.image.u8string(),Action::Image);
    fileChoice(app,window,"Atlas XML",c.atlas.u8string(),Action::Atlas);
    fileChoice(app,window,ui("Icono","Icon"),c.iconPath.u8string(),Action::CharacterIcon);
    ImGui::Spacing(); ImGui::TextUnformatted(ui("Animaciones","Animations"));
    if(ImGui::BeginChild("##animations",ImVec2(0,160),ImGuiChildFlags_Borders)) {
        for(size_t i=0;i<c.take.size();++i) {
            ImGui::PushID((int)i); bool enabled=c.take[i]!=0;
            if(ImGui::Checkbox(c.imported.character.anims[i].name.c_str(),&enabled)) c.take[i]=enabled;
            if(c.imported.character.anims[i].resolvedFrames==0) { ImGui::SameLine(); ImGui::TextDisabled(ui("sin frames","no frames")); }
            ImGui::PopID();
        }
    } ImGui::EndChild();
    if(!c.imported.character.anims.empty()) {
        auto& anim=c.imported.character.anims[app.animation];
        ImGui::SetNextItemWidth(80); if(fullInputInt(uiId("FPS al exportar","Export FPS","animation-fps").c_str(),&anim.fps)) anim.fps=std::clamp(anim.fps,1,240);
        wrappedCheckbox(uiId("Loop al exportar","Export loop","animation-loop").c_str(),&anim.loop);
        ImGui::TextDisabled("%s: %s",ui("Prefijo / símbolo","Prefix / symbol"),anim.atlasPrefix.c_str());
    }
    if(c.animate && c.animateData) {
        ImGui::TextDisabled("Animate · %zu %s · %zu %s",c.animateData->maps.size(),ui("páginas","pages"),c.animateData->atlas.symbolCount(),ui("símbolos","symbols"));
        if(ImGui::Button(ui("Añadir todos los símbolos","Add all symbols"))) {
            for(const auto& symbol:c.animateData->atlas.symbolNames()) {
                bool present=false; for(const auto& anim:c.imported.character.anims) if(anim.atlasPrefix==symbol) present=true;
                if(present) continue; fml::AnimationDef anim; anim.name=symbol; anim.atlasPrefix=symbol; anim.loop=true; anim.resolvedFrames=c.animateData->atlas.frameCount(symbol);
                c.imported.character.anims.push_back(anim); c.take.push_back(anim.resolvedFrames>0);
            }
        }
    }
    for(const auto* candidates:{&c.imageChoices,&c.atlasChoices,&c.iconChoices}) if(!candidates->empty()) {
        ImGui::TextColored(ImVec4(1,.78f,.35f,1),ui("Varias coincidencias: usa Elegir","Multiple matches: use Choose"));
        for(const auto& p:*candidates) ImGui::TextWrapped("%s",p.u8string().c_str());
    }
    if(!c.warnings.empty()) { ImGui::Spacing(); ImGui::TextColored(ImVec4(1,.78f,.35f,1),ui("%zu avisos","%zu warnings"),c.warnings.size()); }
}
void songDetails(App& app,SDL_Window* window) {
    if(app.song<0) { ImGui::TextWrapped(ui("Selecciona una canción.","Select a song.")); return; }
    auto& s=app.catalog.songs[app.song]; auto& active=s.difficulties[app.difficulty];
    ImGui::TextUnformatted(s.title.c_str()); ImGui::TextDisabled(ui("%.0f BPM · %zu dificultades","%.0f BPM · %zu difficulties"),active.chart.bpm,s.difficulties.size()); ImGui::Separator();
    if(ImGui::BeginTable("##difficulties",3,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(ui("Dificultades","Difficulties")); ImGui::TableSetupColumn(ui("Notas","Notes")); ImGui::TableSetupColumn("Audio"); ImGui::TableHeadersRow();
        for(int i=0;i<(int)s.difficulties.size();++i) {
            auto& d=s.difficulties[i]; ImGui::PushID(i); ImGui::TableNextRow(); ImGui::TableNextColumn(); bool take=d.take;
            if(ImGui::Checkbox("##take",&take)) d.take=take; ImGui::SameLine();
            if(ImGui::Selectable(d.name.c_str(),app.difficulty==i)) { app.difficulty=i; app.loadAudio(); }
            tooltip(d.path.u8string()); ImGui::TableNextColumn(); ImGui::Text("%zu",d.chart.notes.size());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(d.audio.instPath.empty()?ui("Pendiente","Missing"):ui("Encontrado","Found")); ImGui::PopID();
        } ImGui::EndTable();
    }
    auto& d=s.difficulties[app.difficulty]; ImGui::Spacing(); ImGui::TextUnformatted("Audio");
    fileChoice(app,window,"Instrumental",d.audio.instPath,Action::Inst);
    fileChoice(app,window,ui("Voces combinadas","Combined vocals"),d.audio.voicesPath,Action::Voices);
    if(ImGui::TreeNode(ui("Voces separadas","Split vocals"))) {
        fileChoice(app,window,ui("Jugador","Player"),d.audio.playerVoicesPath,Action::PlayerVoices);
        fileChoice(app,window,ui("Rival","Opponent"),d.audio.opponentVoicesPath,Action::OpponentVoices); ImGui::TreePop();
    }
    ImGui::Spacing(); ImGui::BeginDisabled(app.audio.trackLoadPending() || !app.audio.trackCount());
    if(ImGui::Button(app.audio.playing()?ui("Pausar","Pause"):ui("Reproducir","Play"))) { if(app.audio.playing()) app.audio.pause(); else app.audio.play(); }
    ImGui::SameLine(); if(ImGui::Button(ui("Inicio","Restart"))) app.audio.seekMs(0);
    float pos=(float)(app.audio.positionMs()/1000.0),duration=(float)(app.audio.durationMs()/1000.0);
    ImGui::TextDisabled("%d:%02d / %d:%02d",(int)pos/60,(int)pos%60,(int)duration/60,(int)duration%60);
    ImGui::SetNextItemWidth(-1); if(ImGui::SliderFloat("##seek",&pos,0,std::max(1.0f,duration),"%.1f s")) app.audio.seekMs(pos*1000);
    ImGui::EndDisabled();
    if(app.audio.trackLoadPending()) ImGui::TextDisabled(ui("Cargando audio...","Loading audio..."));
    float gain=app.audio.masterGain(),rate=app.audio.rate();
    ImGui::SetNextItemWidth(-1); if(fullSliderFloat(uiId("Volumen","Volume","song-volume").c_str(),&gain,0,1,"%.2f")) app.audio.setMasterGain(gain);
    ImGui::SetNextItemWidth(-1); if(fullSliderFloat(uiId("Velocidad","Speed","song-rate").c_str(),&rate,.25f,2,"%.2fx")) app.audio.setRate(rate);
    ImGui::BeginDisabled(!app.audio.trackCount() || app.audio.trackLoadPending());
    if(wrappedCheckbox(uiId("Repetir canción","Loop song","song-loop").c_str(),&app.audioLoop)) { if(app.audioLoop) app.audio.setLoop(0,app.audio.durationMs()); else app.audio.clearLoop(); }
    ImGui::EndDisabled();
    if(ImGui::TreeNode(uiId("Mezcla de pistas","Track mix","song-mix").c_str())) {
        for(size_t i=0;i<app.audio.trackCount();++i) {
            ImGui::PushID((int)i); auto name=app.audio.trackName(i); ImGui::TextUnformatted(fs::u8path(name).filename().u8string().c_str());
            float trackGain=app.audio.trackGain(i); ImGui::SetNextItemWidth(-1); if(ImGui::SliderFloat("##gain",&trackGain,0,1,"%.2f")) app.audio.setTrackGain(i,trackGain); ImGui::PopID();
        } ImGui::TreePop();
    }
    if(ImGui::TreeNode(uiId("Detalles de dificultad","Difficulty details","song-details").c_str())) {
        int sustains=0,playerNotes=0; double end=0;
        for(const auto& note:d.chart.notes) { if(note.sustainMs>0) ++sustains; if(note.isPlayer) ++playerNotes; end=std::max(end,note.timeMs+note.sustainMs); }
        ImGui::Text("%s: %s",ui("Dificultad","Difficulty"),d.name.c_str());
        ImGui::Text("BPM %.2f · %s %.2f",d.chart.bpm,ui("velocidad del chart","chart speed"),d.chart.speed);
        ImGui::Text("%s: %zu · %s: %d",ui("Notas","Notes"),d.chart.notes.size(),ui("sustains","sustains"),sustains);
        ImGui::Text("%s: %d · %s: %zu",ui("Jugador","Player"),playerNotes,ui("Rival / otros","Opponent / others"),d.chart.notes.size()-playerNotes);
        ImGui::Text("%s: %.2f s · %s: %zu",ui("Fin de notas","Last note end"),end/1000,ui("cambios BPM","BPM changes"),d.chart.bpmChanges.size());
        ImGui::Text("%s: %zu · %s: %zu",ui("Strumlines","Strumlines"),d.chart.strumLines.size(),ui("tipos de nota","note types"),d.chart.noteTypes.size());
        for(const auto& type:d.chart.noteTypes) ImGui::BulletText("%s",type.c_str());
        ImGui::TextWrapped("%s: %s",ui("Chart","Chart"),d.path.u8string().c_str());
        ImGui::TextWrapped("%s",ui("Las rutas de audio se asignan solo a esta dificultad.","Audio paths apply only to this difficulty."));
        ImGui::TreePop();
    }
    if(ImGui::TreeNode(uiId("Eventos → Codename","Events → Codename","song-events").c_str())) {
        auto details=eventReport(d); ImGui::Text("%s: %d",ui("Eventos de origen","Source events"),d.events.total);
        for(const auto& e:details["converted"]) ImGui::TextWrapped("%s: %s → %s (%d)",ui("Convertido","Converted"),e["psych"].get<std::string>().c_str(),e["codename"].get<std::string>().c_str(),e["count"].get<int>());
        for(const auto& e:details["unresolved"]) { ImGui::TextColored(ImVec4(1,.78f,.35f,1),"%s: %s (%d)",ui("Sin equivalente","No equivalent"),e["psych"].get<std::string>().c_str(),e["count"].get<int>()); ImGui::TextWrapped("%s",message(e["hint"].get<std::string>()).c_str()); }
        ImGui::TextWrapped("%s",ui("Los eventos sin equivalente se conservan con sus valores; requieren port o script. El bundle incluye events-report.json.","Events without equivalents retain their values and need a port or script. The bundle includes events-report.json."));
        ImGui::TreePop();
    }
    if(!d.warnings.empty() && ImGui::TreeNode(ui("Avisos del chart","Chart warnings"))) { for(const auto& w:d.warnings) ImGui::TextWrapped("%s",message(w).c_str()); ImGui::TreePop(); }
}
void songOptions(App& app,SDL_Window* window) {
    ImGui::TextUnformatted(ui("Dependencias","Dependencies")); ImGui::Separator(); if(app.song<0) return;
    auto& s=app.catalog.songs[app.song]; auto& chart=s.difficulties[app.difficulty].chart;
    if(!s.artist.empty()) ImGui::TextWrapped("%s: %s",ui("Música","Music"),s.artist.c_str());
    if(!s.charter.empty()) ImGui::TextWrapped("%s: %s",ui("Autor del chart","Chart author"),s.charter.c_str());
    ImGui::TextDisabled("ID"); ImGui::SetNextItemWidth(-1); ImGui::InputText("##ID",app.targetId.data(),app.targetId.size());
    for(const auto& role:std::vector<std::pair<std::string,std::string>>{{ui("Jugador","Player"),chart.player1},{ui("Rival","Opponent"),chart.player2},{ui("Acompañante","Companion"),chart.gfVersion}}) {
        ImGui::TextDisabled("%s",role.first.c_str()); ImGui::SameLine(); ImGui::TextUnformatted(role.second.c_str());
    }
    wrappedCheckbox(ui("Traer personajes faltantes","Import missing characters"),&app.bringCharacters);
    wrappedCheckbox(uiId("Importar escenarios con la canción","Import stages with song","song-stages").c_str(),&app.bringStages);
    if(!app.bringStages) {ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.78f,.35f,1));ImGui::TextWrapped("%s",ui("Si falta el escenario en destino, tendrás que cambiar stage en los charts posteriormente.","If the destination lacks this stage, you must change stage in the charts later."));ImGui::PopStyleColor();}
    ImGui::Spacing(); ImGui::TextDisabled(ui("Escenario de origen: %s","Source stage: %s"),chart.stage.c_str());
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##stage",ui("Conservar escenario de cada chart","Keep each chart's stage"),app.stage.data(),app.stage.size());
    ImGui::TextWrapped(ui("Escribe un escenario existente en destino/base para sustituir el de todas las dificultades.","Enter a stage available in the destination/base to replace it in all difficulties."));
    fileChoice(app,window,ui("Icono de canción","Song icon"),s.iconPath.u8string(),Action::SongIcon);
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped(ui("Asociado: %s","Associated: %s"),s.iconId.empty()?ui("ninguno; buscar face","none; find face"):s.iconId.c_str());ImGui::PopStyleColor();
    for(const auto& p:s.iconChoices) ImGui::TextWrapped(ui("Coincidencia: %s","Match: %s"),p.u8string().c_str());
    ImGui::Spacing(); ImGui::TextWrapped(ui("Importación básica de datos y recursos. Los scripts originales se conservan sin activar.","Basic data and resource import. Original scripts are archived without activation."));
}
void stagePreview(App& app) {
    ImGui::TextUnformatted(ui("Escenario · visor FML","Stage · FML viewer")); ImGui::Separator();
    if(app.stageIndex<0) { ImGui::TextWrapped("%s",ui("Selecciona un escenario.","Select a stage.")); return; }
    auto& stage=app.catalog.stages[app.stageIndex].imported.stage;
    bool scenery=false;for(const auto& object:stage.objects)if(object.kind==fml::StageObject::Kind::Sprite || object.kind==fml::StageObject::Kind::Box)scenery=true;
    if(!scenery){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.78f,.35f,1));ImGui::TextWrapped("%s",ui("Sin decorado recuperable: esta vista muestra posiciones y personajes. Carga la fuente del escenario para exportarlo completo.","No recoverable scenery: this view shows positions and characters. Load the stage source to export it completely."));ImGui::PopStyleColor();}
    auto at=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();size.y=std::max(150.0f,size.y-255);
    ImGui::InvisibleButton("##stage-canvas",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle); interactCanvas(app); auto* draw=ImGui::GetWindowDrawList();
    draw->PushClipRect(at,ImVec2(at.x+size.x,at.y+size.y),true);
    draw->AddRectFilled(at,ImVec2(at.x+size.x,at.y+size.y),IM_COL32(35,42,50,255));
    fml::CharacterBinding bindings;
    if(app.showActors) {
        auto actor=[&](int role)->const fml::UniversalCharacter* {int index=app.stageActors[role]; return index>=0 && (size_t)index<app.catalog.characters.size()?&app.catalog.characters[index].imported.character:nullptr;};
        bindings.opponent=actor(0); bindings.player=actor(1); bindings.girlfriend=actor(2);
    }
    app.stageAnimator.updateWithBeat(stage,bindings,app.atlases,app.clock*1000,app.clock*app.stageBpm/60.0,app.playingPreview);
    fml::RenderList render; fml::buildStageRenderList(stage,bindings,app.atlases,app.textures,render,&app.stageAnimator);
    fml::ViewportTransform view;
    if(app.autoFrameStage){fml::PreviewSceneBounds bounds;fml::includePreviewSceneBounds(render,bounds);view=fml::fitPreviewScene(bounds,(int)size.x,(int)size.y);}
    view.zoom*=app.zoom;view.panX=view.panX*app.zoom+app.panX;view.panY=view.panY*app.zoom+app.panY;
    drawScenePreview(app,render,at,size,view,app.viewFlipX,app.viewFlipY);
    if(app.showCameraFrame) drawCameraFrame(at,size,view,app.cameraFrame,app.viewFlipX,app.viewFlipY);
    draw->PopClipRect();
    if(ImGui::Button(uiId(app.playingPreview?"Pausar":"Reproducir",app.playingPreview?"Pause":"Play","stage-play").c_str())) app.playingPreview=!app.playingPreview;
    ImGui::SameLine(); if(ImGui::Button(ui("Inicio","Restart"))) { app.clock=0; app.stageAnimator.rewind(); }
    ImGui::SameLine(); wrappedCheckbox(ui("Repetir","Loop"),&app.loopPreview);
    float controlWidth=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.5f;
    ImGui::BeginGroup();ImGui::TextUnformatted(ui("Tiempo","Time"));ImGui::SetNextItemWidth(controlWidth);
    if(ImGui::SliderFloat("##stage-time",&app.clock,0,app.stageDuration,"%.2f s")){app.playingPreview=false;app.stageAnimator.resync(app.clock*app.stageBpm/60,app.clock*1000);}ImGui::EndGroup();ImGui::SameLine();
    ImGui::BeginGroup();ImGui::TextUnformatted(ui("Velocidad","Speed"));ImGui::SetNextItemWidth(controlWidth);ImGui::SliderFloat("##stage-speed",&app.previewRate,.1f,3,"%.2fx");ImGui::EndGroup();
    wrappedCheckbox(ui("Personajes","Characters"),&app.showActors);nextControl(buttonWidth(ui("Encuadrar escenario","Fit stage"))+ImGui::GetFrameHeight());wrappedCheckbox(ui("Encuadrar escenario","Fit stage"),&app.autoFrameStage);
    nextControl(buttonWidth(ui("Cuadro de cámara","Camera frame"))+ImGui::GetFrameHeight()+buttonWidth(ui("Estilo","Style"))+40);
    cameraFrameToggle(app,ui("El marco de 1280×720 que muestra el juego, como en FML.","The 1280×720 frame the game shows, as in FML."));viewControls(app);
}
void stageOptions(App& app,SDL_Window* window) {
    ImGui::TextUnformatted(ui("Escenario y objetos","Stage and objects")); ImGui::Separator(); if(app.stageIndex<0) return;
    if(ImGui::CollapsingHeader(ui("Personajes opcionales de la vista","Optional preview characters"))) {
    const char* roles[]={ui("Rival","Opponent"),ui("Jugador","Player"),ui("Acompañante","Companion")};
    for(int role=0;role<3;++role) {
        ImGui::PushID(role); int& chosen=app.stageActors[role];
        ImGui::TextUnformatted(roles[role]); ImGui::SetNextItemWidth(-1);
        const char* name=chosen>=0 && (size_t)chosen<app.catalog.characters.size()?app.catalog.characters[chosen].imported.character.id.c_str():ui("Ninguno","None");
        int before=chosen;if(ImGui::BeginCombo("##stage-actor",name)) { if(ImGui::Selectable(ui("Ninguno","None"),chosen<0)) chosen=-1; for(int i=0;i<(int)app.catalog.characters.size();++i) if(ImGui::Selectable(app.catalog.characters[i].imported.character.id.c_str(),chosen==i)) chosen=i; ImGui::EndCombo(); }if(before!=chosen)app.stageAnimator.reset(app.catalog.stages[app.stageIndex].imported.stage);
        ImGui::PopID();
    }
    ImGui::TextWrapped("%s",ui("Esta selección solo cambia la vista del escenario.","These selections only affect the stage preview."));
    }ImGui::Separator();
    auto& source=app.catalog.stages[app.stageIndex]; auto& stage=source.imported.stage;
    ImGui::SetNextItemWidth(-1); fullInputText("ID",app.targetId.data(),app.targetId.size());
    ImGui::TextDisabled("%zu %s",stage.objects.size(),ui("objetos","objects"));
    if(ImGui::CollapsingHeader(ui("Reloj y bucle","Clock and loop"))) {
        if(fullInputFloat(ui("Duración del bucle","Loop duration"),&app.stageDuration,1))app.stageDuration=std::clamp(app.stageDuration,1.0f,300.0f);
        if(fullInputFloat("BPM",&app.stageBpm,1))app.stageBpm=std::clamp(app.stageBpm,1.0f,500.0f);
    }
    if(stage.objects.empty()) return; app.stageObject=std::clamp(app.stageObject,0,(int)stage.objects.size()-1);
    fieldLabel(ui("Objeto","Object"));
    if(ImGui::BeginCombo("##stage-object",stage.objects[app.stageObject].name.c_str())) {
        for(int i=0;i<(int)stage.objects.size();++i) if(ImGui::Selectable(stage.objects[i].name.c_str(),i==app.stageObject)) app.stageObject=i;
        ImGui::EndCombo();
    }
    auto& object=stage.objects[app.stageObject];
    ImGui::TextDisabled("X %.0f · Y %.0f · %s %.2f / %.2f",object.position.x,object.position.y,ui("escala","scale"),object.scale.x,object.scale.y);
    if(object.kind==fml::StageObject::Kind::Sprite) {
        fileChoice(app,window,ui("Imagen","Image"),object.resolvedImage,Action::StageImage);
        fileChoice(app,window,"Atlas XML / Animate",object.resolvedAtlas,Action::StageAtlas);
        if(!object.anims.empty()) {
            fieldLabel(ui("Animación","Animation"));
            if(ImGui::BeginCombo("##stage-animation",object.anims[std::max(0,app.stageAnimator.animIndexOf(app.stageObject,object))%object.anims.size()].name.c_str())) {
                for(int i=0;i<(int)object.anims.size();++i) if(ImGui::Selectable(object.anims[i].name.c_str())) { app.stageAnimator.play(app.stageObject,i); app.clock=0; }
                ImGui::EndCombo();
            }
            if(ImGui::Button(ui("Automático por beat","Automatic beat animation"))) app.stageAnimator.release(app.stageObject);
        }
        for(const auto& p:source.imageChoices[app.stageObject]) ImGui::TextWrapped("%s",p.u8string().c_str());
        for(const auto& p:source.atlasChoices[app.stageObject]) ImGui::TextWrapped("%s",p.u8string().c_str());
    }
    ImGui::Spacing(); ImGui::TextWrapped("%s",ui("Vista de escenario de FML.","FML stage view."));
    if(ImGui::TreeNode(ui("Avisos del escenario","Stage warnings"))) { for(const auto& warning:source.warnings) ImGui::TextWrapped("%s",message(warning).c_str()); ImGui::TreePop(); }
}
void iconPreview(App& app) {
    ImGui::TextUnformatted(ui("Icono · estados de salud","Icon · health states")); ImGui::Separator(); if(app.icon<0) return;
    auto& icon=app.catalog.icons[app.icon]; auto& texture=app.textures.get(icon.source);
    if(!texture.id) { ImGui::TextWrapped("%s",message(texture.error).c_str()); return; }
    auto frames=iconFrames(icon,texture.w,texture.h);const auto* sequence=app.iconState.empty()?frames.sequence(app.iconHealth):frames.animation(app.iconState);
    const auto* frame=sequence?sequence->sample(app.clock,&app.loopPreview):nullptr;
    auto stateLabel=[](const std::string& name)->std::string {if(name=="neutral")return ui("Normal","Normal");if(name=="losing")return ui("Derrota","Defeat");if(name=="winning")return ui("Victoria","Victory");return name;};
    auto at=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail(); size.y=std::max(100.0f,size.y-240);
    ImGui::InvisibleButton("##icon-canvas",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle); interactCanvas(app);
    ImGui::GetWindowDrawList()->PushClipRect(at,ImVec2(at.x+size.x,at.y+size.y),true);
    if(frame) {
        fml::PreviewBounds bounds; bounds.include(0,0); bounds.include(frame->frameW>0?frame->frameW:frame->w,frame->frameH>0?frame->frameH:frame->h);
        auto placement=fml::fitPreviewBounds(at,size,bounds,0,0,!frames.antialiasing,app.zoom,ImVec2(app.panX,app.panY));
        auto placed=fml::placePreviewFrame(*frame,placement,frames.facingLeft!=app.viewFlipX,app.viewFlipY,true,texture.w,texture.h);
        ImGui::GetWindowDrawList()->AddImageQuad(ImTextureRef((ImTextureID)texture.id),placed.p0,ImVec2(placed.p1.x,placed.p0.y),placed.p1,ImVec2(placed.p0.x,placed.p1.y),placed.uv[0],placed.uv[1],placed.uv[2],placed.uv[3]);
    }
    ImGui::GetWindowDrawList()->PopClipRect();
    ImGui::BeginChild("##icon-states",{0,115},ImGuiChildFlags_Borders);
    std::vector<std::string> states;
    for(const auto* state:{"neutral","losing","winning"})if(frames.animation(state))states.push_back(state);
    for(const auto& named:frames.animations) {
        if(named.first=="neutral" || named.first=="losing" || named.first=="winning" || named.first=="0" || named.first=="1" || named.first=="2" || named.first=="idle" || named.first=="normal")continue;
        if(!named.second.frames.empty())states.push_back(named.first);
    }
    for(size_t i=0;i<states.size();++i) {
        if(i)nextControl(94);ImGui::PushID(states[i].c_str());ImGui::BeginGroup();
        auto pos=ImGui::GetCursorScreenPos();auto seq=frames.animation(states[i]);auto sample=seq?seq->sample(app.clock):nullptr;
        if(ImGui::Selectable("##state",sequence && sequence->name==states[i],0,{85,72})){app.iconState=states[i];app.clock=0;}
        if(sample){fml::PreviewBounds bounds;bounds.include(0,0);bounds.include(sample->frameW>0?sample->frameW:fml::frameDrawW(*sample),sample->frameH>0?sample->frameH:fml::frameDrawH(*sample));auto place=fml::fitPreviewBounds(pos,{85,72},bounds,0,0,!frames.antialiasing,.85f);auto piece=fml::placePreviewFrame(*sample,place,frames.facingLeft,false,true,texture.w,texture.h);ImGui::GetWindowDrawList()->AddImageQuad(ImTextureRef((ImTextureID)texture.id),piece.p0,{piece.p1.x,piece.p0.y},piece.p1,{piece.p0.x,piece.p1.y},piece.uv[0],piece.uv[1],piece.uv[2],piece.uv[3]);}
        ImGui::TextUnformatted(stateLabel(states[i]).c_str());ImGui::EndGroup();ImGui::PopID();
    }ImGui::EndChild();
    animationControls(app,sequence?(int)sequence->frames.size():0,sequence?(int)sequence->fps:24,false);
}
void iconOptions(App& app) {
    ImGui::TextUnformatted(ui("Exportar icono","Export icon")); ImGui::Separator(); if(app.icon<0) return;
    const auto& icon=app.catalog.icons[app.icon]; ImGui::SetNextItemWidth(-1); fullInputText("ID",app.targetId.data(),app.targetId.size());
    ImGui::TextWrapped("%s",icon.source.u8string().c_str());
    ImGui::TextWrapped("%s",ui("El bundle conserva la tira completa, su atlas y los datos de salud, si existen.","The bundle preserves the full strip, atlas and health data, when available."));
    if(ImGui::Button(ui("Estado actual como PNG","Current state as PNG"))) { app.zipPlan=false; app.plan=iconPlan(app.catalog,icon,app.destination,app.targetId.data(),false,true,app.iconHealth,app.clock,app.iconState,&app.loopPreview); app.showPlan(true); }
    ImGui::Spacing();ImGui::Separator();ImGui::TextUnformatted(ui("Vista previa","Preview"));
    if(fullSliderFloat(ui("Salud · selección automática","Health · automatic selection"),&app.iconHealth,0,1,"%.2f"))app.iconState.clear();
    viewControls(app);
}
const char* looseKindName(LooseKind kind) {
    switch(kind) {
    case LooseKind::Character:return ui("Personaje","Character"); case LooseKind::Stage:return ui("Escenario","Stage"); case LooseKind::Chart:return "Chart";
    case LooseKind::Animate:return "Animate"; case LooseKind::Sparrow:return "Sparrow"; case LooseKind::Icon:return ui("Icono","Icon");
    case LooseKind::Image:return ui("Imagen","Image"); case LooseKind::Audio:return "Audio"; case LooseKind::Support:return ui("Complemento","Companion"); default:return ui("Sin reconocer","Unrecognized");
    }
}
void looseReview(App& app) {
    if(app.looseRequested) { ImGui::OpenPopup(uiId("Revisar archivos sueltos","Review loose files","loose-review").c_str()); app.looseRequested=false; }
    ImGui::SetNextWindowSize(ImVec2(1060,680),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),ImGuiCond_Appearing,ImVec2(.5f,.5f));
    if(!ImGui::BeginPopupModal(uiId("Revisar archivos sueltos","Review loose files","loose-review").c_str(),nullptr,ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::TextWrapped("%s",ui("Revisa los tipos y las asociaciones. Inst y Voices no crean una canción sin chart. Los archivos pendientes pueden escucharse o asociarse más tarde.","Review types and associations. Inst and Voices cannot create a song without a chart. Pending files can be auditioned or assigned later."));
    size_t pending=0; for(const auto& file:app.catalog.loose) if(!file.associated) ++pending;
    ImGui::Text("%zu %s · %zu %s",app.catalog.loose.size(),ui("archivos","files"),pending,ui("pendientes","pending"));
    if(!app.auditionFiles.empty()) {
        if(ImGui::Button(ui("Escuchar audios marcados juntos","Listen to checked audio together"))) { std::vector<std::string> paths; for(const auto& path:app.auditionFiles) paths.push_back(path.u8string()); app.audio.pause(); app.audio.loadTracksAsync(paths); }
        ImGui::SameLine(); if(ImGui::Button(ui("Exportar grupo de audio...","Export audio group..."))) {
            Plan p; p.destination=app.destination; p.label=app.bundleName.data();
            if(!validId(p.label)) p.errors.push_back("ID de canción inválido.");
            for(const auto& path:app.auditionFiles) { auto probe=fml::probeAudioFile(path); if(!probe.readable || probe.container!=fml::AudioContainer::OggVorbis) p.errors.push_back("Audio OGG Vorbis no válido: "+path.u8string()); else addCopy(p,"audio/"+p.label+"/"+path.filename().u8string(),path); }
            p.warnings.push_back("Solo audio: este grupo no contiene chart ni metadatos de canción; tendrás que asociarlo posteriormente."); destinationCheck(p,app.catalog); app.plan=std::move(p); app.zipPlan=false; app.showPlan(true); ImGui::CloseCurrentPopup();
        }
    }
    ImGui::BeginChild("##loose-table",ImVec2(0,260),ImGuiChildFlags_Borders);
    if(ImGui::BeginTable("##file-groups",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(ui("Archivo","File"),0,2); ImGui::TableSetupColumn(ui("Tipo","Type")); ImGui::TableSetupColumn(ui("Grupo / asociación","Group / association"),0,2); ImGui::TableSetupColumn(ui("Estado","Status")); ImGui::TableHeadersRow();
        for(int i=0;i<(int)app.catalog.loose.size();++i) {
            const auto& file=app.catalog.loose[i]; ImGui::PushID(i); ImGui::TableNextRow(); ImGui::TableNextColumn();
            if(file.kind==LooseKind::Audio) { bool take=app.auditionFiles.count(file.path)!=0; if(ImGui::Checkbox("##audition",&take)) { if(take) app.auditionFiles.insert(file.path); else app.auditionFiles.erase(file.path); } ImGui::SameLine(); }
            if(ImGui::Selectable(file.path.filename().u8string().c_str(),app.looseIndex==i)) { app.looseIndex=i; app.bindRole=file.role=="Inst"?0:file.role=="Player"?2:file.role=="Opponent"?3:1; }
            tooltip(file.path.u8string()); ImGui::TableNextColumn(); ImGui::TextUnformatted(looseKindName(file.kind)); ImGui::TableNextColumn(); ImGui::TextWrapped("%s",file.group.empty()?ui("Por elegir","Choose a group"):file.group.c_str()); ImGui::TableNextColumn(); ImGui::TextUnformatted(file.associated?ui("Resuelto","Resolved"):ui("Revisar","Review")); ImGui::PopID();
        } ImGui::EndTable();
    } ImGui::EndChild();
    if(!app.catalog.loose.empty()) {
        app.looseIndex=std::clamp(app.looseIndex,0,(int)app.catalog.loose.size()-1); auto file=app.catalog.loose[app.looseIndex];
        ImGui::TextWrapped("%s",file.path.u8string().c_str()); ImGui::TextWrapped("%s",message(file.reason).c_str());
        auto combo=[&](const char* label,int& index,int count,const auto& name) {
            if(!count) { index=0; return; } index=std::clamp(index,0,count-1); ImGui::SetNextItemWidth(280);
            if(ImGui::BeginCombo(label,name(index).c_str())) { for(int i=0;i<count;++i) if(ImGui::Selectable(name(i).c_str(),index==i)) index=i; ImGui::EndCombo(); }
        };
        if(file.kind==LooseKind::Audio) {
            if(ImGui::Button(ui("Escuchar este archivo","Listen to this file"))) { app.audio.pause(); app.audio.loadTracksAsync({file.path.u8string()}); }
            ImGui::SameLine(); if(ImGui::Button(app.audio.playing()?ui("Pausa","Pause"):ui("Reproducir","Play"))) { if(app.audio.playing()) app.audio.pause(); else app.audio.play(); }
            if(app.catalog.songs.empty()) ImGui::TextWrapped("%s",ui("Carga un chart para elegir canción y dificultad. La escucha de audio ya está disponible.","Load a chart to select a song and difficulty. Audio audition is available now."));
            else {
                combo(ui("¿A qué canción pertenece?","Which song does it belong to?"),app.bindSong,(int)app.catalog.songs.size(),[&](int i){return app.catalog.songs[i].id+" · "+app.catalog.songs[i].difficulties.front().path.parent_path().filename().u8string();});
                auto& song=app.catalog.songs[app.bindSong]; combo(ui("Dificultad","Difficulty"),app.bindDifficulty,(int)song.difficulties.size(),[&](int i){return song.difficulties[i].name;});
                const char* roles[]={"Inst","Voices",ui("Voces jugador","Player voices"),ui("Voces rival","Opponent voices")};
                combo(ui("Función del audio","Audio role"),app.bindRole,4,[&](int i){return std::string(roles[i]);});
                static bool replace=false; wrappedCheckbox(ui("Reemplazar audio ya asignado","Replace existing audio assignment"),&replace);
                auto& a=song.difficulties[app.bindDifficulty].audio; std::string* role=app.bindRole==0?&a.instPath:app.bindRole==1?&a.voicesPath:app.bindRole==2?&a.playerVoicesPath:&a.opponentVoicesPath;
                bool conflict=!role->empty() && !sameFile(fs::u8path(*role),file.path); if(app.bindRole==1) conflict|=!a.playerVoicesPath.empty() || !a.opponentVoicesPath.empty(); if(app.bindRole>=2) conflict|=!a.voicesPath.empty();
                ImGui::BeginDisabled(conflict && !replace);
                if(ImGui::Button(ui("Asociar a esta dificultad","Assign to this difficulty"))) { *role=file.path.u8string();song.difficulties[app.bindDifficulty].manualAudio=true; if(app.bindRole==1) {a.playerVoicesPath.clear();a.opponentVoicesPath.clear();} if(app.bindRole>=2) a.voicesPath.clear(); replace=false; refreshLoose(app.catalog); if(app.song==app.bindSong && app.difficulty==app.bindDifficulty) app.loadAudio(); }
                ImGui::EndDisabled();
            }
        } else if(file.kind==LooseKind::Image || file.kind==LooseKind::Sparrow || file.kind==LooseKind::Animate || file.kind==LooseKind::Icon) {
            const char* targets[]={ui("Personaje","Character"),ui("Objeto de escenario","Stage object"),ui("Icono independiente","Independent icon")};
            combo(ui("Usar como recurso de","Use as a resource for"),app.bindTarget,3,[&](int i){return std::string(targets[i]);});
            bool png=lower(file.path.extension().u8string())==".png";
            if(app.bindTarget==0 && !app.catalog.characters.empty()) {
                combo(ui("Personaje","Character"),app.bindCharacter,(int)app.catalog.characters.size(),[&](int i){return app.catalog.characters[i].imported.character.id;});
                if(ImGui::Button(ui("Asociar recurso","Assign resource"))) {
                    auto& c=app.catalog.characters[app.bindCharacter]; if(png) c.image=file.path; else { c.atlas=file.path; c.animate=lower(file.path.filename().u8string())=="animation.json"; if(c.animate) { c.animateData=animateResource(file.path); if(!c.animateData->images.empty()) c.image=c.animateData->images.front(); for(auto& anim:c.imported.character.anims) anim.resolvedFrames=c.animateData->atlas.frameCount(anim.atlasPrefix); } else { fml::DiagnosticSink sink; fml::countPsychAnimationFrames(c.imported.character,read(file.path),file.path.u8string(),sink); } for(size_t i=0;i<c.take.size();++i) c.take[i]=c.imported.character.anims[i].resolvedFrames>0; }
                    c.manualBinding=true;c.imported.character.resolvedImage=c.image.generic_u8string(); c.imported.character.resolvedAtlas=c.atlas.generic_u8string(); app.textures.clear(); app.previewKey.clear(); refreshLoose(app.catalog);
                }
            } else if(app.bindTarget==1 && !app.catalog.stages.empty()) {
                combo(ui("Escenario","Stage"),app.bindStage,(int)app.catalog.stages.size(),[&](int i){return app.catalog.stages[i].imported.stage.id;}); auto& s=app.catalog.stages[app.bindStage];
                combo(ui("Objeto","Object"),app.bindObject,(int)s.imported.stage.objects.size(),[&](int i){return s.imported.stage.objects[i].name;});
                if(!s.imported.stage.objects.empty() && ImGui::Button(ui("Asociar recurso","Assign resource"))) { s.manualObjects.insert(app.bindObject);auto& o=s.imported.stage.objects[app.bindObject]; if(png) o.resolvedImage=file.path.generic_u8string(); else { o.resolvedAtlas=file.path.generic_u8string(); s.animateData.erase(app.bindObject); if(lower(file.path.filename().u8string())=="animation.json") { auto resource=animateResource(file.path); s.animateData[app.bindObject]=resource; if(!resource->images.empty()) o.resolvedImage=resource->images.front().generic_u8string(); } } app.atlases.invalidate(o.resolvedAtlas); app.textures.clear(); app.stageAnimator.reset(s.imported.stage); refreshLoose(app.catalog); }
            } else if(app.bindTarget==2 && png) {
                if(ImGui::Button(ui("Añadir como icono","Add as icon"))) { bool exists=false; for(const auto& i:app.catalog.icons) if(sameFile(i.source,file.path)) exists=true; if(!exists) { Icon i; i.id=file.path.stem().u8string(); i.source=file.path;i.manual=true; app.catalog.icons.push_back(i); app.icon=(int)app.catalog.icons.size()-1; } refreshLoose(app.catalog); }
            }
        }
    }
    for(size_t a=0;a<app.catalog.songs.size();++a) for(size_t b=a+1;b<app.catalog.songs.size();++b) if(lower(app.catalog.songs[a].id)==lower(app.catalog.songs[b].id)) {
        ImGui::PushID((int)a*100000+(int)b); ImGui::TextWrapped(ui("¿Son dificultades de la misma canción? %s","Are these difficulties of the same song? %s"),app.catalog.songs[a].id.c_str());
        if(ImGui::Button(ui("Unir grupos de charts","Merge chart groups"))) { std::string error; if(mergeSongGroups(app.catalog,b,a,error)) { app.song=-1; app.selectSong((int)a);app.game.chooseChart(app.catalog,(int)a,0);app.loadGameAudio(); app.batch.songs.clear(); app.chartSelection.clear(); } else app.status=error; }
        ImGui::PopID();
    }
    ImGui::Separator(); ImGui::TextWrapped("%s",message(app.status).c_str());
    if(ImGui::Button(ui("Continuar con estos grupos","Continue with these groups"))) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
}
#include "GameplayUi.hpp"
#include "ResourcesUi.hpp"
void style() {
    ImGui::StyleColorsDark(); auto& s=ImGui::GetStyle();
    s.WindowPadding=ImVec2(18,16); s.FramePadding=ImVec2(9,7); s.ItemSpacing=ImVec2(9,8);
    s.WindowRounding=0; s.ChildRounding=4; s.FrameRounding=4; s.PopupRounding=4; s.ChildBorderSize=1;
    s.Colors[ImGuiCol_WindowBg]=ImVec4(.07f,.095f,.12f,1); s.Colors[ImGuiCol_ChildBg]=ImVec4(.095f,.125f,.155f,1);
    s.Colors[ImGuiCol_PopupBg]=ImVec4(.075f,.105f,.14f,1); s.Colors[ImGuiCol_ModalWindowDimBg]=ImVec4(.015f,.025f,.04f,.78f);
    s.Colors[ImGuiCol_FrameBg]=ImVec4(.065f,.09f,.12f,1); s.Colors[ImGuiCol_Border]=ImVec4(.23f,.29f,.35f,1);
    s.Colors[ImGuiCol_Button]=ImVec4(.15f,.20f,.25f,1); s.Colors[ImGuiCol_ButtonHovered]=ImVec4(.2f,.3f,.4f,1);
    s.Colors[ImGuiCol_Header]=ImVec4(.08f,.31f,.57f,1); s.Colors[ImGuiCol_HeaderHovered]=ImVec4(.1f,.38f,.68f,1);
    s.Colors[ImGuiCol_CheckMark]=ImVec4(.24f,.58f,1,1); s.Colors[ImGuiCol_Text]=ImVec4(.91f,.94f,.97f,1);
}
void draw(App& app,SDL_Window* window) {
    auto* viewport=ImGui::GetMainViewport(); ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Psych Importer",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    app.deps=batchDependencies(app.catalog,app.batch); soloAudio(app);
    if(app.status!=app.loggedStatus) { app.loggedStatus=app.status; logs::info("Estado: "+app.status); }
    toolbar(app,window);
    ImGui::Spacing();
    const char* tabNames[]={ui("Personajes","Characters"),ui("Canciones","Songs"),ui("Escenarios","Stages"),ui("Iconos","Icons"),"Charts","Preview",ui("Recursos","Resources")};
    for(int t=0;t<7;++t) {
        if(t) ImGui::SameLine(); bool activeTab=app.tab==t; if(activeTab) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.08f,.40f,.85f,1));
        static const unsigned tabIcons[]={0xE77B,0xE8D6,0xE91B,0xE76E,0xE8A9,0xE768,0xE8F1};
        if(ImGui::Button((iconLabel(tabIcons[t],tabNames[t])+"###tab-"+std::to_string(t)).c_str(),ImVec2(130,38)) && app.tab!=t) {
            // One playback across Songs, Charts and Preview: the song keeps
            // its place and keeps playing when the tab changes. Songs and
            // Charts share app.audio; Preview has gameAudio, so the position
            // is handed over.
            const bool toPreview=t==5 && app.song>=0 && app.audio.playing();
            const bool fromPreview=app.tab==5 && (t==1 || t==4) && app.game.song>=0 && app.gameAudio.playing();
            const double handoffMs=toPreview?app.audio.positionMs():fromPreview?app.gameAudio.positionMs():0;
            if(fromPreview) { app.song=app.game.song; app.difficulty=app.game.difficulty; app.gameAudio.pause(); app.game.playing=false; }
            // The preview follows the song chosen in Songs/Charts; the one it
            // already has keeps playing.
            const bool sameSong=app.game.song==app.song && app.game.difficulty==app.difficulty;
            if((app.tab==4 || app.tab==1) && t==5 && app.song>=0 && !sameSong) {app.game.chooseChart(app.catalog,app.song,app.difficulty,app.game.keepComposition);app.loadGameAudio();}
            else if(t==5 && !app.gameAudio.trackCount() && !app.gameAudio.trackLoadPending())app.loadGameAudio();
            app.tab=t; app.filter[0]=0; if(app.selectedIndex()>=0) app.select(app.selectedIndex());
            if(t==6) app.resourcesDirty=true;
            if(toPreview && app.game.song==app.song && app.game.difficulty==app.difficulty) {
                app.audio.pause();
                if(app.gameAudio.trackCount() && !app.gameAudio.trackLoadPending()) { seekPreview(app,handoffMs); app.game.playing=true; app.gameAudio.play(); }
                else { app.previewStartMs=handoffMs; app.previewPlayAfterLoad=true; }
            }
            if(fromPreview) {
                app.loadAudio();   // nothing to do when it already holds this song
                if(app.audio.trackCount() && !app.audio.trackLoadPending()) { app.audio.seekMs(handoffMs); app.audio.play(); }
                else { app.audioStartMs=handoffMs; app.audioPlayAfterLoad=true; }
            }
        }
        auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax(); app.tabCenters[t]=ImVec2((a.x+b.x)/2,(a.y+b.y)/2);
        if(activeTab) ImGui::PopStyleColor();
    }
    ImGui::Separator();
    app.markAllVisible=false;
    if(app.tab!=5) selectionBar(app);
    if(app.tab==4 || app.tab==5) {
        ImGui::TextColored(ImVec4(.45f,.74f,1,1),"%s",app.tab==4 && app.mini.readOnly?ui("CODENAME · chart que se exportará · solo inspección","CODENAME · chart used for export · inspection only"):ui("CODENAME · chart que se exportará","CODENAME · chart used for export"));
        ImGui::TextWrapped("%s",ui("Si no incluyes personajes o stage, el juego deberá tener sus claves.","If characters or stage are excluded, the game must provide their keys."));
    }
    float height=std::max(200.0f,ImGui::GetContentRegionAvail().y-(app.tab==5?54:90)); float total=ImGui::GetContentRegionAvail().x;
    float left=std::max(180.0f,total*(app.tab==4?.16f:app.tab==5?.18f:app.tab==6?.25f:.23f)),right=std::max(265.0f,total*(app.tab==4?.28f:app.tab==5?.21f:app.tab==6?.30f:.30f)),middle=total-left-right-18;
    ImGui::BeginDisabled(app.busy || app.writeBusy || app.dialogBusy);
    {PI_PROFILE("panel.list");ImGui::BeginChild("list",ImVec2(left,height),ImGuiChildFlags_Borders); if(app.tab==5)previewSongList(app);else if(app.tab==6)resourcesList(app);else list(app); ImGui::EndChild();} ImGui::SameLine();
    {PI_PROFILE("panel.details");ImGui::BeginChild("details",ImVec2(middle,height),ImGuiChildFlags_Borders); if(app.tab==0) characterPreview(app); else if(app.tab==1) songDetails(app,window); else if(app.tab==2) stagePreview(app); else if(app.tab==3) iconPreview(app); else if(app.tab==5)previewViewport(app);else if(app.tab==6)resourcesDetails(app);else if(app.song>=0) app.mini.grid(app.catalog.songs[app.song].difficulties[app.difficulty],app.audio); ImGui::EndChild();} ImGui::SameLine();
    PI_PROFILE("panel.options+footer");ImGui::BeginChild("options",ImVec2(0,height),ImGuiChildFlags_Borders);ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(9,5)); if(app.tab==0) characterOptions(app,window); else if(app.tab==1) songOptions(app,window); else if(app.tab==2) stageOptions(app,window); else if(app.tab==3) iconOptions(app); else if(app.tab==5)previewCompatibility(app,window);else if(app.tab==6)resourcesReadiness(app);else if(app.song>=0) {
        ImGui::TextUnformatted(ui("Exportación de chart","Chart export"));ImGui::SetNextItemWidth(-1);fullInputText("ID",app.targetId.data(),app.targetId.size());
        wrappedCheckbox(ui("Incluir personajes declarados","Include declared characters"),&app.chartCharacters);
        wrappedCheckbox(ui("Incluir escenario declarado","Include declared stage"),&app.chartStages);
        if(!app.chartCharacters || !app.chartStages)ImGui::TextWrapped("%s",ui("Las claves pueden apuntar a recursos que ya existan en el juego.","Keys may refer to resources already present in the game."));
        app.mini.properties(app.catalog.songs[app.song].difficulties[app.difficulty],app.catalog);
    } ImGui::PopStyleVar();ImGui::EndChild();
    if(app.tab<5) {
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),total-360));
    if(ImGui::Button(ui("Ver informe","View report"),ImVec2(130,36))) app.review(false); ImGui::SameLine();
    ImGui::BeginDisabled(app.selectedIndex()<0 || app.destination.empty());
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.08f,.40f,.85f,1));
    const char* importNames[]={ui("Importar personaje","Import character"),ui("Importar canción","Import song"),ui("Importar escenario","Import stage"),ui("Exportar icono","Export icon"),ui("Exportar chart","Export chart")};
    if(ImGui::Button(importNames[app.tab],ImVec2(190,36))) app.review(true);
    ImGui::PopStyleColor(); ImGui::EndDisabled();
    } else if(app.tab==5) ImGui::TextDisabled("%s",ui("Autoplay · audio de la canción y dificultad seleccionadas","Autoplay · audio from the selected song and difficulty"));
    ImGui::EndDisabled();
    if(app.batchRequested) { ImGui::OpenPopup(uiId("Exportación múltiple","Batch export","batch-export").c_str()); app.batchRequested=false; }
    ImGui::SetNextWindowSize({std::min(720.0f,viewport->WorkSize.x-64),std::min(630.0f,viewport->WorkSize.y-64)},ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),ImGuiCond_Appearing,ImVec2(.5f,.5f));
    if(ImGui::BeginPopupModal(uiId("Exportación múltiple","Batch export","batch-export").c_str(),nullptr,ImGuiWindowFlags_NoSavedSettings)) {
        {
            auto counted=[](size_t n,size_t extra,const char* label,const char* from) { return std::to_string(n)+" "+label+(extra?" (+"+std::to_string(extra)+" "+from+")":""); };
            const char* fromSongs=ui("de las canciones","from the songs");
            ImGui::TextWrapped("%s · %s · %s · %s",counted(app.batch.songs.size(),app.deps.songs.size(),ui("canciones","songs"),ui("por sus personajes","through their characters")).c_str(),
                counted(app.batch.characters.size(),app.deps.characters.size(),ui("personajes","characters"),fromSongs).c_str(),
                counted(app.batch.stages.size(),app.deps.stages.size(),ui("escenarios","stages"),fromSongs).c_str(),counted(app.batch.icons.size(),0,ui("iconos","icons"),"").c_str());
            if(!app.batch.resources.empty()) ImGui::TextWrapped(ui("%zu recursos sueltos (música, efectos, imágenes) a images/, music/ y sounds/ del bundle.","%zu loose resources (music, effects, images) into the bundle's images/, music/ and sounds/."),app.batch.resources.size());
            if(!app.selectionCount()) {
                ImGui::TextColored(ImVec4(1,.78f,.35f,1),"%s",ui("Nada marcado todavía.","Nothing marked yet."));
                if(app.tab!=5) {ImGui::SameLine();if(ImGui::SmallButton(uiId("Marcar todo el mod","Mark the whole mod","batch-mark-all").c_str()))markWholeMod(app);}
            }
        }
        if(!app.chartSelection.empty()) ImGui::Text("%zu charts",app.chartSelection.size());
        fullInputText(uiId("Nombre del bundle","Bundle name","bundle-name").c_str(),app.bundleName.data(),app.bundleName.size());
        wrappedCheckbox(uiId("Añadir canciones que usan los personajes seleccionados","Add songs using the selected characters","related-songs").c_str(),&app.batch.songsUsingCharacters);
        if(app.batch.songsUsingCharacters) ImGui::Text("%zu %s",relatedSongs(app.catalog,app.batch.characters).size(),ui("canciones relacionadas","related songs"));
        // Songs: with what they use, or alone; alone, what to do with what the
        // chart names and the game lacks.
        ImGui::Spacing(); ImGui::TextUnformatted(ui("Canciones","Songs"));
        const bool songsAlone=!app.batch.bringCharacters && !app.batch.bringStages;
        if(ImGui::RadioButton(uiId("Con sus personajes y escenarios","With their characters and stages","songs-full").c_str(),app.batch.bringCharacters && app.batch.bringStages)) { app.batch.bringCharacters=app.batch.bringStages=true; }
        if(ImGui::RadioButton(uiId("Solo canción y chart","Song and chart only","songs-alone").c_str(),songsAlone)) { if(!songsAlone) app.batch.fitChartToGame=true; app.batch.bringCharacters=app.batch.bringStages=false; }
        if(!app.batch.bringCharacters && !app.batch.bringStages) {
            const fs::path gameBase=app.base.empty()?codenameAssetsBeside(app.destination):app.base;
            ImGui::Indent();
            ImGui::TextWrapped("%s",ui("El chart nombra personajes y un escenario. Si el juego no los tiene, ¿qué hago?","The chart names characters and a stage. If the game lacks them, what should I do?"));
            if(ImGui::RadioButton(uiId("Dejar el chart como está (Codename pondrá a bf o irá sin decorado)","Leave the chart as it is (Codename uses bf or no scenery)","fit-leave").c_str(),!app.batch.fitChartToGame)) app.batch.fitChartToGame=false;
            ImGui::BeginDisabled(gameBase.empty());
            if(ImGui::RadioButton(uiId("Cambiar en el chart lo que falte por lo que trae Codename (dad, bf, gf, stage)","Swap in the chart what is missing for Codename's own (dad, bf, gf, stage)","fit-swap").c_str(),app.batch.fitChartToGame && !gameBase.empty())) app.batch.fitChartToGame=true;
            ImGui::EndDisabled();
            if(gameBase.empty()) ImGui::TextDisabled("%s",ui("Para saber qué tiene el juego, elige la base de Codename o exporta a su carpeta mods.","To know what the game has, choose Codename as the base or export into its mods folder."));
            ImGui::Unindent();
        }
        wrappedCheckbox(uiId("Convertir modcharts (shaders, receptores, cámara) a scripts de Codename","Convert modcharts (shaders, receptors, camera) to Codename scripts","batch-modcharts").c_str(),&app.batch.modcharts);
        if(!app.chartSelection.empty()) {
            wrappedCheckbox(ui("Incluir personajes declarados en charts","Include chart characters"),&app.batch.chartCharacters);
            wrappedCheckbox(ui("Incluir escenarios declarados en charts","Include chart stages"),&app.batch.chartStages);
        }
        ImGui::Separator();
        if(ImGui::RadioButton(uiId("Una carpeta general","One general folder","layout-general").c_str(),!app.batch.separate)) app.batch.separate=false;
        if(ImGui::RadioButton(uiId("Una carpeta por bundle","One folder per bundle","layout-separate").c_str(),app.batch.separate)) app.batch.separate=true;
        ImGui::TextWrapped("%s",ui("En ZIP, una carpeta general va en la raíz del ZIP y Codename la carga tal cual desde mods. Las carpetas por bundle hay que descomprimirlas antes.","As a ZIP, one general folder sits at the ZIP's root and Codename loads it as it is from mods. Per-bundle folders must be unpacked first."));
        ImGui::BeginDisabled(!app.selectionCount() || app.destination.empty());
        if(ImGui::Button(ui("Preparar carpetas","Prepare folders"))) { app.prepareBatch(false); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if(ImGui::Button(ui("Preparar ZIP","Prepare ZIP"))) { app.prepareBatch(true); ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled(); ImGui::SameLine(); if(ImGui::Button(ui("Volver","Back"))) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
    }
    looseReview(app);
    if(app.reviewRequested) { ImGui::OpenPopup(uiId("Revisar importación","Review import","review-import").c_str()); app.reviewRequested=false; }
    app.reviewVisible=false;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),ImGuiCond_Appearing,ImVec2(.5f,.5f));
    if(ImGui::BeginPopupModal(uiId("Revisar importación","Review import","review-import").c_str(),nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        app.reviewVisible=true;
        if(app.zipPlan) ImGui::TextWrapped("ZIP: %s",(app.destination/fs::u8path(app.plan.label+".zip")).u8string().c_str());
        const bool blocked=!app.plan.ready(),salvage=blocked && app.partial.ready();
        ImGui::TextWrapped("%s",!blocked?ui("Revisa los archivos antes de escribir en el destino.","Review the files before writing to the destination."):
            salvage?ui("Hay partes que no se pueden exportar. Puedes resolverlas o exportar el resto sin ellas.","Some parts cannot be exported. Resolve them or export the rest without them."):
            ui("Resuelve estas dependencias antes de importar.","Resolve these dependencies before importing."));
        if(ImGui::Checkbox(uiId("Mostrar notas informativas","Show informational notes","report-info").c_str(),&app.reportInfo)) app.report=describe(app.plan,app.reportInfo);
        tooltip(ui("Archivos de referencia y conversiones realizadas. Los avisos sobre elementos ausentes o comportamiento diferente siempre se muestran.","Reference files and completed conversions. Warnings about missing or changed behavior are always shown."));
        ImGui::BeginChild("##plan",ImVec2(760,salvage?300.0f:420.0f),ImGuiChildFlags_Borders); ImGui::TextWrapped("%s",app.localizedReport().c_str()); ImGui::EndChild();
        // A partial export: everything that is sound, with what stays out
        // listed before writing.
        if(salvage) {
            ImGui::Text(ui("Quedará fuera (%zu):","Left out (%zu):"),app.partialOmitted.size());
            ImGui::BeginChild("##omitted",ImVec2(760,110),ImGuiChildFlags_Borders);
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.95f,.78f,.35f,1));
            for(const auto& line:app.partialOmitted) ImGui::TextWrapped("%s",message(line).c_str());
            ImGui::PopStyleColor(); ImGui::EndChild();
        }
        auto write=[&](const Plan& plan) {
            app.writeBusy=true; bool zip=app.zipPlan; app.writing=std::async(std::launch::async,[plan,zip]{return zip?executeZip(plan):execute(plan);}); app.status="Escribiendo importación..."; ImGui::CloseCurrentPopup();
        };
        ImGui::BeginDisabled(blocked); if(ImGui::Button(ui("Importar ahora","Import now"))) write(app.plan); ImGui::EndDisabled();
        if(salvage) {
            ImGui::SameLine();
            auto label=std::string(ui("Exportar sin lo bloqueado","Export without the blocked parts"))+" ("+std::to_string(app.partial.files.size())+" "+ui("archivos","files")+")###export-partial";
            if(ImGui::Button(label.c_str())) { logs::info("Exportación a medias: "+app.partial.label); logs::list("INFO","Queda fuera",app.partialOmitted); write(app.partial); }
            tooltip(ui("Escribe todo lo que sí se puede; lo de la lista de arriba queda fuera y figura en el informe.","Writes everything that can be written; the list above stays out and is recorded in the report."));
        }
        ImGui::SameLine(); if(ImGui::Button(ui("Volver","Back"))) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
    }
    ImGui::Separator(); ImGui::TextDisabled("Psych → Codename"); ImGui::SameLine(); ImGui::TextUnformatted(message(app.status).c_str()); tooltip(message(app.status));
    ImGui::End();
    if(app.reportOpen) {
        ImGui::SetNextWindowSize(ImVec2(850,550),ImGuiCond_FirstUseEver);
        if(ImGui::Begin(uiId("Informe","Report","import-report").c_str(),&app.reportOpen)) {
            // Read only: ImGui does not write into the buffer, so no copy per frame.
            const auto& localized=app.localizedReport();
            ImGui::InputTextMultiline("##report",const_cast<char*>(localized.c_str()),localized.size()+1,ImVec2(-1,-1),ImGuiInputTextFlags_ReadOnly);
        } ImGui::End();
    }
}
bool capture(const fs::path& path,int width,int height) {
    std::vector<unsigned char> pixels((size_t)width*height*4); glPixelStorei(GL_PACK_ALIGNMENT,1); glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    stbi_flip_vertically_on_write(1); return stbi_write_png(path.u8string().c_str(),width,height,4,pixels.data(),width*4)!=0;
}
json previewDiagnostics(App& app) {
    auto& g=app.game;fml::RenderList scene;fml::buildStageRenderList(g.scene,g.bindings(),app.atlases,app.textures,scene,&g.animator);
    size_t actors=0,scenery=0;for(const auto& cmd:scene.cmds)if(cmd.visible && cmd.alpha>0 && cmd.objectIndex>=0 && (size_t)cmd.objectIndex<g.scene.objects.size()) {
        auto kind=g.scene.objects[cmd.objectIndex].kind;if(kind==fml::StageObject::Kind::Character)++actors;else ++scenery;
    }
    std::vector<std::string> absent;for(const auto& path:scene.textures)if(!app.textures.renderer.previewImage(path).ok)absent.push_back(path);
    fml::RenderList notes;std::vector<fml::StrumReceptor> receptors;fml::GameplayOptions opts;opts.downscroll=g.downscroll;
    if(g.hud.preparedNotes)app.atlases.putSparrow(g.hud.notes.atlas,g.hud.preparedNotes->atlas);
    if(g.hud.notesValid){std::vector<fml::NoteSkin> skins(g.chart.strumLines.size(),g.hud.notes);fml::appendReceptorRenderList(g.chart,skins,app.atlases,opts,notes,&receptors);fml::appendNoteRenderList(g.chart,skins,app.atlases,opts,g.positionMs,receptors,notes,nullptr,nullptr,&g.state);}
    auto& renderer=app.textures.renderer;
    return {{"song",g.song>=0?app.catalog.songs[g.song].id:""},{"scene",{{"actors",actors},{"scenery",scenery},{"missingTextures",absent}}},
        {"notes",{{"valid",g.hud.notesValid},{"style",g.hud.selectedStyle},{"commands",notes.cmds.size()},{"receptors",receptors.size()},{"foundStyles",app.catalog.noteStyles.size()}}},
        {"cache",{{"bytes",renderer.vramBytes()},{"budget",renderer.vramBudget()},{"evicted",renderer.texturesEvicted()},{"downscaled",renderer.texturesDownscaled()}}},
        {"hud",{{"available",PreviewHudAvailable},{"enabled",g.showHud},{"fontDetected",!g.hud.font.empty()},{"fontLoaded",app.hudFonts.count(g.hud.font.u8string()) && app.hudFonts[g.hud.font.u8string()]!=nullptr},{"warnings",g.hud.diagnostics}}}};
}
}
// The last line of the log when the program dies: the exception and where,
// as an offset into the executable.
LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    const auto base=(unsigned long long)(uintptr_t)GetModuleHandleW(nullptr),at=(unsigned long long)(uintptr_t)info->ExceptionRecord->ExceptionAddress;
    char text[240]; std::snprintf(text,sizeof text,"Cierre inesperado: excepción 0x%08lX en 0x%llX (exe+0x%llX)",(unsigned long)info->ExceptionRecord->ExceptionCode,at,at-base);
    pi::logs::fatal(text); return EXCEPTION_CONTINUE_SEARCH;
}
// The program has a window, not a console: options that print text write to
// the console it was started from (cmd, PowerShell), unless redirected.
static void consoleForText() {
    auto valid=[](DWORD id){ auto h=GetStdHandle(id); return h && h!=INVALID_HANDLE_VALUE && GetFileType(h)!=FILE_TYPE_UNKNOWN; };
    const bool out=valid(STD_OUTPUT_HANDLE),err=valid(STD_ERROR_HANDLE);
    if((out && err) || !AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* f=nullptr; if(!out) freopen_s(&f,"CONOUT$","w",stdout); if(!err) freopen_s(&f,"CONOUT$","w",stderr);
}
int main(int argc,char** argv) {
    fs::path root,capturePath,inventoryPath,destination,settingsPath,diagnosticPath;std::vector<fs::path> files;int initialTab=0,width=1360,height=860;std::string selection,show,lang,noteStyle,iconState;bool checkTabs=false,checkLanguage=false,checkView=false,checkResources=false,checkMiddle=false;double previewTime=-1;float rankingScale=1;
    std::vector<std::string> args;
    int wideCount=0; auto wide=CommandLineToArgvW(GetCommandLineW(),&wideCount);
    if(wide) {
        for(int i=1;i<wideCount;++i) {
            int size=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,nullptr,0,nullptr,nullptr);
            std::string value(size,'\0'); WideCharToMultiByte(CP_UTF8,0,wide[i],-1,value.data(),size,nullptr,nullptr); value.resize(size-1); args.push_back(std::move(value));
        } LocalFree(wide);
    } else for(int i=1;i<argc;++i) args.push_back(argv[i]);
    for(const auto& arg:args) if(arg=="--version" || arg.rfind("--modchart-dump=",0)==0) { consoleForText(); break; }
    for(const auto& arg:args) if(arg=="--version") { std::printf("Psych Importer %s\n",PSYCH_IMPORTER_VERSION); std::fflush(stdout); return 0; }
    for(const auto& arg:args) {
        auto eq=arg.find('='); auto value=eq==std::string::npos?"":arg.substr(eq+1);
        if(arg.rfind("--root=",0)==0) root=fs::u8path(value);
        if(arg.rfind("--file=",0)==0) files.push_back(fs::u8path(value));
        if(arg.rfind("--capture=",0)==0) capturePath=fs::u8path(value);
        if(arg.rfind("--diagnostics=",0)==0)diagnosticPath=fs::u8path(value);
        if(arg.rfind("--note-style=",0)==0)noteStyle=value;
        if(arg.rfind("--icon-state=",0)==0)iconState=value;
        if(arg.rfind("--ranking-scale=",0)==0)try{rankingScale=std::clamp(std::stof(value),.1f,2.0f);}catch(...){}
        if(arg.rfind("--size=",0)==0)try{auto split=value.find('x');width=std::max(1120,std::stoi(value.substr(0,split)));height=std::max(740,std::stoi(value.substr(split+1)));}catch(...){}
        if(arg.rfind("--inventory=",0)==0) inventoryPath=fs::u8path(value);
        if(arg.rfind("--destination=",0)==0) destination=fs::u8path(value);
        if(arg=="--tab=songs") initialTab=1;
        if(arg=="--tab=stages") initialTab=2;
        if(arg=="--tab=icons") initialTab=3;
        if(arg=="--tab=charts") initialTab=4;
        if(arg=="--tab=preview") initialTab=5;
        if(arg=="--tab=resources") initialTab=6;
        if(arg.rfind("--preview-time=",0)==0)try{previewTime=std::stod(value);}catch(...){}
        if(arg.rfind("--lang=",0)==0) lang=value;
        if(arg.rfind("--settings=",0)==0) settingsPath=fs::u8path(value);
        if(arg.rfind("--show=",0)==0) show=value;
        if(arg.rfind("--select=",0)==0) selection=value;
        if(arg=="--check-tabs") checkTabs=true;
        if(arg=="--check-language") checkLanguage=true;
        if(arg=="--check-view") checkView=true;
        if(arg=="--check-resources")checkResources=true;
        if(arg=="--check-middle")checkMiddle=true;
    }
    // Profiling: --play starts the song once its audio is ready, --run-seconds
    // keeps the preview running before the capture, --profile writes frame
    // times and cache counters. Nothing here changes the preview itself.
    bool playRequested=false,cycleSongs=false;double runSeconds=0;fs::path profilePath;
    for(const auto& arg:args) {
        if(arg=="--play")playRequested=true;
        if(arg.rfind("--run-seconds=",0)==0)try{runSeconds=std::max(0.0,std::stod(arg.substr(14)));}catch(...){}
        if(arg.rfind("--profile=",0)==0)profilePath=fs::u8path(arg.substr(10));
        if(arg=="--cycle-songs")cycleSongs=true;
    }
    if(settingsPath.empty()) { wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768); settingsPath=fs::path(executable).parent_path()/"settings/ui-language.txt"; }
    // Log in logs/ next to the program, or in %LOCALAPPDATA% when that folder
    // cannot be written; --logs=<folder> moves it.
    {
        fs::path folder; for(const auto& arg:args) if(arg.rfind("--logs=",0)==0) folder=fs::u8path(arg.substr(7));
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
        if(folder.empty()) folder=fs::path(executable).parent_path()/"logs";
        if(!pi::logs::open(folder)) if(const wchar_t* local=_wgetenv(L"LOCALAPPDATA")) pi::logs::open(fs::path(local)/"PsychImporter"/"logs");
        std::string line; for(const auto& arg:args) line+=" "+arg;
        pi::logs::info(std::string("Psych Importer " PSYCH_IMPORTER_VERSION " iniciado · build ")+__DATE__+" "+__TIME__+(line.empty()?"":" ·"+line));
        SetUnhandledExceptionFilter(crashFilter);
        std::set_terminate([]{ pi::logs::fatal("Excepción sin capturar: el programa termina."); std::abort(); });
    }
    if(!loadLanguage(settingsPath))setLanguage(Language::English); if(lang=="en") setLanguage(Language::English); else if(lang=="es") setLanguage(Language::Spanish);
    if(!inventoryPath.empty()) {
        try { auto catalog=files.empty()?pi::load(root):pi::loadLoose(files); std::ofstream out(inventoryPath,std::ios::binary); out<<inventory(catalog).dump(2); return out.good()?0:1; }
        catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
    }
    // Headless resource-manager report: what each song, character and stage
    // still lacks, as the Resources tab shows it.
    for(const auto& arg:args) if(arg.rfind("--readiness=",0)==0) {
        try {
            auto catalog=files.empty()?pi::load(root):pi::loadLoose(files);fs::path base;
            for(const auto& other:args) if(other.rfind("--base=",0)==0){base=fs::u8path(other.substr(7));if(fml::psych::directory(base/"assets"))base/="assets";}
            json out=json::array();
            for(const auto& r:exportReadiness(catalog,base)) {
                json important=json::array(),info=json::array();
                for(const auto& w:r.warnings)(importantNotice(w)?important:info).push_back(w);
                out.push_back({{"tab",r.tab},{"name",r.name},{"level",readinessLevel(r)},{"errors",r.errors},{"important",important},{"info",info},{"files",r.files},{"textureMB",r.textureBytes/1048576}});
            }
            std::ofstream file(fs::u8path(arg.substr(12)),std::ios::binary);file<<out.dump(1);return file.good()?0:1;
        } catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
    }
    // --modchart-dump=<song id>: what the reader makes of a song's modchart,
    // per script with its time, to stderr. No window.
    for(const auto& arg:args) if(arg.rfind("--modchart-dump=",0)==0) {
        try {
            auto catalog=files.empty()?pi::load(root):pi::loadLoose(files); const auto wanted=lower(arg.substr(16));
            // * : one line per song of the mod, to compare mods at a glance.
            if(wanted=="*") {
                for(const auto& song:catalog.songs) if(!song.difficulties.empty()) {
                    const auto started=std::chrono::steady_clock::now(); auto m=songModchart(catalog.files,song.difficulties.front());
                    std::string from; for(const auto& t:m.timing) from+=" | "+t.first;
                    std::fprintf(stderr,"%s: %s · %lld ms%s\n",song.id.c_str(),modchartSummary(m).c_str(),pi::logs::since(started),from.c_str());
                }
                return 0;
            }
            for(const auto& song:catalog.songs) if(lower(song.id)==wanted && !song.difficulties.empty()) {
                const auto started=std::chrono::steady_clock::now(); auto m=songModchart(catalog.files,song.difficulties.front());
                std::fprintf(stderr,"%s: %s · %lld ms\n",song.id.c_str(),modchartSummary(m).c_str(),pi::logs::since(started));
                for(const auto& t:m.timing) std::fprintf(stderr,"  %s: %lld ms\n",t.first.c_str(),t.second);
                for(const auto& n:m.notes) std::fprintf(stderr,"  nota: %s\n",n.c_str());
                return 0;
            }
            std::fprintf(stderr,"no song %s\n",wanted.c_str()); return 2;
        } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
    }
    // Headless multiple export: the same batchPlan/execute as the "Multiple
    // selection" dialog, so a bundle can be checked in the engine without
    // driving the window. Lists are ids; "id@N" picks the N-th of equal ids
    // and "*" takes all. The flags mirror the dialog's checkboxes.
    {
        std::string batchName,songList,characterList,stageList,chartList,iconList;fs::path base,reportPath;bool separate=false,zip=false;
        bool noCharacters=false,noStages=false,relatedSongs=false,chartCharacters=false,chartStages=false,partial=false,allResources=false,noModcharts=false,markAll=false,fitChart=false;
        for(const auto& arg:args) {
            auto eq=arg.find('=');auto value=eq==std::string::npos?"":arg.substr(eq+1);
            if(arg.rfind("--export-batch=",0)==0)batchName=value;
            if(arg.rfind("--songs=",0)==0)songList=value;
            if(arg.rfind("--characters=",0)==0)characterList=value;
            if(arg.rfind("--stages=",0)==0)stageList=value;
            if(arg.rfind("--base=",0)==0){base=fs::u8path(value);if(fml::psych::directory(base/"assets"))base/="assets";}
            if(arg.rfind("--report=",0)==0)reportPath=fs::u8path(value);
            if(arg=="--separate")separate=true;
            if(arg=="--zip")zip=true;
            if(arg.rfind("--charts=",0)==0)chartList=value;
            if(arg.rfind("--icons=",0)==0)iconList=value;
            if(arg=="--no-characters")noCharacters=true;
            if(arg=="--no-stages")noStages=true;
            if(arg=="--related-songs")relatedSongs=true;
            if(arg=="--chart-characters")chartCharacters=true;
            if(arg=="--chart-stages")chartStages=true;
            if(arg=="--resources=all")allResources=true;
            if(arg=="--no-modcharts")noModcharts=true;
            if(arg=="--mark-all")markAll=true;
            if(arg=="--fit-chart")fitChart=true;
            if(arg=="--partial")partial=true;
        }
        if(!batchName.empty()) {
            std::string report;int code=0;
            try {
                auto catalog=files.empty()?pi::load(root):pi::loadLoose(files);
                BatchOptions options;options.name=batchName;options.separate=separate;
                options.bringCharacters=!noCharacters;options.bringStages=!noStages;options.songsUsingCharacters=relatedSongs;
                options.chartCharacters=chartCharacters;options.chartStages=chartStages;options.modcharts=!noModcharts;options.zip=zip;options.fitChartToGame=fitChart;
                auto pick=[&](const std::string& list,auto count,auto idOf,std::set<int>& out) {
                    std::stringstream stream(list);std::string item;
                    while(std::getline(stream,item,',')) {
                        if(item.empty())continue;
                        if(item=="*"){for(int i=0;i<count;++i)out.insert(i);continue;}
                        int wanted=0;auto at=item.rfind('@');
                        if(at!=std::string::npos){try{wanted=std::stoi(item.substr(at+1));}catch(...){}item=item.substr(0,at);}
                        int seen=0,found=-1;for(int i=0;i<count;++i)if(lower(idOf(i))==lower(item) && seen++==wanted){found=i;break;}
                        if(found<0){report+="Not found: "+item+"\n";code=2;} else out.insert(found);
                    }
                };
                pick(songList,(int)catalog.songs.size(),[&](int i){return catalog.songs[i].id;},options.songs);
                pick(characterList,(int)catalog.characters.size(),[&](int i){return catalog.characters[i].imported.character.id;},options.characters);
                pick(stageList,(int)catalog.stages.size(),[&](int i){return catalog.stages[i].imported.stage.id;},options.stages);
                pick(iconList,(int)catalog.icons.size(),[&](int i){return catalog.icons[i].id;},options.icons);
                if(allResources) for(const auto& key:wholeModExtras(buildResourceIndex(catalog))) options.resources.insert(key);
                // --mark-all: what "Mark the whole mod" marks in the window.
                if(markAll) { const fs::path wholeBase=base.empty()?codenameAssetsBeside(destination):base; options=wholeModSelection(catalog,wholeBase,options).options; }
                // --charts=song:difficulty[,…] (song may carry @N).
                {std::stringstream stream(chartList);std::string item;
                 while(std::getline(stream,item,',')) {
                    auto colon=item.find(':');if(item.empty() || colon==std::string::npos){if(!item.empty()){report+="Bad chart: "+item+"\n";code=2;}continue;}
                    std::set<int> songs;pick(item.substr(0,colon),(int)catalog.songs.size(),[&](int i){return catalog.songs[i].id;},songs);
                    for(int song:songs){bool found=false;for(int d=0;d<(int)catalog.songs[song].difficulties.size();++d)if(lower(catalog.songs[song].difficulties[d].name)==lower(item.substr(colon+1))){options.charts.insert({song,d});found=true;}
                        if(!found){report+="Chart not found: "+item+"\n";code=2;}}
                 }}
                auto plan=batchPlan(catalog,destination,base,options);
                if(partial)plan=partialPlan(plan);
                if(!plan.ready())code=code?code:3;
                if(code)report+=describe(plan);
                else {
                    bool completed=false;report+=zip?executeZip(plan,&completed):execute(plan,&completed);
                    if(!completed)code=4;
                }
            } catch(const std::exception& e){report+=std::string("Exception: ")+e.what();code=1;}
            if(!reportPath.empty()){std::ofstream out(reportPath,std::ios::binary);out<<report;}
            return code;
        }
    }
    if(!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window=SDL_CreateWindow("Psych Importer " PSYCH_IMPORTER_VERSION,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|(capturePath.empty()?0:SDL_WINDOW_HIDDEN));
    if(!window) { SDL_Quit(); return 2; }
    SDL_SetWindowMinimumSize(window,1120,740); auto context=SDL_GL_CreateContext(window);
    if(!context) { SDL_DestroyWindow(window); SDL_Quit(); return 3; }
    SDL_GL_MakeCurrent(window,context); SDL_GL_SetSwapInterval(capturePath.empty()?1:0);
    ImGui::CreateContext(); ImGui::GetIO().IniFilename=nullptr;
    if(checkView)ImGui::GetIO().ConfigInputTrickleEventQueue=false;
    if(!ImGui::GetIO().Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",17)) ImGui::GetIO().Fonts->AddFontDefault();
    for(const char* path:{"C:/Windows/Fonts/SegoeIcons.ttf","C:/Windows/Fonts/segmdl2.ttf"}) {
        if(!fml::psych::file(fs::u8path(path))) continue;
        static const ImWchar ranges[]={0xE700,0xF8FF,0};
        ImFontConfig config; config.MergeMode=true; config.PixelSnapH=true; config.GlyphOffset=ImVec2(0,3);
        if(ImGui::GetIO().Fonts->AddFontFromFileTTF(path,15,&config,ranges)) { iconFontLoaded=true; break; }
    }
    style(); ImGui_ImplSDL3_InitForOpenGL(window,context); ImGui_ImplOpenGL3_Init("#version 330");
    App app; app.tab=initialTab; app.languagePath=settingsPath; app.destination=destination; app.initialSelection=selection; std::string audioError; app.audio.init(&audioError);app.gameAudio.init(&audioError);app.auditionAudio.init(&audioError);app.cameraFramePath=settingsPath.parent_path()/"camera-frame.txt";loadCameraFrame(app);app.game.chooseChart(app.catalog,-1,0);
    bool fullTextures=false;for(const auto& arg:args)if(arg=="--full-textures")fullTextures=true;
    app.game.showHud=PreviewHudAvailable;
    size_t vramMb=0;for(const auto& arg:args)if(arg.rfind("--vram-mb=",0)==0)try{vramMb=std::stoul(arg.substr(10));}catch(...){}
    // 512 MB keeps a long session around 1 GB of private memory (the GL
    // driver mirrors resident textures); 768 MB reached 1.8 GB.
    app.textures.vramBudget=(vramMb?vramMb:512)*1048576ull;
    app.textures.sizeCap=fullTextures?0:4096;app.textures.async=capturePath.empty() || !profilePath.empty();
    std::string renderError;if(!app.textures.init(renderError)){std::fprintf(stderr,"%s\n",renderError.c_str());return 8;}
    if(!root.empty() || !files.empty()) app.loadSource(root,files);
    // --check-audio-tabs: the Songs audio plays (muted) through Charts,
    // Resources and back to Songs, and must never stop.
    const bool checkAudioTabs=std::find(args.begin(),args.end(),std::string("--check-audio-tabs"))!=args.end();
    int audioStep=0,audioWait=0; bool audioTabsOk=true;
    // --check-resource-audio: an audio file picked in Resources must finish
    // loading and play (muted) on its own player.
    const bool checkResourceAudio=std::find(args.begin(),args.end(),std::string("--check-resource-audio"))!=args.end();
    int resourceAudioStep=0,resourceAudioWait=0; bool resourceAudioOk=false;
    // --check-sheet: the sprite sheet zooms with the wheel and moves when dragged.
    const bool checkSheet=std::find(args.begin(),args.end(),std::string("--check-sheet"))!=args.end();
    int sheetStep=0; bool sheetZoomOk=false,sheetPanOk=false;
    bool running=true,shown=false; int frames=0,result=0,interactionFrame=0; std::set<int> sawTabs; bool sawEnglish=false,sawSpanish=false; std::vector<fs::path> dropped;
    const int viewTabs[]={0,2,3,5};std::set<int> zoomVerified,panVerified,centerVerified;
    size_t resourceStep=0;bool resourcesDone=!checkResources;
    std::vector<double> frameMs,drawMs;Uint64 runStart=0,lastFrame=SDL_GetPerformanceCounter();const double tick=1000.0/(double)SDL_GetPerformanceFrequency();
    pi::profilingEnabled()=!profilePath.empty();const Uint64 launched=SDL_GetPerformanceCounter();double loadMs=-1;int cycleFrame=0,cycleSong=0;std::vector<double> switchMs;
    while(running) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if(e.type==SDL_EVENT_QUIT || (e.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID==SDL_GetWindowID(window))) running=false;
            if(e.type==SDL_EVENT_DROP_BEGIN) dropped.clear();
            if(e.type==SDL_EVENT_DROP_FILE && e.drop.data && !app.busy && !app.writeBusy && !app.dialogBusy) dropped.push_back(fs::u8path(e.drop.data));
            if(e.type==SDL_EVENT_DROP_COMPLETE && !dropped.empty()) { if(dropped.size()==1 && fml::psych::directory(dropped.front())) app.loadSource(dropped.front()); else app.addLoose(dropped); dropped.clear(); }
        }
        {PI_PROFILE("loop.poll");app.acceptDialog(); app.poll();}
        int frameW=0,frameH=0;SDL_GetWindowSizeInPixels(window,&frameW,&frameH);
        app.textures.renderer.beginFrame(frameW,frameH,.07f,.095f,.12f);
        if(app.textures.async)app.textures.renderer.pumpTextureUploads();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL3_NewFrame();
        if((checkTabs || checkLanguage || checkView) && !app.busy) {
            ++interactionFrame; auto& io=ImGui::GetIO();
            if(checkTabs && interactionFrame>=2 && interactionFrame<=29) { int index=(interactionFrame-2)/4; int phase=(interactionFrame-2)%4; if(phase<2) {auto p=app.tabCenters[index];io.AddMousePosEvent(p.x,p.y);io.AddMouseButtonEvent(0,phase==0);} }
            if(checkLanguage && (interactionFrame==32 || interactionFrame==33 || interactionFrame==36 || interactionFrame==37)) { auto p=app.languageCenters[interactionFrame<36?1:0];io.AddMousePosEvent(p.x,p.y);io.AddMouseButtonEvent(0,interactionFrame==32 || interactionFrame==36); }
            if(checkView && interactionFrame>=2 && interactionFrame<=65) {
                int tab=viewTabs[(interactionFrame-2)/16],phase=(interactionFrame-2)%16;
                if(phase<2){auto p=app.tabCenters[tab];io.AddMousePosEvent(p.x,p.y);io.AddMouseButtonEvent(0,phase==0);}
                if(phase==2 || phase==3 || phase==5){auto p=app.viewCanvasCenters[tab];io.AddMousePosEvent(p.x,p.y);if(phase==3)io.AddMouseWheelEvent(0,1);if(phase==5)io.AddMouseButtonEvent(checkMiddle?ImGuiMouseButton_Middle:ImGuiMouseButton_Left,true);}
                if(phase==7 || phase==9){auto p=app.viewCanvasCenters[tab];io.AddMousePosEvent(p.x+24,p.y+16);if(phase==9)io.AddMouseButtonEvent(checkMiddle?ImGuiMouseButton_Middle:ImGuiMouseButton_Left,false);}
                if(phase>=11 && phase<=13){auto p=app.viewCenterButtons[tab];io.AddMousePosEvent(p.x,p.y);if(phase>11)io.AddMouseButtonEvent(0,phase==12);}
            }
        }
        if(checkAudioTabs && shown && !app.busy) {
            auto& io=ImGui::GetIO(); static const int route[]={4,5,4,6,1};
            if(audioStep==0 && app.audio.trackCount() && !app.audio.trackLoadPending()) { app.audio.setMasterGain(0); app.gameAudio.setMasterGain(0); app.audio.play(); audioStep=1; audioWait=0; }
            else if(audioStep>=1 && audioStep<=5) {
                const int target=route[audioStep-1]; ++audioWait;
                if(audioWait==20 || audioWait==21) { auto p=app.tabCenters[target]; io.AddMousePosEvent(p.x,p.y); io.AddMouseButtonEvent(0,audioWait==20); }
                // Preview may still be loading its tracks: wait until something plays.
                auto& engine=app.gameAudio.playing()?app.gameAudio:app.audio;
                if(audioWait>=45 && (engine.playing() || audioWait>=400)) {
                    static double lastMs=0;
                    const double now=engine.positionMs();
                    const bool ok=app.tab==target && engine.playing() && now+50>=lastMs;
                    std::fprintf(stderr,"Audio across tabs: tab %d (wanted %d) playing %d at %.0f ms (before %.0f)\n",app.tab,target,(int)engine.playing(),now,lastMs);
                    lastMs=now;
                    audioTabsOk&=ok; ++audioStep; audioWait=0;
                }
            }
        }
        if(checkSheet && shown && !app.busy) {
            auto& io=ImGui::GetIO(); ++sheetStep; const auto c=app.sheetCenter;
            if(sheetStep==1) { app.tab=0; app.showSheet=true; app.sheetFollow=false; app.sheetZoom=1; app.sheetPanX=app.sheetPanY=0; }
            if(sheetStep==6) { io.AddMousePosEvent(c.x,c.y); }
            if(sheetStep==7) { io.AddMouseWheelEvent(0,2); }
            if(sheetStep==10) { sheetZoomOk=app.sheetZoom>1.3f; io.AddMousePosEvent(c.x,c.y); io.AddMouseButtonEvent(0,true); }
            if(sheetStep>=11 && sheetStep<=14) io.AddMousePosEvent(c.x+12.0f*(sheetStep-10),c.y+8.0f*(sheetStep-10));
            if(sheetStep==15) io.AddMouseButtonEvent(0,false);
            if(sheetStep==18) { sheetPanOk=std::abs(app.sheetPanX)>20; std::fprintf(stderr,"Sheet view: zoom %.2f pan %.0f/%.0f\n",app.sheetZoom,app.sheetPanX,app.sheetPanY); }
        }
        if(checkResourceAudio && shown && !app.busy) {
            if(resourceAudioStep==0) {
                app.tab=6; ensureResources(app);
                for(size_t i=0;i<app.resources.items.size();++i) if(app.resources.items[i].kind==ResourceKind::Audio && !app.resources.items[i].uses.empty()) { app.resource=(int)i; break; }
                resourceAudioStep=app.resource>=0?1:9;
            } else if(resourceAudioStep==1) {
                if(app.auditionAudio.trackCount() && !app.auditionAudio.trackLoadPending()) { app.auditionAudio.setMasterGain(0); app.auditionAudio.play(); resourceAudioStep=2; resourceAudioWait=0; }
                else if(++resourceAudioWait>600) { std::fprintf(stderr,"Resources audio never finished loading\n"); resourceAudioStep=9; }
            } else if(resourceAudioStep==2 && ++resourceAudioWait==30) {
                resourceAudioOk=app.auditionAudio.playing() && app.auditionAudio.positionMs()>0;
                std::fprintf(stderr,"Resources audio: %s playing %d at %.0f ms\n",app.resources.items[app.resource].name.c_str(),(int)app.auditionAudio.playing(),app.auditionAudio.positionMs());
                resourceAudioStep=3;
            }
        }
        ImGui::NewFrame();
        if(!app.busy && !shown) {
            shown=true;
            if(show=="batch") { if(!app.catalog.characters.empty()) app.batch.characters.insert(0); if(app.catalog.characters.size()>1) app.batch.characters.insert(1); if(!app.catalog.songs.empty()) app.batch.songs.insert(0); if(!app.catalog.stages.empty()) app.batch.stages.insert(0); app.batchRequested=true; }
            if(show=="loose") app.looseRequested=true;
            // Captures of the selection bar and the batch dialog.
            if(show=="mark-songs" || show=="batch-songs") for(int i=0;i<(int)app.catalog.songs.size();++i) app.batch.songs.insert(i);
            if(show=="batch-songs") app.batchRequested=true;
            if(show=="mark-all" || show=="batch-mark-all" || show=="batch-songs-alone") markWholeMod(app);
            if(show=="batch-mark-all") app.batchRequested=true;
            if(show=="batch-songs-alone") { app.batch.bringCharacters=app.batch.bringStages=false; app.batch.fitChartToGame=true; app.batchRequested=true; }
            if(show=="zoomed-out") { app.game.viewZoom=.6f; app.zoom=.6f; }
            if(show=="resources-audio") app.resourceFilter=1;
            if(show=="resources-images") app.resourceFilter=2;
            if(show=="sheet" || show=="sheet-whole" || show=="sheet-zoomed") { app.showSheet=true; app.sheetFollow=show=="sheet"; if(show=="sheet-zoomed") { app.sheetZoom=6; } }
            if(show=="modchart-off") app.game.modchartOn=false;
            if(show=="frame-styled") { app.game.viewZoom=.6f; app.zoom=.6f; app.cameraFrame.color={1,.85f,.2f,1}; app.cameraFrame.thickness=3; app.cameraFrame.dim=.55f; }
            if(show=="review") app.review(true);
            if(previewTime>=0)seekPreview(app,previewTime*1000);
            if(app.tab==6 && !selection.empty()) {
                ensureResources(app);
                for(size_t i=0;i<app.resources.items.size();++i) if(lower(app.resources.items[i].name)==lower(selection)) { app.resource=(int)i; break; }
            }
            if(show=="partial") {app.game.stage=-1;app.game.audio.voicesPath.clear();app.game.audio.playerVoicesPath.clear();app.game.audio.opponentVoicesPath.clear();app.loadGameAudio();if(previewTime>=0)seekPreview(app,previewTime*1000);}
            if(show=="generic")app.game.provisionalNotes=true;
            if(show=="hud-off")app.game.showHud=false;
            if(show=="chart-music")app.game.keepComposition=true;
            if(!iconState.empty())app.iconState=iconState;
            app.game.ratingScale=rankingScale;
            if(!noteStyle.empty())for(const auto& style:app.catalog.noteStyles)if(style.name==noteStyle || style.id==noteStyle)if(app.game.hud.setStyle(style)){app.game.provisionalNotes=false;break;}
            if(checkTabs || checkLanguage || checkView || (!show.empty() && show!="loose")) app.looseRequested=false;
        }
        if(shown && playRequested && !app.busy && !app.gameAudio.trackLoadPending()) {
            playRequested=false;app.game.playing=true;if(app.gameAudio.trackCount())app.gameAudio.play();runStart=SDL_GetPerformanceCounter();frameMs.clear();drawMs.clear();
        }
        if(shown && runStart==0 && !playRequested && !app.busy)runStart=SDL_GetPerformanceCounter();
        if(loadMs<0 && shown && !app.busy)loadMs=(SDL_GetPerformanceCounter()-launched)*tick;
        // Switch songs every 90 frames: what a user does browsing the list.
        if(cycleSongs && runStart && app.tab==5 && !app.catalog.songs.empty() && ++cycleFrame%90==0) {
            const Uint64 at=SDL_GetPerformanceCounter();cycleSong=(cycleSong+1)%(int)app.catalog.songs.size();
            choosePreviewSong(app,cycleSong,0,true);switchMs.push_back((SDL_GetPerformanceCounter()-at)*tick);
        }
        if(checkResources && !app.busy && !resourcesDone) {
            if(resourceStep<app.catalog.stages.size()){app.tab=2;app.selectStage((int)resourceStep);}
            else if(resourceStep<app.catalog.stages.size()+app.catalog.characters.size()){app.tab=0;app.selectCharacter((int)(resourceStep-app.catalog.stages.size()));}
            else{resourcesDone=true;app.tab=5;if(previewTime>=0)seekPreview(app,previewTime*1000);}
            ++resourceStep;
        }
        if(app.playingPreview) {
            app.clock+=ImGui::GetIO().DeltaTime*app.previewRate;
            if(app.tab==2 && app.clock>=app.stageDuration) { if(app.loopPreview) { app.clock=std::fmod(app.clock,app.stageDuration); if(app.stageIndex>=0) app.stageAnimator.reset(app.catalog.stages[app.stageIndex].imported.stage); } else {app.clock=app.stageDuration;app.playingPreview=false;} }
        }
        const Uint64 drawStart=SDL_GetPerformanceCounter();
        draw(app,window);
        if(!profilePath.empty() && runStart)drawMs.push_back((SDL_GetPerformanceCounter()-drawStart)*tick);
        if(checkTabs) {
            sawTabs.insert(app.tab);
            if(!app.busy && interactionFrame>1 && app.markAllVisible!=(app.tab!=5)) {std::fprintf(stderr,"Mark whole mod visibility failed on tab %d\n",app.tab);result=5;running=false;}
        }
        if(checkLanguage && interactionFrame>31) { sawEnglish|=language()==Language::English; sawSpanish|=language()==Language::Spanish; }
        if(checkView && interactionFrame>=2 && interactionFrame<=65) {
            int tab=viewTabs[(interactionFrame-2)/16],phase=(interactionFrame-2)%16;
            float zoom=tab==5?app.game.viewZoom:app.zoom,x=tab==5?app.game.viewPanX:app.panX,y=tab==5?app.game.viewPanY:app.panY;
            if(app.tab==tab && phase==4 && zoom>1.01f)zoomVerified.insert(tab);
            if(app.tab==tab && phase==10 && x>10 && y>10)panVerified.insert(tab);
            if(app.tab==tab && phase==14 && std::abs(zoom-1)<.001f && std::abs(x)<.001f && std::abs(y)<.001f)centerVerified.insert(tab);
            if(phase==4 || phase==10 || phase==14)std::fprintf(stderr,"View tab %d/current %d phase %d: zoom %.2f pan %.1f/%.1f mouse %.1f/%.1f center %.1f/%.1f\n",tab,app.tab,phase,zoom,x,y,ImGui::GetIO().MousePos.x,ImGui::GetIO().MousePos.y,app.viewCanvasCenters[tab].x,app.viewCanvasCenters[tab].y);
        }
        {PI_PROFILE("loop.imguiRender");ImGui::Render();} int w=0,h=0; SDL_GetWindowSizeInPixels(window,&w,&h);
        glViewport(0,0,w,h); glClearColor(.07f,.095f,.12f,1); glClear(GL_COLOR_BUFFER_BIT); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if(!app.busy)++frames;
        const bool ranEnough=runSeconds<=0 || (runStart && (SDL_GetPerformanceCounter()-runStart)*tick>=runSeconds*1000);
        if(!capturePath.empty() && !app.busy && resourcesDone && !playRequested && ranEnough && frames>=(checkView?72:(checkTabs || checkLanguage)?44:12) && (!checkAudioTabs || audioStep>=6 || frames>=1500) && (!checkResourceAudio || resourceAudioStep>=3 || frames>=900) && (!checkSheet || sheetStep>=20) && ((!app.audio.trackLoadPending() && !app.gameAudio.trackLoadPending()) || frames>=600)) { result=capture(capturePath,w,h)?0:4;running=false; }
        if(!profilePath.empty())glFinish();
        SDL_GL_SwapWindow(window);
        {const Uint64 now=SDL_GetPerformanceCounter();if(!profilePath.empty() && runStart)frameMs.push_back((now-lastFrame)*tick);lastFrame=now;}
        if(!capturePath.empty() && profilePath.empty()) SDL_Delay(16);
    }
    if(app.busy) app.loading.wait(); if(app.writeBusy) app.writing.wait();
    if(!diagnosticPath.empty() || checkResources) {
        auto diagnostics=previewDiagnostics(app);
        if(!diagnosticPath.empty()){std::ofstream out(diagnosticPath,std::ios::binary);out<<diagnostics.dump(2);}
        if(checkResources && (diagnostics["scene"]["actors"]==0 || !diagnostics["scene"]["missingTextures"].empty() || diagnostics["notes"]["commands"]==0)){std::fprintf(stderr,"Resource navigation lost the gameplay assets\n");result=9;}
    }
    if(!profilePath.empty()) {
        auto stats=[](std::vector<double> v){json j=json::object();if(v.empty())return j;std::sort(v.begin(),v.end());double sum=0;for(double x:v)sum+=x;
            j["frames"]=v.size();j["avgMs"]=sum/v.size();j["p50Ms"]=v[v.size()/2];j["p95Ms"]=v[std::min(v.size()-1,v.size()*95/100)];j["maxMs"]=v.back();return j;};
        PROCESS_MEMORY_COUNTERS_EX memory{};GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&memory,sizeof memory);
        json sections=json::object();for(const auto& entry:pi::profileSections())sections[entry.first]={{"avgMs",entry.second.totalMs/std::max<size_t>(1,entry.second.count)},{"maxMs",entry.second.maxMs},{"count",entry.second.count}};
        json profile={{"frame",stats(frameMs)},{"draw",stats(drawMs)},{"switch",stats(switchMs)},{"loadMs",loadMs},{"sections",sections},
            {"memoryMB",{{"workingSet",memory.WorkingSetSize/1048576},{"peakWorkingSet",memory.PeakWorkingSetSize/1048576},{"private",memory.PrivateUsage/1048576}}},
            {"positionMs",app.game.positionMs},{"preview",previewDiagnostics(app)}};
        std::ofstream out(profilePath,std::ios::binary);out<<profile.dump(2);
    }
    if(checkSheet && (!sheetZoomOk || !sheetPanOk)) { std::fprintf(stderr,"Sprite sheet zoom/pan failed\n"); result=11; }
    if(checkResourceAudio && !resourceAudioOk) { std::fprintf(stderr,"Resources audio did not play\n"); result=10; }
    if(checkAudioTabs && (!audioTabsOk || audioStep<6)) { std::fprintf(stderr,"Audio stopped when changing tab\n"); result=8; }
    if(checkTabs && sawTabs.size()!=7) { std::fprintf(stderr,"UI tab interaction failed\n"); result=5; }
    if(checkLanguage && (!sawEnglish || !sawSpanish)) { std::fprintf(stderr,"UI language interaction failed\n"); result=6; }
    if(checkView && (zoomVerified.size()!=4 || panVerified.size()!=4 || centerVerified.size()!=4)) { std::fprintf(stderr,"Preview view interaction failed: zoom %zu/4, pan %zu/4, center %zu/4\n",zoomVerified.size(),panVerified.size(),centerVerified.size());result=7; }
    if(app.readinessCancel)*app.readinessCancel=true;
    app.audio.shutdown();app.gameAudio.shutdown();app.auditionAudio.shutdown();if(app.cameraFrameDirty)saveCameraFrame(app);
    app.modShaders.clear();   // GL programs, while the context still exists
    pi::logs::info("Sesión cerrada"); pi::logs::close();app.textures.renderer.shutdown(); ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit(); return result;
}
