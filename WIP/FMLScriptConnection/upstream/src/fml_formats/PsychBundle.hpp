// Read-only dependency inventory. No Lua/HScript is executed during import.
#pragma once
#include "PsychSource.hpp"
#include "PsychSongAudio.hpp"
#include "PsychScriptDependencies.hpp"
#include "../../third_party/pugixml.hpp"
#include <set>

namespace fml { namespace psych {
struct Bundle {
    std::vector<std::pair<std::string, std::string>> files; // source, original key
    std::vector<std::string> warnings;
    std::vector<std::string> stages, characters, events, noteTypes;
    std::vector<ScriptDependency> dependencies;
    std::vector<std::string> globalScripts;
};
inline void unique(std::vector<std::string>& list, const std::string& s) {
    if (!s.empty() && std::find(list.begin(), list.end(), s) == list.end()) list.push_back(s);
}
inline std::vector<std::string> quotedStrings(const std::string& text) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\'' && text[i] != '"') continue;
        const char quote = text[i++]; std::string s;
        for (; i < text.size() && text[i] != quote; ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) ++i;
            s += text[i];
        }
        if (relativeKey(s)) unique(out, s);
    }
    return out;
}
inline Bundle collectBundle(const std::vector<fs::path>& sources,
                            const std::vector<fs::path>& charts,
                            const std::string& songId,
                            const std::vector<fs::path>& manual = {},
                            bool includeGlobalScripts = true) {
    using json = nlohmann::json;
    Bundle out;
    std::set<std::string> keys;
    std::map<std::string, fs::path> lookupCache;
    auto add = [&](const fs::path& source, const std::string& key) {
        if (relativeKey(key) && file(source) && keys.insert(key).second)
            out.files.emplace_back(source.u8string(), key);
    };
    auto byKey = [&](const std::string& key) {
        if (keys.count(key)) return;
        auto found = lookupCache.find(key);
        if (found == lookupCache.end()) found = lookupCache.emplace(key, resolve(sources, key)).first;
        add(found->second, key);
    };
    auto directoryFiles = [&](const fs::path& dir, const std::string& prefix, bool scriptsOnly) {
        std::error_code ec;
        std::vector<fs::path> list;
        for (const auto& entry : fs::directory_iterator(dir, ec)) if (entry.is_regular_file(ec)) list.push_back(entry.path());
        std::sort(list.begin(), list.end());
        for (const auto& p : list) {
            const auto ext = extensionLower(p);
            if (scriptsOnly && ext != ".lua" && ext != ".hx") continue;
            add(p, prefix + "/" + p.filename().u8string());
        }
    };
    auto names = [&](const json& document) {
        if (!document.is_object()) return;
        const json& song = document.contains("song") && document["song"].is_object() ? document["song"] : document;
        auto str = [&](const char* key) { return song.contains(key) && song[key].is_string() ? song[key].get<std::string>() : std::string(); };
        for (const char* key : {"player1", "player2", "gfVersion", "player3"}) unique(out.characters, str(key));
        auto stage = str("stage");
        if (stage.empty() && song.contains("notes")) stage = vanillaStage(songId);
        unique(out.stages, stage);
        // Use RAW event names, before chart conversion changes those names.
        if (song.contains("events") && song["events"].is_array())
            for (const auto& group : song["events"])
                if (group.is_array() && group.size() > 1 && group[1].is_array())
                    for (const auto& e : group[1])
                        if (e.is_array() && !e.empty() && e[0].is_string()) unique(out.events, e[0].get<std::string>());
        if (song.contains("notes") && song["notes"].is_array())
            for (const auto& section : song["notes"])
                if (section.is_object() && section.contains("sectionNotes") && section["sectionNotes"].is_array())
                    for (const auto& note : section["sectionNotes"])
                        if (note.is_array() && note.size() > 3 && note[3].is_string()) unique(out.noteTypes, note[3].get<std::string>());
    };
    for (const auto& chart : charts) {
        const auto owner = ownerRoot(chart);
        const auto key = owner.empty() ? "data/" + songId + "/" + chart.filename().u8string() : chart.lexically_relative(owner).generic_u8string();
        add(chart, key);
        const auto parsed = json::parse(read(chart), nullptr, false, true);
        if (parsed.is_discarded()) out.warnings.push_back("JSON no legible: " + key); else names(parsed);
        // PlayState loads song scripts by SONG.song, not by difficulty name or
        // necessarily the folder that held the selected chart JSON.
        if (parsed.is_object() && parsed.contains("song") && parsed["song"].is_object() &&
            parsed["song"].contains("song") && parsed["song"]["song"].is_string()) {
            const auto header = formatSongPath(parsed["song"]["song"].get<std::string>());
            if (relativeKey(header)) for (const auto& root : sources)
                for (const auto* prefix : {"data/","data/songData/"}) {
                    const auto key=std::string(prefix)+header;
                    directoryFiles(root / fs::u8path(key),key,false);
                    directoryFiles(root / fs::u8path(key+"/scripts"),key+"/scripts",true);
                }
        }
        const auto parentKey = fs::u8path(key).parent_path().generic_u8string();
        directoryFiles(chart.parent_path(), parentKey, false);
        directoryFiles(chart.parent_path()/"scripts",parentKey+"/scripts",true);
        const auto events = chart.parent_path() / "events.json";
        if (file(events)) names(json::parse(read(events), nullptr, false, true));
    }
    for (const auto& root : sources) {
        if (includeGlobalScripts) directoryFiles(root / "scripts", "scripts", true);
        directoryFiles(root / "data" / fs::u8path(songId), "data/" + songId, true);
    }
    for (const auto& stage : out.stages)
        for (const char* ext : {".json", ".lua", ".hx"}) byKey("stages/" + stage + ext);
    for (const auto& ch : out.characters)
        for (const char* ext : {".json", ".lua", ".hx"}) byKey("characters/" + ch + ext);
    for (const auto& ev : out.events)
        for (const char* ext : {".lua", ".hx", ".txt"}) byKey("custom_events/" + ev + ext);
    for (const auto& note : out.noteTypes)
        for (const char* ext : {".lua", ".hx", ".txt"}) byKey("custom_notetypes/" + note + ext);

    for (std::size_t i = 0; i < manual.size(); ++i)
        if (std::none_of(out.files.begin(),out.files.end(),[&](const auto& entry) { return fs::u8path(entry.first) == manual[i]; }))
            add(manual[i],"manual-behavior/" + std::to_string(i + 1) + "/" + manual[i].filename().u8string());
    // Literal dependencies, transitively: added scripts/JSON are visited too.
    // Dynamic concatenations cannot be proven statically and are reported.
    bool scripts = false;
    std::vector<std::pair<std::string,std::string>> possibleGlobals;
    std::set<std::string> globalKeys;
    for (const auto& root : sources) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(root/"scripts",ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const auto ext=extensionLower(entry.path());
            const auto key="scripts/"+entry.path().filename().u8string();
            if ((ext==".lua" || ext==".hx") && globalKeys.insert(key).second)
                possibleGlobals.emplace_back(entry.path().u8string(),key);
        }
    }
    std::sort(possibleGlobals.begin(),possibleGlobals.end());
    for (std::size_t at = 0; at < out.files.size() && at < 20000; ++at) {
        const auto entry = out.files[at];
        const auto ext = extensionLower(fs::u8path(entry.first));
        if ((ext == ".frag" || ext == ".vert") && entry.second.rfind("shaders/", 0) == 0) {
            auto sidecar = fs::u8path(entry.second); sidecar.replace_extension(".txt");
            byKey(sidecar.generic_u8string());
        }
        if (ext != ".lua" && ext != ".hx" && ext != ".json" && ext != ".xml") continue;
        scripts |= ext == ".lua" || ext == ".hx";
        const auto document = read(fs::u8path(entry.first));
        if (ext==".lua" || ext==".hx") {
            if (entry.second.rfind("scripts/",0)==0) unique(out.globalScripts,entry.second);
            for (auto ref : scriptDependencies(document,entry.second)) {
                if (ref.status=="external-isolated") {
                    unique(out.warnings,"Accion externa aislada: "+ref.caller+":"+std::to_string(ref.line)+" "+ref.call);
                } else if (ref.status=="engine-method-pending") {
                    unique(out.warnings,"Metodo del motor/fork pendiente: "+ref.caller+":"+std::to_string(ref.line)+" "+ref.key);
                } else if (ref.status=="callback-reference") {
                    auto candidates=out.files;
                    candidates.insert(candidates.end(),possibleGlobals.begin(),possibleGlobals.end());
                    std::set<std::string> seen;
                    bool anyAdded=false;
                    for (const auto& candidate : candidates) {
                        const auto ext=extensionLower(fs::u8path(candidate.first));
                        if ((ext!=".lua" && ext!=".hx") || !seen.insert(candidate.second).second || ref.key.empty()) continue;
                        if (!providesScriptFunction(read(fs::u8path(candidate.first)),candidate.second,ref.key)) continue;
                        anyAdded |= !keys.count(candidate.second);
                        add(fs::u8path(candidate.first),candidate.second);
                        if (!ref.resolved.empty()) ref.resolved+="; ";
                        ref.resolved+=candidate.second;
                    }
                    ref.status=ref.key.empty() ? "dynamic-pending" : ref.resolved.empty() ? "callback-provider-pending" :
                        anyAdded ? "added-callback-provider" : "callback-provider-in-plan";
                    if (ref.status=="callback-provider-pending") unique(out.warnings,
                        "Funcion sin proveedor demostrado: "+ref.caller+":"+std::to_string(ref.line)+" "+ref.key);
                } else if (!relativeKey(ref.key)) {
                    ref.status=ref.key.empty() ? "dynamic-pending" : "outside-root-rejected";
                } else {
                    std::vector<std::string> candidates;
                    std::string key=ref.key;
                    if (ref.call=="require") std::replace(key.begin(),key.end(),'.','/');
                    for (const auto& prefix : {std::string(),std::string("scripts/")}) {
                        candidates.push_back(prefix+key);
                        if (extensionLower(fs::u8path(key))!=".lua" && extensionLower(fs::u8path(key))!=".hx") {
                            candidates.push_back(prefix+key+(ref.call=="addHScript" ? ".hx" : ".lua"));
                            if (ref.call=="Paths.script") candidates.push_back(prefix+key+".hx");
                        }
                    }
                    for (const auto& candidate : candidates) {
                        const auto source=resolve(sources,candidate);
                        if (source.empty()) continue;
                        const bool already=keys.count(candidate)!=0;
                        add(source,candidate); ref.resolved=candidate;
                        ref.status=already ? "already-in-plan" : "added-dependency";
                        break;
                    }
                    if (ref.status.empty()) ref.status="missing";
                }
                if (ref.status=="missing" || ref.status=="dynamic-pending" || ref.status=="outside-root-rejected")
                    unique(out.warnings,"Script pendiente: "+ref.caller+":"+std::to_string(ref.line)+" "+ref.call+"("+ref.key+") ["+ref.status+"]");
                out.dependencies.push_back(std::move(ref));
            }
        }
        std::vector<std::string> literals;
        if (ext == ".xml") {
            // Shader names live in XML attributes, not in the Lua sidecar.
            // Do not treat atlas frame names/numeric coordinates as dependencies.
            pugi::xml_document xml;
            if (!xml.load_buffer(document.data(), document.size()) ||
                std::string(xml.document_element().name()) != "Modchart") continue;
            scripts = true;
            for (const auto event : xml.select_nodes("//Event"))
                for (const char* attribute : {"shader","file","path","image","sound","video"}) {
                    const std::string value = event.node().attribute(attribute).value();
                    if (relativeKey(value)) unique(literals, value);
                }
        } else literals = quotedStrings(document);
        for (const auto& literal : literals) {
            byKey(literal);
            for (const char* suffix : {".lua", ".hx"}) byKey(literal + suffix);
            // Change Character events / addCharacterToList and song scripts can
            // load characters that are absent from the initial chart header.
            for (const char* suffix : {".json", ".lua", ".hx"}) byKey("characters/" + literal + suffix);
            for (const char* prefix : {"images/", "shaders/", "sounds/", "music/", "videos/", "data/"}) {
                const std::string key = std::string(prefix) + literal;
                byKey(key);
                // A literal folder prefix followed by a dynamic filename is
                // common in stages (e.g. 'destiny/' .. name). Preserve that
                // explicitly named resource folder, including nested atlases.
                if (!literal.empty() && literal.back() == '/') for (const auto& root : sources) {
                    std::error_code ec;
                    const auto directory = root / fs::u8path(key);
                    for (auto it = fs::recursive_directory_iterator(directory, fs::directory_options::skip_permission_denied, ec);
                         it != fs::recursive_directory_iterator() && !ec; it.increment(ec)) {
                        if (it->is_symlink(ec) || it.depth() > 8) { it.disable_recursion_pending(); continue; }
                        if (!it->is_regular_file(ec)) continue;
                        const auto ext = it->path().extension().u8string();
                        if (ext == ".png" || ext == ".xml" || ext == ".json" || ext == ".txt" || ext == ".ogg" || ext == ".mp4" || ext == ".webm" || ext == ".frag" || ext == ".vert")
                            add(it->path(), it->path().lexically_relative(root).generic_u8string());
                    }
                }
                if (std::string(prefix) == "images/") {
                    for (const char* e : {".png", ".xml", ".txt", ".json"}) byKey(key + e);
                    // Animate atlases have Animation.json and one or more maps.
                    const auto animation = resolve(sources, key + "/Animation.json");
                    if (!animation.empty()) directoryFiles(animation.parent_path(), key, false);
                } else if (std::string(prefix) == "shaders/") {
                    byKey(key + ".frag"); byKey(key + ".vert"); byKey(key + ".txt");
                } else if (std::string(prefix) == "videos/") {
                    byKey(key + ".mp4"); byKey(key + ".webm");
                } else if (std::string(prefix) == "data/") {
                    byKey(key + ".json"); byKey(key + ".txt");
                } else byKey(key + ".ogg");
            }
            for (const auto& icon : {"images/icons/" + literal, "images/icons/icon-" + literal}) byKey(icon + ".png");
        }
    }
    if (out.files.size() >= 20000) out.warnings.push_back("Limite de 20000 dependencias: revisar el inventario.");
    if (scripts) out.warnings.push_back("Lua/HScript y modcharts se conservan, no se ejecutan. Las rutas calculadas en tiempo de ejecucion y codigo compilado requieren revision manual.");
    return out;
}
} } // namespace fml::psych
