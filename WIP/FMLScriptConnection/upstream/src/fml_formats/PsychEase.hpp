// fml_formats — El nombre de ease que escribe un mod de Psych y el FlxEase
// que el motor le da.
//
// El nombre es una cadena, y Psych la resuelve en
// `LuaUtils.getTweenEaseByString` (`LuaUtils.hx:334-374`). Se reproduce ese
// mapa tal cual, incluida su rareza: `smoothstepout` devuelve
// `smoothStepInOut` en el motor, y emitir `smoothStepOut` -que es lo que uno
// escribiria- daria una curva distinta de la que el mod ve al jugar. Lo que no
// encaja cae en `linear`, que es lo que hace el motor.
//
// Lo comparten el emisor de HScript (cuando la ease es un literal) y la capa
// de compatibilidad (`psych.ease(nombre)`, cuando llega en una variable).
#pragma once

#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fml {
namespace psychease {

inline const std::vector<std::pair<const char*, const char*>>& table() {
    static const std::vector<std::pair<const char*, const char*>> entries = {
        {"backin", "backIn"}, {"backinout", "backInOut"}, {"backout", "backOut"},
        {"bouncein", "bounceIn"}, {"bounceinout", "bounceInOut"}, {"bounceout", "bounceOut"},
        {"circin", "circIn"}, {"circinout", "circInOut"}, {"circout", "circOut"},
        {"cubein", "cubeIn"}, {"cubeinout", "cubeInOut"}, {"cubeout", "cubeOut"},
        {"elasticin", "elasticIn"}, {"elasticinout", "elasticInOut"}, {"elasticout", "elasticOut"},
        {"expoin", "expoIn"}, {"expoinout", "expoInOut"}, {"expoout", "expoOut"},
        {"quadin", "quadIn"}, {"quadinout", "quadInOut"}, {"quadout", "quadOut"},
        {"quartin", "quartIn"}, {"quartinout", "quartInOut"}, {"quartout", "quartOut"},
        {"quintin", "quintIn"}, {"quintinout", "quintInOut"}, {"quintout", "quintOut"},
        {"sinein", "sineIn"}, {"sineinout", "sineInOut"}, {"sineout", "sineOut"},
        {"smoothstepin", "smoothStepIn"}, {"smoothstepinout", "smoothStepInOut"},
        {"smoothstepout", "smoothStepInOut"},   // si, InOut: es lo que hace el motor
        {"smootherstepin", "smootherStepIn"}, {"smootherstepinout", "smootherStepInOut"},
        {"smootherstepout", "smootherStepOut"},
    };
    return entries;
}

// `quadInOut`, `QUAD in out`... -> miembro de FlxEase; `linear` si no encaja.
inline std::string member(const std::string& raw) {
    std::string key;
    for (const char c : raw) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) continue;
        key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const auto& entry : table())
        if (key == entry.first) return entry.second;
    return "linear";
}

}  // namespace psychease
}  // namespace fml
