// Opt-in static reader for Haxe-backed Psych forks. This is deliberately NOT
// an interpreter: no user code, imports, macros, loops or system calls run.
// Only selected creation branches and an explicit vocabulary are recovered.
#pragma once
#include "PsychStage.hpp"
#include "../../third_party/json.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

namespace fml::psych::hx {
using Value = nlohmann::json;
using Env = std::map<std::string, Value>;
struct Token { std::string text; bool quoted = false; int line = 1; };
struct Range { size_t begin = 0, end = 0; bool empty() const { return begin >= end; } };

struct Document {
    std::string source;
    std::vector<Token> tokens;
    std::string error;
    explicit Document(const std::string& bytes) : source(bytes) {
        if (bytes.size() > 8 * 1024 * 1024) { error = "Fuente Haxe mayor de 8 MiB; elige un archivo mas pequeno."; return; }
        int line = 1;
        for (size_t p = 0; p < bytes.size();) {
            const char c = bytes[p];
            if (std::isspace(static_cast<unsigned char>(c))) { if (c == '\n') ++line; ++p; continue; }
            if (bytes.compare(p, 2, "//") == 0) { while (p < bytes.size() && bytes[p] != '\n') ++p; continue; }
            if (bytes.compare(p, 2, "/*") == 0) {
                p += 2;
                while (p + 1 < bytes.size() && bytes.compare(p, 2, "*/") != 0) { if (bytes[p++] == '\n') ++line; }
                if (p + 1 >= bytes.size()) { error = "Comentario Haxe sin cerrar."; break; }
                p += 2; continue;
            }
            Token t; t.line = line;
            if (c == '\'' || c == '"') {
                t.quoted = true; ++p;
                while (p < bytes.size() && bytes[p] != c) {
                    char ch = bytes[p++];
                    if (ch == '\\' && p < bytes.size()) {
                        ch = bytes[p++];
                        if (ch == 'n') ch = '\n'; else if (ch == 'r') ch = '\r'; else if (ch == 't') ch = '\t';
                        else if (ch != '\\' && ch != '\'' && ch != '"') {
                            error = "Escape Haxe no soportado, linea " + std::to_string(line); break;
                        }
                    }
                    if (ch == '\n') ++line;
                    t.text += ch;
                }
                if (p == bytes.size()) { error = "Cadena Haxe sin cerrar."; break; }
                ++p;
            } else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                const bool number = std::isdigit(static_cast<unsigned char>(c));
                const size_t start = p++;
                while (p < bytes.size() && (std::isalnum(static_cast<unsigned char>(bytes[p])) ||
                    bytes[p] == '_' || (number && bytes[p] == '.' && p + 1 < bytes.size() && bytes[p+1] != '.'))) ++p;
                t.text = bytes.substr(start, p-start);
            } else {
                t.text = bytes.substr(p++, 1);
                if (p < bytes.size()) {
                    const auto pair = t.text + bytes[p];
                    if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" || pair == "&&" ||
                        pair == "||" || pair == "+=" || pair == "-=" || pair == "*=" || pair == "/=") { t.text = pair; ++p; }
                }
            }
            tokens.push_back(std::move(t));
            if (tokens.size() > 600000) { error = "Demasiados tokens en el fuente Haxe."; break; }
            if (!error.empty()) break;
        }
    }
    bool is(size_t p, const std::string& s) const { return p < tokens.size() && !tokens[p].quoted && tokens[p].text == s; }
    size_t close(size_t p) const {
        if (p >= tokens.size()) return tokens.size();
        const std::string end = is(p,"(") ? ")" : is(p,"[") ? "]" : is(p,"{") ? "}" : "";
        if (end.empty()) return p;
        std::vector<std::string> stack{end};
        while (++p < tokens.size()) {
            if (tokens[p].quoted) continue;
            const auto& s = tokens[p].text;
            if (s == "(" || s == "[" || s == "{") {
                stack.push_back(s == "(" ? ")" : s == "[" ? "]" : "}");
            } else if (s == ")" || s == "]" || s == "}") {
                if (s != stack.back()) return tokens.size();
                stack.pop_back(); if (stack.empty()) return p;
            }
        }
        return p;
    }
    std::string text(Range r) const {
        std::string s;
        for (size_t p = r.begin; p < r.end; ++p) s += tokens[p].quoted ? Value(tokens[p].text).dump() : tokens[p].text;
        return s;
    }
    std::vector<Range> split(Range r, const std::string& separator = ",") const {
        std::vector<Range> out;
        size_t start = r.begin;
        for (size_t p = r.begin; p < r.end; ++p) {
            if (is(p,"(") || is(p,"[") || is(p,"{")) p = close(p);
            else if (is(p,separator)) { out.push_back({start,p}); start = p+1; }
        }
        if (start < r.end) out.push_back({start,r.end});
        return out;
    }
    Range function(const std::string& name) const {
        for (size_t p = 0; p + 2 < tokens.size(); ++p) {
            if (!is(p,"function") || !is(p+1,name) || !is(p+2,"(")) continue;
            size_t start = close(p+2)+1;
            while (start < tokens.size() && !is(start,"{") && !is(start,";")) ++start;
            if (start < tokens.size() && is(start,"{")) {
                const size_t end = close(start);
                if (end < tokens.size()) return {start+1,end};
            }
        }
        return {};
    }
};

inline std::string stringValue(const Value& v) { return v.is_string() ? v.get<std::string>() : v.dump(); }
inline Value evaluate(const Document& d, Range r, const Env& env, int depth = 0) {
    if (r.empty() || r.end > d.tokens.size() || depth > 48) return {};
    while (d.is(r.begin,"(") && d.close(r.begin) == r.end-1) { ++r.begin; --r.end; }
    if (r.empty()) return {};
    auto eval = [&](Range x) { return evaluate(d,x,env,depth+1); };
    // Lowest precedence first; split at the last binary operator of a level.
    for (const std::set<std::string>& ops : {std::set<std::string>{"||"}, {"&&"}, {"==","!="},
            {"<",">","<=",">="}, {"+","-"}, {"*","/","%"}}) {
        size_t op = r.end;
        for (size_t p = r.begin; p < r.end; ++p) {
            if (d.is(p,"(") || d.is(p,"[") || d.is(p,"{")) { p = d.close(p); continue; }
            if (p == r.begin || d.tokens[p].quoted || !ops.count(d.tokens[p].text)) continue;
            const auto& prev = d.tokens[p-1].text;
            if (prev == "*" || prev == "/" || prev == "+" || prev == "-" || prev == "(" || prev == "=") continue;
            op = p;
        }
        if (op == r.end) continue;
        const auto a = eval({r.begin,op}), b = eval({op+1,r.end});
        if (a.is_null() || b.is_null()) return {};
        const auto& s = d.tokens[op].text;
        if (s == "==") return a == b; if (s == "!=") return a != b;
        if (s == "+" && (a.is_string() || b.is_string())) return stringValue(a) + stringValue(b);
        if (a.is_boolean() && b.is_boolean()) {
            if (s == "&&") return a.get<bool>() && b.get<bool>();
            if (s == "||") return a.get<bool>() || b.get<bool>();
        }
        if (!a.is_number() || !b.is_number()) return {};
        const double x = a.get<double>(), y = b.get<double>();
        if (s == "<") return x < y; if (s == ">") return x > y;
        if (s == "<=") return x <= y; if (s == ">=") return x >= y;
        double z = 0;
        if (s == "+") z = x+y; else if (s == "-") z = x-y; else if (s == "*") z = x*y;
        else if (s == "/" && y != 0) z = x/y; else if (s == "%" && y != 0) z = std::fmod(x,y); else return {};
        return std::isfinite(z) ? Value(z) : Value();
    }
    if (d.is(r.begin,"!") || d.is(r.begin,"-") || d.is(r.begin,"+")) {
        const auto v = eval({r.begin+1,r.end});
        if (d.is(r.begin,"!") && v.is_boolean()) return !v.get<bool>();
        if (v.is_number()) return (d.is(r.begin,"-") ? -1 : 1) * v.get<double>();
        return {};
    }
    if (r.end == r.begin+1) {
        const auto& t = d.tokens[r.begin];
        if (t.quoted) return t.text.find('$') == std::string::npos ? Value(t.text) : Value();
        if (t.text == "true") return true; if (t.text == "false") return false;
        char* end = nullptr; const double n = std::strtod(t.text.c_str(),&end);
        if (end != t.text.c_str() && *end == 0 && std::isfinite(n)) return n;
    }
    if (d.is(r.begin,"[") && d.close(r.begin) == r.end-1) {
        Value a = Value::array();
        for (const auto x : d.split({r.begin+1,r.end-1})) { const auto v = eval(x); if (v.is_null()) return {}; a.push_back(v); }
        return a;
    }
    for (size_t p = r.begin; p < r.end; ++p) if (d.is(p,"(") && d.close(p) == r.end-1) {
        const auto name = d.text({r.begin,p});
        std::vector<Value> args;
        for (const auto a : d.split({p+1,r.end-1})) args.push_back(eval(a));
        if (args.size() == 1) {
            if (name == "Std.int" && args[0].is_number()) return std::trunc(args[0].get<double>());
            if (name == "Std.parseFloat" && !args[0].is_null()) {
                auto s = stringValue(args[0]); char* end = nullptr; const double n = std::strtod(s.c_str(),&end);
                if (end != s.c_str() && *end == 0 && std::isfinite(n)) return n;
            }
            if (name == "Math.isNaN" && args[0].is_number()) return false;
        }
        const auto dot = name.rfind('.');
        if (dot != std::string::npos) {
            const auto found = env.find(name.substr(0,dot));
            if (found != env.end() && found->second.is_string()) {
                std::string s = found->second.get<std::string>(); const auto method = name.substr(dot+1);
                if (method == "endsWith" && args.size() == 1 && args[0].is_string()) {
                    const auto tail = args[0].get<std::string>(); return s.size() >= tail.size() && s.compare(s.size()-tail.size(),tail.size(),tail) == 0;
                }
                if (method == "replace" && args.size() == 2 && args[0].is_string() && args[1].is_string()) {
                    const auto from = args[0].get<std::string>(), to = args[1].get<std::string>();
                    if (from.empty()) return {};
                    for (size_t at = 0; (at = s.find(from,at)) != std::string::npos; at += to.size()) s.replace(at,from.size(),to);
                    return s;
                }
            }
        }
        return {};
    }
    const auto it = env.find(d.text(r)); return it == env.end() ? Value() : it->second;
}

// A bounded structural walker. Unknown conditions/loops skip their ENTIRE
// branch, including unbraced statements. Never concatenate mutually exclusive
// stage cases, frame callbacks, timer callbacks or inactive preprocessor arms.
struct Walker {
    const Document& d;
    Env env;
    std::vector<std::string> issues;
    bool foundStage = false;
    int matchedEvents = 0;
    bool halted = false;
    std::function<void(Range,bool,Walker&)> statement;
    void issue(Range r, const std::string& why) {
        if (issues.size() < 256) issues.push_back("L" + std::to_string(r.begin < d.tokens.size() ? d.tokens[r.begin].line : 0) +
            ": " + why + " [" + d.text(r).substr(0,160) + "]");
    }
    size_t endStatement(size_t p, size_t end) const {
        if (d.is(p,"{")) return std::min(end,d.close(p)+1);
        if (d.is(p,"if")) {
            const size_t cond = p+1;
            if (!d.is(cond,"(")) return end;
            size_t next = endStatement(d.close(cond)+1,end);
            if (d.is(next,"else")) next = endStatement(next+1,end);
            return next;
        }
        for (;p < end; ++p) {
            if (d.is(p,";")) return p+1;
            if (d.is(p,"(") || d.is(p,"[") || d.is(p,"{")) p = d.close(p);
        }
        return end;
    }
    void run(Range range, bool stageScope = false, int depth = 0) {
        if (depth > 48) { issue(range,"Anidamiento Haxe excesivo"); return; }
        for (size_t p = range.begin; p < range.end && !halted;) {
            if (d.is(p,";")) { ++p; continue; }
            if (d.is(p,"return") || d.is(p,"throw")) {
                if (stageScope) issue({p,endStatement(p,range.end)},"Salida de control: se detiene esta lectura");
                halted = true; return;
            }
            if (d.is(p,"#")) {
                const size_t start = p; int level = 0;
                do {
                    if (d.is(p,"#") && d.is(p+1,"if")) ++level;
                    if (d.is(p,"#") && d.is(p+1,"end")) { --level; if (!level) { p += 2; break; } }
                    ++p;
                } while (p < range.end);
                if (stageScope) issue({start,p},"Preprocesador pendiente; no se elige una build por suposicion");
                continue;
            }
            if (d.is(p,"{")) { const auto end = d.close(p); run({p+1,std::min(end,range.end)},stageScope,depth+1); p = end+1; continue; }
            if (d.is(p,"if") && d.is(p+1,"(")) {
                const size_t condEnd = d.close(p+1), body = condEnd+1;
                const size_t yesEnd = endStatement(body,range.end);
                const bool hasElse = d.is(yesEnd,"else");
                const size_t end = hasElse ? endStatement(yesEnd+1,range.end) : yesEnd;
                const auto v = evaluate(d,{p+2,condEnd},env);
                const auto condition = d.text({p+2,condEnd});
                const bool selecting = condition.find("curStage") != std::string::npos || condition.find("SONG.stage") != std::string::npos;
                if (v.is_boolean()) {
                    if (v.get<bool>()) { if (selecting) foundStage = true; run({body,yesEnd},stageScope || selecting,depth+1); }
                    else if (hasElse) run({yesEnd+1,end},stageScope || selecting,depth+1);
                } else if (stageScope || selecting) issue({p,std::min(end,range.end)},"Condicion dinamica: se omiten ambas ramas");
                p = end; continue;
            }
            if (d.is(p,"switch") && d.is(p+1,"(")) {
                const size_t condEnd = d.close(p+1), body = condEnd+1, end = d.close(body);
                if (!d.is(body,"{") || end >= range.end) { issue({p,range.end},"Switch mal formado"); return; }
                const auto selector = d.text({p+2,condEnd});
                const bool selecting = selector == "curStage" || selector == "SONG.stage" || selector == "PlayState.SONG.stage";
                const auto value = evaluate(d,{p+2,condEnd},env);
                Range selected, fallback; bool unknownLabel = false, hasSelected = false, hasFallback = false;
                for (size_t at = body+1; at < end;) {
                    if (!d.is(at,"case") && !d.is(at,"default")) { ++at; continue; }
                    const bool isDefault = d.is(at,"default");
                    size_t colon = at+1;
                    while (colon < end && !d.is(colon,":")) ++colon;
                    if (colon == end) break;
                    size_t next = colon+1;
                    while (next < end && !d.is(next,"case") && !d.is(next,"default")) {
                        if (d.is(next,"{") || d.is(next,"(") || d.is(next,"[")) next = d.close(next);
                        ++next;
                    }
                    if (isDefault) { fallback = {colon+1,next}; hasFallback = true; }
                    else if (!value.is_null()) for (const auto labels : d.split({at+1,colon}))
                        for (const auto label : d.split(labels,"|")) {
                            const auto test = evaluate(d,label,env);
                            if (test.is_null()) unknownLabel = true;
                            else if (test == value) { selected = {colon+1,next}; hasSelected = true; }
                        }
                    at = next;
                }
                if (unknownLabel) {
                    if (stageScope || selecting) issue({p,end+1},"Patrones de case no soportados: switch omitido");
                } else if (!value.is_null()) {
                    if (!hasSelected) selected = fallback;
                    if (hasSelected || hasFallback) { if (selecting) foundStage = true;
                        if (selector == "eventName") ++matchedEvents;
                        run(selected,stageScope || selecting,depth+1); }
                } else if (stageScope) issue({p,end+1},"Switch dinamico no recuperado");
                p = end+1; continue;
            }
            if (d.is(p,"for") || d.is(p,"while") || d.is(p,"try") || d.is(p,"do")) {
                size_t body = p+1; if (d.is(body,"(")) body = d.close(body)+1;
                const size_t end = endStatement(body,range.end);
                if (stageScope) issue({p,end},"Bucle o excepcion pendiente de porteo");
                p = end; continue;
            }
            size_t end = endStatement(p,range.end), last = end;
            if (last > p && d.is(last-1,";")) --last;
            if (last > p && statement) statement({p,last},stageScope,*this);
            p = std::max(p+1,end);
        }
    }
};

inline Env context(const std::string& stage, const std::string& song) {
    Env out = {{"curStage",stage},{"SONG.stage",stage},{"PlayState.SONG.stage",stage},
        {"SONG.song",song},{"PlayState.SONG.song",song},
        {"ClientPrefs.lowQuality",false},{"ClientPrefs.globalAntialiasing",true},
        {"ClientPrefs.data.lowQuality",false},{"ClientPrefs.data.antialiasing",true},
        {"FlxG.width",1280.0},{"FlxG.height",720.0}};
    if (song.empty()) { out.erase("SONG.song"); out.erase("PlayState.SONG.song"); }
    return out;
}
inline bool assignment(const Document& d, Range r, std::string& lhs, Range& rhs, std::string& op) {
    size_t p = r.begin;
    if (d.is(p,"var") || d.is(p,"final")) ++p;
    const size_t start = p;
    for (;p < r.end; ++p) {
        if (d.is(p,"(") || d.is(p,"[") || d.is(p,"{")) break;
        if (d.is(p,"=") || d.is(p,"+=") || d.is(p,"-=") || d.is(p,"*=")) {
            size_t nameEnd = start;
            while (nameEnd < p && !d.is(nameEnd,":")) ++nameEnd;
            lhs = d.text({start,nameEnd}); op = d.tokens[p].text; rhs = {p+1,r.end}; return true;
        }
    }
    return false;
}
inline bool call(const Document& d, Range r, std::string& name, std::vector<Range>& args) {
    for (size_t p = r.begin; p < r.end; ++p) if (d.is(p,"(")) {
        if (d.close(p) != r.end-1) return false;
        name = d.text({r.begin,p}); args = d.split({p+1,r.end-1}); return true;
    }
    return false;
}

inline void recoverStage(const Document& d, PsychStageImport& out, const std::string& song) {
    if (!d.error.empty()) { out.warnings.push_back(d.error); return; }
    if (out.spriteCount > 0) { out.warnings.push_back("Haxe: se conserva el decorado ya recuperado del JSON/Lua, sin duplicarlo."); return; }
    // El decorado no siempre esta en `create`. Un stage `.hx` de Psych lo monta
    // en `onCreatePost`: `assassin.hx` pone ahi 12 de sus 14 sprites y deja en
    // `onCreate` solo dos vacios. Leyendo una sola funcion el escenario salia
    // en cero y el import decia "no hay decorado recuperable", que era cierto
    // pero por el motivo equivocado. Es la misma regla que ya sigue el lector
    // de Lua (`LuaStatic`: onCreate y onCreatePost).
    std::vector<Range> bodies;
    for (const char* entry : {"create", "onCreate", "onCreatePost"}) {
        const Range found = d.function(entry);
        if (!found.empty()) bodies.push_back(found);
    }
    if (bodies.empty()) { out.warnings.push_back("Haxe: no se encontro create/onCreate/onCreatePost; requiere otro archivo fuente."); return; }
    bool hasStageSelector = false;
    for (const Range& one : bodies) {
        const std::string text = d.text(one);
        if (text.find("curStage") != std::string::npos ||
            text.find("SONG.stage") != std::string::npos) hasStageSelector = true;
    }
    Walker w{d}; w.env = context(out.stage.id,song);
    struct Sprite { StageObject object; bool valid = true; std::string parent; std::vector<std::string> children; bool group = false; };
    std::map<std::string,Sprite> sprites;
    std::vector<std::string> order;
    std::map<std::string,StageObject> markers;
    for (const auto& obj : out.stage.objects) {
        if (obj.kind == StageObject::Kind::Player) markers["boyfriendGroup"] = obj;
        if (obj.kind == StageObject::Kind::Opponent) markers["dadGroup"] = obj;
        if (obj.kind == StageObject::Kind::Girlfriend) markers["gfGroup"] = obj;
    }
    bool readingFactory = false;
    int factoriesRead = 0;
    const auto publishGeometry = [](Env& env, const std::string& id, const StageObject& o) {
        env[id+".x"] = o.position.x; env[id+".y"] = o.position.y;
        env[id+".scale.x"] = o.scale.x; env[id+".scale.y"] = o.scale.y;
    };
    w.statement = [&](Range r, bool scope, Walker& state) {
        std::string lhs, op, name; Range rhs; std::vector<Range> args;
        auto eval = [&](Range x) { return evaluate(d,x,state.env); };
        auto warn = [&](const std::string& s) { if (scope) state.issue(r,s); };
        if (assignment(d,r,lhs,rhs,op)) {
            Value value = eval(rhs);
            if (op == "=" && !value.is_null()) state.env[lhs] = value;
            else if (op == "=") state.env.erase(lhs);
            const size_t dot = lhs.find('.');
            // Bounded local sprite factories: literal arguments, one local
            // sprite, straight-line setters and return. Never execute Haxe or
            // accept callbacks, recursion, globals, filesystem or unknown calls.
            if (dot == std::string::npos && scope && !readingFactory && factoriesRead < 128 &&
                call(d,rhs,name,args) && !d.function(name).empty()) {
                const auto body = d.function(name);
                auto parts = d.split(body,";");
                if (!parts.empty() && d.is(parts.back().begin,"return")) {
                    const auto tail = parts.back();
                    const std::string local = d.text({tail.begin+1,tail.end});
                    bool safe = !local.empty() && local.find('.') == std::string::npos;
                    for (size_t i=0; safe && i+1<parts.size(); ++i) {
                        std::string target, action, method; Range valueRange; std::vector<Range> inputs;
                        if (assignment(d,parts[i],target,valueRange,action)) {
                            safe = action == "=" && (target == local || target.rfind(local+".",0) == 0);
                            if (target == local) safe &= i == 0 && d.is(parts[i].begin,"var") && d.is(valueRange.begin,"new");
                        } else safe = call(d,parts[i],method,inputs) && method.rfind(local+".",0) == 0;
                    }
                    Walker helper{d};
                    helper.env = state.env; helper.statement = state.statement;
                    bool parameters = false;
                    for (size_t p=0; safe && p+2<d.tokens.size(); ++p) {
                        if (!d.is(p,"function") || !d.is(p+1,name) || !d.is(p+2,"(")) continue;
                        const auto params = d.split({p+3,d.close(p+2)});
                        safe = params.size() == args.size(); parameters = true;
                        for (size_t i=0; safe && i<params.size(); ++i) {
                            const auto arg = eval(args[i]);
                            safe = !arg.is_null() && (arg.is_primitive() || arg.is_array());
                            if (safe) helper.env[d.tokens[params[i].begin].text] = arg;
                        }
                        break;
                    }
                    if (safe && parameters) {
                        ++factoriesRead;
                        auto previous = sprites;
                        sprites.erase(local);
                        readingFactory = true;
                        for (size_t i=0; i+1<parts.size(); ++i) helper.statement(parts[i],true,helper);
                        readingFactory = false;
                        const auto found = sprites.find(local);
                        const bool ok = found != sprites.end() && found->second.valid &&
                            !found->second.group && !found->second.object.spritePath.empty() && helper.issues.empty();
                        Sprite recovered;
                        if (ok) recovered = found->second;
                        sprites = std::move(previous);
                        if (ok) {
                            recovered.object.name = lhs;
                            publishGeometry(state.env,lhs,recovered.object);
                            sprites[lhs] = std::move(recovered);
                            return;
                        }
                    }
                }
                warn("Funcion auxiliar no recuperable con argumentos estaticos: " + name);
                return;
            }
            if (dot == std::string::npos && d.is(rhs.begin,"new") && scope) {
                size_t open = rhs.begin+1;
                while (open < rhs.end && !d.is(open,"(")) ++open;
                const auto type = d.text({rhs.begin+1,open});
                if (open == rhs.end) { warn("Constructor no reconocido"); return; }
                const size_t close = d.close(open);
                args = d.split({open+1,close});
                Sprite s; s.object.name = lhs;
                s.object.updateHitbox = true;
                if (type.rfind("FlxTypedGroup<",0) == 0 || type == "FlxSpriteGroup" || type == "FlxGroup") {
                    s.group = true; sprites[lhs] = s; return;
                }
                auto number = [&](size_t n, float fallback) { if (n >= args.size()) return fallback;
                    const auto v = eval(args[n]); if (v.is_number()) return v.get<float>(); s.valid = false; return fallback; };
                if (type == "BGSprite") {
                    const auto image = args.empty() ? Value() : eval(args[0]);
                    if (!image.is_string()) { warn("BGSprite con imagen dinamica"); return; }
                    s.object.spritePath = image.get<std::string>();
                    s.object.position = {number(1,0),number(2,0)};
                    // Forks also use (image,x,y,animations,loop). Detect the
                    // argument shape, never a song/stage name; an array is not
                    // an unresolved numeric scroll factor.
                    const bool compact = args.size() > 3 && eval(args[3]).is_array();
                    const size_t animationArg = compact ? 3 : 5;
                    s.object.scroll = compact ? Vec2{1,1} : Vec2{number(3,1),number(4,1)};
                    if (compact) warn("BGSprite: firma compacta imagen/x/y/animaciones/loop recuperada; comprobar constructor del fork");
                    if (args.size() > animationArg) {
                        const auto anims = eval(args[animationArg]);
                        if (anims.is_array()) for (const auto& prefix : anims) {
                            if (!prefix.is_string()) { s.valid = false; break; }
                            AnimationDef a; a.name = a.atlasPrefix = prefix.get<std::string>();
                            if (args.size() > animationArg+1) { auto loop = eval(args[animationArg+1]); if (!loop.is_boolean()) s.valid = false; else a.loop = loop.get<bool>(); }
                            s.object.anims.push_back(a);
                        } else if (d.text(args[animationArg]) != "null") s.valid = false;
                    }
                } else if (type == "FlxSprite" || type == "FunkinSprite") {
                    s.object.position = {number(0,0),number(1,0)};
                    if (close+1 < rhs.end && d.is(close+1,".")) {
                        std::string loader; std::vector<Range> loadArgs;
                        if (call(d,{close+2,rhs.end},loader,loadArgs) && loader == "loadGraphic" && !loadArgs.empty()) {
                            std::string paths; std::vector<Range> pathArgs;
                            if (call(d,loadArgs[0],paths,pathArgs) && paths == "Paths.image" && !pathArgs.empty()) {
                                const auto image = eval(pathArgs[0]); if (image.is_string()) s.object.spritePath = image.get<std::string>();
                            }
                        }
                    }
                } else { warn("Clase de objeto no soportada: " + type); return; }
                if (!s.valid) warn("Argumentos dinamicos; objeto omitido: " + lhs);
                if (s.valid) publishGeometry(state.env,lhs,s.object);
                sprites[lhs] = std::move(s); return;
            }
            if (dot != std::string::npos && sprites.count(lhs.substr(0,dot))) {
                auto& s = sprites.at(lhs.substr(0,dot)); auto& o = s.object;
                const std::string property = lhs.substr(dot+1);
                if (property == "frames") {
                    if (call(d,rhs,name,args) && name == "Paths.getSparrowAtlas" && !args.empty()) {
                        const auto image = eval(args[0]); if (image.is_string()) { o.spritePath = image.get<std::string>(); return; }
                    }
                }
                if (property == "cameras") {
                    if (d.text(rhs) != "[camGame]") { s.valid = false; warn("Camara auxiliar: objeto excluido del stage XML (requiere script)"); }
                    return;
                }
                if (value.is_boolean()) {
                    if (property == "visible") o.properties[property] = {PropertyValue::Type::Bool,value.get<bool>() ? "true" : "false"};
                    else if (property == "flipX") o.flipX = value.get<bool>();
                    else if (property == "flipY") o.unknownAttributes["flipY"] = value.get<bool>() ? "true" : "false";
                    else if (property == "antialiasing") o.antialiasing = value.get<bool>();
                    else if (property != "active") warn("Propiedad booleana pendiente");
                    return;
                }
                if (value.is_number()) {
                    float* target = property == "x" ? &o.position.x : property == "y" ? &o.position.y :
                        property == "alpha" ? &o.alpha : property == "angle" ? &o.angle : nullptr;
                    if (target) { const float n = value.get<float>();
                        if (op == "=") *target = n; else if (op == "+=") *target += n;
                        else if (op == "-=") *target -= n; else if (op == "*=") *target *= n;
                        state.env[lhs] = *target; return;
                    }
                }
                warn("Propiedad dinamica o no soportada"); return;
            }
            if (dot != std::string::npos && markers.count(lhs.substr(0,dot)) && lhs.substr(dot) == ".visible" && value == false) {
                if (lhs.substr(0,dot) == "gfGroup") out.hidesGirlfriend = true;
            } else if (scope && value.is_null()) warn("Asignacion no recuperada");
            else if (scope && dot == std::string::npos) warn("Estado del motor conservado solo como fuente");
            return;
        }
        if (!call(d,r,name,args)) { warn("Instruccion no reconocida"); return; }
        // `add` mete al final; `addBehindGF/BF/Dad` insertan ANTES del grupo del
        // personaje (`BaseStage.hx:94-96`), que es como Psych pone decorado
        // detras de alguien. Sin conocer esas tres, el sprite se creaba y NO se
        // anadia nunca: en `assassin.hx` son 10 de sus 14, y el escenario salia
        // vacio con un "no hay decorado recuperable" que era cierto por el
        // motivo equivocado.
        const char* behindMarker = nullptr;
        if (name == "addBehindGF")       behindMarker = "gfGroup";
        else if (name == "addBehindBF")  behindMarker = "boyfriendGroup";
        else if (name == "addBehindDad") behindMarker = "dadGroup";
        if ((name == "add" || behindMarker != nullptr) && args.size() == 1) {
            const auto child = d.text(args[0]);
            if (!sprites.count(child) && !markers.count(child)) {
                warn("Objeto anadido desde codigo externo: " + child);
                return;
            }
            if (std::find(order.begin(),order.end(),child) != order.end()) return;
            // Si la marca todavia no esta en la lista, anadir al final YA deja
            // el sprite detras: las marcas que nadie coloco se emiten al cerrar.
            const auto at = behindMarker != nullptr
                ? std::find(order.begin(),order.end(),behindMarker)
                : order.end();
            order.insert(at,child);
            return;
        }
        const size_t dot = name.find('.');
        if (dot == std::string::npos || !sprites.count(name.substr(0,dot))) { warn("Llamada pendiente de porteo: " + name); return; }
        const auto id = name.substr(0,dot), method = name.substr(dot+1);
        auto& s = sprites.at(id); auto& o = s.object;
        if (method == "loadGraphic" && args.size() == 1) {
            std::string loader; std::vector<Range> inputs;
            if (call(d,args[0],loader,inputs) && loader == "Paths.image" && !inputs.empty()) {
                const auto image = eval(inputs[0]);
                if (image.is_string()) { o.spritePath = image.get<std::string>(); return; }
            }
        }
        if (method == "add" && s.group && args.size() == 1) {
            const auto child = d.text(args[0]);
            if (sprites.count(child)) { s.children.push_back(child); sprites[child].parent = id; }
            else warn("Hijo de grupo no recuperado: " + child);
            return;
        }
        if (method == "updateHitbox") { o.updateHitbox = true; return; }
        if (method == "scale.set" || method == "scrollFactor.set") {
            if (args.empty()) { warn("Transformacion vacia"); return; }
            auto x = eval(args[0]), y = args.size() > 1 ? eval(args[1]) : x;
            if (x.is_number() && y.is_number()) {
                (method == "scale.set" ? o.scale : o.scroll) = {x.get<float>(),y.get<float>()}; return;
            }
        }
        if (method == "setGraphicSize" && args.size() == 1) {
            // Literal width multiplier; rounding to a source pixel is reported,
            // since source frame geometry is not available to this text reader.
            const auto expression = d.text(args[0]);
            const std::string prefix = "Std.int(" + id + ".width*";
            if (expression.rfind(prefix,0) == 0 && expression.back() == ')') {
                char* end = nullptr; const auto text = expression.substr(prefix.size(),expression.size()-prefix.size()-1);
                const double n = std::strtod(text.c_str(),&end);
                if (end != text.c_str() && *end == 0 && std::isfinite(n) && n > 0) {
                    o.scale = {static_cast<float>(n),static_cast<float>(n)};
                    warn("Escala proporcional recuperada; redondeo al pixel aproximado"); return;
                }
            }
        }
        if (method == "animation.addByPrefix" && args.size() >= 2) {
            auto anim = eval(args[0]), prefix = eval(args[1]);
            auto fps = args.size() > 2 ? eval(args[2]) : Value(24);
            auto loop = args.size() > 3 ? eval(args[3]) : Value(true);
            if (anim.is_string() && prefix.is_string() && fps.is_number() && loop.is_boolean()) {
                AnimationDef a; a.name = anim.get<std::string>(); a.atlasPrefix = prefix.get<std::string>();
                a.fps = std::clamp(fps.get<int>(),1,240); a.loop = loop.get<bool>();
                auto it = std::find_if(o.anims.begin(),o.anims.end(),[&](const auto& b) { return a.name == b.name; });
                if (it == o.anims.end()) o.anims.push_back(a); else *it = a;
                return;
            }
        }
        if (method == "animation.play" && !args.empty()) {
            const auto anim = eval(args[0]);
            if (anim.is_string()) {
                const auto it = std::find_if(o.anims.begin(),o.anims.end(),[&](const auto& a) { return a.name == anim.get<std::string>(); });
                if (it != o.anims.end()) { std::rotate(o.anims.begin(),it,it+1); return; }
            }
        }
        warn("Metodo/argumentos pendientes: " + method);
    };
    // El entorno se comparte entre las funciones a proposito: una variable
    // declarada en `onCreate` se usa en `onCreatePost`, como hace `lightning`.
    for (const Range& one : bodies) w.run(one,!hasStageSelector);
    if (hasStageSelector && !w.foundStage) {
        out.warnings.push_back("Haxe: este fuente no contiene una rama recuperable para el stage " + out.stage.id); return;
    }
    std::vector<StageObject> objects;
    std::set<std::string> emitted;
    std::function<void(const std::string&,bool,float,int)> emit = [&](const auto& id, bool visible, float alpha, int depth) {
        if (depth > 64 || !emitted.insert(id).second) return;
        if (markers.count(id)) { objects.push_back(markers.at(id)); return; }
        const auto it = sprites.find(id); if (it == sprites.end()) return;
        const auto& s = it->second;
        if (!s.valid) return;
        const auto prop = s.object.properties.find("visible");
        visible = visible && (prop == s.object.properties.end() || prop->second.raw != "false");
        alpha *= s.object.alpha;
        if (s.group) { for (const auto& child : s.children) emit(child,visible,alpha,depth+1); return; }
        if (s.object.spritePath.empty()) { w.issues.push_back("Objeto sin imagen estatica: " + id); return; }
        auto object = s.object; object.alpha = alpha;
        if (!object.anims.empty()) object.unknownAttributes["fmlInitialAnim"] = object.anims.front().name;
        if (!visible) object.properties["visible"] = {PropertyValue::Type::Bool,"false"};
        objects.push_back(std::move(object)); ++out.spriteCount; ++out.fromScript;
        if (std::find(out.images.begin(),out.images.end(),s.object.spritePath) == out.images.end()) out.images.push_back(s.object.spritePath);
    };
    for (const auto& id : order) emit(id,true,1,0);
    for (const auto& marker : markers) if (!emitted.count(marker.first)) objects.push_back(marker.second);
    if (out.hidesGirlfriend) for (auto& object : objects)
        if (object.kind == StageObject::Kind::Girlfriend) object.alpha = 0;
    if (out.spriteCount) { out.stage.objects = std::move(objects); out.hasObjects = true; out.objectCount = out.spriteCount; }
    out.warnings.push_back("Haxe: " + std::to_string(out.spriteCount) + " sprites recuperados de create. Vista estatica en calidad normal (1280x720); callbacks, mecanicas, shaders y cambios de acto NO equivalen a un port completo.");
    out.warnings.insert(out.warnings.end(),w.issues.begin(),w.issues.end());
}
} // namespace fml::psych::hx
