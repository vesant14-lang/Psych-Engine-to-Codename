// Behavior discovery is by engine scope and document type, not song filenames.
#pragma once
#include "PsychBundle.hpp"

namespace fml { namespace psych {
inline bool behaviorBelongsToChart(const fs::path& source, const fs::path& chart) {
    if (source.parent_path() == chart.parent_path()) return true;
    const auto parsed = nlohmann::json::parse(read(chart),nullptr,false,true);
    if (!parsed.is_object() || !parsed.contains("song") || !parsed["song"].is_object() ||
        !parsed["song"].contains("song") || !parsed["song"]["song"].is_string()) return false;
    const auto header = formatSongPath(parsed["song"]["song"].get<std::string>());
    if (!relativeKey(header)) return false;
    for (const auto& root : roots(chart)) for (const auto* prefix : {"data/","data/songData/"})
        if (source.parent_path() == root / fs::u8path(std::string(prefix) + header)) return true;
    return false;
}
inline std::string behaviorKind(const fs::path& path, const std::string& text) {
    const auto ext = extensionLower(path);
    if (ext == ".lua") return "Lua de Psych: requiere porteo";
    if (ext == ".hx") return "HScript/Haxe: requiere revision de API";
    if (ext == ".xml") {
        pugi::xml_document xml;
        if (xml.load_buffer(text.data(),text.size()) && std::string(xml.document_element().name()) == "Modchart")
            return "Timeline XML de shaders";
    }
    return {};
}
inline std::string behaviorScope(const std::string& key) {
    if (key.rfind("manual-behavior/",0) == 0) return "Seleccion manual";
    if (key.rfind("stages/",0) == 0) return "Stage solicitado por el chart";
    if (key.rfind("characters/",0) == 0) return "Personaje / dependencia literal";
    if (key.rfind("custom_events/",0) == 0) return "Evento solicitado por el chart";
    if (key.rfind("custom_notetypes/",0) == 0) return "Tipo de nota solicitado por el chart";
    if (key.rfind("scripts/",0) == 0) return "Script global / dependencia literal";
    return "Carpeta de la cancion / dependencia literal";
}
} }
