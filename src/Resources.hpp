#pragma once
#include "../../src/fml_runtime/Scene.hpp"
#include "../../src/fml_app/ModExplorerCatalog.hpp"
#include "../../src/fml_formats/PsychStageSource.hpp"
#include <filesystem>
#include <memory>

namespace pi {
struct AnimateResource {
    fml::AnimateAtlas atlas;
    std::filesystem::path source;
    std::vector<std::filesystem::path> maps,images;
    std::string original,compatible,error,compatibilityError;
};
inline std::shared_ptr<AnimateResource> animateResource(const std::filesystem::path& source) {
    namespace fs=std::filesystem;
    auto result=std::make_shared<AnimateResource>(); result->source=source;
    auto folder=source.parent_path(); fml::AtlasStore store;
    std::error_code pageError;
    for(fs::directory_iterator it(folder,pageError),end;it!=end && !pageError;it.increment(pageError)) {
        auto name=it->path().stem().u8string();
        if(it->path().extension()==".json" && name.rfind("spritemap",0)==0 && name.size()>9) {
            auto number=name.substr(9); if(number.find_first_not_of("0123456789")==std::string::npos && number.size()<5 && std::stoi(number)>8) { result->error="Animate supera las 8 páginas que admite el lector actual de FML."; return result; }
        }
    }
    store.readText=[](const std::string& path) {
        std::error_code ec; auto source=fs::u8path(path);
        auto size=fs::file_size(source,ec); return ec || size>32u*1024u*1024u?std::string():fml::psych::read(source);
    };
    result->original=store.readText(source.generic_u8string());
    if(const auto* atlas=store.getAnimate(source.generic_u8string())) result->atlas=*atlas;
    else { result->error="Animation.json / spritemap inválido o incompleto"; return result; }
    for(int index=1;index<=8;++index) {
        auto map=folder/("spritemap"+std::to_string(index)+".json");
        if(fml::psych::file(map)) result->maps.push_back(map);
    }
    if(result->maps.empty() && fml::psych::file(folder/"spritemap.json")) result->maps.push_back(folder/"spritemap.json");
    for(const auto& name:result->atlas.imagePaths()) {
        auto image=fs::u8path(name); std::error_code ec;
        auto relative=image.lexically_relative(folder);
        if(relative.empty() || relative.is_absolute() || *relative.begin()==".." || !fs::is_regular_file(image,ec)) {
            result->error="Página Animate ausente o fuera de la carpeta: "+name; return result;
        }
        int width=0,height=0;
        if(!fml::psych::pngSize(image,width,height) || width>16384 || height>16384) {
            result->error="Página Animate PNG inválida o demasiado grande: "+name; return result;
        }
        result->images.push_back(image);
    }
    if(result->images.empty()) result->error="Animate no tiene páginas PNG";
    result->compatible=result->atlas.compatibleTimeline(result->original,result->compatibilityError);
    return result;
}
}
