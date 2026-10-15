#pragma once
#include "PsychSource.hpp"
#include "PsychScriptDependencies.hpp"
#include "../fml_core/Hash.hpp"
#include <functional>
#include <optional>
#include <algorithm>
#include <cctype>

namespace fml::psychlib {
using Read = std::function<std::optional<std::string>(const std::string&)>;
using json = nlohmann::json;
inline const char* indexPath() { return "psych-scripts/library/index.json"; }
struct Entry {
    std::string key, origin, hash, original, prepared;
};
inline std::string origin(const std::filesystem::path& source) {
    auto owner = psych::ownerRoot(source);
    // ownerRoot knows charts/stages/characters. Standalone global scripts also
    // need their owning pack, never a shared hash of the empty path.
    if (owner.empty()) {
        auto path = source.parent_path();
        for (int i=0; !path.empty() && i<12; ++i, path=path.parent_path()) {
            const auto name=path.filename().u8string();
            if (name=="scripts" || name=="custom_events" || name=="custom_notetypes" || name=="shaders") {
                owner=path.parent_path(); break;
            }
            if (path==path.root_path()) break;
        }
    }
    if (owner.empty()) owner=source.parent_path();
    auto root = owner.lexically_normal().generic_u8string();
    for (char& c : root) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return sha256Hex(root);
}
inline bool safeKey(const std::string& key) {
    if (key.empty() || key.front() == '/' || key.find('\\') != std::string::npos || key.find(':') != std::string::npos) return false;
    for (const auto& part : std::filesystem::u8path(key)) if (part == ".." || part == ".") return false;
    return true;
}
inline std::vector<Entry> read(const Read& reader) {
    std::vector<Entry> out;
    if (!reader) return out;
    auto text = reader(indexPath());
    if (!text) return out;
    const auto doc = json::parse(*text, nullptr, false);
    if (!doc.is_object() || !doc.contains("format") || doc["format"] != "fml-psych-library-1" || !doc.contains("scripts") || !doc["scripts"].is_array()) return out;
    for (const auto& item : doc["scripts"]) {
        if (!item.is_object()) continue;
        bool valid = true;
        for (auto key : {"key", "origin", "hash", "original", "prepared"})
            if (!item.contains(key) || !item[key].is_string()) valid = false;
        if (!valid) continue;
        Entry e{item["key"], item["origin"], item["hash"], item["original"], item["prepared"]};
        if (!safeKey(e.key) || !safeKey(e.original) || e.original.rfind("psych-scripts/library/", 0) != 0) continue;
        if (!e.prepared.empty() && (!safeKey(e.prepared) || e.prepared.rfind("psych-scripts/library/", 0) != 0)) continue;
        out.push_back(std::move(e));
    }
    return out;
}
inline std::string write(const std::vector<Entry>& entries) {
    json out = {{"format", "fml-psych-library-1"}, {"scripts", json::array()}};
    for (const auto& e : entries) out["scripts"].push_back({{"key",e.key},{"origin",e.origin},{"hash",e.hash},{"original",e.original},{"prepared",e.prepared}});
    return out.dump(2);
}
inline std::optional<std::string> verifiedSource(const Entry& e, const Read& reader) {
    if (!reader) return {};
    auto bytes = reader(e.original);
    if (!bytes || sha256Hex(*bytes) != e.hash) return {};
    return bytes;
}
inline std::string withoutExtension(const std::string& key) {
    return std::filesystem::u8path(key).replace_extension().generic_u8string();
}
// Exact source content wins; a missing literal dependency must have a unique
// version in this source mod's library. Never connect unrelated same-name mods.
inline std::vector<Entry> candidates(const std::vector<Entry>& entries, const Read& reader,
        const std::string& sourceOrigin, const std::vector<std::pair<std::string,std::string>>& files,
        const std::vector<psych::ScriptDependency>& dependencies) {
    std::vector<Entry> out;
    for (const auto& e : entries) {
        if (e.origin != sourceOrigin || !verifiedSource(e, reader)) continue;
        bool found = false, hasSource = false;
        for (const auto& file : files) if (file.second == e.key) {
            hasSource = true;
            found = sha256Hex(psych::read(std::filesystem::u8path(file.first))) == e.hash;
            break;
        }
        if (!found && !hasSource) for (const auto& dep : dependencies) {
            if (dep.status != "missing" || dep.key.empty()) continue;
            const auto wanted = withoutExtension(dep.key);
            const bool matches = wanted == withoutExtension(e.key) ||
                (wanted.find('/') == std::string::npos && wanted == std::filesystem::u8path(e.key).stem().u8string());
            if (!matches) continue;
            int versions = 0;
            for (const auto& other : entries)
                if (other.origin == e.origin && (withoutExtension(other.key) == wanted ||
                    (wanted.find('/') == std::string::npos && std::filesystem::u8path(other.key).stem().u8string() == wanted))) ++versions;
            if (versions == 1) found = true;
        }
        if (found) out.push_back(e);
    }
    // Follow literal calls transitively, but only within this provenance and
    // with one unambiguous stored version. Current source files take precedence.
    for (std::size_t at = 0; at < out.size(); ++at) {
        const auto bytes = verifiedSource(out[at], reader);
        if (!bytes) continue;
        for (const auto& dep : psych::scriptDependencies(*bytes, out[at].key)) {
            if (!dep.status.empty() || dep.key.empty()) continue;
            const auto wanted = withoutExtension(dep.key);
            std::vector<Entry> found;
            for (const auto& entry : entries) {
                if (entry.origin != sourceOrigin) continue;
                if (withoutExtension(entry.key) != wanted && !(wanted.find('/') == std::string::npos &&
                    std::filesystem::u8path(entry.key).stem().u8string() == wanted)) continue;
                if (std::any_of(files.begin(),files.end(),[&](const auto& f) { return f.second == entry.key; })) continue;
                if (verifiedSource(entry,reader)) found.push_back(entry);
            }
            if (found.size() == 1 && std::none_of(out.begin(),out.end(),[&](const auto& e) { return e.original == found[0].original; })) out.push_back(found[0]);
        }
    }
    return out;
}
} // namespace fml::psychlib
