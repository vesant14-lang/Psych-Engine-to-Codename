#pragma once

// Included after App and the resource viewers. Rendering uses FML's scene and
// gameplay state; selections here only compose the preview, never the source.
double previewDuration(const App& app) {
    double end=std::max(10000.0,app.gameAudio.durationMs());
    for(const auto& n:app.game.chart.notes) end=std::max(end,n.timeMs+n.sustainMs+2000);
    return end;
}
void seekPreview(App& app,double ms) { app.gameAudio.seekMs(ms); app.game.seek(ms,app.atlases); }
void advancePreview(App& app) {
    PI_PROFILE("preview.advance");auto& g=app.game; if(!g.playing || app.gameAudio.trackLoadPending()) return;
    double next=app.gameAudio.trackCount()?app.gameAudio.positionMs():g.positionMs+ImGui::GetIO().DeltaTime*1000*g.rate;
    auto end=previewDuration(app);
    if(next>=end-1 || (app.gameAudio.trackCount() && !app.gameAudio.playing())) {
        if(g.loop) {seekPreview(app,0); if(app.gameAudio.trackCount()) app.gameAudio.play(); next=0;}
        else {g.playing=false;app.gameAudio.pause();next=std::min(next,end);}
    }
    g.advance(next,app.atlases,ImGui::GetIO().DeltaTime);
}
void choosePreviewSong(App& app,int song,int difficulty,bool play=false) {
    // Parsed atlases are never released by AtlasStore. After a dozen
    // different songs the store starts again; what is on screen is parsed
    // again on demand and recent songs stay cached.
    if(song>=0 && app.previewSongsSeen.insert(song).second && app.previewSongsSeen.size()>12) {
        auto reader=app.atlases.readText;app.atlases=fml::AtlasStore{};app.atlases.readText=reader;app.previewSongsSeen={song};
    }
    {PI_PROFILE("switch.chooseChart");app.game.chooseChart(app.catalog,song,difficulty,app.game.keepComposition);}
    if(app.game.song>=0){app.song=app.game.song;app.difficulty=app.game.difficulty;std::snprintf(app.targetId.data(),app.targetId.size(),"%s",app.catalog.songs[app.song].id.c_str());}
    app.loadGameAudio(play);
}
void previewSongList(App& app) {
    PI_PROFILE("preview.songList");
    auto& g=app.game;ImGui::TextUnformatted(ui("Canciones","Songs"));ImGui::Separator();
    ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##preview-search",ui("Buscar canción...","Search songs..."),app.filter.data(),app.filter.size());
    if(ImGui::Selectable(ui("Sin chart · solo escena","No chart · scene only"),g.song<0))choosePreviewSong(app,-1,0);
    auto match=lower(app.filter.data());
    for(int i=0;i<(int)app.catalog.songs.size();++i) {
        const auto& song=app.catalog.songs[i];if(lower(song.title+" "+song.id).find(match)==std::string::npos)continue;
        ImGui::PushID(i);auto at=ImGui::GetCursorScreenPos();
        if(ImGui::Selectable("##song",g.song==i,0,{0,56})) {
            int diff=0;for(int j=0;j<(int)song.difficulties.size();++j)if(lower(song.difficulties[j].name)=="normal")diff=j;
            choosePreviewSong(app,i,diff,true);
        }
        if(ImGui::BeginPopupContextItem("##song-actions")) {
            if(ImGui::MenuItem(ui("Reproducir con todos sus assets","Play with all song assets"))){g.keepComposition=false;choosePreviewSong(app,i,0,true);}
            if(ImGui::MenuItem(ui("Solo chart y música","Chart and music only"))){g.keepComposition=true;choosePreviewSong(app,i,0,true);}
            ImGui::EndPopup();
        }
        iconAt(app,song.iconPath,{at.x+4,at.y+4},44);
        auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(at,{at.x+ImGui::GetContentRegionAvail().x,at.y+56},true);
        draw->AddText({at.x+57,at.y+7},ImGui::GetColorU32(ImGuiCol_Text),song.title.c_str());
        auto sub=std::to_string(song.difficulties.size())+" "+ui("dificultades","difficulties");draw->AddText({at.x+57,at.y+31},ImGui::GetColorU32(ImGuiCol_TextDisabled),sub.c_str());draw->PopClipRect();
        tooltip(song.id);ImGui::PopID();
    }
    if(app.catalog.songs.empty())ImGui::TextWrapped("%s",ui("Importa una canción o un chart con su música.","Import a song or a chart with its music."));
}
void previewComposition(App& app) {
    auto& g=app.game;ImGui::TextUnformatted(ui("Escenario","Stage")); ImGui::SetNextItemWidth(-1);
    auto stage=g.stage>=0 && (size_t)g.stage<app.catalog.stages.size()?app.catalog.stages[g.stage].imported.stage.id:std::string(ui("Sin escenario","No stage"));
    bool changed=false;
    if(ImGui::BeginCombo("##preview-stage",stage.c_str())) {if(ImGui::Selectable(ui("Sin escenario","No stage"),g.stage<0)){g.stage=-1;changed=true;} for(int i=0;i<(int)app.catalog.stages.size();++i) {ImGui::PushID(i);if(ImGui::Selectable(app.catalog.stages[i].imported.stage.id.c_str(),g.stage==i)){g.stage=i;changed=true;}ImGui::PopID();}ImGui::EndCombo();}
    ImGui::Spacing(); ImGui::TextUnformatted(ui("Personajes por línea","Characters per line"));
    tooltip(ui("Como en Codename: todos los personajes de una línea cantan sus notas, uno al lado del otro, y la cámara apunta a la línea que tiene el turno.","As in Codename: every character on a line sings its notes, side by side, and the camera aims at the line whose turn it is."));
    bool singersChanged=false;
    for(size_t line=0;line<g.actors.size();++line) {
        ImGui::PushID((int)line);
        Gameplay::TypeSinger* singer=nullptr; for(auto& s:g.typeSingers) if(s.line==(int)line) singer=&s;
        if(singer) ImGui::TextColored(ImVec4(.62f,.76f,.94f,1),"L%zu · %s · %s «%s»",line,g.chart.strumLines[line].position.c_str(),ui("notas","notes"),singer->type.c_str());
        else ImGui::Text("L%zu · %s",line,g.chart.strumLines[line].position.c_str());
        if(g.actors[line].empty())g.actors[line].push_back(-1);
        for(size_t slot=0;slot<g.actors[line].size();++slot) {
            ImGui::PushID((int)slot);auto& selection=g.actors[line][slot];
            const bool removable=!singer && g.actors[line].size()>1;
            ImGui::SetNextItemWidth(removable?ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemSpacing.x:-1);
            const int before=selection;
            auto caption=selection>=0 && (size_t)selection<app.catalog.characters.size()?app.catalog.characters[selection].imported.character.id:std::string(ui("Ninguno","None"));
            if(ImGui::BeginCombo("##preview-character",caption.c_str())) {if(ImGui::Selectable(ui("Ninguno","None"),selection<0)){selection=-1;changed=true;} for(int i=0;i<(int)app.catalog.characters.size();++i){ImGui::PushID(i);if(ImGui::Selectable(app.catalog.characters[i].imported.character.id.c_str(),selection==i)){selection=i;changed=true;}ImGui::PopID();} ImGui::EndCombo();}
            if(singer && selection!=before) { singer->character=selection; singersChanged=true; }
            if(removable) { ImGui::SameLine(); if(ImGui::Button("x##remove-actor",{ImGui::GetFrameHeight(),0})) { g.actors[line].erase(g.actors[line].begin()+(std::ptrdiff_t)slot); changed=true; ImGui::PopID(); break; } tooltip(ui("Quitar de esta línea","Remove from this line")); }
            ImGui::PopID();
        }
        if(!singer && g.actors[line].size()<16 && ImGui::SmallButton(ui("+ Personaje en esta línea","+ Character on this line"))) { g.actors[line].push_back(-1); changed=true; }
        ImGui::PopID();
    }
    // Characters that sing a note type: found in the song's scripts, editable.
    std::set<std::string> customTypes; for(const auto& n:g.chart.notes) if(!n.type.empty()) customTypes.insert(n.type);
    if(!g.typeSingers.empty() || !customTypes.empty()) {
        ImGui::Spacing(); ImGui::TextUnformatted(ui("Personajes por tipo de nota","Characters by note type"));
        tooltip(ui("Las notas de ese tipo son el turno de ese personaje, en su propia línea; el de la línea calla. Un tipo con «-duet» lo cantan los dos. Se detectan en los scripts de la canción.","Notes of that type are that character's turn, on its own line; the line's character stays quiet. A type with \"-duet\" is sung by both. Found in the song's scripts."));
        for(size_t k=0;k<g.typeSingers.size();++k) {
            ImGui::PushID(("singer-"+std::to_string(k)).c_str()); auto& s=g.typeSingers[k];
            ImGui::Text("«%s»",s.type.c_str()); ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemSpacing.x);
            auto caption=s.character>=0 && (size_t)s.character<app.catalog.characters.size()?app.catalog.characters[s.character].imported.character.id:std::string(ui("Ninguno","None"));
            if(ImGui::BeginCombo("##singer",caption.c_str())) { for(int i=0;i<(int)app.catalog.characters.size();++i){ImGui::PushID(i);if(ImGui::Selectable(app.catalog.characters[i].imported.character.id.c_str(),s.character==i)){s.character=i;singersChanged=true;}ImGui::PopID();} ImGui::EndCombo(); }
            ImGui::SameLine(); if(ImGui::Button("x##remove-singer",{ImGui::GetFrameHeight(),0})) { g.typeSingers.erase(g.typeSingers.begin()+(std::ptrdiff_t)k); singersChanged=true; ImGui::PopID(); break; }
            ImGui::PopID();
        }
        std::vector<std::string> free; for(const auto& t:customTypes) if(std::none_of(g.typeSingers.begin(),g.typeSingers.end(),[&](const Gameplay::TypeSinger& s){return t.find(s.type)!=std::string::npos;})) free.push_back(t);
        if(!free.empty()) {
            ImGui::SetNextItemWidth(-1);
            if(ImGui::BeginCombo("##singer-add",ui("+ Asignar un tipo de nota a un personaje","+ Give a note type to a character"))) { for(const auto& t:free) if(ImGui::Selectable(t.c_str())) { g.typeSingers.push_back({t,-1,-1}); singersChanged=true; } ImGui::EndCombo(); }
        }
    }
    if(singersChanged) { g.resetSingers(app.catalog); g.seek(g.positionMs,app.atlases); }
    else if(changed) {g.rebuild(app.catalog);g.seek(g.positionMs,app.atlases);}
}
fml::RatingPopupHud popupWithAssets(const App& app,const fml::RatingPopupHud& source) {
    const auto& g=app.game;auto popup=source;popup.ratingScale=g.ratingScale;popup.comboScale=g.comboScale;
    auto found=g.hud.ratings.find(popup.ratingName);if(found!=g.hud.ratings.end())popup.rating=found->second;
    popup.combo=g.hud.combo;popup.digits=g.hud.digits;return popup;
}
// The modchart's camera shaders as FML's camera chain, each instance with
// its own program (two "mirror"s of one effect hold different values), in
// the order they went on the camera.
std::vector<fml::GlRenderer::ShaderPass> modchartPasses(App& app) {
    std::vector<fml::GlRenderer::ShaderPass> passes; auto& g=app.game;
    if(!g.modchartOn || !g.modchart) return passes;
    std::vector<const ModShader*> order; for(const auto& s:g.modchart->shaders) if(s.game && !s.frag.empty()) order.push_back(&s);
    std::stable_sort(order.begin(),order.end(),[](const ModShader* a,const ModShader* b){return a->order<b->order;});
    for(const auto* s:order) {
        const auto key=s->frag.generic_u8string()+"#"+s->name;
        auto* loaded=app.modShaders.find(key);
        if(!loaded) {
            fml::DiagnosticSink sink; const auto frag=read(s->frag);
            if(s->vert.empty()) app.modShaders.load(key,frag,sink); else app.modShaders.loadRaw(key,frag,read(s->vert),sink);
            loaded=app.modShaders.find(key);
            if(loaded && !loaded->ok) logs::warn("Shader "+s->effect+" no compila: "+loaded->errorHuman.substr(0,600));
        }
        if(!loaded || !loaded->ok) continue;
        const auto values=g.modFrame.shaders.find(s->name);
        for(auto& u:loaded->uniforms) {
            if(u.isSampler || u.vertexAttribute) continue;
            float v=0; bool have=false;
            if(values!=g.modFrame.shaders.end()) { auto f=values->second.find(u.name); if(f!=values->second.end()) { v=f->second; have=true; } }
            if(!have) { auto f=s->initial.find(u.name); if(f!=s->initial.end()) { v=f->second; have=true; } }
            if(u.name=="iTime") { v=(float)(g.positionMs/1000.0); have=true; }
            if(have) { u.value[0]=v; u.assigned=true; }
        }
        passes.push_back({loaded->program,&loaded->uniforms});
    }
    return passes;
}
// Receptors where the modchart has them: their draw commands and the list the
// notes are placed against, so the notes follow.
void applyModReceptors(const ModFrame& frame,fml::RenderList& render,std::vector<fml::StrumReceptor>& receptors) {
    for(auto& r:receptors) { const auto m=frame.receptor(r.strumLine,r.index); r.x+=m.x; r.y+=m.y; r.objectX+=m.x; r.objectY+=m.y; r.alpha*=m.alpha; }
    for(auto& cmd:render.cmds) {
        int line=0,index=0; if(std::sscanf(cmd.runtimeObjectId.c_str(),"strum:%d:receptor:%d",&line,&index)!=2) continue;
        const auto m=frame.receptor(line,index);
        const float cx=cmd.x+cmd.w*.5f+m.x,cy=cmd.y+cmd.h*.5f+m.y;
        cmd.w*=m.scaleX; cmd.h*=m.scaleY; cmd.alpha*=m.alpha;
        if(m.angle!=0) { const float rad=m.angle*3.14159265f/180,c=std::cos(rad),s=std::sin(rad); cmd.ma=c; cmd.mb=s; cmd.mc=-s; cmd.md=c; }
        cmd.x=cx-(cmd.ma*cmd.w*.5f+cmd.mc*cmd.h*.5f); cmd.y=cy-(cmd.mb*cmd.w*.5f+cmd.md*cmd.h*.5f);
    }
}
void previewScene(App& app,ImVec2 at,ImVec2 size) {
    PI_PROFILE("preview.scene");
    auto& g=app.game;auto bindings=g.bindings(); fml::RenderList render;{PI_PROFILE("preview.scene.build");fml::buildStageRenderList(g.scene,bindings,app.atlases,app.textures,render,&g.animator);}
    // PlayState.hx:734/777: comboGroup is part of camGame, drawn over the stage.
    if(PreviewHudAvailable && g.showHud)for(const auto& popup:g.popups)fml::appendRatingPopupRenderList(popupWithAssets(app,popup),g.positionMs,render);
    fml::ViewportTransform view;
    if(g.engineCamera && g.song>=0) {
        // What Codename shows: camGame follows the singer with the stage zoom
        // and the beat bop. The view zoom/pan only moves the editor's window.
        render.camera=g.director.camera();
        // The modchart's camGame, when it sets one.
        for(const auto& p:g.modFrame.game) { if(p.first=="zoom" || p.first=="defaultZoom") render.camera.zoom=p.second; else if(p.first=="angle") render.camera.angle=p.second; }
        view.zoom=g.viewZoom;view.panX=g.viewPanX;view.panY=g.viewPanY;
    } else {
        if(g.fitScene) {
            bool actors=false;for(const auto& cmd:render.cmds)if(cmd.objectIndex>=0 && (size_t)cmd.objectIndex<g.scene.objects.size() && g.scene.objects[cmd.objectIndex].kind==fml::StageObject::Kind::Character)actors=true;
            fml::PreviewSceneBounds bounds{g.haveViewBounds,g.viewLeft,g.viewRight,g.viewTop,g.viewBottom};
            fml::includePreviewSceneBounds(render,bounds,[&](const fml::DrawCmd& cmd){return !actors || (cmd.objectIndex>=0 && (size_t)cmd.objectIndex<g.scene.objects.size() && g.scene.objects[cmd.objectIndex].kind==fml::StageObject::Kind::Character);});
            g.haveViewBounds=bounds.any;g.viewLeft=bounds.left;g.viewRight=bounds.right;g.viewTop=bounds.top;g.viewBottom=bounds.bottom;
            view=fml::fitPreviewScene(bounds,(int)size.x,(int)size.y,24,g.showHud?55:20);
        }
        view.zoom*=g.viewZoom;view.panX=view.panX*g.viewZoom+g.viewPanX;view.panY=view.panY*g.viewZoom+g.viewPanY;
    }
    const auto passes=modchartPasses(app);
    PI_PROFILE("preview.scene.draw");drawScenePreview(app,render,at,size,view,false,false,passes.empty()?nullptr:&passes);
    if(app.showCameraFrame)drawCameraFrame(at,size,view,app.cameraFrame);
}
void previewArrow(ImDrawList* draw,ImVec2 center,float scale,int direction,ImU32 color) {
    const float shape[7][2]={{-14,0},{-2,-12},{-2,-5},{12,-5},{12,5},{-2,5},{-2,12}};
    float angle=direction==0?0:direction==1?-1.5707963f:direction==2?1.5707963f:3.1415926f;ImVec2 points[7];
    for(int i=0;i<7;++i)points[i]={center.x+(shape[i][0]*std::cos(angle)-shape[i][1]*std::sin(angle))*scale,center.y+(shape[i][0]*std::sin(angle)+shape[i][1]*std::cos(angle))*scale};
    draw->AddConcavePolyFilled(points,7,color);
}
void previewHudCommands(App& app,const fml::RenderList& render,ImVec2 at,ImVec2 size) {
    auto* draw=ImGui::GetWindowDrawList();float scale=size.x/1280;const float zoom=app.game.hudZoom();
    for(const auto& cmd:render.cmds) {
        if(!cmd.visible || cmd.alpha<=0)continue;
        float x[4],y[4],q[4];fml::drawCmdCorners(cmd,x,y,q);ImVec2 p[4];for(int i=0;i<4;++i)p[i]={at.x+(640+(x[i]-640)*zoom)*scale,at.y+(360+(y[i]-360)*zoom)*scale};
        auto tint=ImGui::ColorConvertFloat4ToU32({cmd.colorR,cmd.colorG,cmd.colorB,cmd.alpha});
        if(cmd.texture==fml::DrawCmd::SolidTexture)draw->AddQuadFilled(p[0],p[1],p[2],p[3],tint);
        else if(cmd.texture>=0 && (size_t)cmd.texture<render.textures.size()) {
            auto source=render.textures[cmd.texture];auto& hud=app.game.hud;
            auto& t=source==hud.notes.image && hud.preparedNotes?app.textures.memory(source,hud.preparedNotes->pixels):source==hud.notes.image && hud.paletteSheet?app.textures.memory("preview-rgb:"+source,*hud.paletteSheet):app.textures.get(fs::u8path(source));if(!t.id)continue;
            float u0=cmd.sx/t.w,v0=cmd.sy/t.h,u1=(cmd.sx+cmd.sw)/t.w,v1=(cmd.sy+cmd.sh)/t.h;if(cmd.flipX)std::swap(u0,u1);if(cmd.flipY)std::swap(v0,v1);
            draw->AddImageQuad(ImTextureRef((ImTextureID)t.id),p[0],p[1],p[2],p[3],{u0,v0},{u1,v0},{u1,v1},{u0,v1},tint);
        }
    }
}
// Psych notes take their receptor's alpha (Note.copyAlpha): a modchart that
// hides a lane hides its notes too. The exported script does the same.
float modNoteAlpha(const ModFrame& frame,const fml::UniversalChart& chart,int note) {
    if(note<0 || (size_t)note>=chart.notes.size()) return 1;
    const auto& n=chart.notes[note]; return frame.receptor(n.strumLine,n.lane).alpha;
}
void applyModNotes(const ModFrame& frame,const fml::UniversalChart& chart,fml::RenderList& render,size_t first) {
    for(size_t i=first;i<render.cmds.size();++i) { int note=-1; if(std::sscanf(render.cmds[i].runtimeObjectId.c_str(),"note:%d",&note)==1) render.cmds[i].alpha*=modNoteAlpha(frame,chart,note); }
}
void previewGenericNotes(App& app,ImVec2 at,ImVec2 size,const ModFrame* frame=nullptr) {
    auto& g=app.game;auto* draw=ImGui::GetWindowDrawList();float scale=size.x/1280;
    fml::NoteSkinMetrics schematic;schematic.ok=true;schematic.anchorW=schematic.anchorH=156;
    for(int i=0;i<4;++i){schematic.have[i]=schematic.haveNote[i]=schematic.haveHold[i]=schematic.haveHoldEnd[i]=true;schematic.arrow[i].w=schematic.arrow[i].h=156;schematic.arrow[i].frameW=schematic.arrow[i].frameH=156;schematic.note[i]=schematic.arrow[i];schematic.hold[i].w=24;schematic.hold[i].h=40;schematic.holdEnd[i]=schematic.hold[i];}
    std::vector<fml::NoteSkinMetrics> skins(g.chart.strumLines.size(),schematic);fml::GameplayOptions options;options.downscroll=g.downscroll;
    std::vector<fml::StrumReceptor> receptors;fml::layoutStrumLines(g.chart,skins,options,receptors);
    if(frame) for(auto& r:receptors) { const auto m=frame->receptor(r.strumLine,r.index); r.x+=m.x; r.y+=m.y; r.alpha*=m.alpha; }
    const ImU32 colors[]={IM_COL32(194,95,231,255),IM_COL32(85,164,244,255),IM_COL32(97,221,136,255),IM_COL32(242,100,112,255)};
    auto faded=[](ImU32 c,float alpha){ return (c&0x00FFFFFFu)|((ImU32)std::clamp((float)((c>>24)&0xFF)*alpha,0.0f,255.0f)<<24); };
    for(const auto& r:receptors)if(r.visible && r.alpha>0.01f)previewArrow(draw,{at.x+(r.x+r.w*.5f)*scale,at.y+(r.y+r.h*.5f)*scale},r.w*scale/30,r.direction,faded(IM_COL32(92,106,130,230),r.alpha));
    std::vector<fml::PlacedNote> placed;fml::layoutNotes(g.chart,skins,options,g.positionMs,receptors,placed,nullptr,&g.state);
    for(const auto& n:placed) {
        const float alpha=frame?modNoteAlpha(*frame,g.chart,n.index):1.0f; if(alpha<=0.01f) continue;
        auto p=ImVec2(at.x+(n.x+n.w*.5f)*scale,at.y+(n.y+n.h*.5f)*scale);auto color=faded(colors[(n.direction%4+4)%4],alpha);
        if(n.sustain)draw->AddRectFilled({at.x+n.x*scale,at.y+n.y*scale},{at.x+(n.x+n.w)*scale,at.y+(n.y+n.h)*scale},color,n.sustainEnd?3:0);
        else {previewArrow(draw,p,n.w*scale/30,n.direction,color);if(!g.chart.notes[n.index].type.empty())draw->AddCircle(p,n.w*scale*.43f,IM_COL32(255,203,92,255),12,1.5f);}
    }
}
// CoolUtil.quantize(accuracy * 100, 100) printed the way Haxe prints a Float.
std::string codenamePercent(double value) {
    char buffer[32];std::snprintf(buffer,sizeof buffer,"%.2f",std::round(value*100.0)/100.0);
    std::string text=buffer;while(!text.empty() && text.back()=='0')text.pop_back();if(!text.empty() && text.back()=='.')text.pop_back();
    return text+"%";
}
ImU32 codenameRankColor(const std::string& rank) {
    // PlayState.hx:602-609 and the "[N/A]" fallback of updateRatingStuff.
    if(rank=="F")return IM_COL32(255,68,68,255); if(rank=="E")return IM_COL32(255,136,68,255);
    if(rank=="D")return IM_COL32(255,170,68,255); if(rank=="C")return IM_COL32(255,255,68,255);
    if(rank=="B")return IM_COL32(170,255,68,255); if(rank=="A")return IM_COL32(136,255,68,255);
    if(rank=="S" || rank=="S++")return IM_COL32(68,255,255,255); return IM_COL32(136,136,136,255);
}
void previewGameHud(App& app,ImVec2 at,ImVec2 size) {
    if constexpr (!PreviewHudAvailable) return;
    PI_PROFILE("preview.hud");auto& g=app.game;
    // The HUD stays off when it is off, modchart or not (the first public
    // build has no HUD); with it on, the modchart moves receptors and notes.
    const bool modReceptors=g.modchartOn && g.modchart && g.modchart->count(ModTarget::Receptor)>0;
    if(!g.showHud)return;float scale=size.x/1280;auto* draw=ImGui::GetWindowDrawList();
    const float hudZoom=g.hudZoom();
    fml::RenderList render;
    if(!g.provisionalNotes && g.hud.notesValid) {
        if(g.hud.preparedNotes)app.atlases.putSparrow(g.hud.notes.atlas,g.hud.preparedNotes->atlas);
        fml::GameplayOptions options;options.downscroll=g.downscroll;std::vector<fml::NoteSkin> skins(g.chart.strumLines.size(),g.hud.notes);std::vector<fml::StrumReceptor> receptors;
        auto confirms=fml::receptorConfirms(g.chart,g.state,g.time,g.positionMs);
        fml::appendReceptorRenderList(g.chart,skins,app.atlases,options,render,&receptors,nullptr,&confirms);
        if(modReceptors) applyModReceptors(g.modFrame,render,receptors);
        const size_t firstNote=render.cmds.size();
        fml::appendNoteRenderList(g.chart,skins,app.atlases,options,g.positionMs,receptors,render,nullptr,nullptr,&g.state);
        if(modReceptors) applyModNotes(g.modFrame,g.chart,render,firstNote);
    } else previewGenericNotes(app,at,size,modReceptors?&g.modFrame:nullptr);
    if(!g.showHud) { previewHudCommands(app,render,at,size); return; }
    const auto& stats=g.state.stats();g.healthHud.health=(float)(stats.health/std::max(.01,stats.maxHealth));g.healthHud.downscroll=g.downscroll;g.healthHud.animationTimeSeconds=g.positionMs/1000;
    g.healthHud.iconScale=g.song>=0?fml::healthIconBumpScale(g.time,g.positionMs):1.0f;
    fml::HealthHudLayout layout;fml::appendHealthHudRenderList(g.healthHud,render,&layout);
    previewHudCommands(app,render,at,size);
    ImFont* font=ImGui::GetFont();if(!g.hud.font.empty()) {
        auto key=g.hud.font.u8string();auto found=app.hudFonts.find(key);
        if(found==app.hudFonts.end() && app.hudFonts.size()<8) {
            app.hudFonts[key]=nullptr;std::error_code ec;auto length=fs::file_size(g.hud.font,ec);
            if(!ec && length>100 && length<=8u*1024u*1024u) {
                auto bytes=read(g.hud.font);
                if(fml::validTrueType((const unsigned char*)bytes.data(),bytes.size())) {
                    void* memory=IM_ALLOC(bytes.size());
                    if(memory){std::memcpy(memory,bytes.data(),bytes.size());app.hudFonts[key]=ImGui::GetIO().Fonts->AddFontFromMemoryTTF(memory,(int)bytes.size(),24);}
                }
            }
            if(!app.hudFonts[key])g.hud.diagnostics.push_back(ui("Fuente del mod inválida o no compatible; se usa la fuente genérica.","Invalid or unsupported mod font; using the generic font."));
            found=app.hudFonts.find(key);
        }
        if(found!=app.hudFonts.end() && found->second)font=found->second;
    }
    // FunkinText(healthBarBG.x + 50, healthBarBG.y + 30, width - 100, …, 16)
    // with RIGHT/CENTER/LEFT alignment (PlayState.hx:906-918), on camHUD.
    if(layout.barW<=0)return;
    const float fieldX=layout.barX+50,fieldW=std::max(1.0f,layout.barW-100),fieldY=layout.barY+30;
    auto toScreen=[&](float x,float y){return ImVec2(at.x+(640+(x-640)*hudZoom)*scale,at.y+(360+(y-360)*hudZoom)*scale);};
    const float fontSize=std::max(9.0f,16*hudZoom*scale);
    auto text=[&](const std::string& value,int align,ImU32 color,const std::string& tail={},ImU32 tailColor=0) {
        auto whole=value+tail;auto box=font->CalcTextSizeA(fontSize,FLT_MAX,0,whole.c_str());
        auto left=toScreen(fieldX,fieldY),right=toScreen(fieldX+fieldW,fieldY);
        float x=align==0?left.x:align==1?(left.x+right.x-box.x)*.5f:right.x-box.x;ImVec2 p{x,left.y};
        for(auto offset:{ImVec2(-1,0),ImVec2(1,0),ImVec2(0,-1),ImVec2(0,1)})draw->AddText(font,fontSize,{p.x+offset.x,p.y+offset.y},IM_COL32(0,0,0,255),whole.c_str());
        draw->AddText(font,fontSize,p,color,value.c_str());
        if(!tail.empty())draw->AddText(font,fontSize,{p.x+font->CalcTextSizeA(fontSize,FLT_MAX,0,value.c_str()).x,p.y},tailColor,tail.c_str());
    };
    const ImU32 white=IM_COL32(255,255,255,255);
    const bool rated=stats.accuracyDenominator>0;const std::string rank=rated?stats.rank():"[N/A]";
    text("Accuracy:"+(rated?codenamePercent(stats.accuracy()*100):std::string("-%"))+" - ",0,white,rank,codenameRankColor(rank));
    text("Misses:"+std::to_string(stats.misses),1,white);
    text("Score:"+std::to_string(stats.score),2,white);
}
void previewViewport(App& app) {
    auto& g=app.game;advancePreview(app);g.updateModchart();
    std::string title=g.song>=0?app.catalog.songs[g.song].title+" / "+app.catalog.songs[g.song].difficulties[g.difficulty].name:ui("Vista de escena","Scene preview");
    ImGui::TextUnformatted(title.c_str());ImGui::SameLine();ImGui::TextDisabled("· Autoplay");ImGui::Separator();
    auto available=ImGui::GetContentRegionAvail();auto canvasAt=ImGui::GetCursorScreenPos();
    float controlsHeight=3*ImGui::GetFrameHeight()+2*ImGui::GetTextLineHeight()+22+7*ImGui::GetStyle().ItemSpacing.y;
    float h=std::max(140.0f,available.y-controlsHeight);
    ImVec2 size{std::min(available.x,h*1280/720),0};size.y=size.x*720/1280;ImVec2 at{canvasAt.x+(available.x-size.x)*.5f,canvasAt.y+(h-size.y)*.5f};
    // The scene fills the whole panel; the game's 16:9 frame sits centred in
    // it (the camera frame marks it) and the HUD stays inside that frame. At
    // zoom 1 the frame is exactly the old canvas: the same fit, by height.
    const ImVec2 full{available.x,h};
    ImGui::InvisibleButton("##game-canvas",full,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);auto* draw=ImGui::GetWindowDrawList();
    app.viewCanvasCenters[app.tab]={canvasAt.x+full.x*.5f,canvasAt.y+full.y*.5f};
    interactPreviewView(g.viewZoom,g.viewPanX,g.viewPanY,app.viewCanvasCenters[app.tab]);
    draw->PushClipRect(canvasAt,{canvasAt.x+full.x,canvasAt.y+full.y},true);draw->AddRectFilled(canvasAt,{canvasAt.x+full.x,canvasAt.y+full.y},IM_COL32(20,29,43,255));
    previewScene(app,canvasAt,full);draw->PopClipRect();
    ImGui::BeginDisabled(app.gameAudio.trackLoadPending());
    if(ImGui::Button(g.playing?ui("Pausar","Pause"):ui("Reproducir","Play"))) {g.playing=!g.playing;app.audio.pause();if(g.playing){if(g.positionMs>=previewDuration(app)-1)seekPreview(app,0);if(app.gameAudio.trackCount())app.gameAudio.play();}else app.gameAudio.pause();}
    ImGui::SameLine();if(ImGui::Button(ui("Inicio","Restart")))seekPreview(app,0);ImGui::SameLine();wrappedCheckbox(ui("Repetir","Loop"),&g.loop);ImGui::EndDisabled();
    float seconds=g.positionMs/1000,duration=previewDuration(app)/1000;ImGui::TextDisabled("%d:%02d / %d:%02d · %.1f BPM",(int)seconds/60,(int)seconds%60,(int)duration/60,(int)duration%60,g.time.bpmAt(g.positionMs));
    ImGui::SetNextItemWidth(-1);if(ImGui::SliderFloat("##game-seek",&seconds,0,duration,"%.2f s"))seekPreview(app,seconds*1000);
    PI_PROFILE("preview.waveform");auto peaks=app.gameAudio.waveformPeaks(220);if(!peaks.empty()) {auto p=ImGui::GetCursorScreenPos();ImVec2 waveSize{ImGui::GetContentRegionAvail().x,22};ImGui::InvisibleButton("##game-wave",waveSize);for(size_t i=0;i<peaks.size();++i){float x=p.x+waveSize.x*i/peaks.size(),v=peaks[i]*10;draw->AddLine({x,p.y+11-v},{x,p.y+11+v},IM_COL32(66,158,200,255));}if(ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))seekPreview(app,std::clamp((ImGui::GetIO().MousePos.x-p.x)/waveSize.x,0.0f,1.0f)*duration*1000);}
    float controlWidth=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.5f;
    ImGui::BeginGroup();ImGui::TextUnformatted(ui("Velocidad","Speed"));ImGui::SetNextItemWidth(controlWidth);if(ImGui::SliderFloat("##game-rate",&g.rate,.25f,2,"%.2fx"))app.gameAudio.setRate(g.rate);ImGui::EndGroup();ImGui::SameLine();
    ImGui::BeginGroup();ImGui::TextUnformatted(ui("Volumen","Volume"));ImGui::SetNextItemWidth(controlWidth);if(ImGui::SliderFloat("##game-volume",&g.volume,0,1,"%.2f"))app.gameAudio.setMasterGain(g.volume);ImGui::EndGroup();
    if(app.gameAudio.trackLoadPending())ImGui::TextDisabled(ui("Cargando audio...","Loading audio..."));
}
void previewCompatibility(App& app,SDL_Window* window) {
    PI_PROFILE("preview.options");
    auto& g=app.game;ImGui::TextUnformatted(ui("Opciones de preview","Preview options"));ImGui::Separator();
    ImGui::TextUnformatted(ui("Al cambiar canción","When switching songs"));ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##song-load-mode",g.keepComposition?ui("Solo chart y música","Chart and music only"):ui("Todos sus assets","All song assets"))) {
        if(ImGui::Selectable(ui("Todos sus assets","All song assets"),!g.keepComposition))g.keepComposition=false;
        if(ImGui::Selectable(ui("Solo chart y música","Chart and music only"),g.keepComposition))g.keepComposition=true;
        ImGui::EndCombo();
    }
    tooltip(ui("Solo chart y música conserva escenario, personajes y ajustes de reproducción.","Chart and music only keeps the stage, characters and playback settings."));
    if(g.song>=0 && (size_t)g.song<app.catalog.songs.size()) {
        const auto& song=app.catalog.songs[g.song];ImGui::Spacing();ImGui::TextUnformatted(ui("Dificultad","Difficulty"));ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##preview-difficulty",song.difficulties[g.difficulty].name.c_str())) {for(int i=0;i<(int)song.difficulties.size();++i)if(ImGui::Selectable(song.difficulties[i].name.c_str(),g.difficulty==i))choosePreviewSong(app,g.song,i,true);ImGui::EndCombo();}
        ImGui::TextDisabled("%.1f BPM · %.2fx · %zu %s",g.chart.bpm,g.chart.speed,g.chart.notes.size(),ui("notas","notes"));
        if(ImGui::SmallButton(ui("Recargar canción","Reload song")))choosePreviewSong(app,g.song,g.difficulty);
    }
    ImGui::Spacing();ImGui::Separator();
    previewZoomControls(g.viewZoom);
    if(g.modchartLoading()) ImGui::TextDisabled("%s",ui("Leyendo el modchart de la canción...","Reading the song's modchart..."));
    else if(g.modchart && !g.modchart->empty()) {
        wrappedCheckbox(uiId("Modchart de la canción","Song modchart","preview-modchart").c_str(),&g.modchartOn);
        std::string detail=modchartSummary(*g.modchart); for(const auto& s:g.modchart->sources) detail+="\n"+s.filename().u8string();
        if(!g.modchart->notes.empty()) detail+="\n"+std::to_string(g.modchart->notes.size())+" "+ui("partes que no se pudieron leer (en el informe)","parts that could not be read (in the report)");
        tooltip(detail); ImGui::TextDisabled("%s",modchartSummary(*g.modchart).c_str());
    }
    wrappedCheckbox(ui("Cámara del juego","Game camera"),&g.engineCamera);
    tooltip(ui("Como en Codename: la cámara sigue a quien canta, con el zoom del escenario y el rebote por beat.","As in Codename: the camera follows the singer with the stage zoom and the beat bop."));
    cameraFrameToggle(app,ui("El marco de 1280×720 que muestra el juego, como en FML. Se ve al alejar o mover la vista.","The 1280×720 frame the game shows, as in FML. Visible when the view zooms out or moves."));
    ImGui::BeginDisabled(g.engineCamera && g.song>=0);wrappedCheckbox(ui("Encuadrar escena","Fit scene"),&g.fitScene);ImGui::EndDisabled();nextControl(buttonWidth(ui("Centrar","Center")));if(ImGui::SmallButton(ui("Centrar","Center"))){g.viewZoom=1;g.viewPanX=g.viewPanY=0;}
    auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();app.viewCenterButtons[app.tab]={(a.x+b.x)*.5f,(a.y+b.y)*.5f};
    tooltip(ui("Arrastra con el botón izquierdo o central y usa la rueda para ampliar.","Drag with the left or middle button and use the wheel to zoom."));
    if(ImGui::CollapsingHeader(ui("Escena y personajes","Scene and characters"),ImGuiTreeNodeFlags_DefaultOpen))previewComposition(app);
    if(ImGui::CollapsingHeader(ui("Recursos usados","Active resources"))) {
        pathRow("Inst",fs::u8path(g.audio.instPath));
        for(const auto& p:{g.audio.voicesPath,g.audio.playerVoicesPath,g.audio.opponentVoicesPath})if(!p.empty())pathRow("Voices",fs::u8path(p));
    }
    if(g.song>=0 && ImGui::SmallButton(ui("Informe de conversión","Conversion report"))) {
        const auto& d=app.catalog.songs[g.song].difficulties[g.difficulty];
        app.report=json{{"song",app.catalog.songs[g.song].id},{"difficulty",d.name},{"events",eventReport(d)},{"notes",noteReport(app.catalog,d)}}.dump(2);app.reportOpen=true;
    }
}
