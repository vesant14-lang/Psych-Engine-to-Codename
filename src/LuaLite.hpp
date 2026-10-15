#pragma once
// A small Lua 5.1 interpreter for reading Psych scripts, not for running a
// game: enough of the language (locals, closures, tables, if/for/while,
// strings, math) to replay a script's callbacks over a song and record what
// it does. Every run has a budget of steps, so a script cannot hang or grow
// without bound; unknown globals called as functions do nothing.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace pi::lua {

struct Table; struct Closure; struct Value;
using Native = std::function<std::vector<Value>(std::vector<Value>&)>;

struct Value {
    enum Kind { Nil, Bool, Num, Str, Tab, Fun, Nat } kind = Nil;
    bool b = false; double n = 0; std::string s;
    std::shared_ptr<Table> t; std::shared_ptr<Closure> f; std::shared_ptr<Native> native;
    static Value boolean(bool v) { Value x; x.kind = Bool; x.b = v; return x; }
    static Value number(double v) { Value x; x.kind = Num; x.n = v; return x; }
    static Value string(std::string v) { Value x; x.kind = Str; x.s = std::move(v); return x; }
    bool truthy() const { return !(kind == Nil || (kind == Bool && !b)); }
    std::string key() const {   // table key
        if (kind == Num) { char buf[40]; std::snprintf(buf, sizeof buf, "#%.17g", n); return buf; }
        if (kind == Str) return "$" + s;
        if (kind == Bool) return b ? "T" : "F";
        return "@" + std::to_string(reinterpret_cast<std::uintptr_t>(t ? (void*)t.get() : f ? (void*)f.get() : (void*)native.get()));
    }
};
// Names in `fields` ("$name"), every other key in `keys` with the key
// itself kept beside the value. `border` is the array length: n with t[n]
// set and t[n + 1] nil, kept up to date so `#t` does not count.
struct Table { std::map<std::string, Value> fields; std::map<std::string, std::pair<Value, Value>> keys; double border = 0; };

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
struct Budget : std::runtime_error { using std::runtime_error::runtime_error; };

// --- tokens ------------------------------------------------------------------
struct Token { enum Type { Name, Number, String, Symbol, End } type = End; std::string text; double number = 0; int line = 1; };

inline std::vector<Token> tokenize(const std::string& src) {
    std::vector<Token> out; size_t i = 0; int line = 1; const size_t n = src.size();
    auto longBracket = [&](size_t at, size_t& endOut) -> int {   // level of [==[ at `at`, -1 if none
        if (at >= n || src[at] != '[') return -1;
        size_t k = at + 1; int level = 0; while (k < n && src[k] == '=') { ++level; ++k; }
        if (k < n && src[k] == '[') { endOut = k + 1; return level; }
        return -1;
    };
    auto readLong = [&](size_t start, int level) -> std::string {
        std::string close = "]" + std::string(level, '=') + "]";
        size_t e = src.find(close, start); if (e == std::string::npos) e = n;
        std::string body = src.substr(start, e - start);
        for (char c : body) if (c == '\n') ++line;
        i = std::min(n, e + close.size());
        if (!body.empty() && body[0] == '\n') body.erase(0, 1);
        return body;
    };
    while (i < n) {
        char c = src[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        if (c == '-' && i + 1 < n && src[i + 1] == '-') {
            size_t after; int level = longBracket(i + 2, after);
            if (level >= 0) { readLong(after, level); continue; }
            while (i < n && src[i] != '\n') ++i; continue;
        }
        Token t; t.line = line;
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t s = i; while (i < n && (std::isalnum((unsigned char)src[i]) || src[i] == '_')) ++i;
            t.type = Token::Name; t.text = src.substr(s, i - s); out.push_back(t); continue;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)src[i + 1]))) {
            size_t s = i;
            if (c == '0' && i + 1 < n && (src[i + 1] == 'x' || src[i + 1] == 'X')) { i += 2; while (i < n && std::isxdigit((unsigned char)src[i])) ++i; t.number = (double)std::stoll(src.substr(s + 2, i - s - 2), nullptr, 16); }
            else {
                while (i < n && (std::isdigit((unsigned char)src[i]) || src[i] == '.')) ++i;
                if (i < n && (src[i] == 'e' || src[i] == 'E')) { ++i; if (i < n && (src[i] == '+' || src[i] == '-')) ++i; while (i < n && std::isdigit((unsigned char)src[i])) ++i; }
                try { t.number = std::stod(src.substr(s, i - s)); } catch (...) { t.number = 0; }
            }
            t.type = Token::Number; out.push_back(t); continue;
        }
        if (c == '"' || c == '\'') {
            char q = c; ++i; std::string s;
            while (i < n && src[i] != q) {
                if (src[i] == '\n') ++line;
                if (src[i] == '\\' && i + 1 < n) {
                    char e = src[++i];
                    switch (e) { case 'n': s += '\n'; break; case 't': s += '\t'; break; case 'r': s += '\r'; break; case '\n': s += '\n'; ++line; break;
                        default: if (std::isdigit((unsigned char)e)) { int v = 0, k = 0; while (k < 3 && i < n && std::isdigit((unsigned char)src[i])) { v = v * 10 + (src[i] - '0'); ++i; ++k; } --i; s += (char)v; } else s += e; }
                    ++i; continue;
                }
                s += src[i++];
            }
            ++i; t.type = Token::String; t.text = s; out.push_back(t); continue;
        }
        if (c == '[') { size_t after; int level = longBracket(i, after); if (level >= 0) { t.type = Token::String; t.text = readLong(after, level); out.push_back(t); continue; } }
        static const char* three[] = {"..."}; static const char* two[] = {"==", "~=", "<=", ">=", "..", "::", "//", "!="};
        bool matched = false;
        for (auto s : three) if (src.compare(i, 3, s) == 0) { t.type = Token::Symbol; t.text = s; i += 3; matched = true; break; }
        if (!matched) for (auto s : two) if (src.compare(i, 2, s) == 0) { t.type = Token::Symbol; t.text = s; i += 2; matched = true; break; }
        if (!matched) { t.type = Token::Symbol; t.text = std::string(1, c); ++i; }
        if (t.text == "!=") t.text = "~=";
        out.push_back(t);
    }
    Token end; end.line = line; out.push_back(end);
    return out;
}

// --- syntax tree -------------------------------------------------------------
struct Node;
using NodeP = std::shared_ptr<Node>;
struct Node {
    enum Type {
        // expressions
        ENil, ETrue, EFalse, ENum, EStr, EVararg, EFunc, ETable, EBin, EUn, EName, EIndex, ECall, EMethod, EParen,
        // statements
        SLocal, SAssign, SCall, SIf, SWhile, SRepeat, SNumFor, SGenFor, SFunc, SLocalFunc, SReturn, SBreak, SDo, SNone
    } type = SNone;
    std::string text, key; double number = 0; int line = 0;   // key: "$name", for globals
    std::vector<std::string> names;           // params, locals, for vars
    std::vector<NodeP> a, b;                 // children (exprs / targets / args / blocks)
    std::vector<std::pair<NodeP, NodeP>> fields;   // table: key (null = array) -> value
    NodeP x, y, z, w;                        // misc children
    std::vector<std::vector<NodeP>> blocks;  // if blocks
    bool vararg = false;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : t(std::move(tokens)) {}
    std::vector<NodeP> chunk() { auto b = block(); if (peek().type != Token::End) fail("se esperaba el final"); return b; }
private:
    std::vector<Token> t; size_t p = 0;
    const Token& peek(int k = 0) const { return t[std::min(p + k, t.size() - 1)]; }
    bool is(const char* s) const { return (peek().type == Token::Symbol || peek().type == Token::Name) && peek().text == s; }
    bool accept(const char* s) { if (is(s)) { ++p; return true; } return false; }
    [[noreturn]] void fail(const std::string& m) const { throw Error("línea " + std::to_string(peek().line) + ": " + m); }
    void expect(const char* s) { if (!accept(s)) fail(std::string("se esperaba '") + s + "'"); }
    std::string name() { if (peek().type != Token::Name) fail("se esperaba un nombre"); return t[p++].text; }
    static bool blockEnd(const Token& k) { return k.type == Token::End || (k.type == Token::Name && (k.text == "end" || k.text == "else" || k.text == "elseif" || k.text == "until")); }
    NodeP make(Node::Type type) { auto n = std::make_shared<Node>(); n->type = type; n->line = peek().line; return n; }

    std::vector<NodeP> block() {
        std::vector<NodeP> out;
        while (!blockEnd(peek())) {
            if (accept(";")) continue;
            if (is("return")) { ++p; auto r = make(Node::SReturn); if (!blockEnd(peek()) && !is(";")) r->a = exprList(); accept(";"); out.push_back(r); break; }
            out.push_back(statement());
        }
        return out;
    }
    NodeP statement() {
        if (accept("if")) {
            auto n = make(Node::SIf); n->a.push_back(expr()); expect("then"); n->blocks.push_back(block());
            while (accept("elseif")) { n->a.push_back(expr()); expect("then"); n->blocks.push_back(block()); }
            if (accept("else")) n->blocks.push_back(block());
            expect("end"); return n;
        }
        if (accept("while")) { auto n = make(Node::SWhile); n->x = expr(); expect("do"); n->blocks.push_back(block()); expect("end"); return n; }
        if (accept("do")) { auto n = make(Node::SDo); n->blocks.push_back(block()); expect("end"); return n; }
        if (accept("repeat")) { auto n = make(Node::SRepeat); n->blocks.push_back(block()); expect("until"); n->x = expr(); return n; }
        if (accept("for")) {
            std::string first = name();
            if (accept("=")) {
                auto n = make(Node::SNumFor); n->names = {first}; n->x = expr(); expect(","); n->y = expr();
                if (accept(",")) n->z = expr(); expect("do"); n->blocks.push_back(block()); expect("end"); return n;
            }
            auto n = make(Node::SGenFor); n->names = {first}; while (accept(",")) n->names.push_back(name());
            expect("in"); n->a = exprList(); expect("do"); n->blocks.push_back(block()); expect("end"); return n;
        }
        if (accept("function")) {
            auto n = make(Node::SFunc); NodeP target = make(Node::EName); target->text = name();
            while (is(".") || is(":")) {
                bool method = accept(":"); if (!method) expect(".");
                auto idx = make(Node::EIndex); idx->x = target; auto key = make(Node::EStr); key->text = name(); idx->y = key; target = idx;
                if (method) { n->text = "self"; break; }
            }
            n->x = target; n->y = funcBody(n->text == "self"); return n;
        }
        if (accept("local")) {
            if (accept("function")) { auto n = make(Node::SLocalFunc); n->names = {name()}; n->y = funcBody(false); return n; }
            auto n = make(Node::SLocal); n->names.push_back(name());
            while (accept("<")) { name(); expect(">"); }   // 5.4 attributes
            while (accept(",")) n->names.push_back(name());
            if (accept("=")) n->a = exprList();
            return n;
        }
        if (accept("break")) return make(Node::SBreak);
        if (accept("goto")) { name(); return make(Node::SNone); }
        if (accept("::")) { name(); expect("::"); return make(Node::SNone); }
        // assignment or call
        auto first = suffixed();
        if (is("=") || is(",")) {
            auto n = make(Node::SAssign); n->a.push_back(first);
            while (accept(",")) n->a.push_back(suffixed());
            expect("="); n->b = exprList(); return n;
        }
        if (first->type != Node::ECall && first->type != Node::EMethod) fail("sentencia no válida");
        auto n = make(Node::SCall); n->x = first; return n;
    }
    NodeP funcBody(bool method) {
        auto f = make(Node::EFunc); if (method) f->names.push_back("self");
        expect("(");
        if (!is(")")) do { if (accept("...")) { f->vararg = true; break; } f->names.push_back(name()); } while (accept(","));
        expect(")"); f->blocks.push_back(block()); expect("end"); return f;
    }
    std::vector<NodeP> exprList() { std::vector<NodeP> v{expr()}; while (accept(",")) v.push_back(expr()); return v; }
    NodeP primary() {
        if (peek().type == Token::Name && !isKeyword(peek().text)) { auto n = make(Node::EName); n->text = t[p++].text; n->key = "$" + n->text; return n; }
        if (accept("(")) { auto n = make(Node::EParen); n->x = expr(); expect(")"); return n; }
        fail("expresión no válida");
    }
    static bool isKeyword(const std::string& s) {
        static const char* k[] = {"and","break","do","else","elseif","end","false","for","function","goto","if","in","local","nil","not","or","repeat","return","then","true","until","while"};
        for (auto w : k) if (s == w) return true; return false;
    }
    NodeP suffixed() {
        auto e = primary();
        while (true) {
            if (accept(".")) { auto n = make(Node::EIndex); n->x = e; auto k = make(Node::EStr); k->text = name(); n->y = k; e = n; }
            else if (accept("[")) { auto n = make(Node::EIndex); n->x = e; n->y = expr(); expect("]"); e = n; }
            else if (accept(":")) { auto n = make(Node::EMethod); n->x = e; n->text = name(); n->a = args(); e = n; }
            else if (is("(") || is("{") || peek().type == Token::String) { auto n = make(Node::ECall); n->x = e; n->a = args(); e = n; }
            else return e;
        }
    }
    std::vector<NodeP> args() {
        if (peek().type == Token::String) { auto s = make(Node::EStr); s->text = t[p++].text; return {s}; }
        if (is("{")) return {table()};
        expect("("); std::vector<NodeP> v; if (!is(")")) v = exprList(); expect(")"); return v;
    }
    NodeP table() {
        auto n = make(Node::ETable); expect("{");
        while (!is("}")) {
            if (accept("[")) { auto k = expr(); expect("]"); expect("="); n->fields.push_back({k, expr()}); }
            else if (peek().type == Token::Name && peek(1).type == Token::Symbol && peek(1).text == "=") { auto k = make(Node::EStr); k->text = name(); expect("="); n->fields.push_back({k, expr()}); }
            else n->fields.push_back({nullptr, expr()});
            if (!accept(",") && !accept(";")) break;
        }
        expect("}"); return n;
    }
    NodeP simple() {
        const auto& k = peek();
        if (k.type == Token::Number) { auto n = make(Node::ENum); n->number = k.number; ++p; return n; }
        if (k.type == Token::String) { auto n = make(Node::EStr); n->text = k.text; ++p; return n; }
        if (accept("nil")) return make(Node::ENil);
        if (accept("true")) return make(Node::ETrue);
        if (accept("false")) return make(Node::EFalse);
        if (accept("...")) return make(Node::EVararg);
        if (accept("function")) return funcBody(false);
        if (is("{")) return table();
        return suffixed();
    }
    static int priority(const std::string& op, bool left) {
        if (op == "or") return 1; if (op == "and") return 2;
        if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "~=" || op == "==") return 3;
        if (op == "|") return 4; if (op == "~") return 5; if (op == "&") return 6; if (op == "<<" || op == ">>") return 7;
        if (op == "..") return left ? 9 : 8;   // right associative
        if (op == "+" || op == "-") return 10;
        if (op == "*" || op == "/" || op == "//" || op == "%") return 11;
        if (op == "^") return left ? 14 : 13;  // right associative
        return -1;
    }
    NodeP expr(int limit = 0) {
        NodeP left;
        if (is("not") || is("-") || is("#")) { auto n = make(Node::EUn); n->text = t[p++].text; n->x = expr(12); left = n; }
        else left = simple();
        while (true) {
            const auto& k = peek(); if (k.type != Token::Symbol && k.type != Token::Name) break;
            int pr = priority(k.text, true); if (pr < 0 || pr <= limit) break;
            auto op = t[p++].text; auto n = make(Node::EBin); n->text = op; n->x = left; n->y = expr(priority(op, false)); left = n;
        }
        return left;
    }
};

// --- interpreter -------------------------------------------------------------
struct Scope { std::map<std::string, Value> vars; std::shared_ptr<Scope> parent; };
using ScopeP = std::shared_ptr<Scope>;
struct Closure { NodeP fn; ScopeP scope; };

// Lua 5.1 patterns (lstrlib.c's matcher): string.find/match/gmatch/gsub.
// Psych scripts split event values with gmatch("([^,]+)"); without patterns
// Wii's Better Zoom read nil and zoomed the camera to 0. The recursion is
// bounded like everything else here.
struct LuaMatch {
    const char *srcInit = nullptr, *srcEnd = nullptr, *pEnd = nullptr;
    int level = 0, depth = 0;
    struct Capture { const char* init; std::ptrdiff_t len; } capture[32];
    static constexpr std::ptrdiff_t Unfinished = -1, Position = -2;

    static bool classMatch(int c, int cl) {
        bool r;
        switch (std::tolower(cl)) {
            case 'a': r = std::isalpha(c) != 0; break; case 'c': r = std::iscntrl(c) != 0; break; case 'd': r = std::isdigit(c) != 0; break;
            case 'l': r = std::islower(c) != 0; break; case 'p': r = std::ispunct(c) != 0; break; case 's': r = std::isspace(c) != 0; break;
            case 'u': r = std::isupper(c) != 0; break; case 'w': r = std::isalnum(c) != 0; break; case 'x': r = std::isxdigit(c) != 0; break;
            case 'z': r = c == 0; break; default: return cl == c;
        }
        return std::isupper(cl) ? !r : r;
    }
    const char* classEnd(const char* p) const {
        if (*p == '%') { if (p + 1 >= pEnd) throw Error("patrón mal formado (acaba en %)"); return p + 2; }
        if (*p == '[') {
            ++p; if (*p == '^') ++p;
            do { if (p >= pEnd) throw Error("patrón mal formado (falta ])"); if (*(p++) == '%' && p < pEnd) ++p; } while (*p != ']');
            return p + 1;
        }
        return p + 1;
    }
    static bool bracketMatch(int c, const char* p, const char* ec) {
        bool sig = true; if (*(p + 1) == '^') { sig = false; ++p; }
        while (++p < ec) {
            if (*p == '%') { ++p; if (classMatch(c, (unsigned char)*p)) return sig; }
            else if (*(p + 1) == '-' && p + 2 < ec) { p += 2; if ((unsigned char)*(p - 2) <= c && c <= (unsigned char)*p) return sig; }
            else if ((unsigned char)*p == c) return sig;
        }
        return !sig;
    }
    bool single(const char* s, const char* p, const char* ep) const {
        if (s >= srcEnd) return false;
        const int c = (unsigned char)*s;
        switch (*p) { case '.': return true; case '%': return classMatch(c, (unsigned char)*(p + 1)); case '[': return bracketMatch(c, p, ep - 1); default: return (unsigned char)*p == c; }
    }
    const char* balance(const char* s, const char* p) const {
        if (p + 1 >= pEnd) throw Error("patrón mal formado (%b)");
        if (s >= srcEnd || *s != *p) return nullptr;
        const char b = *p, e = *(p + 1); int open = 1;
        while (++s < srcEnd) { if (*s == e) { if (--open == 0) return s + 1; } else if (*s == b) ++open; }
        return nullptr;
    }
    int toClose() const { for (int l = level - 1; l >= 0; --l) if (capture[l].len == Unfinished) return l; throw Error("patrón inválido (paréntesis)"); }
    const char* match(const char* s, const char* p) {
        struct Depth { int& d; explicit Depth(int& d) : d(d) { ++d; } ~Depth() { --d; } } nested(depth);
        if (depth > 200) throw Error("patrón demasiado complejo");
        while (true) {
            if (p >= pEnd) return s;
            if (*p == '(') {
                const bool position = *(p + 1) == ')';
                if (level >= 32) throw Error("demasiadas capturas");
                capture[level].init = s; capture[level].len = position ? Position : Unfinished; ++level;
                const char* r = match(s, p + (position ? 2 : 1)); if (!r) --level; return r;
            }
            if (*p == ')') {
                const int l = toClose(); capture[l].len = s - capture[l].init;
                const char* r = match(s, p + 1); if (!r) capture[l].len = Unfinished; return r;
            }
            if (*p == '$' && p + 1 == pEnd) return s == srcEnd ? s : nullptr;
            if (*p == '%' && *(p + 1) == 'b') { s = balance(s, p + 2); if (!s) return nullptr; p += 4; continue; }
            if (*p == '%' && *(p + 1) == 'f') {
                p += 2; if (*p != '[') throw Error("falta [ tras %f");
                const char* ep = classEnd(p); const int prev = s == srcInit ? 0 : (unsigned char)*(s - 1), cur = s < srcEnd ? (unsigned char)*s : 0;
                if (!bracketMatch(prev, p, ep - 1) && bracketMatch(cur, p, ep - 1)) { p = ep; continue; }
                return nullptr;
            }
            if (*p == '%' && std::isdigit((unsigned char)*(p + 1))) {
                const int l = *(p + 1) - '1';
                if (l < 0 || l >= level || capture[l].len == Unfinished) throw Error("captura inválida");
                const std::size_t len = (std::size_t)capture[l].len;
                if ((std::size_t)(srcEnd - s) < len || std::memcmp(capture[l].init, s, len) != 0) return nullptr;
                s += len; p += 2; continue;
            }
            const char* ep = classEnd(p);
            const bool m = single(s, p, ep);
            const char op = ep < pEnd ? *ep : '\0';
            if (op == '?') { if (m) { const char* r = match(s + 1, ep + 1); if (r) return r; } p = ep + 1; continue; }
            if (op == '*' || op == '+') {
                if (op == '+' && !m) return nullptr;
                std::ptrdiff_t i = 0; while (single(s + i, p, ep)) ++i;
                for (; i >= (op == '+' ? 1 : 0); --i) { const char* r = match(s + i, ep + 1); if (r) return r; }
                return nullptr;
            }
            if (op == '-') {
                while (true) { const char* r = match(s, ep + 1); if (r) return r; if (single(s, p, ep)) ++s; else return nullptr; }
            }
            if (!m) return nullptr;
            ++s; p = ep;
        }
    }
};

class Interpreter {
public:
    std::shared_ptr<Table> globals = std::make_shared<Table>();
    long long budget = 0;                      // steps left in the current run
    int depth = 0, maxDepth = 120;             // nested Lua calls
    std::map<std::string, int> missingCalls;   // unknown functions called, by name

    Interpreter() { installLibrary(); }
    void setGlobal(const std::string& name, Value v) { globals->fields["$" + name] = std::move(v); }
    Value getGlobal(const std::string& name) const { auto f = globals->fields.find("$" + name); return f == globals->fields.end() ? Value{} : f->second; }
    void setNative(const std::string& name, Native fn) { Value v; v.kind = Value::Nat; v.native = std::make_shared<Native>(std::move(fn)); setGlobal(name, v); }
    void load(const std::string& source) {
        Parser parser(tokenize(source)); auto body = parser.chunk();
        chunks.push_back(body); auto scope = std::make_shared<Scope>(); execBlock(body, scope);
    }
    std::vector<Value> call(const Value& fn, std::vector<Value> args) {
        if (fn.kind == Value::Nat) return (*fn.native)(args);
        if (fn.kind != Value::Fun) return {};
        // Calls nest on the C++ stack: a script that recurses without end (a
        // table copy whose memo needs getfenv, Rev-Mixed's zCameraFix.lua)
        // stops here with an error instead of taking the program down.
        struct Depth { int& d; explicit Depth(int& d) : d(d) { ++d; } ~Depth() { --d; } } nested(depth);
        if (depth > maxDepth) throw Error("demasiadas llamadas anidadas (más de " + std::to_string(maxDepth) + ")");
        const auto& f = *fn.f->fn; auto scope = std::make_shared<Scope>(); scope->parent = fn.f->scope;
        for (size_t i = 0; i < f.names.size(); ++i) scope->vars[f.names[i]] = i < args.size() ? args[i] : Value{};
        if (f.vararg) { varargs.push_back(args.size() > f.names.size() ? std::vector<Value>(args.begin() + f.names.size(), args.end()) : std::vector<Value>{}); }
        Flow flow = execBlock(f.blocks[0], scope);
        if (f.vararg) varargs.pop_back();
        return flow.kind == Flow::Return ? flow.values : std::vector<Value>{};
    }
    bool callGlobal(const std::string& name, std::vector<Value> args) {
        auto fn = getGlobal(name); if (fn.kind != Value::Fun && fn.kind != Value::Nat) return false; call(fn, std::move(args)); return true;
    }
    static std::string toString(const Value& v) {
        switch (v.kind) {
            case Value::Nil: return "nil"; case Value::Bool: return v.b ? "true" : "false";
            case Value::Num: { if (std::floor(v.n) == v.n && std::fabs(v.n) < 1e15) { char b[32]; std::snprintf(b, sizeof b, "%.0f", v.n); return b; } char b[40]; std::snprintf(b, sizeof b, "%.14g", v.n); return b; }
            case Value::Str: return v.s; default: return "table";
        }
    }
    static double toNumber(const Value& v, bool* ok = nullptr) {
        if (ok) *ok = true;
        if (v.kind == Value::Num) return v.n;
        if (v.kind == Value::Str) { try { size_t used = 0; double d = std::stod(v.s, &used); if (used) return d; } catch (...) {} }
        if (ok) *ok = false; return 0;
    }
private:
    std::vector<std::vector<NodeP>> chunks;    // keep the trees alive for closures
    std::vector<std::vector<Value>> varargs;
    struct Flow { enum Kind { Normal, Break, Return } kind = Normal; std::vector<Value> values; };
    void tick() { if (--budget < 0) throw Budget("límite de pasos"); }

    Value* lookup(const ScopeP& scope, const std::string& n) {
        for (auto s = scope.get(); s; s = s->parent.get()) { auto f = s->vars.find(n); if (f != s->vars.end()) return &f->second; }
        return nullptr;
    }
    Value index(const Value& obj, const Value& key) {
        if (obj.kind != Value::Tab) return {};
        if (key.kind == Value::Str) { auto f = obj.t->fields.find("$" + key.s); return f == obj.t->fields.end() ? Value{} : f->second; }
        auto f = obj.t->keys.find(key.key()); return f == obj.t->keys.end() ? Value{} : f->second.second;
    }
    static bool has(const Table& t, double n) { return t.keys.count(Value::number(n).key()) != 0; }
    void setIndex(const Value& obj, const Value& key, Value v) {
        if (obj.kind != Value::Tab || key.kind == Value::Nil) return;
        auto& t = *obj.t;
        if (key.kind == Value::Str) { if (v.kind == Value::Nil) t.fields.erase("$" + key.s); else t.fields["$" + key.s] = std::move(v); return; }
        const auto k = key.key();
        if (v.kind == Value::Nil) {
            t.keys.erase(k);
            if (key.kind == Value::Num && key.n >= 1 && key.n <= t.border && std::floor(key.n) == key.n) t.border = key.n - 1;
            return;
        }
        t.keys[k] = {key, std::move(v)};
        if (key.kind == Value::Num && key.n == t.border + 1) { t.border = key.n; while (has(t, t.border + 1)) ++t.border; }
    }
    static double length(const Value& v) {
        if (v.kind == Value::Str) return (double)v.s.size();
        if (v.kind != Value::Tab) return 0;
        auto& t = *v.t; while (t.border > 0 && !has(t, t.border)) --t.border; while (has(t, t.border + 1)) ++t.border;
        return t.border;
    }
    // Lua's next(): the array part in order, then every other key, then names.
    std::vector<Value> nextOf(const Value& obj, const Value& key) {
        if (obj.kind != Value::Tab) return {Value{}};
        auto& t = *obj.t; const double n = length(obj);
        auto isArray = [&](const Value& k) { return k.kind == Value::Num && k.n >= 1 && k.n <= n && std::floor(k.n) == k.n; };
        auto fromKeys = [&](std::map<std::string, std::pair<Value, Value>>::iterator it) -> std::vector<Value> {
            for (; it != t.keys.end(); ++it) if (!isArray(it->second.first)) return {it->second.first, it->second.second};
            if (!t.fields.empty()) return {Value::string(t.fields.begin()->first.substr(1)), t.fields.begin()->second};
            return {Value{}};
        };
        if (key.kind == Value::Nil) { if (n >= 1) return {Value::number(1), index(obj, Value::number(1))}; return fromKeys(t.keys.begin()); }
        if (isArray(key)) { if (key.n < n) return {Value::number(key.n + 1), index(obj, Value::number(key.n + 1))}; return fromKeys(t.keys.begin()); }
        if (key.kind == Value::Str) { auto it = t.fields.upper_bound("$" + key.s); if (it == t.fields.end()) return {Value{}}; return {Value::string(it->first.substr(1)), it->second}; }
        return fromKeys(t.keys.upper_bound(key.key()));
    }
    std::vector<Value> evalMulti(const NodeP& e, const ScopeP& s) {
        if (e->type == Node::ECall || e->type == Node::EMethod) return evalCall(e, s);
        if (e->type == Node::EVararg) return varargs.empty() ? std::vector<Value>{} : varargs.back();
        return {eval(e, s)};
    }
    std::vector<Value> evalList(const std::vector<NodeP>& list, const ScopeP& s) {
        std::vector<Value> out;
        for (size_t i = 0; i < list.size(); ++i) {
            if (i + 1 == list.size()) { auto more = evalMulti(list[i], s); out.insert(out.end(), more.begin(), more.end()); }
            else out.push_back(eval(list[i], s));
        }
        return out;
    }
    std::vector<Value> evalCall(const NodeP& e, const ScopeP& s) {
        tick();
        if (e->type == Node::EMethod) {
            auto obj = eval(e->x, s); auto fn = index(obj, Value::string(e->text));
            auto args = evalList(e->a, s); args.insert(args.begin(), obj);
            if (fn.kind == Value::Fun || fn.kind == Value::Nat) return call(fn, args);
            if (obj.kind == Value::Str) {   // ("x"):upper() and friends
                auto lib = index(getGlobal("string"), Value::string(e->text)); if (lib.kind == Value::Nat) return call(lib, args);
            }
            ++missingCalls[":" + e->text]; return {};
        }
        auto fn = eval(e->x, s); auto args = evalList(e->a, s);
        if (fn.kind == Value::Fun || fn.kind == Value::Nat) return call(fn, args);
        // Psych API functions this reader does not model: nothing happens.
        std::string name = e->x->type == Node::EName ? e->x->text : e->x->type == Node::EIndex && e->x->y->type == Node::EStr ? e->x->y->text : "?";
        ++missingCalls[name]; return {};
    }
    Value arith(const std::string& op, const Value& l, const Value& r) {
        bool lo, ro; double a = toNumber(l, &lo), b = toNumber(r, &ro);
        if (!lo || !ro) return {};
        if (op == "+") return Value::number(a + b); if (op == "-") return Value::number(a - b);
        if (op == "*") return Value::number(a * b); if (op == "/") return Value::number(a / b);
        if (op == "//") return Value::number(std::floor(a / b));
        if (op == "%") return Value::number(b == 0 ? NAN : a - std::floor(a / b) * b);
        if (op == "^") return Value::number(std::pow(a, b));
        return {};
    }
    static bool equal(const Value& a, const Value& b) {
        if (a.kind != b.kind) return false;
        switch (a.kind) { case Value::Nil: return true; case Value::Bool: return a.b == b.b; case Value::Num: return a.n == b.n; case Value::Str: return a.s == b.s;
            case Value::Tab: return a.t == b.t; case Value::Fun: return a.f == b.f; default: return a.native == b.native; }
    }
    Value eval(const NodeP& e, const ScopeP& s) {
        tick();
        switch (e->type) {
            case Node::ENil: return {};
            case Node::ETrue: return Value::boolean(true);
            case Node::EFalse: return Value::boolean(false);
            case Node::ENum: return Value::number(e->number);
            case Node::EStr: return Value::string(e->text);
            case Node::EVararg: return varargs.empty() || varargs.back().empty() ? Value{} : varargs.back()[0];
            case Node::EParen: return eval(e->x, s);
            case Node::EFunc: { Value v; v.kind = Value::Fun; v.f = std::make_shared<Closure>(); v.f->fn = e; v.f->scope = s; return v; }
            case Node::EName: {
                if (auto* v = lookup(s, e->text)) return *v;
                auto f = globals->fields.find(e->key); return f == globals->fields.end() ? Value{} : f->second;
            }
            case Node::EIndex: {
                auto obj = eval(e->x, s); auto key = eval(e->y, s);
                if (e->x->type == Node::EName && e->x->text == "_G" && key.kind == Value::Str) return getGlobal(key.s);
                return index(obj, key);
            }
            case Node::ECall: case Node::EMethod: { auto r = evalCall(e, s); return r.empty() ? Value{} : r[0]; }
            case Node::ETable: {
                Value v; v.kind = Value::Tab; v.t = std::make_shared<Table>(); double next = 1;
                for (size_t i = 0; i < e->fields.size(); ++i) {
                    const auto& f = e->fields[i];
                    if (f.first) setIndex(v, eval(f.first, s), eval(f.second, s));
                    else if (i + 1 == e->fields.size()) { for (auto& x : evalMulti(f.second, s)) setIndex(v, Value::number(next++), x); }
                    else setIndex(v, Value::number(next++), eval(f.second, s));
                }
                return v;
            }
            case Node::EUn: {
                auto v = eval(e->x, s);
                if (e->text == "not") return Value::boolean(!v.truthy());
                if (e->text == "#") return Value::number(length(v));
                bool ok; double d = toNumber(v, &ok); return ok ? Value::number(-d) : Value{};
            }
            case Node::EBin: {
                const auto& op = e->text;
                if (op == "and") { auto l = eval(e->x, s); return l.truthy() ? eval(e->y, s) : l; }
                if (op == "or") { auto l = eval(e->x, s); return l.truthy() ? l : eval(e->y, s); }
                auto l = eval(e->x, s), r = eval(e->y, s);
                if (op == "..") return Value::string(toString(l) + toString(r));
                if (op == "==") return Value::boolean(equal(l, r));
                if (op == "~=") return Value::boolean(!equal(l, r));
                if (op == "<" || op == ">" || op == "<=" || op == ">=") {
                    if (l.kind == Value::Str && r.kind == Value::Str) { int c = l.s.compare(r.s); return Value::boolean(op == "<" ? c < 0 : op == ">" ? c > 0 : op == "<=" ? c <= 0 : c >= 0); }
                    bool lo, ro; double a = toNumber(l, &lo), b = toNumber(r, &ro); if (!lo || !ro) return Value::boolean(false);
                    return Value::boolean(op == "<" ? a < b : op == ">" ? a > b : op == "<=" ? a <= b : a >= b);
                }
                return arith(op, l, r);
            }
            default: return {};
        }
    }
    void assign(const NodeP& target, const ScopeP& s, Value v) {
        if (target->type == Node::EName) { if (auto* slot = lookup(s, target->text)) *slot = std::move(v); else setGlobal(target->text, std::move(v)); return; }
        if (target->type == Node::EIndex) {
            auto obj = eval(target->x, s); auto key = eval(target->y, s);
            if (target->x->type == Node::EName && target->x->text == "_G" && key.kind == Value::Str) { setGlobal(key.s, std::move(v)); return; }
            setIndex(obj, key, std::move(v));
        }
    }
    Flow execBlock(const std::vector<NodeP>& block, const ScopeP& parent) {
        // Only a block that declares locals needs a scope of its own.
        bool declares = false; for (const auto& st : block) if (st->type == Node::SLocal || st->type == Node::SLocalFunc) { declares = true; break; }
        ScopeP scope = parent; if (declares) { scope = std::make_shared<Scope>(); scope->parent = parent; }
        for (const auto& st : block) { Flow f = exec(st, scope); if (f.kind != Flow::Normal) return f; }
        return {};
    }
    Flow exec(const NodeP& st, const ScopeP& s) {
        tick();
        switch (st->type) {
            case Node::SLocal: { auto values = evalList(st->a, s); for (size_t i = 0; i < st->names.size(); ++i) s->vars[st->names[i]] = i < values.size() ? values[i] : Value{}; return {}; }
            case Node::SAssign: { auto values = evalList(st->b, s); for (size_t i = 0; i < st->a.size(); ++i) assign(st->a[i], s, i < values.size() ? values[i] : Value{}); return {}; }
            case Node::SCall: evalCall(st->x, s); return {};
            case Node::SIf: {
                for (size_t i = 0; i < st->a.size(); ++i) if (eval(st->a[i], s).truthy()) return execBlock(st->blocks[i], s);
                if (st->blocks.size() > st->a.size()) return execBlock(st->blocks.back(), s);
                return {};
            }
            case Node::SWhile: { while (eval(st->x, s).truthy()) { auto f = execBlock(st->blocks[0], s); if (f.kind == Flow::Break) break; if (f.kind == Flow::Return) return f; } return {}; }
            case Node::SRepeat: { while (true) { auto f = execBlock(st->blocks[0], s); if (f.kind == Flow::Break) break; if (f.kind == Flow::Return) return f; if (eval(st->x, s).truthy()) break; } return {}; }
            case Node::SDo: return execBlock(st->blocks[0], s);
            case Node::SNumFor: {
                double a = toNumber(eval(st->x, s)), b = toNumber(eval(st->y, s)), step = st->z ? toNumber(eval(st->z, s)) : 1;
                if (step == 0) return {};
                for (double i = a; step > 0 ? i <= b : i >= b; i += step) {
                    auto scope = std::make_shared<Scope>(); scope->parent = s; scope->vars[st->names[0]] = Value::number(i);
                    auto f = execBlock(st->blocks[0], scope); if (f.kind == Flow::Break) break; if (f.kind == Flow::Return) return f;
                }
                return {};
            }
            case Node::SGenFor: {
                auto init = evalList(st->a, s); Value fn = init.size() > 0 ? init[0] : Value{}, state = init.size() > 1 ? init[1] : Value{}, control = init.size() > 2 ? init[2] : Value{};
                while (true) {
                    auto r = call(fn, {state, control}); if (r.empty() || r[0].kind == Value::Nil) break; control = r[0];
                    auto scope = std::make_shared<Scope>(); scope->parent = s;
                    for (size_t i = 0; i < st->names.size(); ++i) scope->vars[st->names[i]] = i < r.size() ? r[i] : Value{};
                    auto f = execBlock(st->blocks[0], scope); if (f.kind == Flow::Break) break; if (f.kind == Flow::Return) return f;
                }
                return {};
            }
            case Node::SFunc: { Value v; v.kind = Value::Fun; v.f = std::make_shared<Closure>(); v.f->fn = st->y; v.f->scope = s; assign(st->x, s, v); return {}; }
            case Node::SLocalFunc: { s->vars[st->names[0]] = Value{}; Value v; v.kind = Value::Fun; v.f = std::make_shared<Closure>(); v.f->fn = st->y; v.f->scope = s; s->vars[st->names[0]] = v; return {}; }
            case Node::SReturn: { Flow f; f.kind = Flow::Return; f.values = evalList(st->a, s); return f; }
            case Node::SBreak: { Flow f; f.kind = Flow::Break; return f; }
            default: return {};
        }
    }
    void installLibrary() {
        auto num = [](std::vector<Value>& a, size_t i) { return i < a.size() ? toNumber(a[i]) : 0.0; };
        Value math; math.kind = Value::Tab; math.t = std::make_shared<Table>();
        auto fn = [&](const Value& table, const std::string& name, Native f) { Value v; v.kind = Value::Nat; v.native = std::make_shared<Native>(std::move(f)); table.t->fields["$" + name] = v; };
        fn(math, "sin", [num](auto& a) { return std::vector<Value>{Value::number(std::sin(num(a, 0)))}; });
        fn(math, "cos", [num](auto& a) { return std::vector<Value>{Value::number(std::cos(num(a, 0)))}; });
        fn(math, "tan", [num](auto& a) { return std::vector<Value>{Value::number(std::tan(num(a, 0)))}; });
        fn(math, "abs", [num](auto& a) { return std::vector<Value>{Value::number(std::fabs(num(a, 0)))}; });
        fn(math, "floor", [num](auto& a) { return std::vector<Value>{Value::number(std::floor(num(a, 0)))}; });
        fn(math, "ceil", [num](auto& a) { return std::vector<Value>{Value::number(std::ceil(num(a, 0)))}; });
        fn(math, "sqrt", [num](auto& a) { return std::vector<Value>{Value::number(std::sqrt(num(a, 0)))}; });
        fn(math, "rad", [num](auto& a) { return std::vector<Value>{Value::number(num(a, 0) * 3.14159265358979 / 180)}; });
        fn(math, "deg", [num](auto& a) { return std::vector<Value>{Value::number(num(a, 0) * 180 / 3.14159265358979)}; });
        fn(math, "min", [num](auto& a) { double m = num(a, 0); for (size_t i = 1; i < a.size(); ++i) m = std::min(m, num(a, i)); return std::vector<Value>{Value::number(m)}; });
        fn(math, "max", [num](auto& a) { double m = num(a, 0); for (size_t i = 1; i < a.size(); ++i) m = std::max(m, num(a, i)); return std::vector<Value>{Value::number(m)}; });
        fn(math, "fmod", [num](auto& a) { return std::vector<Value>{Value::number(std::fmod(num(a, 0), num(a, 1)))}; });
        // Deterministic: the same script reads the same way every time.
        fn(math, "random", [num](auto& a) { double lo = a.size() > 1 ? num(a, 0) : 1, hi = a.size() > 1 ? num(a, 1) : a.empty() ? 1 : num(a, 0);
            return std::vector<Value>{Value::number(a.empty() ? .5 : std::floor((lo + hi) / 2))}; });
        math.t->fields["$pi"] = Value::number(3.14159265358979); math.t->fields["$huge"] = Value::number(HUGE_VAL);
        setGlobal("math", math);
        Value str; str.kind = Value::Tab; str.t = std::make_shared<Table>();
        fn(str, "lower", [](auto& a) { std::string s = a.empty() ? "" : toString(a[0]); for (auto& c : s) c = (char)std::tolower((unsigned char)c); return std::vector<Value>{Value::string(s)}; });
        fn(str, "upper", [](auto& a) { std::string s = a.empty() ? "" : toString(a[0]); for (auto& c : s) c = (char)std::toupper((unsigned char)c); return std::vector<Value>{Value::string(s)}; });
        fn(str, "len", [](auto& a) { return std::vector<Value>{Value::number(a.empty() ? 0.0 : (double)toString(a[0]).size())}; });
        fn(str, "sub", [num](auto& a) { std::string s = a.empty() ? "" : toString(a[0]); long n = (long)s.size(); long i = a.size() > 1 ? (long)num(a, 1) : 1, j = a.size() > 2 ? (long)num(a, 2) : -1;
            if (i < 0) i = n + i + 1; if (j < 0) j = n + j + 1; i = std::max(1L, i); j = std::min(n, j); return std::vector<Value>{Value::string(i > j ? "" : s.substr(i - 1, j - i + 1))}; });
        // One search with Lua patterns: the match's start and end, and its
        // captures (the whole match when there are none).
        struct Found { bool ok = false; std::size_t start = 0, end = 0; std::vector<Value> captures; };
        auto search = [](const std::string& src, const std::string& pat, std::size_t from) {
            Found out; LuaMatch ms; ms.srcInit = src.data(); ms.srcEnd = src.data() + src.size();
            const bool anchor = !pat.empty() && pat[0] == '^';
            const char* p = pat.data() + (anchor ? 1 : 0); ms.pEnd = pat.data() + pat.size();
            for (const char* s = src.data() + std::min(from, src.size()); s <= ms.srcEnd; ++s) {
                ms.level = 0; ms.depth = 0;
                if (const char* e = ms.match(s, p)) {
                    out.ok = true; out.start = (std::size_t)(s - ms.srcInit); out.end = (std::size_t)(e - ms.srcInit);
                    for (int i = 0; i < ms.level; ++i) out.captures.push_back(ms.capture[i].len == LuaMatch::Position ? Value::number((double)(ms.capture[i].init - ms.srcInit) + 1)
                                                                                                                      : Value::string(std::string(ms.capture[i].init, (std::size_t)ms.capture[i].len)));
                    if (out.captures.empty()) out.captures.push_back(Value::string(src.substr(out.start, out.end - out.start)));
                    return out;
                }
                if (anchor) break;
            }
            return out;
        };
        auto start = [](const std::vector<Value>& a, std::size_t i, std::size_t size) {
            long init = i < a.size() && a[i].kind != Value::Nil ? (long)toNumber(a[i]) : 1;
            if (init < 0) init = (long)size + init + 1; return (std::size_t)std::max(1L, init) - 1;
        };
        fn(str, "find", [search, start](auto& a) {
            const std::string s = a.size() > 0 ? toString(a[0]) : "", pat = a.size() > 1 ? toString(a[1]) : "";
            const std::size_t from = start(a, 2, s.size());
            const bool plain = (a.size() > 3 && a[3].kind != Value::Nil && !(a[3].kind == Value::Bool && !a[3].b)) || pat.find_first_of("^$*+?.([%-") == std::string::npos;
            if (plain) { auto at = from > s.size() ? std::string::npos : s.find(pat, from);
                return at == std::string::npos ? std::vector<Value>{Value{}} : std::vector<Value>{Value::number((double)at + 1), Value::number((double)(at + pat.size()))}; }
            auto f = search(s, pat, from); if (!f.ok) return std::vector<Value>{Value{}};
            std::vector<Value> out{Value::number((double)f.start + 1), Value::number((double)f.end)};
            if (!(f.captures.size() == 1 && f.captures[0].kind == Value::Str && f.captures[0].s == s.substr(f.start, f.end - f.start))) out.insert(out.end(), f.captures.begin(), f.captures.end());
            return out; });
        fn(str, "match", [search, start](auto& a) {
            const std::string s = a.size() > 0 ? toString(a[0]) : "", pat = a.size() > 1 ? toString(a[1]) : "";
            auto f = search(s, pat, start(a, 2, s.size())); return f.ok ? f.captures : std::vector<Value>{Value{}}; });
        fn(str, "gmatch", [search](auto& a) {
            struct State { std::string s, pat; std::size_t pos = 0; bool done = false; };
            auto st = std::make_shared<State>(); st->s = a.size() > 0 ? toString(a[0]) : ""; st->pat = a.size() > 1 ? toString(a[1]) : "";
            if (!st->pat.empty() && st->pat[0] == '^') st->pat = "%^" + st->pat.substr(1);   // gmatch never anchors
            Value it; it.kind = Value::Nat; it.native = std::make_shared<Native>([st, search](std::vector<Value>&) {
                if (st->done || st->pos > st->s.size()) return std::vector<Value>{Value{}};
                auto f = search(st->s, st->pat, st->pos); if (!f.ok) { st->done = true; return std::vector<Value>{Value{}}; }
                st->pos = f.end == f.start ? f.end + 1 : f.end; return f.captures; });
            return std::vector<Value>{it}; });
        fn(str, "gsub", [this, search](auto& a) {
            const std::string s = a.size() > 0 ? toString(a[0]) : "", pat = a.size() > 1 ? toString(a[1]) : "";
            const Value repl = a.size() > 2 ? a[2] : Value{}; const long most = a.size() > 3 && a[3].kind != Value::Nil ? (long)toNumber(a[3]) : -1;
            std::string out; std::size_t pos = 0; long count = 0;
            while ((most < 0 || count < most) && pos <= s.size()) {
                auto f = search(s, pat, pos); if (!f.ok) break;
                out += s.substr(pos, f.start - pos); const std::string whole = s.substr(f.start, f.end - f.start);
                Value with;
                if (repl.kind == Value::Str || repl.kind == Value::Num) {
                    const std::string r = toString(repl); std::string done;
                    for (std::size_t i = 0; i < r.size(); ++i) {
                        if (r[i] != '%' || i + 1 >= r.size()) { done += r[i]; continue; }
                        const char c = r[++i];
                        if (c == '0') done += whole; else if (std::isdigit((unsigned char)c)) { std::size_t k = (std::size_t)(c - '1'); if (k < f.captures.size()) done += toString(f.captures[k]); } else done += c;
                    }
                    with = Value::string(done);
                } else if (repl.kind == Value::Tab) with = index(repl, f.captures.front());
                else if (repl.kind == Value::Fun || repl.kind == Value::Nat) { auto r = call(repl, f.captures); with = r.empty() ? Value{} : r[0]; }
                out += with.kind == Value::Nil || (with.kind == Value::Bool && !with.b) ? whole : toString(with);
                ++count;
                if (f.end == f.start) { if (f.start < s.size()) out += s[f.start]; pos = f.start + 1; } else pos = f.end;
            }
            if (pos < s.size()) out += s.substr(pos);
            return std::vector<Value>{Value::string(out), Value::number((double)count)}; });
        fn(str, "rep", [num](auto& a) { std::string s = a.empty() ? "" : toString(a[0]), sep = a.size() > 2 ? toString(a[2]) : "", out; long n = (long)num(a, 1);
            if (n > 0 && (s.size() + sep.size()) * (std::size_t)n > 1000000) throw Error("string.rep demasiado grande");
            for (long i = 0; i < n; ++i) { if (i) out += sep; out += s; } return std::vector<Value>{Value::string(out)}; });
        fn(str, "reverse", [](auto& a) { std::string s = a.empty() ? "" : toString(a[0]); std::reverse(s.begin(), s.end()); return std::vector<Value>{Value::string(s)}; });
        fn(str, "byte", [num](auto& a) { std::string s = a.empty() ? "" : toString(a[0]); long i = a.size() > 1 ? (long)num(a, 1) : 1, j = a.size() > 2 ? (long)num(a, 2) : i;
            if (i < 0) i = (long)s.size() + i + 1; if (j < 0) j = (long)s.size() + j + 1; std::vector<Value> out;
            for (long k = std::max(1L, i); k <= std::min((long)s.size(), j); ++k) out.push_back(Value::number((unsigned char)s[(std::size_t)k - 1])); return out; });
        fn(str, "char", [](auto& a) { std::string s; for (const auto& v : a) s += (char)(int)toNumber(v); return std::vector<Value>{Value::string(s)}; });
        fn(str, "format", [](auto& a) { std::string f = a.empty() ? "" : toString(a[0]), out; size_t arg = 1;
            for (size_t i = 0; i < f.size(); ++i) { if (f[i] != '%' || i + 1 >= f.size()) { out += f[i]; continue; } size_t j = i + 1; while (j < f.size() && !std::isalpha((unsigned char)f[j]) && f[j] != '%') ++j;
                if (j >= f.size()) break; char c = f[j]; std::string spec = f.substr(i, j - i + 1);
                if (c == '%') out += '%'; else if (arg < a.size()) { char buf[96]; if (c == 'd' || c == 'i') std::snprintf(buf, sizeof buf, (spec.substr(0, spec.size() - 1) + "lld").c_str(), (long long)toNumber(a[arg])); else if (c == 'f' || c == 'g' || c == 'e') std::snprintf(buf, sizeof buf, spec.c_str(), toNumber(a[arg])); else std::snprintf(buf, sizeof buf, "%s", toString(a[arg]).c_str()); out += buf; ++arg; }
                i = j; }
            return std::vector<Value>{Value::string(out)}; });
        setGlobal("string", str);
        // Psych's own string helpers (FunkinLua: stringSplit, stringTrim...).
        setNative("stringSplit", [this](auto& a) {
            const std::string s = a.size() > 0 ? toString(a[0]) : "", sep = a.size() > 1 ? toString(a[1]) : "";
            Value t; t.kind = Value::Tab; t.t = std::make_shared<Table>(); double n = 0; std::size_t at = 0;
            if (sep.empty()) { for (char c : s) setIndex(t, Value::number(++n), Value::string(std::string(1, c))); return std::vector<Value>{t}; }
            while (true) { auto next = s.find(sep, at); setIndex(t, Value::number(++n), Value::string(s.substr(at, next == std::string::npos ? std::string::npos : next - at))); if (next == std::string::npos) break; at = next + sep.size(); }
            return std::vector<Value>{t}; });
        setNative("stringTrim", [](auto& a) { std::string s = a.empty() ? "" : toString(a[0]); auto b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
            return std::vector<Value>{Value::string(b == std::string::npos ? "" : s.substr(b, e - b + 1))}; });
        setNative("stringStartsWith", [](auto& a) { std::string s = a.size() > 0 ? toString(a[0]) : "", p = a.size() > 1 ? toString(a[1]) : ""; return std::vector<Value>{Value::boolean(s.rfind(p, 0) == 0)}; });
        setNative("stringEndsWith", [](auto& a) { std::string s = a.size() > 0 ? toString(a[0]) : "", p = a.size() > 1 ? toString(a[1]) : "";
            return std::vector<Value>{Value::boolean(s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0)}; });
        Value tab; tab.kind = Value::Tab; tab.t = std::make_shared<Table>();
        fn(tab, "remove", [this](auto& a) { if (a.empty() || a[0].kind != Value::Tab) return std::vector<Value>{};
            double n = length(a[0]); if (n < 1) return std::vector<Value>{Value{}};
            double pos = a.size() > 1 ? toNumber(a[1]) : n; if (pos < 1 || pos > n) return std::vector<Value>{Value{}};
            auto removed = index(a[0], Value::number(pos));
            for (double k = pos; k < n; ++k) setIndex(a[0], Value::number(k), index(a[0], Value::number(k + 1)));
            setIndex(a[0], Value::number(n), Value{}); return std::vector<Value>{removed}; });
        // Not Lua, but common in Psych scripts (Roblox habit): the index of a value.
        fn(tab, "find", [this](auto& a) { if (a.size() < 2 || a[0].kind != Value::Tab) return std::vector<Value>{Value{}};
            double n = length(a[0]); for (double k = 1; k <= n; ++k) if (equal(index(a[0], Value::number(k)), a[1])) return std::vector<Value>{Value::number(k)};
            return std::vector<Value>{Value{}}; });
        fn(tab, "insert", [this](auto& a) { if (a.empty() || a[0].kind != Value::Tab) return std::vector<Value>{};
            double n = length(a[0]); if (a.size() == 2) setIndex(a[0], Value::number(n + 1), a[1]);
            else if (a.size() >= 3) { double pos = toNumber(a[1]); for (double k = n; k >= pos; --k) setIndex(a[0], Value::number(k + 1), index(a[0], Value::number(k))); setIndex(a[0], Value::number(pos), a[2]); }
            return std::vector<Value>{}; });
        setGlobal("table", tab);
        setNative("tostring", [](auto& a) { return std::vector<Value>{Value::string(a.empty() ? "nil" : toString(a[0]))}; });
        setNative("tonumber", [](auto& a) { bool ok; double d = a.empty() ? 0 : toNumber(a[0], &ok); return std::vector<Value>{a.empty() || !ok ? Value{} : Value::number(d)}; });
        setNative("type", [](auto& a) { static const char* n[] = {"nil", "boolean", "number", "string", "table", "function", "function"}; return std::vector<Value>{Value::string(a.empty() ? "nil" : n[a[0].kind])}; });
        setNative("print", [](auto&) { return std::vector<Value>{}; });
        setNative("debugPrint", [](auto&) { return std::vector<Value>{}; });
        setNative("ipairs", [this](auto& a) {
            Value it; it.kind = Value::Nat; it.native = std::make_shared<Native>([this](std::vector<Value>& x) {
                double i = (x.size() > 1 ? toNumber(x[1]) : 0) + 1; auto v = x.empty() ? Value{} : index(x[0], Value::number(i));
                return v.kind == Value::Nil ? std::vector<Value>{Value{}} : std::vector<Value>{Value::number(i), v}; });
            return std::vector<Value>{it, a.empty() ? Value{} : a[0], Value::number(0)}; });
        setNative("pairs", [this](auto& a) {
            // Snapshot of the keys: numeric ones first, in order, then names.
            auto keys = std::make_shared<std::vector<std::pair<Value, Value>>>();
            if (!a.empty() && a[0].kind == Value::Tab) {
                const double n = length(a[0]);
                for (double i = 1; i <= n; ++i) keys->push_back({Value::number(i), index(a[0], Value::number(i))});
                for (const auto& k : a[0].t->keys) { const auto& key = k.second.first; if (!(key.kind == Value::Num && key.n >= 1 && key.n <= n && std::floor(key.n) == key.n)) keys->push_back(k.second); }
                for (const auto& f : a[0].t->fields) keys->push_back({Value::string(f.first.substr(1)), f.second});
            }
            auto at = std::make_shared<size_t>(0);
            Value it; it.kind = Value::Nat; it.native = std::make_shared<Native>([keys, at](std::vector<Value>&) {
                if (*at >= keys->size()) return std::vector<Value>{Value{}}; auto kv = (*keys)[(*at)++]; return std::vector<Value>{kv.first, kv.second}; });
            return std::vector<Value>{it, Value{}, Value{}}; });
        setNative("next", [this](auto& a) { return nextOf(a.empty() ? Value{} : a[0], a.size() > 1 ? a[1] : Value{}); });
        setNative("pcall", [this](auto& a) {
            if (a.empty()) return std::vector<Value>{Value::boolean(false)};
            std::vector<Value> rest(a.begin() + 1, a.end());
            try { auto r = call(a[0], rest); r.insert(r.begin(), Value::boolean(true)); return r; }
            catch (const Budget&) { throw; } catch (const std::exception& e) { return std::vector<Value>{Value::boolean(false), Value::string(e.what())}; } });
        Value G; G.kind = Value::Tab; G.t = globals; setGlobal("_G", G);
    }
};
}
