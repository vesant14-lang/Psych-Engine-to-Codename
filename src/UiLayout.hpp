#pragma once
#include "../../third_party/imgui/imgui.h"
#include <string>
#include <algorithm>

namespace pi {
inline std::string fieldId(const char* label) {return std::string("##")+label;}
inline void fieldLabel(const char* label) {auto text=std::string(label);auto id=text.find("##");if(id!=std::string::npos)text.resize(id);ImGui::TextWrapped("%s",text.c_str());ImGui::SetNextItemWidth(-1);}
inline bool fullSliderFloat(const char* label,float* value,float minimum,float maximum,const char* format="%.3f") {fieldLabel(label);return ImGui::SliderFloat(fieldId(label).c_str(),value,minimum,maximum,format);}
inline bool fullSliderInt(const char* label,int* value,int minimum,int maximum) {fieldLabel(label);return ImGui::SliderInt(fieldId(label).c_str(),value,minimum,maximum);}
inline bool fullInputFloat(const char* label,float* value,float step=0) {fieldLabel(label);return ImGui::InputFloat(fieldId(label).c_str(),value,step);}
inline bool fullInputDouble(const char* label,double* value,double step=0) {fieldLabel(label);return ImGui::InputDouble(fieldId(label).c_str(),value,step);}
inline bool fullInputInt(const char* label,int* value) {fieldLabel(label);return ImGui::InputInt(fieldId(label).c_str(),value);}
inline bool fullInputText(const char* label,char* value,size_t length) {fieldLabel(label);return ImGui::InputText(fieldId(label).c_str(),value,length);}
inline void nextControl(float width) {
    const auto a=ImGui::GetItemRectMax();
    const float right=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x;
    if(a.x+ImGui::GetStyle().ItemSpacing.x+width<=right)ImGui::SameLine();
}
inline float buttonWidth(const char* label) {return ImGui::CalcTextSize(label,nullptr,true).x+2*ImGui::GetStyle().FramePadding.x;}
inline bool wrappedCheckbox(const char* label,bool* value) {
    std::string original(label),visible=original.substr(0,original.find("##"));
    const float width=std::max(40.0f,ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemInnerSpacing.x);
    std::string wrapped,line;size_t start=0;
    while(start<visible.size()) {
        auto end=visible.find(' ',start);if(end==std::string::npos)end=visible.size();auto word=visible.substr(start,end-start);
        if(!line.empty() && ImGui::CalcTextSize((line+" "+word).c_str()).x>width){wrapped+=line+"\n";line=word;}
        else{if(!line.empty())line+=' ';line+=word;}start=end+1;
    }
    wrapped+=line;return ImGui::Checkbox((wrapped+"###"+original).c_str(),value);
}
}
