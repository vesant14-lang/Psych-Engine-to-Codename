#pragma once
#include "I18n.hpp"
#include "../../third_party/imgui/imgui.h"
#include <algorithm>
#include <cmath>

namespace pi {
inline void zoomPreviewAt(float& zoom,float& panX,float& panY,ImVec2 center,ImVec2 pointer,float wheel) {
    float next=std::clamp(zoom*std::pow(1.15f,wheel),.1f,5.0f),ratio=next/zoom;
    panX=(pointer.x-center.x)-((pointer.x-center.x)-panX)*ratio;
    panY=(pointer.y-center.y)-((pointer.y-center.y)-panY)*ratio;
    zoom=next;
}
inline void interactPreviewView(float& zoom,float& panX,float& panY,ImVec2 center) {
    auto& io=ImGui::GetIO();
    if(ImGui::IsItemHovered()) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if(io.MouseWheel)zoomPreviewAt(zoom,panX,panY,center,io.MousePos,io.MouseWheel);
    }
    if(ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {panX+=io.MouseDelta.x;panY+=io.MouseDelta.y;}
}
inline void previewZoomControls(float& zoom) {
    ImGui::TextDisabled("Zoom");ImGui::SameLine();
    float button=ImGui::GetFrameHeight(),spacing=ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(50.0f,ImGui::GetContentRegionAvail().x-2*button-2*spacing));
    ImGui::SliderFloat("##view-zoom",&zoom,.1f,5.0f,"%.2fx");
    ImGui::SameLine();if(ImGui::Button("-##view-zoom-out",{button,0}))zoom=std::max(.1f,zoom/1.15f);
    ImGui::SameLine();if(ImGui::Button("+##view-zoom-in",{button,0}))zoom=std::min(5.0f,zoom*1.15f);
}
}
