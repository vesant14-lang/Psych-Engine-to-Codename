#pragma once
#include "Catalog.hpp"
#include "../../src/fml_runtime/PreviewGameplay.hpp"
#include "PreviewHud.hpp"
#include "../../src/fml_runtime/PreviewDirector.hpp"
#include "Profile.hpp"
#include "Modchart.hpp"
#include <regex>

namespace pi {
// The first public and developer packages ship without the preview HUD.
inline constexpr bool PreviewHudAvailable=false;
struct Gameplay {
    int song=-1,difficulty=0,stage=-1;
    std::vector<std::vector<int>> actors;
    fml::UniversalChart chart;
    fml::UniversalStage scene;
    fml::psych::SongAudio audio;
    fml::TimeMap time;
    fml::PreviewGameplayState state;
    fml::StageAnimator animator;
    std::vector<fml::UniversalCharacter> models;
    std::vector<int> roles;
    std::vector<std::vector<std::pair<size_t,size_t>>> placements;
    double positionMs=0;
    bool playing=false,loop=false;
    bool showHud=PreviewHudAvailable,provisionalNotes=true,downscroll=false;
    bool keepComposition=false;
    bool fitScene=true;
    float viewZoom=1,viewPanX=0,viewPanY=0;
    PreviewHud hud;
    fml::HealthHud healthHud;
    float ratingScale=1,comboScale=1;
    std::vector<fml::RatingPopupHud> popups;
    fml::PreviewDirector director;
    std::vector<bool> idleAlt;
    // Engine camera: Codename's camGame follow and zoom (as in FML). Off, the
    // view frames the whole scene instead.
    bool engineCamera=true;
    float rate=1,volume=1;
    std::vector<std::string> missing;
    float viewLeft=0,viewTop=0,viewRight=0,viewBottom=0;
    bool haveViewBounds=false;
    // Psych "extra characters" scripts pair a character with a note type
    // ({'spongemii','Darnote1'}): those notes are that character's turn and
    // the line's own character stays quiet; a "-duet" type sings both. Each
    // singer gets its own strumline, the way Codename holds characters
    // (PlayState.hx:808-821), placed where its notes' line stands.
    struct TypeSinger { std::string type; int character=-1; int line=-1; };
    std::vector<TypeSinger> typeSingers;
    std::vector<char> duetNotes;       // per note: the line's own characters sing too
    // The song's modchart, played on top of the preview when switched on.
    std::shared_ptr<const Modchart> modchart; ModchartPlayer modPlayer; ModFrame modFrame; bool modchartOn=true;
    // Read off the UI thread: a long Lua replay must not freeze a song change.
    // A job for a song left behind is cancelled and kept until it ends.
    std::future<std::shared_ptr<const Modchart>> modchartJob; std::shared_ptr<std::atomic<bool>> modchartCancel;
    std::vector<std::future<std::shared_ptr<const Modchart>>> retiredJobs;
    void requestModchart(const Catalog& c,const Difficulty& d) {
        if(modchartCancel) *modchartCancel=true;
        if(modchartJob.valid()) retiredJobs.push_back(std::move(modchartJob));
        modchart.reset(); modPlayer=ModchartPlayer{}; modFrame={};
        if(auto ready=peekSongModchart(d)) { modchart=ready; modPlayer.prepare(*modchart); return; }
        modchartCancel=std::make_shared<std::atomic<bool>>(false);
        auto files=c.files; auto diff=d; auto cancel=modchartCancel;
        modchartJob=std::async(std::launch::async,[files,diff,cancel]{ return cachedSongModchart(files,diff,cancel.get()); });
    }
    void pollModchart() {
        retiredJobs.erase(std::remove_if(retiredJobs.begin(),retiredJobs.end(),[](auto& j){return j.wait_for(std::chrono::seconds(0))==std::future_status::ready;}),retiredJobs.end());
        if(modchartJob.valid() && modchartJob.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try { modchart=modchartJob.get(); if(modchart) modPlayer.prepare(*modchart); } catch(...) { modchart.reset(); }
        }
    }
    bool modchartLoading() const { return modchartJob.valid(); }
    void updateModchart() { pollModchart(); modFrame=modchartOn && modchart && !modchart->empty()?evaluateModchart(modPlayer,time.beatAt(positionMs)*std::max(1,chart.stepsPerBeat)):ModFrame{}; }
    size_t chartLines=0;               // strumlines of the chart; the rest are singers'
    // The song's characters by note type, as the export reads them.
    static std::vector<TypeSinger> scriptedTypeSingers(const Catalog& c,const Difficulty& d) {
        std::vector<TypeSinger> out; for(const auto& s:pi::scriptedTypeSingers(c,d)) out.push_back({s.type,s.character,-1}); return out;
    }
    void applyTypeSingers(const Catalog& c) {
        chartLines=chart.strumLines.size(); duetNotes.assign(chart.notes.size(),0);
        for(auto& s:typeSingers) {
            s.line=-1;
            if(s.type.empty() || s.character<0 || (size_t)s.character>=c.characters.size()) continue;
            std::map<int,int> from;
            for(const auto& n:chart.notes) if(n.type.find(s.type)!=std::string::npos && n.strumLine>=0 && (size_t)n.strumLine<chartLines) ++from[n.strumLine];
            if(from.empty()) continue;
            const int origin=std::max_element(from.begin(),from.end(),[](const auto& a,const auto& b){return a.second<b.second;})->first;
            auto line=chart.strumLines[origin]; line.characters={c.characters[s.character].imported.character.id}; line.visible=false;
            s.line=(int)chart.strumLines.size(); chart.strumLines.push_back(line); actors.push_back({s.character});
            for(size_t i=0;i<chart.notes.size();++i) {
                auto& n=chart.notes[i]; if(n.type.find(s.type)==std::string::npos) continue;
                n.singerStrumLine=s.line; if(lower(n.type).find("-duet")!=std::string::npos) duetNotes[i]=1;
            }
        }
    }
    // After the singers were edited: the chart again from the catalog, the
    // chart's own lines keep their characters.
    void resetSingers(const Catalog& c) {
        if(song<0 || (size_t)song>=c.songs.size() || difficulty<0 || (size_t)difficulty>=c.songs[song].difficulties.size()) return;
        actors.resize(std::min(actors.size(),chartLines));
        chart=c.songs[song].difficulties[difficulty].chart;
        while(actors.size()<chart.strumLines.size()) actors.push_back({-1});
        applyTypeSingers(c);
        popups.clear(); time.build(chart); state.reset(chart); rebuild(c);
    }
    static int uniqueCharacter(const Catalog& c,const std::string& id) {
        int found=-1; for(int i=0;i<(int)c.characters.size();++i) if(lower(c.characters[i].imported.character.id)==lower(id)) {if(found>=0)return -1;found=i;} return found;
    }
    void chooseChart(const Catalog& c,int index,int diff,bool preserveComposition=false) {
        auto oldActors=actors;auto oldLines=chart.strumLines;int oldStage=stage;auto oldHud=hud;
        if(index<0 || (size_t)index>=c.songs.size() || diff<0 || (size_t)diff>=c.songs[index].difficulties.size()){index=-1;diff=0;}
        song=index; difficulty=diff; positionMs=0; playing=false; stage=-1; audio={}; actors.clear(); chart={};
        if(index>=0 && (size_t)index<c.songs.size() && diff>=0 && (size_t)diff<c.songs[index].difficulties.size()) {
            const auto& source=c.songs[index].difficulties[diff]; chart=source.chart; audio=source.audio;
            stage=source.exportStage;
            if(stage<0)for(int i=0;i<(int)c.stages.size();++i) if(lower(c.stages[i].imported.stage.id)==lower(chart.stage)) {if(stage>=0){stage=-1;break;}stage=i;}
            for(const auto& line:chart.strumLines) { std::vector<int> selections; for(const auto& id:line.characters) {auto chosen=source.exportCharacters.find(id);selections.push_back(chosen==source.exportCharacters.end()?uniqueCharacter(c,id):chosen->second);} actors.push_back(std::move(selections)); }
            typeSingers=scriptedTypeSingers(c,source);
            {PI_PROFILE("switch.modchart");requestModchart(c,source);}
        } else {
            if(modchartCancel) *modchartCancel=true; if(modchartJob.valid()) retiredJobs.push_back(std::move(modchartJob));
            modchart.reset(); modPlayer=ModchartPlayer{};
            typeSingers.clear();
            const char* ids[]={"dad","bf","gf"}; const char* positions[]={"dad","boyfriend","girlfriend"};
            for(int i=0;i<3;++i) {fml::ChartStrumLine line;line.type=i;line.position=positions[i];line.visible=false; chart.strumLines.push_back(line);actors.push_back({uniqueCharacter(c,ids[i])});}
        }
        {PI_PROFILE("switch.hudResolve");hud.resolve(c,index>=0 && (size_t)index<c.songs.size()?c.songs[index].difficulties[diff].path:fs::path());}
        if(preserveComposition) {
            stage=oldStage;hud=oldHud;
            std::set<size_t> used;
            for(size_t i=0;i<chart.strumLines.size();++i) {
                int match=-1;
                if(i<oldLines.size() && !used.count(i) && oldLines[i].type==chart.strumLines[i].type && oldLines[i].position==chart.strumLines[i].position)match=(int)i;
                if(match<0)for(size_t j=0;j<oldLines.size();++j)if(!used.count(j) && oldLines[j].type==chart.strumLines[i].type && oldLines[j].position==chart.strumLines[i].position){match=(int)j;break;}
                if(match<0)for(size_t j=0;j<oldLines.size();++j)if(!used.count(j) && oldLines[j].type==chart.strumLines[i].type){match=(int)j;break;}
                if(match>=0)used.insert((size_t)match);
                actors[i]=match>=0 && (size_t)match<oldActors.size()?oldActors[match]:std::vector<int>{-1};
            }
        }
        applyTypeSingers(c);
        {PI_PROFILE("switch.timeState");popups.clear();time.build(chart); state.reset(chart);} {PI_PROFILE("switch.rebuild");rebuild(c);}
    }
    void rebuild(const Catalog& c) {
        fml::UniversalStage original;
        if(stage>=0 && (size_t)stage<c.stages.size()) original=c.stages[stage].imported.stage;
        scene=original; scene.id="psych-partial-preview";
        scene.objects.clear();
        auto isCharacter=[](const fml::StageObject& o){return o.kind==fml::StageObject::Kind::Player || o.kind==fml::StageObject::Kind::Opponent || o.kind==fml::StageObject::Kind::Girlfriend || o.kind==fml::StageObject::Kind::Character;};
        auto layerOrder=[](int role){return role==2?0:role==0?1:2;};
        struct ActorGroup {size_t line;int role;std::vector<std::pair<fml::StageObject,size_t>> markers;};
        std::vector<ActorGroup> groups;
        std::vector<std::vector<size_t>> groupsAt(original.objects.size()+1);
        if(stage<0) scene.zoom=.8f;
        models.clear(); roles.clear(); placements.clear(); placements.resize(chart.strumLines.size()); missing.clear();haveViewBounds=false;
        size_t count=0; for(const auto& line:actors) count+=line.size(); models.reserve(count); roles.reserve(count);
        std::map<size_t,int> singersAt;
        if(stage<0) missing.push_back("stage");
        if(song<0)missing.push_back("chart");
        if(audio.instPath.empty()) missing.push_back("Inst"); if(!audio.hasVoices()) missing.push_back("Voices");
        for(size_t line=0;line<chart.strumLines.size();++line) {
            int role=std::clamp(chart.strumLines[line].type,0,2); auto kind=role==1?fml::StageObject::Kind::Player:role==2?fml::StageObject::Kind::Girlfriend:fml::StageObject::Kind::Opponent;
            fml::StageObject anchor; anchor.kind=kind; anchor.position=role==1?fml::Vec2{850,210}:role==2?fml::Vec2{550,120}:fml::Vec2{180,120}; anchor.characterSpacing={170,0};
            size_t anchorIndex=original.objects.size();
            for(size_t i=0;i<original.objects.size();++i)if(isCharacter(original.objects[i]) && original.objects[i].name==chart.strumLines[line].position){anchorIndex=i;break;}
            if(anchorIndex==original.objects.size())for(size_t i=0;i<original.objects.size();++i)if(original.objects[i].kind==kind){anchorIndex=i;break;}
            size_t insertion=anchorIndex;
            if(anchorIndex<original.objects.size())anchor=original.objects[anchorIndex];
            else {
                // Missing anchors use Psych's GF/opponent/player order. Keep
                // any scenery after the existing actors in the foreground.
                for(size_t i=0;i<original.objects.size();++i)if(isCharacter(original.objects[i])) {
                    insertion=i+1;
                    int existingRole=original.objects[i].kind==fml::StageObject::Kind::Girlfriend?2:original.objects[i].kind==fml::StageObject::Kind::Player?1:0;
                    if(layerOrder(existingRole)>layerOrder(role)){insertion=i;break;}
                }
            }
            if(line>=actors.size() || actors[line].empty()) missing.push_back("L"+std::to_string(line)+": character");
            if(line>=actors.size()) continue;
            groupsAt[insertion].push_back(groups.size());groups.push_back({line,role,{}});
            // A singer's own line stands where its notes' line stands, as in
            // Codename; the preview moves each one aside so all can be seen.
            float aside=0; if(line>=chartLines && chartLines>0) aside=(role==1?1.0f:-1.0f)*230.0f*(float)(++singersAt[anchorIndex]);
            for(size_t slot=0;slot<actors[line].size();++slot) {
                int index=actors[line][slot]; if(index<0 || (size_t)index>=c.characters.size()) {missing.push_back("L"+std::to_string(line)+" / "+std::to_string(slot)+": character");continue;}
                const auto& source=c.characters[index]; if(source.image.empty() || source.atlas.empty()) missing.push_back(source.imported.character.id+": atlas / image");
                models.push_back(source.imported.character); auto modelIndex=models.size()-1; models.back().id="preview-L"+std::to_string(line)+"-"+std::to_string(slot); roles.push_back(role);
                auto marker=anchor; marker.kind=fml::StageObject::Kind::Character; marker.name=models.back().id; marker.position.x+=anchor.characterSpacing.x*slot+aside; marker.position.y+=anchor.characterSpacing.y*slot;
                // Each clone already owns its absolute position. FML's role
                // index must not apply the group's spacing a second time.
                marker.characterSpacing={0,0};
                groups.back().markers.push_back({marker,modelIndex});
            }
        }
        // Replace the stage's character markers in place, preserving FML's
        // background, character and foreground layers independently of lanes.
        for(size_t i=0;i<groupsAt.size();++i) {
            auto& here=groupsAt[i];
            std::stable_sort(here.begin(),here.end(),[&](size_t a,size_t b){return layerOrder(groups[a].role)<layerOrder(groups[b].role);});
            for(size_t index:here)for(auto& marker:groups[index].markers) {
                placements[groups[index].line].push_back({scene.objects.size(),marker.second});scene.objects.push_back(std::move(marker.first));
            }
            if(i<original.objects.size() && !isCharacter(original.objects[i]))scene.objects.push_back(original.objects[i]);
        }
        healthHud={};healthHud.image=hud.healthBar.image;healthHud.imageW=hud.healthBar.width;healthHud.imageH=hud.healthBar.height;
        if(healthHud.imageW<=0){healthHud.imageW=600;healthHud.imageH=20;}
        bool havePlayer=false,haveOpponent=false;
        for(size_t i=0;i<actors.size();++i)for(int actor:actors[i])if(actor>=0 && i<chart.strumLines.size()) {
            if(chart.strumLines[i].type==1 && !havePlayer){PreviewHud::bindIcon(c,actor,true,healthHud);havePlayer=true;}
            if(chart.strumLines[i].type==0 && !haveOpponent){PreviewHud::bindIcon(c,actor,false,healthHud);haveOpponent=true;}
        }
        animator.reset(scene); animator.resync(time.beatAt(positionMs),positionMs);
    }
    fml::CharacterBinding bindings() const {
        fml::CharacterBinding b;
        for(size_t i=0;i<models.size();++i) {auto* model=&models[i]; if(roles[i]==1)b.players.push_back(model);else if(roles[i]==2)b.girlfriends.push_back(model);else b.opponents.push_back(model);}
        if(!b.players.empty())b.player=b.players.front();if(!b.opponents.empty())b.opponent=b.opponents.front();if(!b.girlfriends.empty())b.girlfriend=b.girlfriends.front(); return b;
    }
    float stageZoom() const {return stage>=0?scene.zoom:1.05f;}
    // camHUD zoom (the beat bop); the framed view keeps the HUD still.
    float hudZoom() const {return engineCamera && song>=0?director.hudZoom():1.0f;}
    // getStrumlineCamPos(line): the camera point of that line's own actors,
    // through the same Character.getCameraPosition rules FML uses.
    bool cameraTarget(int line,fml::Vec2& out,fml::AtlasStore& atlases) const {
        if(line<0 || (size_t)line>=placements.size() || placements[line].empty())return false;
        fml::CharacterBinding b;fml::StageObject::Kind kind=fml::StageObject::Kind::Opponent;
        for(const auto& p:placements[line]) {
            const auto* model=&models[p.second];
            if(roles[p.second]==1){b.players.push_back(model);kind=fml::StageObject::Kind::Player;}
            else if(roles[p.second]==2){b.girlfriends.push_back(model);kind=fml::StageObject::Kind::Girlfriend;}
            else b.opponents.push_back(model);
        }
        if(!b.players.empty())b.player=b.players.front();if(!b.opponents.empty())b.opponent=b.opponents.front();if(!b.girlfriends.empty())b.girlfriend=b.girlfriends.front();
        return fml::characterCameraPoint(scene,b,atlases,kind,out);
    }
    fml::PreviewDirector::TargetFn cameraTargets(fml::AtlasStore& atlases) const {
        return [this,&atlases](int line,fml::Vec2& out){return cameraTarget(line,out,atlases);};
    }
    static bool hasAnimation(const fml::UniversalCharacter& ch,const std::string& name) {
        for(const auto& a:ch.anims)if(a.name==name)return true; return false;
    }
    // FML's firePreviewGameplayEvent: the pose lasts the character's holdTime
    // from the instant the event was resolved, every sustain piece plays it
    // again, alt/no-anim note types and Alt Animation Toggle apply.
    void singEvent(const fml::PreviewGameplayEvent& event,double observedMs) {
        if(event.noteIndex>=chart.notes.size())return;
        const auto& n=chart.notes[event.noteIndex];
        const bool missed=event.kind==fml::PreviewGameplayEventKind::PlayerMiss || event.kind==fml::PreviewGameplayEventKind::SustainBreak;
        if(!missed)director.noteHit();
        int line=n.singerStrumLine>=0?n.singerStrumLine:n.strumLine; if(line<0 || (size_t)line>=placements.size())return;
        if(!missed && fml::noteTypeSuppressesAnimation(n.type))return;
        const bool alt=fml::noteTypeAltAnimation(n.type) || director.altSing(n.strumLine);
        const double eventBeat=time.beatAt(event.resolvedAtMs),observedBeat=time.beatAt(observedMs);
        const double elapsed=std::max(0.0,observedMs-event.resolvedAtMs);
        std::vector<int> singers{line};
        if(event.noteIndex<duetNotes.size() && duetNotes[event.noteIndex] && n.strumLine!=line && n.strumLine>=0 && (size_t)n.strumLine<placements.size()) singers.push_back(n.strumLine);
        for(int target:singers) for(const auto& p:placements[target]) {
            const auto& ch=models[p.second];
            // Psych "Hey!" (PlayState.hx:2985/3069), as the exported script does.
            if(!missed && n.type=="Hey!" && !event.sustainTick) {
                const char* anim=hasAnimation(ch,"hey")?"hey":hasAnimation(ch,"cheer")?"cheer":"";
                if(*anim){animator.playCharacterAnimation(p.first,ch,anim,true,fml::CharacterAnimContext::None,0.0);continue;}
            }
            auto base=fml::singAnimationForNote(n,ch);
            std::string requested=base+(missed?(event.animationSuffix.empty()?"miss":event.animationSuffix):(alt && ch.id!="pico-speakers"?"-alt":""));
            if(missed){auto declared=fml::resolveDirectionalMissAnimation(ch,n.direction,requested);if(!declared.empty())requested=declared;}
            const double hold=std::max(0.001,eventBeat+fml::characterSingHoldBeats(ch,chart.stepsPerBeat)-observedBeat);
            bool played=animator.sing(p.first,ch,requested,hold,elapsed);
            if(!missed && !played && requested!=base)animator.sing(p.first,ch,base,hold,elapsed);
        }
    }
    // FML's restoreActiveNotesAt: after a seek each actor resumes the pose
    // of its last resolved note while that note's holdTime has not expired.
    void restoreSinging(double positionMs) {
        const double currentBeat=time.beatAt(positionMs);
        struct Active {const fml::ChartNote* note=nullptr;size_t object=0,model=0;double until=0,lastHit=0;};
        std::map<size_t,Active> byObject;
        for(size_t i=0;i<chart.notes.size() && chart.notes[i].timeMs<positionMs;++i) {
            const auto& note=chart.notes[i];
            const auto* phase=state.noteState(i);
            if(phase && (phase->phase==fml::PreviewNotePhase::Pending || phase->phase==fml::PreviewNotePhase::AwaitingResolution ||
                         phase->phase==fml::PreviewNotePhase::Missed || phase->phase==fml::PreviewNotePhase::Cancelled))continue;
            if(fml::noteTypeSuppressesAnimation(note.type))continue;
            int line=note.singerStrumLine>=0?note.singerStrumLine:note.strumLine;if(line<0 || (size_t)line>=placements.size())continue;
            const double lastHit=fml::lastSustainHitAt(chart,note,positionMs);
            std::vector<int> singers{line};
            if(i<duetNotes.size() && duetNotes[i] && note.strumLine!=line && note.strumLine>=0 && (size_t)note.strumLine<placements.size()) singers.push_back(note.strumLine);
            for(int target:singers) for(const auto& p:placements[target]) {
                const double until=time.beatAt(lastHit)+fml::characterSingHoldBeats(models[p.second],chart.stepsPerBeat);
                if(until<=currentBeat)continue;
                auto found=byObject.find(p.first);
                if(found==byObject.end() || found->second.note->timeMs<=note.timeMs)byObject[p.first]={&note,p.first,p.second,until,lastHit};
            }
        }
        for(const auto& entry:byObject) {
            const auto& a=entry.second;const auto& ch=models[a.model];
            const bool alt=fml::noteTypeAltAnimation(a.note->type) || director.altSing(a.note->strumLine);
            auto base=fml::singAnimationForNote(*a.note,ch);auto requested=base+(alt && ch.id!="pico-speakers"?"-alt":"");
            if(!animator.sing(a.object,ch,requested,a.until-currentBeat,positionMs-a.lastHit) && requested!=base)
                animator.sing(a.object,ch,base,a.until-currentBeat,positionMs-a.lastHit);
        }
    }
    // Play Animation and Alt Animation Toggle reach every actor of the line.
    void applyCharacterEvents() {
        for(const auto& request:director.takeAnimations()) {
            if(request.strumLine<0 || (size_t)request.strumLine>=placements.size())continue;
            for(const auto& p:placements[request.strumLine]) {
                const auto& ch=models[p.second];
                const double hold=request.context==fml::CharacterAnimContext::Sing || request.context==fml::CharacterAnimContext::Miss?fml::characterSingHoldBeats(ch,chart.stepsPerBeat):0.0;
                animator.playCharacterAnimation(p.first,ch,request.animation,request.forced,request.context,hold);
            }
        }
        idleAlt.resize(placements.size(),false);
        for(size_t line=0;line<placements.size();++line) {
            bool alt=director.altIdle((int)line); if(alt==idleAlt[line])continue; idleAlt[line]=alt;
            for(const auto& p:placements[line])animator.setIdleSuffix(p.first,alt?"-alt":"");
        }
    }
    // PlayState.displayRating/displayRatingNumbers: several popups live at
    // once, each fading on the song clock (FML's recordRatingPopup).
    void recordPopup(const fml::PreviewGameplayEvent& event,int combo) {
        if(event.sustainTick || event.kind!=fml::PreviewGameplayEventKind::PlayerHit || event.judgement==fml::PreviewJudgement::None || event.judgement==fml::PreviewJudgement::Miss)return;
        fml::RatingPopupHud popup;popup.eventId=event.eventId;popup.shownAtMs=event.resolvedAtMs;popup.beatMs=60000.0/std::max(1.0f,time.bpmAt(event.resolvedAtMs));
        popup.ratingName=event.ratingName;popup.comboValue=combo;popup.showRating=event.showRating;
        // The COMBO sprite only shows when a script sets displayCombo:
        // defaultDisplayCombo = false (PlayState.hx:564, 2102).
        popup.showCombo=false;
        // Codename minDigitDisplay = 10 (PlayState.hx:568, 2101).
        popup.showNumbers=combo==0 || combo>=10;popup.world=true;
        popups.push_back(popup);while(popups.size()>24)popups.erase(popups.begin());
    }
    void seek(double ms,fml::AtlasStore& atlases) {
        PI_PROFILE("game.seek");
        positionMs=std::max(0.0,ms); state.seek(chart,positionMs); popups.clear(); idleAlt.assign(placements.size(),false);
        animator.resync(time.beatAt(positionMs),positionMs);
        for(const auto& line:placements)for(const auto& p:line)animator.setIdleSuffix(p.first,"");
        director.resync(chart,time,stageZoom(),positionMs,cameraTargets(atlases));
        auto b=bindings(); animator.updateWithBeat(scene,b,atlases,positionMs,time.beatAt(positionMs),false);
        director.takeAnimations(); applyCharacterEvents();
        restoreSinging(positionMs);
        if(const auto& last=state.seekRating();last) {
            const double beatMs=60000.0/std::max(1.0f,time.bpmAt(last->event.resolvedAtMs));
            if(positionMs>=last->event.resolvedAtMs && positionMs-last->event.resolvedAtMs<=200.0+(last->combo>=10?2.0:1.0)*beatMs)recordPopup(last->event,last->combo);
        }
    }
    void advance(double ms,fml::AtlasStore& atlases,float elapsedSeconds=0) {
        if(ms<positionMs || ms-positionMs>1000) { seek(ms,atlases); return; }
        positionMs=std::max(0.0,ms); auto b=bindings(); animator.updateWithBeat(scene,b,atlases,positionMs,time.beatAt(positionMs),playing);
        director.advance(chart,time,positionMs,elapsedSeconds,playing,cameraTargets(atlases));
        applyCharacterEvents();
        state.advance(chart,positionMs);
        for(const auto& event:state.takeEvents()) {
            singEvent(event,positionMs);
            if(state.commit(chart,event))recordPopup(event,state.stats().combo);
        }
    }
};
}
