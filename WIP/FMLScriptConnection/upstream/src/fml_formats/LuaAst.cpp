#include "LuaAst.hpp"

#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <map>

namespace fml {
namespace lua {

const char* toString(NodeKind kind) {
    switch (kind) {
        case NodeKind::Nil:          return "nil";
        case NodeKind::True:         return "true";
        case NodeKind::False:        return "false";
        case NodeKind::Number:       return "number";
        case NodeKind::String:       return "string";
        case NodeKind::Vararg:       return "vararg";
        case NodeKind::Name:         return "name";
        case NodeKind::Index:        return "index";
        case NodeKind::Call:         return "call";
        case NodeKind::MethodCall:   return "method-call";
        case NodeKind::Function:     return "function";
        case NodeKind::Table:        return "table";
        case NodeKind::Field:        return "field";
        case NodeKind::Binary:       return "binary";
        case NodeKind::Unary:        return "unary";
        case NodeKind::Block:        return "block";
        case NodeKind::Local:        return "local";
        case NodeKind::Assign:       return "assign";
        case NodeKind::ExprStat:     return "expr-stat";
        case NodeKind::If:           return "if";
        case NodeKind::Clause:       return "clause";
        case NodeKind::While:        return "while";
        case NodeKind::Repeat:       return "repeat";
        case NodeKind::NumericFor:   return "numeric-for";
        case NodeKind::GenericFor:   return "generic-for";
        case NodeKind::Return:       return "return";
        case NodeKind::Break:        return "break";
        case NodeKind::Do:           return "do";
        case NodeKind::FunctionDecl: return "function-decl";
        case NodeKind::NameList:     return "name-list";
        case NodeKind::ExprList:     return "expr-list";
        case NodeKind::Bad:          return "bad";
    }
    return "?";
}

namespace {

// ---------------------------------------------------------------- tokens ---

enum class Tok { End, Name, Keyword, Number, String, Symbol };

struct Token {
    Tok         kind = Tok::End;
    std::string text;
    double      number = 0.0;
    int         line = 1;
    int         column = 1;
    bool        longForm = false;   // cadena [[ ]]: sin escapes
};

bool isNameStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}
bool isNamePart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

const char* const kKeywords[] = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
    "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
    "true", "until", "while",
};

bool isKeyword(const std::string& word) {
    for (const char* k : kKeywords) if (word == k) return true;
    return false;
}

// Los simbolos de varios caracteres van primero: si `..` se probara despues de
// `.`, `a..b` se leeria como dos indexaciones y el error saldria muy lejos de
// donde esta.
const char* const kSymbols[] = {
    "...", "..", "==", "~=", "<=", ">=",
    "+", "-", "*", "/", "%", "^", "#", "<", ">", "=",
    "(", ")", "{", "}", "[", "]", ";", ":", ",", ".",
};

class Lexer {
public:
    Lexer(const std::string& source, const std::string& path,
          PortDiagnostics& pending)
        : m_src(source), m_path(path), m_pending(pending) {}

    // Divide la fuente entera. Un token malo se informa y se salta: preferimos
    // seguir y dar varios avisos situados a parar en el primero.
    std::vector<Token> run() {
        std::vector<Token> out;
        for (;;) {
            skipTrivia();
            if (m_at >= m_src.size()) break;
            Token token = next();
            if (token.kind == Tok::End) break;
            out.push_back(std::move(token));
        }
        Token end;
        end.line = m_line;
        out.push_back(end);
        return out;
    }

    bool ok() const { return m_ok; }

private:
    void report(PortCause cause, const std::string& detail, int line) {
        PortDiagnostic d;
        d.cause = cause;
        d.resolution = PortResolution::EditByHand;
        d.path = m_path;
        d.line = line;
        d.detail = detail;
        m_pending.push_back(std::move(d));
        m_ok = false;
    }

    char peek(std::size_t ahead = 0) const {
        return m_at + ahead < m_src.size() ? m_src[m_at + ahead] : '\0';
    }
    void advance() {
        if (m_at < m_src.size()) {
            if (m_src[m_at] == '\n') { ++m_line; m_column = 1; }
            else ++m_column;
            ++m_at;
        }
    }

    // Devuelve el nivel de un abre-largo `[`, `[=[`, `[==[`… o -1.
    int longBracketLevel(std::size_t at) const {
        if (at >= m_src.size() || m_src[at] != '[') return -1;
        std::size_t i = at + 1;
        int level = 0;
        while (i < m_src.size() && m_src[i] == '=') { ++level; ++i; }
        return (i < m_src.size() && m_src[i] == '[') ? level : -1;
    }

    std::string readLongBody(int level, int startLine) {
        for (int i = 0; i < level + 2; ++i) advance();   // [ ={level} [
        if (peek() == '\n') advance();                   // Lua come el salto inicial
        std::string body;
        for (;;) {
            if (m_at >= m_src.size()) {
                report(PortCause::UnsupportedSyntax,
                       "bloque largo sin cerrar", startLine);
                return body;
            }
            if (peek() == ']') {
                std::size_t i = m_at + 1;
                int found = 0;
                while (i < m_src.size() && m_src[i] == '=') { ++found; ++i; }
                if (found == level && i < m_src.size() && m_src[i] == ']') {
                    for (int k = 0; k < level + 2; ++k) advance();
                    return body;
                }
            }
            body += peek();
            advance();
        }
    }

    void skipTrivia() {
        for (;;) {
            while (m_at < m_src.size() &&
                   std::isspace(static_cast<unsigned char>(peek())) != 0)
                advance();
            if (peek() == '-' && peek(1) == '-') {
                const int line = m_line;
                advance(); advance();
                const int level = longBracketLevel(m_at);
                if (level >= 0) { readLongBody(level, line); continue; }
                while (m_at < m_src.size() && peek() != '\n') advance();
                continue;
            }
            return;
        }
    }

    Token next() {
        Token token;
        token.line = m_line;
        token.column = m_column;
        const char c = peek();

        if (isNameStart(c)) {
            std::string word;
            while (m_at < m_src.size() && isNamePart(peek())) { word += peek(); advance(); }
            token.kind = isKeyword(word) ? Tok::Keyword : Tok::Name;
            token.text = std::move(word);
            return token;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))) != 0)) {
            std::string text;
            if (c == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                text += peek(); advance();
                text += peek(); advance();
                while (m_at < m_src.size() &&
                       std::isxdigit(static_cast<unsigned char>(peek())) != 0) {
                    text += peek(); advance();
                }
                token.number = static_cast<double>(std::strtoul(text.c_str() + 2, nullptr, 16));
            } else {
                bool exponent = false;
                while (m_at < m_src.size()) {
                    const char d = peek();
                    if (std::isdigit(static_cast<unsigned char>(d)) != 0 || d == '.') {
                        text += d; advance(); continue;
                    }
                    if ((d == 'e' || d == 'E') && !exponent) {
                        exponent = true;
                        text += d; advance();
                        if (peek() == '+' || peek() == '-') { text += peek(); advance(); }
                        continue;
                    }
                    break;
                }
                token.number = std::strtod(text.c_str(), nullptr);
            }
            token.kind = Tok::Number;
            token.text = std::move(text);
            return token;
        }

        if (c == '\'' || c == '"') {
            const char quote = c;
            const int line = m_line;
            advance();
            std::string value;
            for (;;) {
                if (m_at >= m_src.size() || peek() == '\n') {
                    report(PortCause::UnsupportedSyntax, "cadena sin cerrar", line);
                    break;
                }
                if (peek() == quote) { advance(); break; }
                if (peek() == '\\') {
                    advance();
                    const char e = peek();
                    switch (e) {
                        case 'n':  value += '\n'; advance(); break;
                        case 't':  value += '\t'; advance(); break;
                        case 'r':  value += '\r'; advance(); break;
                        case 'a':  value += '\a'; advance(); break;
                        case 'b':  value += '\b'; advance(); break;
                        case 'f':  value += '\f'; advance(); break;
                        case 'v':  value += '\v'; advance(); break;
                        case '\n': value += '\n'; advance(); break;
                        default:
                            if (std::isdigit(static_cast<unsigned char>(e)) != 0) {
                                int code = 0, digits = 0;
                                while (digits < 3 && m_at < m_src.size() &&
                                       std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                                    code = code * 10 + (peek() - '0');
                                    advance(); ++digits;
                                }
                                value += static_cast<char>(code & 0xFF);
                            } else {
                                value += e; advance();
                            }
                            break;
                    }
                    continue;
                }
                value += peek();
                advance();
            }
            token.kind = Tok::String;
            token.text = std::move(value);
            return token;
        }

        const int level = longBracketLevel(m_at);
        if (level >= 0) {
            token.kind = Tok::String;
            token.longForm = true;
            token.text = readLongBody(level, m_line);
            return token;
        }

        for (const char* symbol : kSymbols) {
            const std::size_t len = std::char_traits<char>::length(symbol);
            if (m_src.compare(m_at, len, symbol) == 0) {
                for (std::size_t i = 0; i < len; ++i) advance();
                token.kind = Tok::Symbol;
                token.text = symbol;
                return token;
            }
        }

        report(PortCause::UnsupportedSyntax,
               std::string("caracter inesperado '") + c + "'", m_line);
        advance();
        return next();
    }

    const std::string& m_src;
    const std::string& m_path;
    PortDiagnostics&   m_pending;
    std::size_t        m_at = 0;
    int                m_line = 1;
    int                m_column = 1;
    bool               m_ok = true;
};

// ---------------------------------------------------------------- parser ---

// Prioridades de Lua 5.1 (`lparser.c`). `..` y `^` asocian a la derecha, y por
// eso su prioridad derecha es menor que la izquierda.
struct BinaryPriority { int left, right; };

bool binaryPriority(const std::string& op, BinaryPriority* out) {
    static const std::map<std::string, BinaryPriority> table = {
        {"or", {1, 1}},   {"and", {2, 2}},
        {"<",  {3, 3}},   {">",   {3, 3}}, {"<=", {3, 3}},
        {">=", {3, 3}},   {"~=",  {3, 3}}, {"==", {3, 3}},
        {"..", {5, 4}},
        {"+",  {6, 6}},   {"-",   {6, 6}},
        {"*",  {7, 7}},   {"/",   {7, 7}}, {"%",  {7, 7}},
        {"^",  {10, 9}},
    };
    const auto found = table.find(op);
    if (found == table.end()) return false;
    *out = found->second;
    return true;
}

const int kUnaryPriority = 8;

class Parser {
public:
    Parser(std::vector<Token> tokens, const std::string& path,
           PortDiagnostics& pending)
        : m_tokens(std::move(tokens)), m_path(path), m_pending(pending) {}

    Node run() {
        Node block = parseBlock();
        if (!m_failed && current().kind != Tok::End)
            fail("sobra codigo despues del final del bloque");
        return block;
    }

    bool ok() const { return !m_failed; }

private:
    // -- utilidades ---------------------------------------------------------
    const Token& current() const { return m_tokens[m_at]; }
    const Token& ahead(std::size_t n) const {
        return m_tokens[m_at + n < m_tokens.size() ? m_at + n : m_tokens.size() - 1];
    }
    void step() { if (m_at + 1 < m_tokens.size()) ++m_at; }

    bool isSymbol(const char* text) const {
        return current().kind == Tok::Symbol && current().text == text;
    }
    bool isKeywordTok(const char* text) const {
        return current().kind == Tok::Keyword && current().text == text;
    }
    bool accept(const char* text) {
        if (isSymbol(text) || isKeywordTok(text)) { step(); return true; }
        return false;
    }
    bool expect(const char* text, const char* what) {
        if (accept(text)) return true;
        fail(std::string("falta '") + text + "' " + what);
        return false;
    }

    Node fail(const std::string& detail) {
        if (!m_failed) {
            PortDiagnostic d;
            d.cause = PortCause::UnsupportedSyntax;
            d.resolution = PortResolution::EditByHand;
            d.path = m_path;
            d.line = current().line;
            d.column = current().column;
            d.detail = detail;
            m_pending.push_back(std::move(d));
            m_failed = true;
        }
        Node bad;
        bad.kind = NodeKind::Bad;
        bad.line = current().line;
        return bad;
    }

    Node make(NodeKind kind) {
        Node n;
        n.kind = kind;
        n.line = current().line;
        return n;
    }

    // -- bloques y sentencias ----------------------------------------------
    bool blockEnds() const {
        if (current().kind == Tok::End) return true;
        if (current().kind != Tok::Keyword) return false;
        const std::string& t = current().text;
        return t == "end" || t == "else" || t == "elseif" || t == "until";
    }

    Node parseBlock() {
        Node block = make(NodeKind::Block);
        while (!m_failed && !blockEnds()) {
            if (accept(";")) continue;
            const bool last = isKeywordTok("return") || isKeywordTok("break");
            Node stat = parseStatement();
            if (m_failed) break;
            block.children.push_back(std::move(stat));
            // `return` y `break` cierran el bloque: lo que venga detras no es
            // alcanzable y en Lua es un error de sintaxis, no algo que ignorar.
            if (last) { accept(";"); break; }
        }
        return block;
    }

    Node parseStatement() {
        if (isKeywordTok("if")) return parseIf();
        if (isKeywordTok("while")) return parseWhile();
        if (isKeywordTok("do")) {
            Node node = make(NodeKind::Do);
            step();
            node.children.push_back(parseBlock());
            expect("end", "para cerrar el 'do'");
            return node;
        }
        if (isKeywordTok("for")) return parseFor();
        if (isKeywordTok("repeat")) return parseRepeat();
        if (isKeywordTok("function")) return parseFunctionDecl();
        if (isKeywordTok("local")) return parseLocal();
        if (isKeywordTok("return")) {
            Node node = make(NodeKind::Return);
            step();
            Node values = make(NodeKind::ExprList);
            if (!blockEnds() && !isSymbol(";")) values = parseExprList();
            node.children.push_back(std::move(values));
            return node;
        }
        if (isKeywordTok("break")) { Node node = make(NodeKind::Break); step(); return node; }
        return parseExprStatement();
    }

    Node parseIf() {
        Node node = make(NodeKind::If);
        step();                                  // if
        for (;;) {
            Node clause = make(NodeKind::Clause);
            clause.children.push_back(parseExpr(0));
            if (m_failed) return node;
            if (!expect("then", "despues de la condicion")) return node;
            clause.children.push_back(parseBlock());
            node.children.push_back(std::move(clause));
            if (isKeywordTok("elseif")) { step(); continue; }
            break;
        }
        if (isKeywordTok("else")) {
            step();
            Node clause = make(NodeKind::Clause);   // sin condicion = else
            clause.children.push_back(parseBlock());
            node.children.push_back(std::move(clause));
        }
        expect("end", "para cerrar el 'if'");
        return node;
    }

    Node parseWhile() {
        Node node = make(NodeKind::While);
        step();
        node.children.push_back(parseExpr(0));
        if (m_failed) return node;
        if (!expect("do", "despues de la condicion del 'while'")) return node;
        node.children.push_back(parseBlock());
        expect("end", "para cerrar el 'while'");
        return node;
    }

    Node parseRepeat() {
        Node node = make(NodeKind::Repeat);
        step();
        node.children.push_back(parseBlock());
        if (!expect("until", "para cerrar el 'repeat'")) return node;
        node.children.push_back(parseExpr(0));
        return node;
    }

    Node parseFor() {
        const int line = current().line;
        step();                                  // for
        if (current().kind != Tok::Name) return fail("falta el nombre del contador");

        // `for i = a, b [, c]` frente a `for k, v in ...`: decide el token que
        // sigue al primer nombre.
        if (ahead(1).kind == Tok::Symbol && ahead(1).text == "=") {
            Node node = make(NodeKind::NumericFor);
            node.line = line;
            Node name = make(NodeKind::Name);
            name.text = current().text;
            node.children.push_back(std::move(name));
            step(); step();                      // nombre, '='
            node.children.push_back(parseExpr(0));
            if (!expect(",", "entre el inicio y el limite del 'for'")) return node;
            node.children.push_back(parseExpr(0));
            if (accept(",")) {
                node.children.push_back(parseExpr(0));
            } else {
                // El paso SIEMPRE esta en el arbol: quien lo recorra no tiene
                // que saber que Lua lo omite, y no hay dos formas del mismo
                // bucle segun quien lo escribio.
                Node one = make(NodeKind::Number);
                one.number = 1.0;
                one.text = "1";
                node.children.push_back(std::move(one));
            }
            if (m_failed) return node;
            if (!expect("do", "antes del cuerpo del 'for'")) return node;
            node.children.push_back(parseBlock());
            expect("end", "para cerrar el 'for'");
            return node;
        }

        Node node = make(NodeKind::GenericFor);
        node.line = line;
        Node names = make(NodeKind::NameList);
        for (;;) {
            if (current().kind != Tok::Name) return fail("falta un nombre en el 'for'");
            Node name = make(NodeKind::Name);
            name.text = current().text;
            names.children.push_back(std::move(name));
            step();
            if (!accept(",")) break;
        }
        node.children.push_back(std::move(names));
        if (!expect("in", "en el 'for ... in'")) return node;
        node.children.push_back(parseExprList());
        if (m_failed) return node;
        if (!expect("do", "antes del cuerpo del 'for'")) return node;
        node.children.push_back(parseBlock());
        expect("end", "para cerrar el 'for'");
        return node;
    }

    Node parseFunctionDecl() {
        Node node = make(NodeKind::FunctionDecl);
        step();                                  // function
        if (current().kind != Tok::Name) return fail("falta el nombre de la funcion");
        std::string name = current().text;
        step();
        bool method = false;
        for (;;) {
            if (accept(".")) {
                if (current().kind != Tok::Name) return fail("falta el nombre tras '.'");
                name += "." + current().text;
                step();
                continue;
            }
            if (isSymbol(":")) {
                step();
                if (current().kind != Tok::Name) return fail("falta el nombre tras ':'");
                name += ":" + current().text;
                step();
                method = true;
            }
            break;
        }
        node.text = std::move(name);
        node.flag = method;
        node.children.push_back(parseFunctionBody(method));
        return node;
    }

    Node parseFunctionBody(bool method) {
        Node fn = make(NodeKind::Function);
        if (!expect("(", "para abrir los parametros")) return fn;
        Node params = make(NodeKind::NameList);
        if (method) {
            // `function a:b()` recibe `self` sin escribirlo. Ponerlo en el
            // arbol evita que el consumidor tenga que recordarlo.
            Node self = make(NodeKind::Name);
            self.text = "self";
            params.children.push_back(std::move(self));
        }
        if (!isSymbol(")")) {
            for (;;) {
                if (isSymbol("...")) { fn.flag = true; step(); break; }
                if (current().kind != Tok::Name) { fail("parametro no valido"); break; }
                Node name = make(NodeKind::Name);
                name.text = current().text;
                params.children.push_back(std::move(name));
                step();
                if (!accept(",")) break;
            }
        }
        fn.children.push_back(std::move(params));
        if (m_failed) return fn;
        if (!expect(")", "para cerrar los parametros")) return fn;
        fn.children.push_back(parseBlock());
        expect("end", "para cerrar la funcion");
        return fn;
    }

    Node parseLocal() {
        step();                                  // local
        if (isKeywordTok("function")) {
            step();
            if (current().kind != Tok::Name) return fail("falta el nombre de la funcion local");
            Node node = make(NodeKind::FunctionDecl);
            node.text = current().text;
            step();
            node.children.push_back(parseFunctionBody(false));
            return node;
        }
        Node node = make(NodeKind::Local);
        Node names = make(NodeKind::NameList);
        for (;;) {
            if (current().kind != Tok::Name) return fail("falta el nombre de la local");
            Node name = make(NodeKind::Name);
            name.text = current().text;
            names.children.push_back(std::move(name));
            step();
            if (!accept(",")) break;
        }
        node.children.push_back(std::move(names));
        node.children.push_back(accept("=") ? parseExprList() : make(NodeKind::ExprList));
        return node;
    }

    // `a, b = 1, 2` y `f(x)` empiezan igual: hay que leer la primera expresion
    // sufijada y mirar que viene detras.
    Node parseExprStatement() {
        const int line = current().line;
        Node first = parseSuffixed();
        if (m_failed) return first;

        if (isSymbol("=") || isSymbol(",")) {
            Node node = make(NodeKind::Assign);
            node.line = line;
            Node targets = make(NodeKind::ExprList);
            targets.children.push_back(std::move(first));
            while (accept(",")) {
                targets.children.push_back(parseSuffixed());
                if (m_failed) return node;
            }
            node.children.push_back(std::move(targets));
            if (!expect("=", "en la asignacion")) return node;
            node.children.push_back(parseExprList());
            return node;
        }

        if (first.kind != NodeKind::Call && first.kind != NodeKind::MethodCall)
            return fail("una expresion suelta que no es una llamada");
        Node node = make(NodeKind::ExprStat);
        node.line = line;
        node.children.push_back(std::move(first));
        return node;
    }

    // -- expresiones --------------------------------------------------------
    Node parseExprList() {
        Node list = make(NodeKind::ExprList);
        for (;;) {
            list.children.push_back(parseExpr(0));
            if (m_failed || !accept(",")) break;
        }
        return list;
    }

    Node parseExpr(int limit) {
        Node left;
        if (isKeywordTok("not") || isSymbol("-") || isSymbol("#")) {
            Node unary = make(NodeKind::Unary);
            unary.text = current().text;
            step();
            unary.children.push_back(parseExpr(kUnaryPriority));
            left = std::move(unary);
        } else {
            left = parseSimple();
        }
        if (m_failed) return left;

        for (;;) {
            const bool operatorToken = current().kind == Tok::Symbol ||
                                       isKeywordTok("and") || isKeywordTok("or");
            if (!operatorToken) break;
            BinaryPriority priority{};
            if (!binaryPriority(current().text, &priority)) break;
            if (priority.left <= limit) break;
            Node binary = make(NodeKind::Binary);
            binary.text = current().text;
            step();
            Node right = parseExpr(priority.right);
            binary.children.push_back(std::move(left));
            binary.children.push_back(std::move(right));
            left = std::move(binary);
            if (m_failed) break;
        }
        return left;
    }

    Node parseSimple() {
        switch (current().kind) {
            case Tok::Number: {
                Node n = make(NodeKind::Number);
                n.number = current().number;
                n.text = current().text;
                step();
                return n;
            }
            case Tok::String: {
                Node n = make(NodeKind::String);
                n.text = current().text;
                n.flag = current().longForm;
                step();
                return n;
            }
            case Tok::Keyword: {
                const std::string& t = current().text;
                if (t == "nil")   { Node n = make(NodeKind::Nil);   step(); return n; }
                if (t == "true")  { Node n = make(NodeKind::True);  step(); return n; }
                if (t == "false") { Node n = make(NodeKind::False); step(); return n; }
                if (t == "function") { step(); return parseFunctionBody(false); }
                break;
            }
            case Tok::Symbol: {
                if (isSymbol("...")) { Node n = make(NodeKind::Vararg); step(); return n; }
                if (isSymbol("{")) return parseTable();
                break;
            }
            default: break;
        }
        return parseSuffixed();
    }

    Node parsePrimary() {
        if (current().kind == Tok::Name) {
            Node n = make(NodeKind::Name);
            n.text = current().text;
            step();
            return n;
        }
        if (accept("(")) {
            Node inner = parseExpr(0);
            expect(")", "para cerrar el parentesis");
            return inner;
        }
        return fail("se esperaba un nombre o '('");
    }

    Node parseSuffixed() {
        Node node = parsePrimary();
        while (!m_failed) {
            if (isSymbol(".")) {
                step();
                if (current().kind != Tok::Name) return fail("falta el nombre tras '.'");
                Node index = make(NodeKind::Index);
                Node key = make(NodeKind::String);
                key.text = current().text;
                step();
                index.children.push_back(std::move(node));
                index.children.push_back(std::move(key));
                node = std::move(index);
                continue;
            }
            if (isSymbol("[")) {
                step();
                Node index = make(NodeKind::Index);
                Node key = parseExpr(0);
                if (m_failed) return index;
                expect("]", "para cerrar el indice");
                index.children.push_back(std::move(node));
                index.children.push_back(std::move(key));
                node = std::move(index);
                continue;
            }
            if (isSymbol(":")) {
                step();
                if (current().kind != Tok::Name) return fail("falta el nombre del metodo");
                Node call = make(NodeKind::MethodCall);
                call.text = current().text;
                step();
                call.children.push_back(std::move(node));
                appendArguments(call);
                node = std::move(call);
                continue;
            }
            if (isSymbol("(") || isSymbol("{") || current().kind == Tok::String) {
                Node call = make(NodeKind::Call);
                call.children.push_back(std::move(node));
                appendArguments(call);
                node = std::move(call);
                continue;
            }
            break;
        }
        return node;
    }

    // `f(a, b)`, `f{...}` y `f"texto"` son la misma llamada con tres formas.
    void appendArguments(Node& call) {
        if (current().kind == Tok::String) {
            Node text = make(NodeKind::String);
            text.text = current().text;
            text.flag = current().longForm;
            step();
            call.children.push_back(std::move(text));
            return;
        }
        if (isSymbol("{")) { call.children.push_back(parseTable()); return; }
        if (!expect("(", "para abrir los argumentos")) return;
        if (accept(")")) return;
        for (;;) {
            call.children.push_back(parseExpr(0));
            if (m_failed || !accept(",")) break;
        }
        expect(")", "para cerrar los argumentos");
    }

    Node parseTable() {
        Node table = make(NodeKind::Table);
        if (!expect("{", "para abrir la tabla")) return table;
        while (!m_failed && !isSymbol("}")) {
            Node field = make(NodeKind::Field);
            if (isSymbol("[")) {
                step();
                field.children.push_back(parseExpr(0));
                if (m_failed) break;
                expect("]", "para cerrar la clave");
                expect("=", "tras la clave de la tabla");
                field.children.push_back(parseExpr(0));
            } else if (current().kind == Tok::Name && ahead(1).kind == Tok::Symbol &&
                       ahead(1).text == "=") {
                Node key = make(NodeKind::String);
                key.text = current().text;
                step(); step();
                field.children.push_back(std::move(key));
                field.children.push_back(parseExpr(0));
            } else {
                field.children.push_back(parseExpr(0));   // elemento de array
            }
            table.children.push_back(std::move(field));
            if (!accept(",") && !accept(";")) break;
        }
        expect("}", "para cerrar la tabla");
        return table;
    }

    std::vector<Token> m_tokens;
    const std::string& m_path;
    PortDiagnostics&   m_pending;
    std::size_t        m_at = 0;
    bool               m_failed = false;
};

}  // namespace

// ------------------------------------------------------------------ api ---

Result<Chunk> parseLua(const std::string& source, const std::string& path) {
    if (source.empty())
        return Result<Chunk>::fail("el script esta vacio");

    Chunk chunk;
    Lexer lexer(source, path, chunk.pending);
    std::vector<Token> tokens = lexer.run();
    Parser parser(std::move(tokens), path, chunk.pending);
    chunk.root = parser.run();
    chunk.complete = lexer.ok() && parser.ok();
    return Result<Chunk>::ok(std::move(chunk));
}

std::string luaNumberToString(double value) {
    // Lua imprime los enteros sin parte decimal, y de ahi salen los nombres de
    // los tags: `'bar'..i` tiene que dar `bar1`, no `bar1.0`.
    if (std::isfinite(value) && value == std::floor(value) &&
        std::fabs(value) < 1e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%lld",
                      static_cast<long long>(value));
        return buffer;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.14g", value);
    return buffer;
}

std::string calleeName(const Node& call) {
    const Node* target = call.child(0);
    if (target == nullptr) return std::string();
    if (call.kind == NodeKind::MethodCall) {
        const std::string base = target->kind == NodeKind::Name ? target->text : std::string();
        return base.empty() ? std::string() : base + ":" + call.text;
    }
    // `a.b.c(...)` viaja como Index(Index(a,"b"),"c"): se deshace hacia atras.
    std::string name;
    const Node* at = target;
    while (at != nullptr) {
        if (at->kind == NodeKind::Name) {
            name = name.empty() ? at->text : at->text + "." + name;
            return name;
        }
        if (at->kind != NodeKind::Index) return std::string();
        const Node* key = at->child(1);
        if (key == nullptr || key->kind != NodeKind::String) return std::string();
        name = name.empty() ? key->text : key->text + "." + name;
        at = at->child(0);
    }
    return std::string();
}

void walk(const Node& node, const std::function<bool(const Node&)>& visit) {
    if (!visit(node)) return;
    for (const Node& child : node.children) walk(child, visit);
}

}  // namespace lua
}  // namespace fml
