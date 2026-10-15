#pragma once
#include "UiLayout.hpp"
#include "Catalog.hpp"
#include "I18n.hpp"
#include "NoteSupport.hpp"
#include "../../src/fml_app/chart_editor/ChartSpace.hpp"
#include "../../src/fml_audio/AudioEngine.hpp"
#include "../../third_party/imgui/imgui.h"
#include <array>
#include <cmath>

namespace pi {
struct MiniChart {
    struct History { std::vector<fml::UniversalChart> undo,redo; };
    std::map<std::string,History> history;
    std::string documentKey;
    int selected=-1,line=0,quant=16,focus=-1;
    float beat=0,pixelsPerBeat=80;
    float horizontal=0;
    // The first public build only inspects charts: they export as the mod
    // ships them. Editing stays behind this switch until it is polished.
    bool readOnly=true;
    std::string waveformKey;
    std::vector<float> waveform;
    void checkpoint(Difficulty& d,const fml::UniversalChart& before) {
        auto& h=history[d.path.generic_u8string()]; if(h.undo.size()>=32) h.undo.erase(h.undo.begin()); h.undo.push_back(before); h.redo.clear(); d.edited=true;
    }
    bool undo(Difficulty& d,bool redo=false) {
        auto& h=history[d.path.generic_u8string()]; auto& from=redo?h.redo:h.undo; auto& to=redo?h.undo:h.redo;
        if(from.empty()) return false; to.push_back(d.chart); d.chart=std::move(from.back()); from.pop_back(); d.edited=true; selected=-1; return true;
    }
    bool insert(Difficulty& d,int strum,int lane,double ms) {
        if(strum<0 || (size_t)strum>=d.chart.strumLines.size() || lane<0 || lane>=d.chart.strumLines[strum].keyCount || !std::isfinite(ms) || ms<0) return false;
        checkpoint(d,d.chart); fml::ChartNote note; note.strumLine=strum; note.lane=lane; note.direction=lane%4; note.isPlayer=d.chart.strumLines[strum].type==1; note.timeMs=ms; d.chart.notes.push_back(note);
        std::stable_sort(d.chart.notes.begin(),d.chart.notes.end(),[](const auto& a,const auto& b){ return a.timeMs<b.timeMs; }); selected=-1; return true;
    }
    bool erase(Difficulty& d,int index) {
        if(index<0 || (size_t)index>=d.chart.notes.size()) return false; checkpoint(d,d.chart); d.chart.notes.erase(d.chart.notes.begin()+index); selected=-1; return true;
    }
    void grid(Difficulty& d,fml::AudioEngine& audio) {
        if(documentKey!=d.path.generic_u8string()) { documentKey=d.path.generic_u8string(); selected=-1; line=0; focus=-1; beat=0; }
        ImGui::TextUnformatted(readOnly?ui("Inspector de chart · FML","Chart inspector · FML"):ui("Mini editor · FML","Mini editor · FML")); ImGui::SameLine(); ImGui::TextDisabled("%s · %.0f BPM · %zu %s",d.name.c_str(),d.chart.bpm,d.chart.notes.size(),ui("notas","notes"));
        if(!readOnly) { if(ImGui::Button(ui("Deshacer","Undo"))) undo(d); ImGui::SameLine(); if(ImGui::Button(ui("Rehacer","Redo"))) undo(d,true); ImGui::SameLine(); }
        if(ImGui::Button(audio.playing()?ui("Pausa","Pause"):ui("Reproducir","Play"))) { if(audio.playing()) audio.pause(); else audio.play(); }
        ImGui::SetNextItemWidth(70); ImGui::InputInt("Snap",&quant,0); quant=std::clamp(quant,1,192); ImGui::SameLine();
        ImGui::SetNextItemWidth(90); ImGui::SliderFloat("Zoom",&pixelsPerBeat,20,180,"%.0f");
        fml::TimeMap time; time.build(d.chart); if(audio.playing()) beat=(float)time.beatAt(audio.positionMs());
        ImGui::SetNextItemWidth(130); if(ImGui::InputFloat("Beat",&beat,1,4)) { beat=std::max(0.0f,beat); audio.seekMs(time.msAtBeat(beat)); }
        ImGui::SameLine(); wrappedCheckbox(ui("Todas las líneas","All lines"),&allLines);
        if(readOnly) ImGui::TextWrapped("%s",ui("Solo inspección · Click: ver los datos de una nota · Rueda: navegar","Inspection only · Click: see a note's data · Wheel: navigate"));
        else ImGui::TextWrapped("%s",ui("Click: colocar / seleccionar · Click derecho: borrar · Rueda: navegar","Click: place / select · Right click: delete · Wheel: navigate"));
        auto at=ImGui::GetCursorScreenPos(); auto size=ImGui::GetContentRegionAvail(); size.y=std::max(100.0f,size.y-65);
        fml::ChartSpace space; space.setVerticalViewport(at.y+56,at.y+size.y,at.y+size.y*.30f,beat,pixelsPerBeat); space.setLaneLayout(0,34,18); space.rebuildLines(d.chart.strumLines,allLines?-1:line);
        auto usable=size.x-132; horizontal=std::clamp(horizontal,0.0f,std::max(0.0f,space.contentWidth()-usable));
        auto centered=std::max(0.0f,(usable-space.contentWidth())*.5f); space.setLaneLayout(at.x+48+centered-horizontal,34,18); space.rebuildLines(d.chart.strumLines,allLines?-1:line);
        ImGui::InvisibleButton("##mini-grid",size); bool hovered=ImGui::IsItemHovered(); auto* draw=ImGui::GetWindowDrawList(); draw->PushClipRect(at,ImVec2(at.x+size.x,at.y+size.y),true);
        draw->AddRectFilled(at,ImVec2(at.x+size.x,at.y+size.y),IM_COL32(15,22,31,255));
        static ImU32 lineColors[]={IM_COL32(225,100,120,255),IM_COL32(75,165,245,255),IM_COL32(178,120,240,255)};
        for(const auto& l:space.lines()) {
            auto color=lineColors[std::clamp(d.chart.strumLines[l.strumLine].type,0,2)]; draw->AddRectFilled(ImVec2(l.left,at.y+56),ImVec2(l.right,at.y+size.y),(color&0x00ffffffu)|0x14000000u);
            draw->AddRectFilled(ImVec2(l.left,at.y),ImVec2(l.right,at.y+51),IM_COL32(33,44,60,255),4); draw->AddRectFilled(ImVec2(l.left,at.y),ImVec2(l.right,at.y+3),color);
            std::string caption="L"+std::to_string(l.strumLine)+" · "+d.chart.strumLines[l.strumLine].position; draw->AddText(ImVec2(l.left+6,at.y+7),color,caption.c_str());
            std::string names; for(const auto& name:d.chart.strumLines[l.strumLine].characters) {if(!names.empty())names+=" + ";names+=name;}
            ImVec4 clip(l.left+3,at.y+27,l.right-3,at.y+49); draw->AddText(ImGui::GetFont(),ImGui::GetFontSize()*.85f,ImVec2(l.left+6,at.y+29),IM_COL32(200,211,225,255),names.c_str(),nullptr,0,&clip);
        }
        for(const auto& l:space.lanes()) draw->AddLine(ImVec2(l.left,at.y+56),ImVec2(l.left,at.y+size.y),IM_COL32(55,67,81,255));
        auto range=space.visibleBeats();
        for(double b=std::floor(range.first);b<=range.last;b+=.25) {
            auto y=space.yForBeat(b); bool whole=std::abs(b-std::round(b))<.001; draw->AddLine(ImVec2(at.x,y),ImVec2(at.x+size.x,y),!whole?IM_COL32(31,41,53,255):(int)b%4?IM_COL32(50,62,77,255):IM_COL32(92,108,126,255));
            if(whole) { char label[32]; std::snprintf(label,sizeof(label),"%.0f",b); draw->AddText(ImVec2(at.x+5,y+2),IM_COL32(130,144,161,255),label); }
        }
        int hit=-1; auto mouse=ImGui::GetIO().MousePos;
        static ImU32 colors[]={IM_COL32(190,90,220,255),IM_COL32(85,155,235,255),IM_COL32(85,205,120,255),IM_COL32(232,90,95,255)};
        for(int i=0;i<(int)d.chart.notes.size();++i) {
            const auto& note=d.chart.notes[i]; const fml::ChartLaneGeometry* lane=nullptr;
            for(const auto& l:space.lanes()) if(l.strumLine==note.strumLine && l.lane==note.lane) { lane=&l; break; }
            if(!lane) continue; auto y=space.yForBeat(time.beatAt(note.timeMs)),end=space.yForBeat(time.beatAt(note.timeMs+note.sustainMs));
            if(end<at.y || y>at.y+size.y) continue; auto color=colors[(note.direction%4+4)%4];
            if(note.sustainMs>0) draw->AddRectFilled(ImVec2(lane->left+9,y),ImVec2(lane->right-9,end),color);
            const float shape[7][2]={{-14,0},{-2,-12},{-2,-5},{12,-5},{12,5},{-2,5},{-2,12}};
            ImVec2 points[7]; float angle=note.direction==0?0:note.direction==1?-1.5707963f:note.direction==2?1.5707963f:3.1415926f;
            for(int p=0;p<7;++p) points[p]=ImVec2((lane->left+lane->right)*.5f+shape[p][0]*std::cos(angle)-shape[p][1]*std::sin(angle),y+shape[p][0]*std::sin(angle)+shape[p][1]*std::cos(angle));
            draw->AddConcavePolyFilled(points,7,color); draw->AddPolyline(points,7,IM_COL32(235,241,251,230),ImDrawFlags_Closed,1.0f);
            if(!note.type.empty()) draw->AddCircleFilled(ImVec2(lane->right-4,y-10),3,IM_COL32(255,210,100,255));
            if(i==selected) draw->AddRect(ImVec2(lane->left,y-10),ImVec2(lane->right,y+10),IM_COL32_WHITE,3.0f,2.0f,ImDrawFlags_None);
            if(mouse.x>=lane->left && mouse.x<lane->right && std::abs(mouse.y-y)<10) hit=i;
        }
        for(const auto& event:d.chart.events) { auto y=space.yForBeat(time.beatAt(event.timeMs)); if(y<at.y+56 || y>at.y+size.y) continue; draw->AddRectFilled(ImVec2(at.x+size.x-74,y-7),ImVec2(at.x+size.x-3,y+7),event.global?IM_COL32(132,90,184,255):IM_COL32(54,124,159,255),3); if(hovered && mouse.x>at.x+size.x-74 && std::abs(mouse.y-y)<8) ImGui::SetTooltip("%s",event.name.c_str()); }
        draw->AddLine(ImVec2(at.x,space.playheadY()),ImVec2(at.x+size.x,space.playheadY()),IM_COL32(255,215,100,255),2); draw->PopClipRect();
        if(hovered && ImGui::GetIO().MouseWheel) { beat=std::max(0.0f,beat-ImGui::GetIO().MouseWheel); if(!audio.playing()) audio.seekMs(time.msAtBeat(beat)); }
        if(hovered && mouse.y>at.y+56 && ImGui::IsMouseClicked(0)) { if(hit>=0) selected=hit; else if(readOnly) selected=-1; else if(const auto* lane=space.laneAtX(mouse.x)) insert(d,lane->strumLine,lane->lane,time.msAtBeat(fml::ChartSpace::snapBeat(space.beatAtY(mouse.y),quant))); }
        if(!readOnly && hovered && ImGui::IsMouseClicked(1) && hit>=0) erase(d,hit);
        if(space.contentWidth()>usable) {ImGui::TextUnformatted(ui("Desplazamiento horizontal","Horizontal scroll"));ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##horizontal-scroll",&horizontal,0,std::max(0.0f,space.contentWidth()-usable),"%.0f");}
        if(waveformKey!=documentKey || waveform.empty()) { waveform=audio.waveformPeaks(350); waveformKey=documentKey; }
        auto waveAt=ImGui::GetCursorScreenPos(); auto waveSize=ImVec2(size.x,24); ImGui::InvisibleButton("##waveform",waveSize); auto duration=std::max(audio.durationMs(),d.chart.lastNoteMs());
        draw->AddRectFilled(waveAt,ImVec2(waveAt.x+waveSize.x,waveAt.y+24),IM_COL32(18,29,42,255),3);
        for(size_t i=0;i<waveform.size();++i) {float x=waveAt.x+i*waveSize.x/waveform.size(),v=waveform[i]*10; draw->AddLine(ImVec2(x,waveAt.y+12-v),ImVec2(x,waveAt.y+12+v),IM_COL32(77,157,194,220));}
        if(ImGui::IsItemHovered() && ImGui::IsMouseClicked(0) && duration>0) { auto ms=std::clamp((mouse.x-waveAt.x)/waveSize.x,0.0f,1.0f)*duration; audio.seekMs(ms); beat=(float)time.beatAt(ms); }
    }
    bool allLines=true;
    // Read-only view of what properties() edits. The export choices (which
    // loaded stage or character fills a key) stay: they do not touch the chart.
    void inspect(Difficulty& d,const Catalog& catalog) {
        const auto& chart=d.chart; ImGui::Separator();
        ImGui::TextDisabled("%s",d.path.filename().u8string().c_str());if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",d.path.u8string().c_str());
        ImGui::Text("%.1f BPM · %s %.2fx · %d/%d",chart.bpm,ui("velocidad","speed"),chart.speed,chart.beatsPerMeasure,chart.stepsPerBeat);
        ImGui::Text("Stage: %s",chart.stage.empty()?"-":chart.stage.c_str());
        ImGui::TextUnformatted(ui("Recurso de escenario al exportar","Stage asset on export"));ImGui::SetNextItemWidth(-1);
        auto stageName=d.exportStage>=0 && (size_t)d.exportStage<catalog.stages.size()?catalog.stages[d.exportStage].imported.stage.id:std::string(ui("Resolver por clave / existente","Resolve by key / existing"));
        if(ImGui::BeginCombo("##export-stage-asset",stageName.c_str())) { if(ImGui::Selectable(ui("Resolver por clave / existente","Resolve by key / existing"),d.exportStage<0))d.exportStage=-1; for(int i=0;i<(int)catalog.stages.size();++i) if(ImGui::Selectable(catalog.stages[i].imported.stage.id.c_str(),d.exportStage==i))d.exportStage=i; ImGui::EndCombo(); }
        if(!chart.strumLines.empty()) {
            line=std::clamp(line,0,(int)chart.strumLines.size()-1); ImGui::TextUnformatted(ui("Línea","Line")); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##selected-line",&line); line=std::clamp(line,0,(int)chart.strumLines.size()-1);
            const auto& current=chart.strumLines[line];
            const char* types[]={ui("rival","opponent"),ui("jugador","player"),"extra"};
            ImGui::TextWrapped("%s · %d %s · %s%s",types[std::clamp(current.type,0,2)],current.keyCount,ui("teclas","keys"),current.position.c_str(),current.visible?"":ui(" · receptores ocultos"," · hidden receptors"));
            std::string names; for(const auto& name:current.characters) { if(!names.empty()) names+=", "; names+=name; }
            ImGui::TextWrapped("%s: %s",ui("Personajes","Characters"),names.empty()?"-":names.c_str());
            for(const auto& key:current.characters) {
                ImGui::PushID(key.c_str()); ImGui::TextWrapped(ui("Recurso para clave: %s","Asset for key: %s"),key.c_str());
                auto assignment=d.exportCharacters.find(key); int chosen=assignment==d.exportCharacters.end()?-1:assignment->second;
                auto caption=chosen>=0 && (size_t)chosen<catalog.characters.size()?catalog.characters[chosen].imported.character.id:std::string(ui("Resolver por clave / existente","Resolve by key / existing")); ImGui::SetNextItemWidth(-1);
                if(ImGui::BeginCombo("##export-actor",caption.c_str())) { if(ImGui::Selectable(ui("Resolver por clave / existente","Resolve by key / existing"),chosen<0)) d.exportCharacters.erase(key); for(int i=0;i<(int)catalog.characters.size();++i) if(ImGui::Selectable(catalog.characters[i].imported.character.id.c_str(),chosen==i)) d.exportCharacters[key]=i; ImGui::EndCombo(); } ImGui::PopID();
            }
        }
        auto support=noteSupport(catalog,d);
        if(!support.empty()) {
            ImGui::Separator(); ImGui::TextUnformatted(ui("Notas custom","Custom notes"));
            for(const auto& type:support) { ImGui::TextColored(ImVec4(1,.78f,.35f,1),"%s · %zu",type.type.c_str(),type.count); if(!type.script.path.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s",type.script.path.filename().u8string().c_str()); } }
        }
        if(selected>=0 && (size_t)selected<chart.notes.size()) {
            const auto& note=chart.notes[selected]; ImGui::Separator(); ImGui::TextUnformatted(ui("Nota seleccionada","Selected note"));
            ImGui::Text("%s: %.1f ms · sustain %.1f ms",ui("Tiempo","Time"),note.timeMs,note.sustainMs);
            ImGui::Text("%s L%d · %s %d",ui("Línea","Line"),note.strumLine,ui("carril","lane"),note.lane);
            if(note.singerStrumLine>=0) ImGui::Text("%s: L%d",ui("Canta la línea","Sung by line"),note.singerStrumLine);
            if(!note.type.empty()) ImGui::TextWrapped("%s: %s",ui("Tipo","Type"),note.type.c_str());
        }
        ImGui::Spacing(); ImGui::TextWrapped("%s",ui("Solo inspección: el chart se exporta tal como viene del mod.","Inspection only: the chart exports as the mod ships it."));
    }
    void properties(Difficulty& d,const Catalog& catalog) {
        if(readOnly) { inspect(d,catalog); return; }
        auto& chart=d.chart; ImGui::Separator();
        ImGui::TextDisabled("%s",d.path.filename().u8string().c_str());if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",d.path.u8string().c_str()); if(d.edited) ImGui::TextColored(ImVec4(1,.75f,.3f,1),"%s",ui("Borrador modificado · se incluye al exportar","Modified draft · included on export"));
        if(ImGui::TreeNode(ui("Tiempo y velocidad","Timing and scroll speed"))) {
        auto before=chart; ImGui::TextUnformatted("BPM"); ImGui::SetNextItemWidth(-1); bool changed=ImGui::InputFloat("##chart-bpm",&chart.bpm,1);
        ImGui::TextUnformatted(ui("Velocidad de notas","Scroll speed")); ImGui::SetNextItemWidth(-1); changed|=ImGui::InputFloat("##chart-speed",&chart.speed,.1f); chart.bpm=std::clamp(chart.bpm,1.0f,1000.0f); chart.speed=std::clamp(chart.speed,.01f,20.0f);
        if(changed) { if(!chart.bpmChanges.empty() && chart.bpmChanges.front().timeMs==0) chart.bpmChanges.front().bpm=chart.bpm; checkpoint(d,before); }
        ImGui::TreePop();}
        std::array<char,1024> text{}; auto input=[&](const char* label,std::string& value) { ImGui::TextWrapped("%s",label); ImGui::SetNextItemWidth(-1); std::snprintf(text.data(),text.size(),"%s",value.c_str()); if(ImGui::InputText((std::string("##")+label).c_str(),text.data(),text.size())) { auto old=chart; value=text.data(); checkpoint(d,old); } };
        input("Stage",chart.stage);
        ImGui::TextWrapped("%s",ui("Claves de destino: se escribirán en el chart. Puedes elegir otro recurso cargado para cada clave.","Destination keys are written into the chart. You may choose a different loaded asset for each key."));
        ImGui::TextUnformatted(ui("Recurso de escenario","Stage asset"));ImGui::SetNextItemWidth(-1);
        auto stageName=d.exportStage>=0 && (size_t)d.exportStage<catalog.stages.size()?catalog.stages[d.exportStage].imported.stage.id:std::string(ui("Resolver por clave / existente","Resolve by key / existing"));
        if(ImGui::BeginCombo("##export-stage-asset",stageName.c_str())) { if(ImGui::Selectable(ui("Resolver por clave / existente","Resolve by key / existing"),d.exportStage<0))d.exportStage=-1; for(int i=0;i<(int)catalog.stages.size();++i) if(ImGui::Selectable(catalog.stages[i].imported.stage.id.c_str(),d.exportStage==i))d.exportStage=i; ImGui::EndCombo(); }
        if(!chart.strumLines.empty()) {
            line=std::clamp(line,0,(int)chart.strumLines.size()-1); ImGui::TextUnformatted(ui("Línea","Line")); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##selected-line",&line); line=std::clamp(line,0,(int)chart.strumLines.size()-1); auto& current=chart.strumLines[line];
            if(ImGui::TreeNode(ui("Ajustes de línea","Line settings"))) {
            ImGui::TextWrapped("%s",ui("Tipo: 0 rival / 1 jugador / 2 extra","Type: 0 opponent / 1 player / 2 extra")); ImGui::SetNextItemWidth(-1);
            auto old=chart; bool edit=ImGui::InputInt("##line-type",&current.type); edit|=wrappedCheckbox(ui("Receptores visibles","Visible receptors"),&current.visible);
            ImGui::TextUnformatted(ui("Teclas","Keys")); ImGui::SetNextItemWidth(-1); int count=current.keyCount; if(ImGui::InputInt("##keys",&count)) { int minimum=1; for(const auto& note:chart.notes) if(note.strumLine==line) minimum=std::max(minimum,note.lane+1); current.keyCount=std::clamp(count,minimum,32); edit=true; }
            if(edit) checkpoint(d,old); input(ui("Posición","Position"),current.position);
            ImGui::TreePop();}
            std::string names; for(const auto& name:current.characters) { if(!names.empty()) names+=", "; names+=name; }
            std::snprintf(text.data(),text.size(),"%s",names.c_str());
            ImGui::TextWrapped("%s",ui("Personajes (separados por coma)","Characters (comma separated)")); ImGui::SetNextItemWidth(-1);
            if(ImGui::InputText("##line-characters",text.data(),text.size())) {
                auto snapshot=chart; current.characters.clear(); std::string value=text.data(); size_t at=0;
                while(at<=value.size()) { auto end=value.find(',',at); auto part=value.substr(at,end==std::string::npos?end:end-at); auto first=part.find_first_not_of(" \t"),last=part.find_last_not_of(" \t"); if(first!=std::string::npos) current.characters.push_back(part.substr(first,last-first+1)); if(end==std::string::npos) break; at=end+1; }
                checkpoint(d,snapshot);
            }
            for(const auto& key:current.characters) {
                ImGui::PushID(key.c_str()); ImGui::TextWrapped(ui("Recurso para clave: %s","Asset for key: %s"),key.c_str());
                auto assignment=d.exportCharacters.find(key); int chosen=assignment==d.exportCharacters.end()?-1:assignment->second;
                auto caption=chosen>=0 && (size_t)chosen<catalog.characters.size()?catalog.characters[chosen].imported.character.id:std::string(ui("Resolver por clave / existente","Resolve by key / existing")); ImGui::SetNextItemWidth(-1);
                if(ImGui::BeginCombo("##export-actor",caption.c_str())) { if(ImGui::Selectable(ui("Resolver por clave / existente","Resolve by key / existing"),chosen<0)) d.exportCharacters.erase(key); for(int i=0;i<(int)catalog.characters.size();++i) if(ImGui::Selectable(catalog.characters[i].imported.character.id.c_str(),chosen==i)) d.exportCharacters[key]=i; ImGui::EndCombo(); } ImGui::PopID();
            }
        }
        if(ImGui::TreeNodeEx(ui("Notas custom","Custom notes"))) {
            auto support=noteSupport(catalog,d);
            if(support.empty()) ImGui::TextDisabled("%s",ui("Sin tipos custom en uso","No custom types in use"));
            for(const auto& type:support) {
                ImGui::PushID(type.type.c_str()); ImGui::TextColored(ImVec4(1,.78f,.35f,1),"%s · %zu",type.type.c_str(),type.count);
                if(!type.script.path.empty())ImGui::TextWrapped("%s",type.script.path.filename().u8string().c_str());
                if(ImGui::Button(ui("Convertir este tipo a nota normal","Convert this type to normal notes"))) { checkpoint(d,chart); for(auto& note:chart.notes) if(note.type==type.type) note.type.clear(); }
                ImGui::PopID();
            }
            ImGui::TextWrapped("%s",ui("El tipo original se conserva al exportar.","The original type is preserved on export.")); ImGui::TreePop();
        }
        if(selected>=0 && (size_t)selected<chart.notes.size() && !chart.strumLines.empty()) {
            ImGui::Separator(); ImGui::TextUnformatted(ui("Nota seleccionada","Selected note")); auto old=chart; auto& note=chart.notes[selected];
            bool edit=fullInputDouble(ui("Tiempo (ms)","Time (ms)"),&note.timeMs,25); edit|=fullInputDouble(ui("Sustain (ms)","Sustain (ms)"),&note.sustainMs,25); edit|=fullInputInt(ui("Línea de nota","Note line"),&note.strumLine); note.strumLine=std::clamp(note.strumLine,0,(int)chart.strumLines.size()-1);
            edit|=fullInputInt(ui("Carril","Lane"),&note.lane); note.lane=std::clamp(note.lane,0,chart.strumLines[note.strumLine].keyCount-1); note.direction=note.lane%4; note.isPlayer=chart.strumLines[note.strumLine].type==1;
            edit|=fullInputInt(ui("Línea que canta (-1 hereda)","Singer line (-1 inherits)"),&note.singerStrumLine); note.singerStrumLine=std::clamp(note.singerStrumLine,-1,(int)chart.strumLines.size()-1); note.timeMs=std::max(0.0,note.timeMs); note.sustainMs=std::max(0.0,note.sustainMs);
            if(edit) checkpoint(d,old); input(ui("Tipo de nota","Note type"),note.type);
            if(ImGui::Button(ui("Borrar nota","Delete note"))) erase(d,selected);
        }
        ImGui::TextWrapped("%s",ui("Los originales se conservan. Exporta el borrador desde esta pestaña.","Originals are preserved. Export the draft from this tab."));
    }
};
}
