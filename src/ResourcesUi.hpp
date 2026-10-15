#pragma once

// Included after App and GameplayUi. The resource manager: every file of the
// mod with what uses it, a player for Inst/Voices (alone or together, as in
// Note Lab's resource panel) and what each song, character and stage still
// lacks before export.

inline ImVec4 resourceColor(int level) {
    return level==2?ImVec4(1.0f,.42f,.42f,1):level==1?ImVec4(1.0f,.78f,.35f,1):ImVec4(.45f,.85f,.55f,1);
}
inline int resourceLevel(const ResourceItem& item) { return !item.problems.empty()?2:item.uses.empty()?1:0; }
inline const char* resourceKindLabel(ResourceKind kind) {
    switch(kind) {
        case ResourceKind::Audio: return ui("Audio","Audio");
        case ResourceKind::Image: return ui("Imagen","Image");
        case ResourceKind::Atlas: return ui("Atlas","Atlas");
        case ResourceKind::Chart: return ui("Chart / JSON","Chart / JSON");
        case ResourceKind::Script: return ui("Script","Script");
    }
    return "";
}
inline std::string fileSizeText(const fs::path& p) {
    std::error_code ec; auto size=fs::file_size(p,ec); if(ec) return "-";
    char text[32]; if(size>=1024ull*1024ull) std::snprintf(text,sizeof text,"%.1f MB",size/1048576.0); else std::snprintf(text,sizeof text,"%.0f KB",size/1024.0);
    return text;
}
void ensureResources(App& app) {
    if(app.resourcesDirty) {
        // Rebuilt on entering the tab: the selected file stays selected (and
        // its player keeps going).
        std::string kept=app.resource>=0 && (size_t)app.resource<app.resources.items.size()?resourceKey(app.resources.items[app.resource].path):std::string();
        app.resources=buildResourceIndex(app.catalog); app.resourcesDirty=false; app.resourceFiltered.clear(); app.resource=-1;
        if(auto found=app.resources.byPath.find(kept);!kept.empty() && found!=app.resources.byPath.end()) app.resource=(int)found->second;
    }
}
void ensureReadiness(App& app) {
    if(app.readinessBusy && app.readinessJob.valid() && app.readinessJob.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try { app.readiness=app.readinessJob.get(); } catch(const std::exception& e) { app.readiness.clear(); app.status=e.what(); }
        app.readinessBusy=false;
    }
    if(app.readinessDirty && !app.readinessBusy && !app.busy && (!app.catalog.songs.empty() || !app.catalog.characters.empty() || !app.catalog.stages.empty())) {
        app.readinessDirty=false; app.readinessBusy=true;
        app.readinessCancel=std::make_shared<std::atomic<bool>>(false);
        auto cancel=app.readinessCancel;
        app.readinessJob=std::async(std::launch::async,[catalog=app.catalog,base=app.base,cancel]{ return exportReadiness(catalog,base,cancel.get()); });
    }
}
// The player: one file, or every track of the song difficulty that uses it.
void loadAudition(App& app,const ResourceItem& item,bool withSong) {
    app.auditionAudio.pause(); app.auditionAudio.clearLoop(); app.auditionRange=false; app.auditionPeaks.clear();
    std::vector<std::string> paths{item.path.u8string()};
    if(withSong) for(const auto& use:item.uses) if(use.tab==1 && use.index>=0 && use.difficulty>=0 && (size_t)use.index<app.catalog.songs.size()) {
        const auto& a=app.catalog.songs[use.index].difficulties[use.difficulty].audio; paths.clear();
        for(const auto& p:{a.instPath,a.voicesPath,a.playerVoicesPath,a.opponentVoicesPath}) if(!p.empty()) paths.push_back(p);
        break;
    }
    app.auditionKey=resourceKey(item.path)+(withSong?"|song":"");
    app.auditionAudio.loadTracksAsync(paths);
}
void resourceAudio(App& app,const ResourceItem& item) {
    bool fromSong=false; for(const auto& use:item.uses) if(use.tab==1 && use.difficulty>=0) fromSong=true;
    if(fromSong && wrappedCheckbox(ui("Escuchar con su canción (Inst + Voices juntos)","Play with its song (Inst + Voices together)"),&app.auditionWithSong)) loadAudition(app,item,app.auditionWithSong);
    const auto wanted=resourceKey(item.path)+(app.auditionWithSong && fromSong?"|song":"");
    if(app.auditionKey!=wanted) loadAudition(app,item,app.auditionWithSong && fromSong);
    const bool ready=app.auditionAudio.trackCount() && !app.auditionAudio.trackLoadPending();
    if(!ready) { ImGui::TextDisabled("%s",app.auditionAudio.trackLoadPending()?ui("Cargando audio...","Loading audio..."):ui("No se pudo cargar el audio.","Audio could not be loaded.")); return; }
    if(app.auditionPeaks.empty()) app.auditionPeaks=app.auditionAudio.waveformPeaks(360);
    // Waveform with the play head; a click seeks there.
    const float duration=(float)(app.auditionAudio.durationMs()/1000.0);
    auto at=ImGui::GetCursorScreenPos();ImVec2 size{ImGui::GetContentRegionAvail().x,64};
    ImGui::InvisibleButton("##resource-wave",size);auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(at,{at.x+size.x,at.y+size.y},IM_COL32(20,29,43,255),4);
    if(app.auditionRange && duration>0) draw->AddRectFilled({at.x+size.x*app.auditionA/duration,at.y},{at.x+size.x*app.auditionB/duration,at.y+size.y},IM_COL32(66,158,200,48));
    for(size_t i=0;i<app.auditionPeaks.size();++i){float x=at.x+size.x*i/app.auditionPeaks.size(),v=app.auditionPeaks[i]*size.y*.46f;draw->AddLine({x,at.y+size.y*.5f-v},{x,at.y+size.y*.5f+v},IM_COL32(66,158,200,255));}
    float pos=(float)(app.auditionAudio.positionMs()/1000.0);
    if(duration>0){float x=at.x+size.x*pos/duration;draw->AddLine({x,at.y},{x,at.y+size.y},IM_COL32(255,255,255,220),2);}
    if(ImGui::IsItemHovered() && ImGui::IsMouseClicked(0) && duration>0) app.auditionAudio.seekMs(std::clamp((ImGui::GetIO().MousePos.x-at.x)/size.x,0.0f,1.0f)*duration*1000);
    if(ImGui::Button(app.auditionAudio.playing()?ui("Pausar","Pause"):ui("Reproducir","Play"))) { if(app.auditionAudio.playing()) app.auditionAudio.pause(); else { if(app.auditionRange && (pos<app.auditionA || pos>=app.auditionB)) app.auditionAudio.seekMs(app.auditionA*1000); app.auditionAudio.play(); } }
    ImGui::SameLine(); if(ImGui::Button(ui("Detener","Stop"))) { app.auditionAudio.pause(); app.auditionAudio.seekMs(app.auditionRange?app.auditionA*1000:0); }
    ImGui::SameLine(); if(ImGui::Checkbox(ui("Repetir","Loop"),&app.auditionLoop)) { if(app.auditionLoop) app.auditionAudio.setLoop(app.auditionRange?app.auditionA*1000:0,app.auditionRange?app.auditionB*1000:app.auditionAudio.durationMs()); else app.auditionAudio.clearLoop(); }
    ImGui::SameLine(); ImGui::TextDisabled("%d:%02d / %d:%02d",(int)pos/60,(int)pos%60,(int)duration/60,(int)duration%60);
    ImGui::SetNextItemWidth(-1); if(ImGui::SliderFloat("##resource-seek",&pos,0,std::max(.01f,duration),"%.2f s")) app.auditionAudio.seekMs(pos*1000);
    float half=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.5f;
    float gain=app.auditionAudio.masterGain(),rate=app.auditionAudio.rate();
    ImGui::BeginGroup();ImGui::TextUnformatted(ui("Volumen","Volume"));ImGui::SetNextItemWidth(half);if(ImGui::SliderFloat("##resource-gain",&gain,0,1,"%.2f"))app.auditionAudio.setMasterGain(gain);ImGui::EndGroup();ImGui::SameLine();
    ImGui::BeginGroup();ImGui::TextUnformatted(ui("Velocidad (cambia el tono)","Speed (changes pitch)"));ImGui::SetNextItemWidth(half);if(ImGui::SliderFloat("##resource-rate",&rate,.25f,2,"%.2fx"))app.auditionAudio.setRate(rate);ImGui::EndGroup();
    ImGui::TextUnformatted(ui("Balance estéreo","Stereo balance"));ImGui::SetNextItemWidth(-1);
    if(ImGui::SliderFloat("##resource-pan",&app.auditionPan,-1,1,"%.2f")) for(size_t i=0;i<app.auditionAudio.trackCount();++i) app.auditionAudio.setTrackPan(i,app.auditionPan);
    if(ImGui::Checkbox(ui("Tramo A–B","A–B range"),&app.auditionRange)) {
        if(app.auditionRange && app.auditionB<=app.auditionA) { app.auditionA=0; app.auditionB=duration; }
        if(app.auditionLoop) { if(app.auditionRange) app.auditionAudio.setLoop(app.auditionA*1000,app.auditionB*1000); else app.auditionAudio.setLoop(0,app.auditionAudio.durationMs()); }
    }
    if(app.auditionRange) {
        ImGui::SetNextItemWidth(-1);
        if(ImGui::DragFloatRange2("##resource-range",&app.auditionA,&app.auditionB,.05f,0,duration,"A %.2f s","B %.2f s",ImGuiSliderFlags_AlwaysClamp) && app.auditionLoop) app.auditionAudio.setLoop(app.auditionA*1000,app.auditionB*1000);
        if(ImGui::SmallButton(ui("Marcar A aquí","Set A here"))) { app.auditionA=pos; if(app.auditionLoop) app.auditionAudio.setLoop(app.auditionA*1000,app.auditionB*1000); }
        ImGui::SameLine(); if(ImGui::SmallButton(ui("Marcar B aquí","Set B here"))) { app.auditionB=pos; if(app.auditionLoop) app.auditionAudio.setLoop(app.auditionA*1000,app.auditionB*1000); }
    }
    if(app.auditionAudio.trackCount()>1) {
        ImGui::TextUnformatted(ui("Pistas","Tracks"));
        for(size_t i=0;i<app.auditionAudio.trackCount();++i) {
            ImGui::PushID((int)i);bool on=app.auditionAudio.trackGain(i)>0;
            if(ImGui::Checkbox(fs::u8path(app.auditionAudio.trackName(i)).filename().u8string().c_str(),&on)) app.auditionAudio.setTrackGain(i,on?1.0f:0.0f);
            ImGui::PopID();
        }
    }
    ImGui::TextDisabled("%s",ui("Lo que ajustes aquí es solo para escuchar: no cambia el archivo ni la exportación.","Settings here are for listening only: they do not change the file or the export."));
}
void resourceImage(App& app,const ResourceItem& item) {
    auto& texture=app.textures.get(item.path);
    if(!texture.id) { ImGui::TextWrapped("%s",ui("No se pudo abrir la imagen.","The image could not be opened.")); return; }
    auto info=app.textures.renderer.imageInfo(item.path.u8string());
    const int w=info.ok?info.w:texture.w,h=info.ok?info.h:texture.h;
    ImGui::TextDisabled("%d x %d px · %.1f MB %s",w,h,(double)w*h*4/1048576.0,ui("en memoria de vídeo","of video memory"));
    if(w>4096 || h>4096) ImGui::TextColored(resourceColor(1),"%s",ui("Más de 4096 px: la vista previa la reduce; Codename la carga entera.","Over 4096 px: the preview reduces it; Codename loads it whole."));
    auto at=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();size.y=std::max(120.0f,size.y-8);
    ImGui::InvisibleButton("##resource-image",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);interactCanvas(app);
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(at,{at.x+size.x,at.y+size.y},true);
    draw->AddRectFilled(at,{at.x+size.x,at.y+size.y},IM_COL32(20,29,43,255));
    const float fit=std::min(size.x/std::max(1,w),size.y/std::max(1,h))*app.zoom;
    ImVec2 c{at.x+size.x*.5f+app.panX,at.y+size.y*.5f+app.panY},half{w*fit*.5f,h*fit*.5f};
    draw->AddImage(ImTextureRef((ImTextureID)texture.id),{c.x-half.x,c.y-half.y},{c.x+half.x,c.y+half.y});
    draw->PopClipRect();
}
void resourceText(App& app,const ResourceItem& item) {
    if(app.resourceTextKey!=resourceKey(item.path)) {
        app.resourceTextKey=resourceKey(item.path);
        std::error_code ec;auto size=fs::file_size(item.path,ec);
        app.resourceText=ec?std::string():read(item.path).substr(0,96*1024);
        if(!ec && size>96*1024) app.resourceText+="\n…";
    }
    if(item.kind==ResourceKind::Atlas) {
        std::set<std::string> prefixes; size_t frames=0;
        if(lower(item.path.extension().u8string())==".txt") for(const auto& n:packerFrameNames(app.resourceText)) { ++frames; auto p=n; while(!p.empty() && (std::isdigit((unsigned char)p.back()) || p.back()=='_')) p.pop_back(); prefixes.insert(p); }
        else { size_t at=0; while((at=app.resourceText.find("name=\"",at))!=std::string::npos) { at+=6; auto end=app.resourceText.find('"',at); auto n=app.resourceText.substr(at,end-at); ++frames; while(!n.empty() && std::isdigit((unsigned char)n.back())) n.pop_back(); prefixes.insert(n); at=end; } }
        ImGui::Text("%zu %s · %zu %s",frames,ui("fotogramas","frames"),prefixes.size(),ui("animaciones","animations"));
        if(ImGui::TreeNode(ui("Animaciones del atlas","Atlas animations"))) { int shown=0; for(const auto& p:prefixes) { if(++shown>80) { ImGui::TextDisabled("…"); break; } ImGui::BulletText("%s",p.c_str()); } ImGui::TreePop(); }
    }
    ImGui::InputTextMultiline("##resource-text",app.resourceText.data(),app.resourceText.size()+1,ImVec2(-1,std::max(120.0f,ImGui::GetContentRegionAvail().y-6)),ImGuiInputTextFlags_ReadOnly);
}
void goToUse(App& app,const ResourceUse& use) {
    if(use.tab<0 || use.index<0) return;
    app.tab=use.tab; app.filter[0]=0; app.select(use.index);
    if(use.tab==1 && use.difficulty>=0 && (size_t)use.difficulty<app.catalog.songs[use.index].difficulties.size()) { app.difficulty=use.difficulty; app.loadAudio(); }
}
// Audio and images split by use, as Note Lab splits sounds: one button per
// use with its count, and "All".
bool resourceUseFilter(App& app) {
    const bool audio=app.resourceFilter==1; int& current=audio?app.resourceAudioUse:app.resourceImageUse;
    const auto kind=audio?ResourceKind::Audio:ResourceKind::Image;
    const char* audioNames[]={ui("Canciones","Songs"),ui("Música","Music"),ui("Efectos","Effects")};
    const char* imageNames[]={ui("Personajes","Characters"),ui("Escenarios","Stages"),ui("Iconos","Icons"),ui("Otras","Other")};
    const int uses=audio?3:4; std::array<size_t,4> counts{}; size_t total=0;
    for(const auto& item:app.resources.items) if(item.kind==kind) { ++total; if(item.use>=0 && item.use<uses) ++counts[item.use]; }
    bool changed=false;
    for(int k=0;k<=uses;++k) {
        const int value=k<uses?k:-1;
        auto label=std::string(k<uses?(audio?audioNames[k]:imageNames[k]):ui("Todo","All"))+" "+std::to_string(k<uses?counts[k]:total)+"###use-"+std::to_string(k);
        if(k) nextControl(buttonWidth(label.c_str()));
        const bool active=current==value; if(active) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.08f,.40f,.85f,1));
        if(ImGui::Button(label.c_str())) { current=value; changed=true; }
        if(active) ImGui::PopStyleColor();
    }
    if(audio) tooltip(ui("Como en Note Lab: el audio de las canciones (Inst y Voices, bajo songs/), la música de menús (music/) y los efectos (sounds/ y el resto).",
                         "As in Note Lab: song audio (Inst and Voices, under songs/), menu music (music/) and effects (sounds/ and the rest)."));
    else tooltip(ui("Por lo que las usa: hojas de personajes, piezas de escenario, iconos; el resto son menús, HUD y demás.",
                    "By what uses them: character sheets, stage pieces, icons; the rest are menus, HUD and so on."));
    return changed;
}
void resourcesList(App& app) {
    ensureResources(app);
    auto& index=app.resources;
    ImGui::TextUnformatted(ui("Recursos del mod","Mod resources")); ImGui::Separator();
    ImGui::TextDisabled("%zu %s · %zu %s · %zu %s",index.count(ResourceKind::Audio),ui("audio","audio"),index.count(ResourceKind::Image),ui("imágenes","images"),index.items.size(),ui("en total","in total"));
    const char* filters[]={ui("Todo","All"),ui("Audio","Audio"),ui("Imágenes","Images"),ui("Atlas","Atlases"),ui("Charts / JSON","Charts / JSON"),ui("Scripts","Scripts"),ui("Con problemas","With problems"),ui("Sin usar","Unused")};
    ImGui::SetNextItemWidth(-1); bool changed=ImGui::Combo("##resource-filter",&app.resourceFilter,filters,8);
    if(app.resourceFilter==1 || app.resourceFilter==2) changed|=resourceUseFilter(app);
    ImGui::SetNextItemWidth(-1); changed|=ImGui::InputTextWithHint("##resource-search",ui("Buscar ruta o uso...","Search path or use..."),app.resourceSearch.data(),app.resourceSearch.size());
    // Into the export: everything listed that can go, never song audio.
    if(ImGui::SmallButton(uiId("Marcar lo listado para exportar","Mark the listed files for export","resources-mark").c_str()))
        for(size_t i:app.resourceFiltered) if(i!=SIZE_MAX && extraResource(index.items[i])) app.batch.resources.insert(index.items[i].path.generic_u8string());
    tooltip(ui("Marca los archivos que muestra el filtro, salvo el audio de canciones (va con su canción).","Marks the files the filter shows, except song audio (it goes with its song)."));
    if(!app.batch.resources.empty()) { ImGui::SameLine(); if(ImGui::SmallButton(uiId("Quitar","Clear","resources-clear").c_str())) app.batch.resources.clear(); ImGui::SameLine(); ImGui::TextDisabled(ui("%zu marcados","%zu marked"),app.batch.resources.size()); }
    // Song audio is listed under its song, like Note Lab.
    const bool grouped=app.resourceFilter==1 && app.resourceAudioUse==(int)AudioUse::Song;
    if(changed || app.resourceFiltered.empty()) {
        app.resourceFiltered.clear(); auto match=lower(app.resourceSearch.data());
        for(size_t i=0;i<index.items.size();++i) {
            const auto& item=index.items[i];
            bool keep=app.resourceFilter==0 || (app.resourceFilter>=1 && app.resourceFilter<=5 && (int)item.kind==app.resourceFilter-1) ||
                      (app.resourceFilter==6 && !item.problems.empty()) || (app.resourceFilter==7 && item.uses.empty());
            if(keep && app.resourceFilter==1 && app.resourceAudioUse>=0) keep=item.use==app.resourceAudioUse;
            if(keep && app.resourceFilter==2 && app.resourceImageUse>=0) keep=item.use==app.resourceImageUse;
            if(keep && !match.empty()) { std::string hay=lower(item.path.generic_u8string()+"\n"+item.group); for(const auto& u:item.uses) hay+="\n"+lower(u.label); keep=hay.find(match)!=std::string::npos; }
            if(keep) app.resourceFiltered.push_back(i);
        }
        if(grouped) std::stable_sort(app.resourceFiltered.begin(),app.resourceFiltered.end(),[&](size_t a,size_t b){
            const auto &x=index.items[a],&y=index.items[b];
            if(lower(x.group)!=lower(y.group)) return x.group.empty()!=y.group.empty()?!x.group.empty():lower(x.group)<lower(y.group);
            return lower(x.name)<lower(y.name);
        });
        if(app.resourceFiltered.empty()) app.resourceFiltered.push_back(SIZE_MAX);   // marks "computed, empty"
    }
    const int rows=app.resourceFiltered.size()==1 && app.resourceFiltered[0]==SIZE_MAX?0:(int)app.resourceFiltered.size();
    auto drawRow=[&](size_t i) {
        const auto& item=index.items[i];
        ImGui::PushID((int)i);
        // Marked for export: music, effects and images. Song audio goes with
        // its song; charts and scripts are not exported as resources.
        if(extraResource(item)) {
            const auto key=item.path.generic_u8string(); bool marked=app.batch.resources.count(key)!=0;
            if(ImGui::Checkbox("##export",&marked)) { if(marked) app.batch.resources.insert(key); else app.batch.resources.erase(key); }
            tooltip(ui("Exportar este archivo con el bundle (images/, music/ o sounds/).","Export this file with the bundle (images/, music/ or sounds/)."));
        } else {
            ImGui::Dummy({ImGui::GetFrameHeight(),ImGui::GetFrameHeight()});
            tooltip(item.kind==ResourceKind::Audio?ui("El audio de canciones va con su canción.","Song audio goes with its song."):ui("Va con su canción, personaje o escenario.","Goes with its song, character or stage."));
        }
        ImGui::SameLine(); auto at=ImGui::GetCursorScreenPos();
        if(ImGui::Selectable("##resource",app.resource==(int)i,0,{0,40})) { app.resource=(int)i; app.zoom=1; app.panX=app.panY=0; }
        auto* draw=ImGui::GetWindowDrawList(); const float right=at.x+ImGui::GetContentRegionAvail().x;
        draw->AddCircleFilled({at.x+8,at.y+12},4.5f,ImGui::ColorConvertFloat4ToU32(resourceColor(resourceLevel(item))));
        draw->PushClipRect(at,{right,at.y+40},true);
        draw->AddText({at.x+20,at.y+3},ImGui::GetColorU32(ImGuiCol_Text),item.name.c_str());
        std::string sub=item.uses.empty()?std::string(ui("sin usar · ","unused · "))+item.folder:item.uses.front().label+(item.uses.size()>1?" +"+std::to_string(item.uses.size()-1):"");
        draw->AddText({at.x+20,at.y+21},ImGui::GetColorU32(ImGuiCol_TextDisabled),sub.c_str());
        draw->PopClipRect(); tooltip(item.path.u8string()); ImGui::PopID();
    };
    if(grouped) {
        // A few hundred rows at most; a header above each song.
        std::string last; bool first=true;
        for(int row=0;row<rows;++row) {
            const auto& item=index.items[app.resourceFiltered[row]];
            if(first || item.group!=last) {
                if(!first) ImGui::Spacing();
                ImGui::TextColored(ImVec4(.45f,.74f,1,1),"%s",iconLabel(0xE8D6,item.group.empty()?ui("Sin carpeta de canción","No song folder"):item.group).c_str());
                last=item.group; first=false;
            }
            drawRow(app.resourceFiltered[row]);
        }
    } else {
        // Thousands of files: only the visible rows are drawn.
        ImGuiListClipper clipper; clipper.Begin(rows,44);
        while(clipper.Step()) for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) drawRow(app.resourceFiltered[row]);
    }
    if(!rows) ImGui::TextWrapped("%s",index.items.empty()?ui("Abre un mod para ver sus recursos.","Open a mod to see its resources."):ui("Nada coincide con el filtro.","Nothing matches the filter."));
}
void resourcesDetails(App& app) {
    ensureResources(app);
    if(app.resource<0 || (size_t)app.resource>=app.resources.items.size()) {
        ImGui::TextWrapped("%s",ui("Selecciona un recurso para verlo o escucharlo. Nada suena solo.","Select a resource to view or play it. Nothing plays by itself."));
        return;
    }
    const auto& item=app.resources.items[app.resource];
    ImGui::TextUnformatted(item.name.c_str()); ImGui::SameLine(); ImGui::TextDisabled("· %s · %s",resourceKindLabel(item.kind),fileSizeText(item.path).c_str());
    pathRow(ui("Ruta:","Path:"),item.path);
    for(const auto& p:item.problems) ImGui::TextColored(resourceColor(2),"%s",message(p).c_str());
    if(item.uses.empty()) ImGui::TextColored(resourceColor(1),"%s",ui("Ningún personaje, canción o escenario lo usa: no se exporta.","No character, song or stage uses it: it is not exported."));
    else if(ImGui::TreeNodeEx(ui("Lo usan","Used by"),item.uses.size()<=4?ImGuiTreeNodeFlags_DefaultOpen:0)) {
        for(size_t u=0;u<item.uses.size();++u) {
            ImGui::PushID((int)u); const auto& use=item.uses[u];
            if(use.tab>=0) { if(ImGui::SmallButton(ui("Ir","Go"))) goToUse(app,use); ImGui::SameLine(); }
            ImGui::TextUnformatted(use.label.c_str()); ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if(item.kind==ResourceKind::Audio) {
        auto probe=fml::probeAudioFile(item.path);
        if(probe.readable) ImGui::TextDisabled("%s · %d Hz · %s",probe.container==fml::AudioContainer::OggVorbis?"OGG Vorbis":probe.declaredExtension.c_str(),probe.sampleRate,probe.channels==1?ui("mono","mono"):ui("estéreo","stereo"));
        if(probe.readable && probe.container!=fml::AudioContainer::OggVorbis) ImGui::TextColored(resourceColor(2),"%s",ui("No es OGG Vorbis: Codename no lo carga y el export lo omite.","Not OGG Vorbis: Codename will not load it and the export skips it."));
        ImGui::Separator(); resourceAudio(app,item);
    } else if(item.kind==ResourceKind::Image) { ImGui::Separator(); resourceImage(app,item); }
    else { ImGui::Separator(); resourceText(app,item); }
}
void resourcesReadiness(App& app) {
    ensureReadiness(app);
    ImGui::TextUnformatted(ui("Lo que falta para exportar","What export still needs")); ImGui::Separator();
    if(app.readinessBusy) { ImGui::PushTextWrapPos(0.0f); ImGui::TextDisabled("%s",ui("Analizando el mod con el mismo plan que la exportación...","Checking the mod with the same plan the export uses...")); ImGui::PopTextWrapPos(); }
    ImGui::BeginDisabled(app.readinessBusy);
    if(ImGui::SmallButton(ui("Volver a analizar","Check again"))) { app.readinessDirty=true; app.resourcesDirty=true; }
    ImGui::EndDisabled();
    wrappedCheckbox(ui("Solo lo que tiene avisos o bloqueos","Only items with notices or blocks"),&app.readinessOnlyIssues);
    const char* groups[]={ui("Personajes","Characters"),ui("Canciones","Songs"),ui("Escenarios","Stages")};
    for(int tab:{1,0,2}) {
        int ready=0,warn=0,blocked=0;
        for(const auto& r:app.readiness) if(r.tab==tab) { int level=readinessLevel(r); if(level==2) ++blocked; else if(level==1) ++warn; else ++ready; }
        if(ready+warn+blocked==0) continue;
        char header[160]; std::snprintf(header,sizeof header,"%s · %d %s · %d %s · %d %s###readiness-%d",groups[tab],ready,ui("listos","ready"),warn,ui("con avisos","with notices"),blocked,ui("no se exportan","not exported"),tab);
        if(!ImGui::CollapsingHeader(header,tab==1?ImGuiTreeNodeFlags_DefaultOpen:0)) continue;
        for(size_t k=0;k<app.readiness.size();++k) {
            const auto& r=app.readiness[k]; if(r.tab!=tab) continue;
            const int level=readinessLevel(r);
            if(app.readinessOnlyIssues && level==0) continue;
            ImGui::PushID((int)k);
            ImGui::PushStyleColor(ImGuiCol_Text,resourceColor(level));
            bool open=ImGui::TreeNode("##entry","%s %s",level==2?"×":level==1?"!":"·",r.name.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine(); if(ImGui::SmallButton(ui("Ir","Go"))) goToUse(app,{r.name,r.tab,r.index});
            if(open) {
                for(const auto& e:r.errors) { ImGui::PushStyleColor(ImGuiCol_Text,resourceColor(2)); ImGui::TextWrapped("%s",message(e).c_str()); ImGui::PopStyleColor(); }
                for(const auto& w:r.warnings) if(importantNotice(w)) { ImGui::PushStyleColor(ImGuiCol_Text,resourceColor(1)); ImGui::TextWrapped("%s",message(w).c_str()); ImGui::PopStyleColor(); }
                for(const auto& w:r.warnings) if(!importantNotice(w)) { ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextWrapped("%s",message(w).c_str()); ImGui::PopStyleColor(); }
                ImGui::TextDisabled("%zu %s · %.0f MB %s",r.files,ui("archivos","files"),r.textureBytes/1048576.0,ui("de texturas","of textures"));
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    if(app.readiness.empty() && !app.readinessBusy) ImGui::TextWrapped("%s",ui("Abre un mod para revisar qué le falta.","Open a mod to check what it lacks."));
}
