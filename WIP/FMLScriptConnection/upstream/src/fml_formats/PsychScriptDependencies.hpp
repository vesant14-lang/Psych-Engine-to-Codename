#pragma once
#include "PsychSource.hpp"
#include "LuaAst.hpp"
#include "PsychHaxeSource.hpp"

namespace fml::psych {
struct ScriptDependency {
    std::string caller, call, key, status, resolved;
    int line = 0;
};
// Discover references, not execution paths. Literal calls in callbacks count
// even if the callback is not reached during initial stage creation.
inline std::vector<ScriptDependency> scriptDependencies(const std::string& source, const std::string& path) {
    std::vector<ScriptDependency> out;
    const auto scriptCall=[](const std::string& s) {
        return s=="addLuaScript" || s=="addHScript" || s=="callScript" || s=="dofile" || s=="require" ||
            s=="loadSongScript" || s=="Paths.lua" || s=="Paths.script";
    };
    const auto callback=[](const std::string& s) {
        return s=="callOnLuas" || s=="callOnHScript" || s=="callOnHScripts" || s=="callOnScripts";
    };
    const auto external=[](const std::string& s) {
        return s=="os.execute" || s=="io.popen" || s=="Sys.command" || s=="Sys.exit" ||
            s=="changeWallpaper" || s=="setWallpaper" || s=="SystemParametersInfo" ||
            s.find("window.")!=std::string::npos || s.find("Window.")!=std::string::npos;
    };
    if (extensionLower(fs::u8path(path))==".lua") {
        const auto parsed=lua::parseLua(source,path);
        if (!parsed) return out;
        lua::walk(parsed.value().root,[&](const lua::Node& n) {
            if (n.kind!=lua::NodeKind::Call) return true;
            const auto name=lua::calleeName(n);
            if (!scriptCall(name) && !external(name) && !callback(name) && name!="callMethod") return true;
            const auto* arg=n.child(1);
            out.push_back({path,name,arg && arg->kind==lua::NodeKind::String ? arg->text : "",
                external(name) ? "external-isolated" : callback(name) ? "callback-reference" :
                name=="callMethod" ? "engine-method-pending" : "", "",n.line});
            return true;
        });
    } else {
        const hx::Document d(source);
        for (size_t i=0;i+1<d.tokens.size();++i) {
            if (d.tokens[i].quoted) continue;
            std::string name=d.tokens[i].text;
            size_t end=i+1;
            while (end+1<d.tokens.size() && d.is(end,".") && !d.tokens[end+1].quoted) {
                name+='.'+d.tokens[end+1].text; end+=2;
            }
            if (!d.is(end,"(") || (!scriptCall(name) && !external(name))) continue;
            const auto args=d.split({end+1,d.close(end)});
            std::string key;
            if (!args.empty() && args[0].end==args[0].begin+1 && d.tokens[args[0].begin].quoted)
                key=d.tokens[args[0].begin].text;
            out.push_back({path,name,key,external(name) ? "external-isolated" : "","",d.tokens[i].line});
            i=end;
        }
    }
    return out;
}
inline bool providesScriptFunction(const std::string& source, const std::string& path, const std::string& name) {
    if (extensionLower(fs::u8path(path))==".lua") {
        const auto parsed=lua::parseLua(source,path);
        if (!parsed) return false;
        // Top-level declarations only: a function hidden in a runtime branch
        // is not proof that the broadcast has a provider.
        for (const auto& n : parsed.value().root.children)
            if (n.kind==lua::NodeKind::FunctionDecl && n.text==name) return true;
        return false;
    }
    return !hx::Document(source).function(name).empty();
}
inline nlohmann::json dependencyReport(const std::vector<ScriptDependency>& dependencies) {
    auto out=nlohmann::json::array();
    for (const auto& ref : dependencies) out.push_back({{"caller",ref.caller},{"line",ref.line},
        {"call",ref.call},{"requested",ref.key},{"status",ref.status},{"resolved",ref.resolved}});
    return out;
}
}
