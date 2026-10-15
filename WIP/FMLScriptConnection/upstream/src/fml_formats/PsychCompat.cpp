#include "PsychCompat.hpp"

#include "../fml_core/Hash.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace fml {
namespace psychcompat {

// Generado en PsychCompatScript.cpp desde psych-compat/fml-psych-compat.hx.
const char* const* compatScriptParts(std::size_t* count);

const std::string& body() {
    static const std::string text = [] {
        std::size_t count = 0;
        const char* const* parts = compatScriptParts(&count);
        std::string out;
        for (std::size_t i = 0; i < count; ++i) out += parts[i];
        return out;
    }();
    return text;
}

namespace {

std::string quoteHx(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default:   out += c; break;
        }
    }
    return out + "\"";
}

bool identifier(const std::string& text) {
    if (text.empty() || (std::isdigit(static_cast<unsigned char>(text[0])) != 0)) return false;
    for (const char c : text)
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') return false;
    return true;
}

// Lo que va debajo de la cabecera: las opciones, el cuerpo y los alias.
std::string rest(const Options& options) {
    std::string out;
    out += "// Opciones elegidas al importar (Funkin Mod Lab las reescribe al regenerar).\n";
    out += std::string("var fmlVerbose = ") + (options.verbose ? "true" : "false") + ";\n";
    out += std::string("var fmlWindowReal = ") + (options.windowReal ? "true" : "false") + ";\n\n";
    out += body();
    if (!options.aliases.empty()) {
        out += "\n// ALIAS ELEGIDOS POR EL USUARIO para funciones de un fork sin fuente\n"
               "// (psych-scripts/api-map.txt). Son APROXIMADOS: la funcion del fork no es\n"
               "// la de Psych, solo se parece. Quitalos del api-map y regenera para\n"
               "// deshacerlos.\n";
        for (const auto& alias : options.aliases) {
            if (!identifier(alias.first) || !identifier(alias.second)) continue;
            out += "// fml-alias " + alias.first + " = " + alias.second + "\n";
            out += "if (Reflect.hasField(psych, " + quoteHx(alias.second) + ")) Reflect.setField(psych, " +
                   quoteHx(alias.first) + ", Reflect.field(psych, " + quoteHx(alias.second) + "));\n";
        }
    }
    return out;
}

}  // namespace

std::string emit(const Options& options) {
    const std::string tail = rest(options);
    return "// fml-psych-compat v" + std::to_string(kVersion) + " sha256=" + sha256Hex(tail) + "\n" + tail;
}

Installed inspect(const std::string& text) {
    Installed out;
    const std::string mark = "// fml-psych-compat v";
    if (text.compare(0, mark.size(), mark) != 0) return out;
    const std::size_t eol = text.find('\n');
    const std::string header = text.substr(0, eol == std::string::npos ? text.size() : eol);
    out.ours = true;
    out.version = std::atoi(header.c_str() + mark.size());
    const std::size_t sha = header.find("sha256=");
    const std::string tail = eol == std::string::npos ? std::string() : text.substr(eol + 1);
    out.edited = sha == std::string::npos || header.substr(sha + 7) != sha256Hex(tail);
    out.options.verbose = tail.find("var fmlVerbose = true;") != std::string::npos;
    out.options.windowReal = tail.find("var fmlWindowReal = true;") != std::string::npos;
    std::istringstream lines(tail);
    std::string line;
    while (std::getline(lines, line)) {
        const std::string prefix = "// fml-alias ";
        if (line.compare(0, prefix.size(), prefix) != 0) continue;
        const std::size_t eq = line.find(" = ");
        if (eq == std::string::npos) continue;
        out.options.aliases.emplace_back(line.substr(prefix.size(), eq - prefix.size()), line.substr(eq + 3));
    }
    return out;
}

}  // namespace psychcompat
}  // namespace fml
