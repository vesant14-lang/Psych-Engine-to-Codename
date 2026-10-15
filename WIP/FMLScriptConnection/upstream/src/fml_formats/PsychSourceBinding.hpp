#pragma once
#include "PsychSource.hpp"
#include "../fml_io/SafeWrite.hpp"
#include <sstream>

namespace fml::psych {
inline std::string sourceModKey(fs::path root) {
    root = unwrap(root);
    if (root.filename() == "assets") root = root.parent_path();
    std::error_code ec;
    auto canonical = fs::weakly_canonical(root, ec);
    auto key = (ec ? root.lexically_normal() : canonical).generic_u8string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}
inline fs::path boundSource(const fs::path& config, const fs::path& mod) {
    const auto doc = nlohmann::json::parse(read(config/"psych-source-bindings.json"), nullptr, false);
    const auto key = sourceModKey(mod);
    if (!doc.is_object() || !doc.contains(key) || !doc[key].is_string()) return {};
    return fs::u8path(doc[key].get<std::string>());
}
inline fs::path discoverStageSource(const fs::path& mod) {
    auto base = unwrap(mod);
    if (base.filename() == "assets") base = base.parent_path();
    // No drive-wide scan or unrelated engine source. Only this package.
    for (const auto* name : {"source/PlayState.hx", "source/states/PlayState.hx", "PlayState.hx"}) {
        const auto candidate = base/name;
        if (file(candidate)) return candidate;
    }
    return {};
}
inline bool bindStageSource(const fs::path& config, const fs::path& mod,
        const fs::path& source, std::string& error) {
    if (config.empty() || mod.empty()) { error="No hay carpeta de configuracion/mod"; return false; }
    std::error_code ec;
    const auto size = fs::file_size(source, ec);
    if (ec || !size || size > 8*1024*1024) { error="Fuente Haxe no legible o demasiado grande"; return false; }
    const auto bytes = read(source);
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : bytes) { hash ^= c; hash *= 1099511628211ull; }
    std::ostringstream name; name << std::hex << hash << ".hx";
    const auto cached = config/"psych-sources"/name.str();
    auto text = read(config/"psych-source-bindings.json");
    auto doc = text.empty() ? nlohmann::json::object() : nlohmann::json::parse(text,nullptr,false);
    if (!doc.is_object()) { error="La configuracion de fuentes no es valida; se conserva sin reemplazar"; return false; }
    DiagnosticSink sink;
    SafeWriter writer(config, SafeWriter::timestamp());
    if (file(cached) && read(cached) != bytes) { error="Conflicto en la cache de fuentes"; return false; }
    if (!file(cached) && !writer.write(cached, bytes, sink)) { error="No se pudo conservar el fuente"; return false; }
    doc[sourceModKey(mod)] = fs::absolute(cached).u8string();
    if (!writer.write(config/"psych-source-bindings.json",doc.dump(2),sink)) {
        error="No se pudo guardar la asociacion del fuente"; return false;
    }
    return true;
}
}
