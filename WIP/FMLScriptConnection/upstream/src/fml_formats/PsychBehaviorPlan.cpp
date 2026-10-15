#include "PsychBehaviorPlan.hpp"

#include "LuaAst.hpp"
#include "PsychSource.hpp"
#include "../../third_party/json.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace fml {
namespace psychplan {

namespace {

namespace fs = std::filesystem;

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool startsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

// El nombre de archivo tal cual, sin lo que un sistema de archivos no admite.
// Los espacios se quedan: el nombre de un evento custom de Psych ES el
// nombre del archivo, espacios incluidos (`Set Cam Zoom.json`).
std::string safeStem(const std::string& stem) {
    std::string out;
    for (const char c : stem) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') continue;
        out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? "script" : out;
}

std::string stemOf(const std::string& key) {
    const fs::path p = fs::u8path(key);
    return p.stem().u8string();
}

// El nombre de la cancion como Psych lo compara en un global:
// `songName == 'Been Patient'`, o su forma de carpeta `been-patient`.
bool mentionsSong(const std::string& source, const std::string& psychName, const std::string& songId) {
    if (psychName.empty() && songId.empty()) return false;
    const std::string text = lower(source);
    auto quoted = [&](const std::string& needle) {
        if (needle.empty()) return false;
        const std::string n = lower(needle);
        return text.find("'" + n + "'") != std::string::npos || text.find("\"" + n + "\"") != std::string::npos;
    };
    if (quoted(psychName) || quoted(songId)) return true;
    std::string folder = lower(psychName);
    for (char& c : folder) if (c == ' ') c = '-';
    return quoted(folder);
}

bool referenced(const std::string& key, const std::vector<psych::ScriptDependency>& dependencies) {
    for (const auto& dep : dependencies) {
        if (dep.status != "added-dependency" && dep.status != "already-in-plan" &&
            dep.status != "added-callback-provider" && dep.status != "callback-provider-in-plan")
            continue;
        // `resolved` puede llevar varias claves separadas por "; ".
        std::size_t at = 0;
        while (at <= dep.resolved.size()) {
            const std::size_t end = dep.resolved.find("; ", at);
            const std::string one = dep.resolved.substr(at, end == std::string::npos ? std::string::npos : end - at);
            if (one == key) return true;
            if (end == std::string::npos) break;
            at = end + 2;
        }
    }
    return false;
}

void translate(Script& entry, const std::string& source) {
    const Result<lua::Chunk> chunk = lua::parseLua(source, entry.key);
    if (!chunk) {
        entry.status = "Bloqueado";
        entry.reason = "no se pudo leer el Lua: " + chunk.error();
        entry.active = false;
        entry.destination.clear();
        return;
    }
    hscript::EmitOptions options;
    options.path = entry.key;
    options.source = source;
    options.compat = true;
    options.stageAlreadyEmitted = false;
    options.scriptName = entry.key;
    hscript::EmitResult emitted = hscript::emitHscript(chunk.value(), options);
    entry.hscript = "// FML: generado desde " + entry.key + " sobre la capa psych.* (songs/fml-psych-compat.hx).\n"
                    "// Edita libremente: al reimportar no se pisa un archivo cuya huella no sea la generada.\n" +
                    emitted.source;
    entry.hooks = emitted.hooks;
    entry.pendingCalls = emitted.pendingCalls;
    entry.diagnostics = emitted.pending;
    for (const PortDiagnostic& d : emitted.pending)
        if (d.resolution == PortResolution::Approximate) ++entry.approximations;
    if (!chunk.value().complete) {
        // El lector de Lua nunca falla del todo: devuelve lo que si leyo y los
        // huecos con su linea. Si no leyo NADA util no hay que activar; si
        // leyo parte, se instala con los huecos como PENDIENTE en el .hx y
        // dichos aqui: activo con pendientes, no mudo.
        if (emitted.emitted == 0) {
            entry.status = "Bloqueado";
            entry.reason = "no se pudo leer el Lua" +
                (chunk.value().pending.empty() ? std::string() : ": " + chunk.value().pending.front().detail);
            entry.active = false;
            entry.destination.clear();
            entry.hscript.clear();
            return;
        }
        entry.reason = "el Lua se leyo con huecos (ver PENDIENTE en el .hx)";
    }
    entry.status = entry.approximations > 0 ? "Aproximado" : "Activado";
    entry.active = true;
}

}  // namespace

std::string eventSchemaJson() {
    nlohmann::json out;
    out["params"] = nlohmann::json::array({
        {{"name", "Value 1"}, {"type", "String"}, {"defaultValue", ""}},
        {{"name", "Value 2"}, {"type", "String"}, {"defaultValue", ""}},
    });
    return out.dump(4);
}

std::string scriptMapJson(const Plan& plan) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& entry : plan.scriptMap) out[entry.first] = entry.second;
    return out.dump(2);
}

Plan planBehavior(const std::vector<std::pair<std::string, std::string>>& files,
                  const std::vector<psych::ScriptDependency>& dependencies,
                  const Options& options) {
    Plan plan;
    std::set<std::string> destinations;
    std::set<std::string> luaStems;   // custom_events con .lua: su .txt no es "compilado"
    for (const auto& file : files) {
        const std::string key = file.second;
        const std::string ext = psych::extensionLower(fs::u8path(key));
        if (ext == ".lua" || ext == ".hx") luaStems.insert(lower(key.substr(0, key.size() - ext.size())));
    }
    const std::string songFolder = "songs/" + options.songId + "/scripts/";
    // El inventario del bundle puede traer el mismo archivo bajo dos claves
    // (`custom_events/X.lua` y, por una cita literal, `X.lua`): un script se
    // planifica una vez, con la primera clave, que es la de su carpeta.
    std::set<std::string> seenSources;

    for (const auto& file : files) {
        const std::string key = file.second;
        const std::string ext = psych::extensionLower(fs::u8path(key));
        // Una clave sin carpeta (`static.lua`) viene de una cita literal
        // resuelta por nombre: el papel lo dice la carpeta REAL del archivo.
        std::string low = lower(key);
        std::string foreignSong;
        if (low.find('/') == std::string::npos && (ext == ".lua" || ext == ".hx")) {
            const std::string real = lower(fs::u8path(file.first).generic_u8string());
            for (const char* folder : {"stages", "custom_events", "custom_notetypes", "characters", "scripts"}) {
                const std::string marker = std::string("/") + folder + "/";
                if (real.find(marker) != std::string::npos) { low = std::string(folder) + "/" + low; break; }
            }
            const std::size_t data = real.rfind("/data/");
            if (low.find('/') == std::string::npos && data != std::string::npos) {
                const std::string rest = real.substr(data + 6);
                const std::size_t slash = rest.find('/');
                const std::string owner = slash == std::string::npos ? "" : rest.substr(0, slash);
                if (!owner.empty() && owner != lower(options.songId)) foreignSong = owner;
                low = "data/" + rest;
            }
        }
        if (ext == ".lua" || ext == ".hx") {
            std::error_code ec;
            const fs::path canonical = fs::weakly_canonical(fs::u8path(file.first), ec);
            const std::string sourceKey = lower((ec ? fs::u8path(file.first) : canonical).generic_u8string());
            if (!seenSources.insert(sourceKey).second) continue;
        }
        Script entry;
        entry.source = file.first;
        entry.key = key;
        const std::string stem = safeStem(stemOf(key));

        // ---- lo compilado en el fork: se detecta y se dice ----------------
        if (startsWith(low, "custom_events/") && ext == ".txt") {
            if (luaStems.count(lower(key.substr(0, key.size() - 4)))) continue;  // el .txt acompana a un .lua
            entry.role = "evento";
            entry.psychName = stemOf(key);
            entry.status = "Detectado";
            entry.reason = "evento sin script: su comportamiento va compilado en el motor del mod (fork). Solo con el fuente Haxe se puede recuperar";
            plan.scripts.push_back(entry);
            ++plan.detected;
            ++plan.customEvents;
            continue;
        }
        if (startsWith(low, "custom_notetypes/") && ext == ".txt") {
            if (luaStems.count(lower(key.substr(0, key.size() - 4)))) continue;
            entry.role = "nota";
            entry.psychName = stemOf(key);
            entry.status = "Detectado";
            entry.reason = "tipo de nota sin script: su comportamiento va compilado en el motor del mod (fork)";
            plan.scripts.push_back(entry);
            ++plan.detected;
            ++plan.customNotes;
            continue;
        }
        if (ext != ".lua" && ext != ".hx") continue;

        const auto stored = options.sourceText.find(file.first);
        const std::string source = stored != options.sourceText.end() ? stored->second : psych::read(fs::u8path(file.first));
        bool empty = true;
        for (const char c : source) if (!std::isspace(static_cast<unsigned char>(c))) { empty = false; break; }

        if (startsWith(low, "custom_events/")) {
            entry.role = "evento";
            entry.psychName = stemOf(key);
            ++plan.customEvents;
        } else if (startsWith(low, "custom_notetypes/")) {
            entry.role = "nota";
            entry.psychName = stemOf(key);
            ++plan.customNotes;
        } else if (startsWith(low, "characters/")) {
            entry.role = "personaje";
            entry.psychName = stemOf(key);
        } else if (startsWith(low, "stages/")) {
            entry.role = "escenario";
            entry.psychName = stemOf(key);
        } else if (startsWith(low, "scripts/")) {
            entry.role = "global";
        } else if (startsWith(low, "data/") || startsWith(low, "songdata/") || startsWith(low, "manual-behavior/")) {
            entry.role = "cancion";
        } else {
            entry.role = "otro";
        }

        if (empty) {
            entry.status = "Detectado";
            entry.reason = entry.role == "nota"
                ? "tipo de nota de 0 bytes: su comportamiento va compilado en el motor del mod (fork)"
                : "archivo vacio: no hay nada que activar";
            plan.scripts.push_back(entry);
            ++plan.detected;
            continue;
        }

        if (ext == ".hx") {
            entry.role = "hscript";
            entry.status = "Conservado";
            entry.reason = "HScript de Psych (otra API): se conserva; su traduccion llega en otra etapa";
            plan.scripts.push_back(entry);
            ++plan.conserved;
            continue;
        }
        if (!foreignSong.empty()) {
            // El script de OTRA cancion, citado por nombre desde esta: viaja
            // con el bundle, pero es de aquella y se activa al importarla.
            entry.role = "cancion";
            entry.status = "Conservado";
            entry.reason = "script de otra cancion (data/" + foreignSong + "/), citado por nombre: se activa al importar esa cancion";
            plan.scripts.push_back(entry);
            ++plan.conserved;
            continue;
        }

        // ---- destino por contrato ------------------------------------------
        std::string destination;
        if (entry.role == "cancion" || entry.role == "otro") {
            destination = songFolder;
            for (const Binding& binding : options.manual)
                if (fs::u8path(binding.source) == fs::u8path(file.first) && !binding.difficulty.empty()) {
                    entry.difficulty = binding.difficulty;
                    destination += binding.difficulty + "/";
                }
            destination += stem + ".hx";
        } else if (entry.role == "global") {
            const bool needed = options.includeGlobals || referenced(key, dependencies) ||
                                mentionsSong(source, options.psychSongName, options.songId);
            if (!needed) {
                entry.status = "Conservado";
                entry.reason = "script general del mod que esta cancion no pide por nombre ni por dependencia; marca \"incluir scripts generales\" para activarlo";
                plan.scripts.push_back(entry);
                ++plan.conserved;
                continue;
            }
            destination = songFolder + stem + ".hx";
            if (destinations.count(destination)) destination = songFolder + "global-" + stem + ".hx";
        } else if (entry.role == "escenario") {
            destination = "data/stages/" + stem + ".hx";
        } else if (entry.role == "personaje") {
            destination = "songs/fml-psych-chars/" + stem + ".hx";
        } else if (entry.role == "evento") {
            destination = "data/events/" + stem + ".hx";
            entry.extras.emplace_back("data/events/" + stem + ".json", eventSchemaJson());
        } else if (entry.role == "nota") {
            destination = "data/notes/" + stem + ".hx";
        }
        while (destinations.count(destination)) destination = destination.substr(0, destination.size() - 3) + "-2.hx";
        entry.destination = destination;

        if (!options.activate || std::find(options.disconnectedKeys.begin(), options.disconnectedKeys.end(), key) != options.disconnectedKeys.end()) {
            entry.status = "Conservado";
            entry.reason = options.activate ? "biblioteca existente: el usuario no conecto esta cancion" : "activacion desactivada al importar";
            entry.destination.clear();
            plan.scripts.push_back(entry);
            ++plan.conserved;
            continue;
        }

        translate(entry, source);
        if (entry.active) {
            destinations.insert(entry.destination);
            if (entry.status == "Aproximado") ++plan.approximate; else ++plan.active;
            plan.pendingCalls += static_cast<int>(entry.pendingCalls.size());
            for (const auto& call : entry.pendingCalls)
                if (std::find(plan.pendingNames.begin(), plan.pendingNames.end(), call.name) == plan.pendingNames.end())
                    plan.pendingNames.push_back(call.name);
            for (const auto& hook : entry.hooks)
                if (std::find(plan.hooks.begin(), plan.hooks.end(), hook.codename) == plan.hooks.end())
                    plan.hooks.push_back(hook.codename);
            // Como lo nombra un addLuaScript: con carpeta, sin extension, y el tallo.
            const std::string installed = entry.destination.substr(0, entry.destination.size() - 3);
            const std::string keyNoExt = key.substr(0, key.size() - ext.size());
            plan.scriptMap[keyNoExt] = installed;
            plan.scriptMap[stemOf(key)] = installed;
            if (startsWith(low, "scripts/")) plan.scriptMap["scripts/" + stemOf(key)] = installed;
        } else {
            ++plan.blocked;
        }
        plan.scripts.push_back(entry);
    }
    std::sort(plan.pendingNames.begin(), plan.pendingNames.end());
    return plan;
}

}  // namespace psychplan
}  // namespace fml
