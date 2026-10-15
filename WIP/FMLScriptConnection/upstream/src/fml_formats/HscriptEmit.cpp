#include "HscriptEmit.hpp"

#include "PsychApiCatalog.hpp"
#include "PsychEase.hpp"
#include "PsychHooks.hpp"

#include <cctype>
#include <cstdio>
#include <map>
#include <set>

namespace fml {
namespace hscript {

namespace {

using lua::Node;
using lua::NodeKind;

// A local alias/parameter shadows an API only inside its lexical scope.
// Retaining it across functions emitted later turns valid Psych calls into
// unknown bare HScript functions (e.g. a local getProperty in another helper).
struct LocalScope {
    std::set<std::string>& names;
    std::set<std::string> previous;
    explicit LocalScope(std::set<std::string>& value) : names(value), previous(value) {}
    ~LocalScope() { names = std::move(previous); }
};

bool isIdentifier(const std::string& text) {
    if (text.empty()) return false;
    if (std::isdigit(static_cast<unsigned char>(text[0]))) return false;
    for (const char c : text)
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') return false;
    static const std::set<std::string> reserved = {
        "var", "function", "if", "else", "while", "for", "do", "return",
        "break", "new", "true", "false", "null", "this", "switch", "case",
    };
    return reserved.count(text) == 0;
}

// Un identificador de Lua que en Haxe es palabra reservada (`class`, `cast`,
// `new`...) no puede escribirse tal cual: se le anade un guion bajo. Lua no
// tiene esas palabras reservadas, asi que si aparecen es como nombre.
std::string safeName(const std::string& name) {
    static const std::set<std::string> reserved = {
        "abstract", "break", "case", "cast", "catch", "class", "continue", "default", "do", "dynamic",
        "else", "enum", "extends", "extern", "false", "final", "for", "function", "if", "implements",
        "import", "in", "inline", "interface", "macro", "new", "null", "operator", "overload", "override",
        "package", "private", "public", "return", "static", "super", "switch", "this", "throw", "true",
        "try", "typedef", "untyped", "using", "var", "while",
    };
    return reserved.count(name) != 0 ? name + "_" : name;
}

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out + "\"";
}

// El nombre de ease que escribe el mod es una cadena, y Psych la resuelve en
// `LuaUtils.getTweenEaseByString` (`LuaUtils.hx:334-374`). La tabla vive en
// PsychEase.hpp, compartida con la capa de compatibilidad.
std::string easeMember(const std::string& raw) { return psychease::member(raw); }

// Los valores de retorno que Psych consulta en los hooks (`LuaUtils.hx`).
bool isFunctionStop(const std::string& name) {
    return name == "Function_Stop" || name == "Function_StopLua" ||
           name == "Function_StopHScript" || name == "Function_StopAll";
}
bool isFunctionContinue(const std::string& name) { return name == "Function_Continue"; }

// Si una expresion contiene alguna llamada: al nivel superior de un script de
// cancion la capa aun no existe, y una llamada ahi no puede ejecutarse.
bool containsCall(const Node& node) {
    if (node.kind == NodeKind::Call || node.kind == NodeKind::MethodCall) return true;
    if (node.kind == NodeKind::Function) return false;  // el cuerpo corre despues
    for (const Node& child : node.children)
        if (containsCall(child)) return true;
    return false;
}

bool referencesAny(const Node& node, const std::set<std::string>& names, bool includeClosures = false) {
    if (node.kind == NodeKind::Name && names.count(node.text) != 0) return true;
    if (node.kind == NodeKind::Function && !includeClosures) return false;
    for (const Node& child : node.children)
        if (referencesAny(child, names, includeClosures)) return true;
    return false;
}

class Emitter {
public:
    Emitter(const EmitOptions& options, EmitResult& out)
        : m_options(options), m_out(out) {}

    void run(const Node& block) {
        collectNames(block);
        // Las funciones del propio script se conocen ANTES de emitir nada: en
        // Lua es normal que onCreate llame a un helper definido mas abajo.
        for (const Node& statement : block.children)
            if (statement.kind == NodeKind::FunctionDecl && statement.text.find('.') == std::string::npos &&
                statement.text.find(':') == std::string::npos)
                m_localFunctions.insert(statement.text);
        if (!m_options.compat) { emitBlock(block, 0); return; }
        runCompat(block);
    }

private:
    // -- informe ------------------------------------------------------------
    void refuse(int line, const std::string& detail,
                PortResolution resolution = PortResolution::EditByHand) {
        PortDiagnostic d;
        d.cause = PortCause::UnsupportedSyntax;
        d.resolution = resolution;
        d.path = m_options.path;
        d.line = line;
        d.detail = detail;
        m_out.pending.push_back(std::move(d));
        ++m_out.refused;
    }

    void approximate(int line, const std::string& detail) {
        PortDiagnostic d;
        d.cause = PortCause::MissingCapability;
        d.resolution = PortResolution::Approximate;
        d.path = m_options.path;
        d.line = line;
        d.detail = detail;
        m_out.pending.push_back(std::move(d));
    }

    static std::string indent(int depth) { return std::string(depth * 4, ' '); }

    // `-1` no llega como numero sino como el unario `-` sobre el 1, que es lo
    // que es. Un paso negativo es la forma normal de un bucle descendente, asi
    // que hay que reconocerlo o `for i = 4, 1, -1` se rechaza por no ser
    // literal cuando lo es.
    static bool literalNumber(const lua::Node& node, double* out) {
        if (node.kind == NodeKind::Number) { *out = node.number; return true; }
        if (node.kind != NodeKind::Unary) return false;
        const lua::Node* operand = node.child(0);
        if (operand == nullptr || operand->kind != NodeKind::Number) return false;
        if (node.text == "-") { *out = -operand->number; return true; }
        return false;
    }

    
    void pending(int depth, int line, const std::string& what) {
        m_out.source += indent(depth) + "// PENDIENTE (linea " +
                        std::to_string(line) + "): " + what + "\n";
      
        const std::string original = sourceLine(line);
        if (!original.empty())
            m_out.source += indent(depth) + "//   original: " + original + "\n";
        // Y si el catalogo sabe que se usa en Codename, se dice aqui: es
        // exactamente el dato que uno iria a buscar.
        const psychapi::Entry* entry = psychapi::find(what);
        if (entry != nullptr && entry->codename != nullptr &&
            *entry->codename != '\0')
            m_out.source += indent(depth) + "//   en Codename: " +
                            entry->codename + "\n";
    }

    // La linea `line` del original, recortada. Se calcula sobre la marcha
    // porque los pendientes son pocos; indexar el archivo entero para esto
    // seria pagar por lo que casi nunca se usa.
    std::string sourceLine(int line) const {
        if (line <= 0 || m_options.source.empty()) return std::string();
        int current = 1;
        std::size_t at = 0;
        while (current < line && at < m_options.source.size()) {
            if (m_options.source[at] == '\n') ++current;
            ++at;
        }
        if (current != line || at >= m_options.source.size()) return std::string();
        const std::size_t end = m_options.source.find('\n', at);
        std::string text = m_options.source.substr(
            at, end == std::string::npos ? std::string::npos : end - at);
        // Sin la sangria del original y sin pasarse de largo.
        std::size_t from = 0;
        while (from < text.size() &&
               std::isspace(static_cast<unsigned char>(text[from])) != 0) ++from;
        text = text.substr(from);
        while (!text.empty() &&
               std::isspace(static_cast<unsigned char>(text.back())) != 0)
            text.pop_back();
        if (text.size() > 110) text = text.substr(0, 107) + "...";
        return text;
    }

    // -- expresiones --------------------------------------------------------
    // Devuelve false cuando no hay equivalente demostrable. El texto queda sin
    // tocar para que el llamante decida como marcarlo.
    bool expression(const Node& node, std::string& out, int depth) {
        if (depth > m_options.maxDepth) return false;
        switch (node.kind) {
            case NodeKind::Nil:    out = "null";  return true;
            case NodeKind::True:   out = "true";  return true;
            case NodeKind::False:  out = "false"; return true;
            case NodeKind::Number: out = lua::luaNumberToString(node.number); return true;
            case NodeKind::String: out = quote(node.text); return true;
            case NodeKind::Name:
                // `Function_Stop` fuera de un `return` es un valor de Psych
                // que Codename no conoce: se escribe como cadena para que
                // compare igual (`ret == Function_Stop` en el propio Lua).
                if (m_options.compat && (isFunctionStop(node.text) || isFunctionContinue(node.text))) {
                    out = quote(node.text);
                    return true;
                }
                out = safeName(node.text);
                return true;
            case NodeKind::Index:  return index(node, out, depth);
            case NodeKind::Binary: return binary(node, out, depth);
            case NodeKind::Unary:  return unary(node, out, depth);
            case NodeKind::Table:  return table(node, out, depth);
            case NodeKind::Call:
            case NodeKind::MethodCall: {
                std::string call;
                if (!callExpression(node, call, depth)) return false;
                out = call;
                return true;
            }
            case NodeKind::Function: {
                std::string body;
                if (!functionLiteral(node, body, depth)) return false;
                out = body;
                return true;
            }
            default: return false;
        }
    }

    bool index(const Node& node, std::string& out, int depth) {
        const Node* target = node.child(0);
        const Node* key = node.child(1);
        if (target == nullptr || key == nullptr) return false;
        std::string base;
        if (!expression(*target, base, depth + 1)) return false;

        // `a.b` viaja como a["b"]: si la clave es un identificador se escribe
        // con punto, que es lo que un humano espera leer.
        if (key->kind == NodeKind::String && isIdentifier(key->text)) {
            // `t.class` no compila en Haxe: va entre corchetes.
            out = safeName(key->text) == key->text ? base + "." + key->text : base + "[" + quote(key->text) + "]";
            return true;
        }
        std::string inner;
        if (!expression(*key, inner, depth + 1)) return false;
        if (key->kind == NodeKind::String) { out = base + "[" + inner + "]"; return true; }

        // Indice numerico. Solo se ajusta si SABEMOS que es una tabla de este
        // chunk: las de Lua empiezan en 1 y los Array de Haxe en 0. Sobre algo
        // que no declaramos no se puede suponer, porque los grupos de Psych ya
        // empiezan en 0 y restar uno desplazaria todo sin avisar.
        //
        // Con la capa se ajusta siempre: en el Lua de Psych un indice numerico
        // solo cae sobre tablas de Lua (las del propio script, las que
        // devuelven `stringSplit` o `getVar`...), nunca sobre un grupo del
        // motor, que se pide por API (`getPropertyFromGroup`).
        if (!m_options.compat &&
            (target->kind != NodeKind::Name || m_luaTables.count(target->text) == 0))
            return false;
        out = base + "[(" + inner + ") - 1]";
        return true;
    }

    bool binary(const Node& node, std::string& out, int depth) {
        const Node* left = node.child(0);
        const Node* right = node.child(1);
        if (left == nullptr || right == nullptr) return false;
        std::string a, b;
        if (!expression(*left, a, depth + 1) || !expression(*right, b, depth + 1))
            return false;
        const std::string& op = node.text;

       
        if (op == "..") {
            out = "(Std.string(" + a + ") + Std.string(" + b + "))";
            return true;
        }
        if (op == "~=") { out = "(" + a + " != " + b + ")"; return true; }
        if (op == "and" || op == "or") {
            // `and`/`or` devuelven un VALOR. Solo se emiten como operador
            // logico cuando los dos lados son claramente booleanos; si no,
            // habria que decidir el valor y eso no es traducir. Con la capa,
            // `psych.luaOr`/`psych.luaAnd` devuelven el valor como Lua
            // (evaluando los dos lados, que es la diferencia dicha).
            if (!booleanish(*left) || !booleanish(*right)) {
                if (!m_options.compat) return false;
                out = std::string(op == "and" ? "psych.luaAnd(" : "psych.luaOr(") + a + ", " + b + ")";
                return true;
            }
            out = "(" + a + (op == "and" ? " && " : " || ") + b + ")";
            return true;
        }
        if (op == "^") { out = "Math.pow(" + a + ", " + b + ")"; return true; }
        if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
            op == "==" || op == "<" || op == "<=" || op == ">" || op == ">=") {
            out = "(" + a + " " + op + " " + b + ")";
            return true;
        }
        return false;
    }

    // Si una expresion es booleana sin lugar a dudas. Se usa para no convertir
    // `a or 10` -que vale 10- en `a || 10`, que en Haxe no significa eso.
    static bool booleanish(const Node& node) {
        if (node.kind == NodeKind::True || node.kind == NodeKind::False) return true;
        if (node.kind == NodeKind::Unary) return node.text == "not";
        if (node.kind != NodeKind::Binary) return false;
        const std::string& op = node.text;
        return op == "==" || op == "~=" || op == "<" || op == "<=" ||
               op == ">" || op == ">=" || op == "and" || op == "or";
    }

    bool unary(const Node& node, std::string& out, int depth) {
        const Node* operand = node.child(0);
        if (operand == nullptr) return false;
        std::string inner;
        if (!expression(*operand, inner, depth + 1)) return false;
        if (node.text == "-")   { out = "(-" + inner + ")"; return true; }
        if (node.text == "not") {
            // `not x` en Lua es verdadero para nil y false, y solo para esos.
            // `!x` en Haxe sobre un objeto o un numero no es lo mismo.
            if (m_options.compat && !booleanish(*operand)) { out = "(!psych.truthy(" + inner + "))"; return true; }
            out = "(!" + inner + ")";
            return true;
        }
        if (node.text == "#") {
            // `#t` es la longitud, y solo se sabe que lo es sobre una tabla
            // nuestra o una cadena literal (con la capa, sobre cualquiera).
            if (operand->kind == NodeKind::String) { out = inner + ".length"; return true; }
            if (m_options.compat ||
                (operand->kind == NodeKind::Name && m_luaTables.count(operand->text) != 0)) {
                out = inner + ".length";
                return true;
            }
            return false;
        }
        return false;
    }

    bool table(const Node& node, std::string& out, int depth) {
        // Solo la parte de array: una tabla con claves es un objeto anonimo en
        // Haxe y su semantica de acceso no es la misma. Con la capa, una tabla
        // SOLO con claves identificador se emite como objeto anonimo
        // (`{x: 1, y: 2}`), que es lo que un `t.x` posterior espera; una
        // mezcla de claves y posiciones no tiene forma en Haxe y se rechaza.
        std::string items;
        bool keyed = false, positional = false;
        for (const Node& field : node.children) {
            if (field.kind != NodeKind::Field) return false;
            if (field.size() == 1) {
                positional = true;
                std::string value;
                if (!expression(field.children[0], value, depth + 1)) return false;
                if (!items.empty()) items += ", ";
                items += value;
                continue;
            }
            if (field.size() != 2 || !m_options.compat) return false;
            keyed = true;
            const Node& key = field.children[0];
            if (key.kind != NodeKind::String || !isIdentifier(key.text)) return false;
            std::string value;
            if (!expression(field.children[1], value, depth + 1)) return false;
            if (!items.empty()) items += ", ";
            items += key.text + ": " + value;
        }
        if (keyed && positional) return false;
        out = keyed ? "{" + items + "}" : "[" + items + "]";
        return true;
    }

    bool functionLiteral(const Node& node, std::string& out, int depth) {
        LocalScope scope(m_declared);
        const Node* params = node.child(0);
        const Node* body = node.child(1);
        if (params == nullptr || body == nullptr || node.flag) return false;  // sin varargs
        std::string list;
        for (std::size_t i = 0; i < params->size(); ++i) {
            if (i) list += ", ";
            list += "?" + safeName(params->children[i].text);
            m_declared.insert(params->children[i].text);
        }
        const std::string before = m_out.source;
        m_out.source.clear();
        emitFunctionBody(*body, depth + 1);
        const std::string inner = m_out.source;
        m_out.source = before;
        out = "function(" + list + ") {\n" + inner + indent(depth) + "}";
        return true;
    }

    std::string joined(const std::vector<std::string>& args) const {
        std::string list;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i) list += ", ";
            list += args[i];
        }
        return list;
    }

    // -- llamadas -----------------------------------------------------------
    // Emite la llamada si el catalogo dice que hay equivalente. `out` queda con
    // el HScript; false significa que hay que marcarla pendiente.
    bool callExpression(const Node& node, std::string& out, int depth) {
        const std::string name = lua::calleeName(node);
        if (name.empty()) return false;

        std::vector<std::string> args;
        for (std::size_t i = 1; i < node.size(); ++i) {
            std::string one;
            if (!expression(node.children[i], one, depth + 1)) return false;
            args.push_back(std::move(one));
        }

        if (m_options.compat) return compatCall(node, name, args, out);

        // Las que tienen forma propia, porque su equivalente no es una llamada.
        const Node* first = node.child(1);
        const bool literalFirst = first != nullptr && first->kind == NodeKind::String;
        if (name == "setProperty" && args.size() >= 2 && literalFirst) {
            std::string path;
            if (!propertyPath(first->text, path)) return false;
            out = path + " = " + args[1];
            return true;
        }
        if (name == "getProperty" && args.size() >= 1 && literalFirst) {
            std::string path;
            if (!propertyPath(first->text, path)) return false;
            out = path;
            return true;
        }
      
        if (name == "makeLuaText" && args.size() >= 5 && literalFirst) {
            if (!isIdentifier(first->text)) return false;
            const std::string tag = first->text;
            out = "{ " + tag + " = new FlxText(" + args[3] + ", " + args[4] +
                  ", " + args[2] + ", " + args[1] + ", 16); " +
                  tag + ".setFormat(Paths.font(\"vcr.ttf\"), 16, FlxColor.WHITE, "
                  "CENTER, FlxTextBorderStyle.OUTLINE, FlxColor.BLACK); " +
                  tag + ".cameras = [camHUD]; }";
            return true;
        }
        if (name == "addLuaText" && args.size() >= 1 && literalFirst) {
            if (!isIdentifier(first->text)) return false;
            out = "add(" + first->text + ")";
            return true;
        }
        if ((name == "setTextString" || name == "setTextSize") &&
            args.size() >= 2 && literalFirst) {
            if (!isIdentifier(first->text)) return false;
            out = first->text + (name == "setTextString" ? ".text = " : ".size = ") +
                  args[1];
            return true;
        }
        if (name == "setTextAlignment" && args.size() >= 2 && literalFirst) {
            const Node* second = node.child(2);
            if (!isIdentifier(first->text) || second == nullptr ||
                second->kind != NodeKind::String)
                return false;
            std::string align = second->text;
            for (char& c : align) c = static_cast<char>(std::toupper(
                static_cast<unsigned char>(c)));
            if (align != "LEFT" && align != "CENTER" && align != "RIGHT") return false;
            out = first->text + ".alignment = " + align;
            return true;
        }

        // ---- temporizadores ---------------------------------------------
        // `onTimerCompleted(tag, loops, loopsLeft)` en ESE orden
        // (`FunkinLua.hx:672`), y aparece 128 veces en el corpus.
        if (name == "runTimer" && args.size() >= 2 && literalFirst) {
            const std::string loops = args.size() >= 3 ? args[2] : std::string("1");
            // Igual que el tween: la difusion es lo que hace el motor.
            const std::string body =
                "{ PlayState.instance.scripts.call(\"onTimerCompleted\", [" +
                quote(first->text) + ", fmlTimer.loops, fmlTimer.loopsLeft]); }";
            const std::string timer = "new FlxTimer().start(" + args[1] +
                                      ", function(fmlTimer) " + body + ", " +
                                      loops + ")";
            // `runTimer` tambien cancela el que tuviera esa etiqueta
            // (`FunkinLua.hx:667`).
            const std::string tag = first->text;
            m_needsTimerRegistry = true;
            out = "{ if (fmlTimers.exists(" + quote(tag) + ")) fmlTimers.get(" +
                  quote(tag) + ").cancel(); fmlTimers.set(" + quote(tag) + ", " +
                  timer + "); }";
            return true;
        }

        // ---- llamada entre scripts -------------------------------------------
        // `callOnLuas(nombre, args)` difunde a TODOS los scripts, y Codename
        // tiene exactamente eso: `ScriptPack.call` recorre los scripts activos
        // y llama la funcion en cada uno. `PlayState.instance` es publico
        // (`PlayState.hx:48`), `scripts` tambien (`:98`) y `PlayState` esta
        // expuesto a todo script (`Script.hx:86`).
        //
        // Esto es lo que sostiene una secuencia entre archivos: Boxing Match WG
        // llama asi a `endFly` y `beginFinalPhase`, que estan definidas en su
        // escenario y no en el script de la cancion.
        if ((name == "callOnLuas" || name == "callOnHScript" ||
             name == "callOnScripts") && args.size() >= 1) {
            std::string params = "[]";
            if (args.size() >= 2) params = args[1];
            out = "PlayState.instance.scripts.call(" + args[0] + ", " + params + ")";
            return true;
        }

        // ---- la cola ---------------------------------------------------------
        if (name == "debugPrint" && !args.empty()) {
            out = "trace(" + args[0] + ")";
            return true;
        }
        if ((name == "getRandomInt" || name == "getRandomFloat") &&
            args.size() >= 2) {
            // El tercer argumento de Psych es una lista de exclusiones; sin
            // equivalente, asi que con ella no se emite.
            if (args.size() > 2) return false;
            out = std::string("FlxG.random.") +
                  (name == "getRandomInt" ? "int(" : "float(") + args[0] + ", " +
                  args[1] + ")";
            return true;
        }
        if (name == "setTextFont" && args.size() >= 2 && literalFirst) {
            if (!isIdentifier(first->text)) return false;
            out = first->text + ".font = Paths.font(" + args[1] + ")";
            return true;
        }
        if (name == "removeLuaSprite" && args.size() >= 1 && literalFirst) {
            if (!isIdentifier(first->text)) return false;
            // Psych DESTRUYE por omision (`FunkinLua.hx:1117`), no solo saca.
            const bool destroy = args.size() < 2 || args[1] != "false";
            out = "{ remove(" + first->text + ");" +
                  (destroy ? " " + first->text + ".destroy();" : "") + " }";
            return true;
        }
        if (name == "screenCenter" && args.size() >= 1 && literalFirst) {
            if (!isIdentifier(first->text)) return false;
            std::string axes = "XY";
            const Node* second = node.child(2);
            if (second != nullptr) {
                if (second->kind != NodeKind::String) return false;
                if (second->text == "x") axes = "X";
                else if (second->text == "y") axes = "Y";
                else if (second->text != "xy") return false;
            }
            out = first->text + ".screenCenter(FlxAxes." + axes + ")";
            return true;
        }
        if (name == "math.lerp" && args.size() >= 3) {
            out = "FlxMath.lerp(" + args[0] + ", " + args[1] + ", " + args[2] + ")";
            return true;
        }
        if (name == "table.insert" && args.size() == 2) {
            out = args[0] + ".push(" + args[1] + ")";
            return true;
        }

        // ---- cancelar por etiqueta -----------------------------------------
        // Ahora que hay registro, estas dos dejan de ser imposibles: sin el, no
        // habia a que tween ni a que temporizador referirse.
        if ((name == "cancelTween" || name == "cancelTimer") && args.size() >= 1) {
            const Node* tagNode = node.child(1);
            if (tagNode == nullptr || tagNode->kind != NodeKind::String) return false;
            const bool tween = name == "cancelTween";
            const std::string map = tween ? "fmlTweens" : "fmlTimers";
            if (tween) m_needsTweenRegistry = true; else m_needsTimerRegistry = true;
            // El motor hace cancel(), destroy() y quita la entrada
            // (`LuaUtils.hx:293-300`, `:311-318`).
            out = "{ if (" + map + ".exists(" + quote(tagNode->text) + ")) { " +
                  map + ".get(" + quote(tagNode->text) + ").cancel(); " +
                  map + ".remove(" + quote(tagNode->text) + "); } }";
            return true;
        }

       
        if (name == "triggerEvent" && args.size() >= 1 && literalFirst) {
            if (first->text != "Camera Follow Pos") return false;
            if (args.size() < 3) return false;
            out = "camFollow.setPosition(" + args[1] + ", " + args[2] + ")";
            m_forcedCameraEmitted = true;
            return true;
        }

        // ---- propiedades de clase ------------------------------------------
        // Solo las clases que existen en los dos motores con la misma forma.
        // `ClientPrefs` y `GameOverSubstate` NO estan aqui a proposito: ver el
        // catalogo.
        if ((name == "setPropertyFromClass" || name == "getPropertyFromClass") &&
            args.size() >= 2 && literalFirst) {
            const Node* fieldNode = node.child(2);
            if (fieldNode == nullptr || fieldNode->kind != NodeKind::String ||
                !isIdentifier(fieldNode->text))
                return false;
            std::string owner;
            if (first->text == "flixel.FlxG" || first->text == "FlxG") owner = "FlxG";
            else if (first->text == "Conductor") owner = "Conductor";
            else return false;
            const std::string path = owner + "." + fieldNode->text;
            if (name == "getPropertyFromClass") { out = path; return true; }
            if (args.size() < 3) return false;
            out = path + " = " + args[2];
            return true;
        }

        // ---- sonido -------------------------------------------------------
        // Las cuatro van juntas o no van: `stopSound`, `pauseSound` y
        // `resumeSound` NO reciben el sonido, reciben la etiqueta con la que
        // `playSound` lo guardo (`FunkinLua.hx:1305-1335`). Emitir el play sin
        // el registro daria un sonido que suena y que nada puede parar.
        if (name == "playSound" && args.size() >= 1) {
            const std::string volume = args.size() >= 2 ? args[1] : std::string("1");
            const Node* tagNode = node.child(3);
            if (tagNode == nullptr) {
                out = "FlxG.sound.play(Paths.sound(" + args[0] + "), " + volume + ")";
                return true;
            }
            if (tagNode->kind != NodeKind::String) return false;
            const std::string tag = soundTag(tagNode->text);
            m_needsSoundRegistry = true;
            // El motor para el anterior con esa misma etiqueta antes de sonar
            // el nuevo; si no, se solapan.
            out = "{ if (fmlSounds.exists(" + quote(tag) + ")) fmlSounds.get(" +
                  quote(tag) + ").stop(); fmlSounds.set(" + quote(tag) +
                  ", FlxG.sound.play(Paths.sound(" + args[0] + "), " + volume + ")); }";
            return true;
        }
        if ((name == "stopSound" || name == "pauseSound" || name == "resumeSound") &&
            args.size() >= 1) {
            const Node* tagNode = node.child(1);
            if (tagNode == nullptr || tagNode->kind != NodeKind::String) return false;
            const std::string tag = soundTag(tagNode->text);
            // Rareza del motor: las tres exigen `tag.length > 1`, asi que una
            // etiqueta de un solo caracter NO hace nada. Emitir algo que si
            // funcione seria cambiar el comportamiento.
            if (tag.size() <= 1) {
                out = "null /* el motor ignora una etiqueta de un caracter */";
                return true;
            }
            m_needsSoundRegistry = true;
            // Y otra: `resumeSound` llama a `play()`, no a `resume()`.
            const std::string action = name == "stopSound"    ? ".stop(); fmlSounds.remove(" + quote(tag) + ");"
                                     : name == "pauseSound"   ? ".pause();"
                                                              : ".play();";
            out = "{ if (fmlSounds.exists(" + quote(tag) + ")) fmlSounds.get(" +
                  quote(tag) + ")" + action + " }";
            return true;
        }

        // ---- color ------------------------------------------------------
        if (name == "getColorFromHex" && args.size() >= 1) {
            out = "FlxColor.fromString(" + args[0] + ")";
            return true;
        }
        if (name == "doTweenColor" && args.size() >= 4) {
            const Node* targetNode = node.child(2);
            if (targetNode == nullptr || targetNode->kind != NodeKind::String ||
                !isIdentifier(targetNode->text))
                return false;
            std::string ease = "linear";
            const Node* easeNode = node.child(5);
            if (easeNode != nullptr) {
                if (easeNode->kind != NodeKind::String) return false;
                ease = easeMember(easeNode->text);
            }
            // En `FlxTween.color` la duracion va SEGUNDA, no cuarta.
            out = "FlxTween.color(" + targetNode->text + ", " + args[3] + ", " +
                  targetNode->text + ".color, FlxColor.fromString(" + args[2] +
                  "), {ease: FlxEase." + ease + "})";
            return true;
        }

        // ---- personajes ---------------------------------------------------
        if (name == "characterPlayAnim" && args.size() >= 2 && literalFirst) {
            const std::string who = first->text == "bf" || first->text == "boyfriend"
                                        ? "boyfriend"
                                    : first->text == "gf" || first->text == "girlfriend"
                                        ? "girlfriend"
                                    : first->text == "dad" ? "dad"
                                                           : std::string();
            if (who.empty()) return false;
            const std::string force = args.size() >= 3 ? args[2] : std::string("false");
            out = who + ".playAnim(" + args[1] + ", " + force + ")";
            return true;
        }

        if (name == "math.clamp" && args.size() >= 3) {
            out = "FlxMath.bound(" + args[0] + ", " + args[1] + ", " + args[2] + ")";
            return true;
        }

        if (name.rfind("doTween", 0) == 0 && name != "doTweenColor") {
            std::string tween;
            if (tweenCall(node, name, args, tween, depth)) { out = tween; return true; }
            return false;
        }
        if (name == "getSongPosition" && args.empty()) {
            out = "Conductor.songPosition";
            return true;
        }
        if (name == "setObjectCamera" && args.size() >= 2 && literalFirst) {
            const Node* second = node.child(2);
            if (second == nullptr || second->kind != NodeKind::String) return false;
            const std::string camera = second->text == "hud" || second->text == "camHUD"
                                           ? "camHUD"
                                       : second->text == "other" || second->text == "camOther"
                                           ? "camOther"
                                           : "camGame";
            if (!isIdentifier(first->text)) return false;
            out = first->text + ".camera = " + camera;
            return true;
        }

        // Las funciones del propio script se llaman igual.
        if (m_localFunctions.count(name) != 0) {
            out = name + "(" + joined(args) + ")";
            return true;
        }

        return builtinCall(name, args, out);
    }

    // Los builtins de Lua que Haxe tiene con otro nombre. Comun a los dos modos.
    bool builtinCall(const std::string& name, const std::vector<std::string>& args,
                     std::string& out) {
        const psychapi::Entry* entry = psychapi::find(name);
        if (entry != nullptr && entry->support == psychapi::Support::NotEngine) {
            if (name == "tostring" && args.size() == 1) { out = "Std.string(" + args[0] + ")"; return true; }
            if (name == "tonumber" && args.size() == 1) {
                // `tonumber(nil)` es nil en Lua; `Std.parseFloat(null)` no.
                out = (m_options.compat ? "psych.toNumber(" : "Std.parseFloat(") + args[0] + ")";
                return true;
            }
            if (name == "type" && args.size() == 1 && m_options.compat) { out = "psych.luaType(" + args[0] + ")"; return true; }
            if (name.rfind("math.", 0) == 0) {
                out = "Math." + name.substr(5) + "(" + joined(args) + ")";
                return true;
            }
        }
        return false;
    }

    // -- el modo de capa -----------------------------------------------------
    // Toda llamada de la API sale como `psych.<nombre>(args)`; lo que la capa
    // no implementa, como `psych.pending("nombre", [args])`, que devuelve null
    // y traza una vez en el juego. Los builtins de Lua y las funciones del
    // propio script siguen igual.
    bool compatCall(const Node& node, const std::string& name,
                    const std::vector<std::string>& args, std::string& out) {
        // close() apaga el script que llama; en Codename cada script tiene
        // disableScript() en su propio scope (Script.hx:287).
        if (name == "close" && args.empty()) { out = "disableScript()"; return true; }
        // Una variable local que guarda una funcion (`local temp = function...`)
        // se llama tal cual.
        if (m_declared.count(name) != 0 && name.find('.') == std::string::npos) {
            out = safeName(name) + "(" + joined(args) + ")";
            return true;
        }
        if (m_localFunctions.count(name) != 0) {
            // Un hook llamado a mano (`onCreate()`) tiene ya su nombre de Codename.
            const psychhooks::Hook* hook = psychhooks::find(name);
            out = (hook != nullptr ? std::string(hook->codename) : safeName(name)) + "(" + joined(args) + ")";
            return true;
        }
        if (name == "math.lerp" && args.size() >= 3) { out = "FlxMath.lerp(" + joined(args) + ")"; return true; }
        if (name == "math.clamp" && args.size() >= 3) { out = "FlxMath.bound(" + joined(args) + ")"; return true; }
        if (name == "table.insert" && args.size() == 2) { out = args[0] + ".push(" + args[1] + ")"; return true; }
        if (name == "table.insert" && args.size() == 3) { out = args[0] + ".insert((" + args[1] + ") - 1, " + args[2] + ")"; return true; }
        if (name == "table.remove" && args.size() == 1) { out = args[0] + ".pop()"; return true; }
        if (name == "table.remove" && args.size() == 2) { out = args[0] + ".splice((" + args[1] + ") - 1, 1)"; return true; }
        if (name == "table.concat") { out = "psych.tableConcat(" + joined(args) + ")"; return true; }
        if (name == "string.len" && args.size() == 1) { out = "Std.string(" + args[0] + ").length"; return true; }
        if (name == "string.upper" && args.size() == 1) { out = "Std.string(" + args[0] + ").toUpperCase()"; return true; }
        if (name == "string.lower" && args.size() == 1) { out = "Std.string(" + args[0] + ").toLowerCase()"; return true; }
        if (name == "string.sub" && args.size() >= 2) { out = "psych.stringSub(" + joined(args) + ")"; return true; }
        if (name == "string.rep" && args.size() >= 2) { out = "psych.stringRep(" + joined(args) + ")"; return true; }
        if (name == "string.format") { out = "psych.stringFormat(" + joined(args) + ")"; return true; }
        if (name == "string.find" && args.size() >= 2) { out = "psych.stringFind(" + joined(args) + ")"; return true; }
        if (name == "string.gsub" && args.size() >= 3) { out = "psych.stringGsub(" + joined(args) + ")"; return true; }
        if (name == "math.random") { out = "psych.mathRandom(" + joined(args) + ")"; return true; }
        if (name == "math.floor" && args.size() == 1) { out = "Math.floor(" + args[0] + ")"; return true; }
        if (name == "math.ceil" && args.size() == 1) { out = "Math.ceil(" + args[0] + ")"; return true; }
        if (name == "math.huge" ) { out = "Math.POSITIVE_INFINITY"; return true; }
        if (name == "unpack" || name == "table.unpack") return false;
        if (name == "print" && !args.empty()) { out = "trace(" + joined(args) + ")"; return true; }
        if (name == "getfenv" || name == "setfenv" || name == "rawget" || name == "rawset" || name == "pcall" || name == "error" || name == "assert") return false;
        // Un metodo sobre un objeto (`obj:metodo()`) no es API de Psych: se
        // deja tal cual, que es lo que Haxe entiende.
        if (node.kind == NodeKind::MethodCall) {
            const Node* object = node.child(0);
            std::string base;
            if (object == nullptr || !expression(*object, base, 0)) return false;
            out = base + "." + node.text + "(" + joined(args) + ")";
            return true;
        }
        if (builtinCall(name, args, out)) return true;

        const psychapi::Entry* entry = psychapi::find(name);
        if (entry != nullptr && entry->compat != nullptr) {
            // Una llamada con un punto (`obj.setX`) no es de la API: la capa no
            // la conoce con ese nombre.
            out = std::string("psych.") + entry->compat + "(" + joined(args) + ")";
            if (entry->compatNote != nullptr)
                approximate(node.line, std::string("'") + name + "': " + entry->compatNote +
                                       " (" + entry->psychSource + ")");
            return true;
        }
        // Sin equivalente en la capa: activo con pendiente visible.
        if (name.find('.') != std::string::npos || name.find(':') != std::string::npos) return false;
        PortDiagnostic reason;
        if (psychapi::pendingForCompat(name, node.line, m_options.path, &reason))
            m_out.pending.push_back(reason);
        m_out.pendingCalls.push_back({name, node.line});
        ++m_out.refused;
        out = "psych.pending(" + quote(name) + ", [" + joined(args) + "])";
        return true;
    }

    // `doTweenY(etiqueta, objeto, valor, duracion, ease)`. El PRIMER argumento
    // es la etiqueta, no el objeto: `FunkinLua.hx:1505` lo pasa a
    // `tweenPrepare(tag, vars)` y el objeto es el segundo. Confundirlos mueve
    // otra cosa, y compila igual.
    bool tweenCall(const Node& node, const std::string& name,
                   const std::vector<std::string>& args, std::string& out,
                   int depth) {
        static const std::map<std::string, const char*> fields = {
            {"doTweenX", "x"}, {"doTweenY", "y"}, {"doTweenAngle", "angle"},
            {"doTweenAlpha", "alpha"}, {"doTweenZoom", "zoom"},
        };
        const auto field = fields.find(name);
        if (field == fields.end() || args.size() < 4) return false;

        const Node* tagNode = node.child(1);
        const Node* targetNode = node.child(2);
        if (tagNode == nullptr || targetNode == nullptr) return false;
        // El objeto se nombra con una cadena -`'crate'` o `'crate.scale'`- y esa
        // ruta tiene el mismo limite que `setProperty`: un nivel comprobado.
        if (targetNode->kind != NodeKind::String) return false;
        std::string target = targetNode->text;
        if (target.find('.') != std::string::npos) {
            std::string path;
            if (!propertyPath(target, path)) return false;
            target = path;
        } else if (!isIdentifier(target)) {
            return false;
        }

        std::string ease = "linear";
        const Node* easeNode = node.child(5);
        if (easeNode != nullptr) {
            if (easeNode->kind != NodeKind::String) return false;
            ease = easeMember(easeNode->text);
        }

        std::string options = "{ease: FlxEase." + ease;
        // `onTweenCompleted` se difunde a TODOS los scripts
        // (`FunkinLua.hx:1513`), y ahora se puede emitir tal cual: no hace falta
        // que el manejador este en este archivo, porque `ScriptPack.call` hace
        // lo mismo que `callOnLuas`. Antes esto se dejaba caer con un aviso; era
        // lo honesto entonces y ya no hace falta.
        if (tagNode->kind == NodeKind::String) {
            options += ", onComplete: function(fmlTween) { "
                       "PlayState.instance.scripts.call(\"onTweenCompleted\", [" +
                       quote(tagNode->text) + ", " + quote(targetNode->text) + "]); }";
        }
        options += "}";

        const std::string tween = "FlxTween.tween(" + target + ", {" +
                                  field->second + ": " + args[2] + "}, " +
                                  args[3] + ", " + options + ")";
        // `tweenPrepare` CANCELA el tween que tuviera esa etiqueta antes de
        // empezar el nuevo (`LuaUtils.hx:304`). Sin eso, dos tweens con la
        // misma etiqueta se pelean por el mismo objeto y el resultado depende
        // de cual acabe antes.
        if (tagNode->kind == NodeKind::String) {
            const std::string tag = tagNode->text;
            m_needsTweenRegistry = true;
            out = "{ if (fmlTweens.exists(" + quote(tag) + ")) fmlTweens.get(" +
                  quote(tag) + ").cancel(); fmlTweens.set(" + quote(tag) + ", " +
                  tween + "); }";
        } else {
            out = tween;
        }
        (void)depth;
        return true;
    }

    // El motor quita los puntos de la etiqueta antes de guardarla
    // (`FunkinLua.hx:1307`). Si no se hace lo mismo, `stopSound('a.b')` busca
    // una clave que `playSound('x', 1, 'a.b')` nunca guardo.
    static std::string soundTag(const std::string& raw) {
        std::string out;
        for (const char c : raw) if (c != '.') out += c;
        return out;
    }

    // `'crate.alpha'` -> `crate.alpha`. Solo un nivel: `Stage.hx:52-53` mete
    // cada sprite del escenario como variable del script por su nombre, y eso
    // es lo unico comprobado. Una ruta mas profunda o hacia una clase de Psych
    // no lo esta.
    static bool propertyPath(const std::string& raw, std::string& out) {
        const std::size_t dot = raw.find('.');
        if (dot == std::string::npos || raw.find('.', dot + 1) != std::string::npos)
            return false;
        const std::string object = raw.substr(0, dot);
        const std::string field = raw.substr(dot + 1);
        if (!isIdentifier(object) || !isIdentifier(field)) return false;
        out = object + "." + field;
        return true;
    }

    // -- sentencias ---------------------------------------------------------
    void emitBlock(const Node& block, int depth) {
        LocalScope scope(m_declared);
        for (const Node& statement : block.children) emitStatement(statement, depth);
    }

    void emitStatement(const Node& node, int depth) {
        switch (node.kind) {
            case NodeKind::Local:        return emitLocal(node, depth);
            case NodeKind::Assign:       return emitAssign(node, depth);
            case NodeKind::ExprStat:     return emitCallStatement(node, depth);
            case NodeKind::If:           return emitIf(node, depth);
            case NodeKind::While:        return emitWhile(node, depth);
            case NodeKind::NumericFor:   return emitNumericFor(node, depth);
            case NodeKind::GenericFor:   return emitGenericFor(node, depth);
            case NodeKind::FunctionDecl: return emitFunction(node, depth);
            case NodeKind::Do:
                if (node.child(0) != nullptr) {
                    m_out.source += indent(depth) + "{\n";
                    emitBlock(*node.child(0), depth + 1);
                    m_out.source += indent(depth) + "}\n";
                    ++m_out.emitted;
                }
                return;
            case NodeKind::Break:
                m_out.source += indent(depth) + "break;\n";
                ++m_out.emitted;
                return;
            case NodeKind::Return: {
                const Node* values = node.child(0);
                if (values == nullptr || values->size() == 0) {
                    m_out.source += indent(depth) + "return;\n";
                    ++m_out.emitted;
                    return;
                }
                if (values->size() > 1) {
                    refuse(node.line, "retorno multiple: Haxe devuelve un solo valor");
                    pending(depth, node.line, "return con varios valores");
                    return;
                }
                // `return Function_Stop` dentro de un hook: `event.cancel()`
                // si Codename lo consulta; si no, un return sin efecto, dicho.
                const Node& value = values->children[0];
                if (m_options.compat && value.kind == NodeKind::Name &&
                    (isFunctionStop(value.text) || isFunctionContinue(value.text))) {
                    if (isFunctionStop(value.text) && !m_currentHook.empty()) {
                        if (m_currentHookCancellable) {
                            m_out.source += indent(depth) + "{ fmlHookEvent.cancel(); return; }\n";
                        } else {
                            approximate(node.line, "'" + value.text + "' en '" + m_currentHook +
                                "': Codename no consulta el retorno de este hook, asi que no corta nada");
                            m_out.source += indent(depth) + "return; // " + value.text + ": Codename no lo consulta aqui\n";
                        }
                    } else {
                        m_out.source += indent(depth) + "return " + quote(value.text) + ";\n";
                    }
                    ++m_out.emitted;
                    return;
                }
                std::string text;
                if (!expression(value, text, 0)) {
                    refuse(node.line, "no hay equivalente demostrable para el valor devuelto");
                    pending(depth, node.line, "return");
                    return;
                }
                m_out.source += indent(depth) + "return " + text + ";\n";
                ++m_out.emitted;
                return;
            }
            case NodeKind::Repeat: {
                const Node* body = node.child(0);
                const Node* condition = node.child(1);
                if (body == nullptr || condition == nullptr) return;
                std::string test;
                if (!expression(*condition, test, 0)) {
                    refuse(node.line, "condicion del 'repeat' sin equivalente demostrable");
                    pending(depth, node.line, "repeat ... until");
                    return;
                }
                const std::string guard = loopGuard();
                if (!guard.empty()) m_out.source += indent(depth) + "{\n" + indent(depth + 1) + "var " + guard + " = 0;\n";
                const int inner = guard.empty() ? depth : depth + 1;
                m_out.source += indent(inner) + "do {\n";
                if (!guard.empty()) m_out.source += indent(inner + 1) + loopGuardCheck(guard, node.line);
                emitBlock(*body, inner + 1);
                m_out.source += indent(inner) + "} while (!(" + test + "));\n";
                if (!guard.empty()) m_out.source += indent(depth) + "}\n";
                ++m_out.emitted;
                return;
            }
            default:
                refuse(node.line, std::string("sentencia sin equivalente: ") +
                                      lua::toString(node.kind));
                pending(depth, node.line, lua::toString(node.kind));
                return;
        }
    }

    void emitLocal(const Node& node, int depth) {
        const Node* names = node.child(0);
        const Node* values = node.child(1);
        if (names == nullptr) return;
        for (std::size_t i = 0; i < names->size(); ++i) {
            const std::string name = safeName(names->children[i].text);
            m_declared.insert(names->children[i].text);
            const Node* value = values != nullptr ? values->child(i) : nullptr;
            if (value == nullptr) {
                m_out.source += indent(depth) + "var " + name + " = null;\n";
                ++m_out.emitted;
                continue;
            }
            std::string text;
            if (!expression(*value, text, 0)) {
                refuse(node.line, "'" + name + "': el valor no tiene equivalente demostrable");
                pending(depth, node.line, "local " + name);
                continue;
            }
            // Una tabla literal declarada aqui SI se sabe que es de Lua, y por
            // tanto que su primer elemento es el 1.
            if (value->kind == NodeKind::Table) {
                m_luaTables.insert(names->children[i].text);
                if (!value->children.empty() && value->children[0].size() == 2) m_keyedTables.insert(names->children[i].text);
            }
            m_out.source += indent(depth) + "var " + name + " = " + text + ";\n";
            ++m_out.emitted;
        }
    }

    void emitAssign(const Node& node, int depth) {
        const Node* targets = node.child(0);
        const Node* values = node.child(1);
        if (targets == nullptr || values == nullptr) return;
        if (targets->size() != values->size()) {
            refuse(node.line, "asignacion multiple con distinto numero de valores");
            pending(depth, node.line, "asignacion multiple");
            return;
        }
        for (std::size_t i = 0; i < targets->size(); ++i) {
            std::string left, right;
            if (!expression(targets->children[i], left, 0) ||
                !expression(values->children[i], right, 0)) {
                refuse(node.line, "asignacion sin equivalente demostrable");
                pending(depth, node.line, "asignacion");
                continue;
            }
            m_out.source += indent(depth) + left + " = " + right + ";\n";
            ++m_out.emitted;
        }
    }

    void emitCallStatement(const Node& node, int depth) {
        const Node* call = node.child(0);
        if (call == nullptr) return;
        const std::string name = lua::calleeName(*call);
        const psychapi::Entry* entry = psychapi::find(name);

        // Lo declarativo YA es el XML del escenario. Emitirlo otra vez como
        // codigo crearia el objeto dos veces (PORTEO_ASISTIDO_PLAN seccion D:
        // un unico responsable de creacion). Con la capa no hace falta: la
        // capa adopta por nombre el sprite que ya este en el XML.
        if (!m_options.compat && m_options.stageAlreadyEmitted && entry != nullptr &&
            entry->support == psychapi::Support::Declarative) {
            m_out.source += indent(depth) + "// (en el XML del escenario) " +
                            name + "\n";
            ++m_out.declarative;
            return;
        }

        std::string text;
        if (callExpression(*call, text, 0)) {
            m_out.source += indent(depth) + text + ";\n";
            ++m_out.emitted;
            return;
        }
        PortDiagnostic reason;
        if (psychapi::pendingFor(name, call->line, m_options.path, &reason)) {
            m_out.pending.push_back(reason);
            ++m_out.refused;
        } else {
            refuse(call->line, "'" + name + "': argumentos sin equivalente demostrable");
        }
        pending(depth, call->line, name.empty() ? "llamada" : name);
    }

    void emitIf(const Node& node, int depth) {
        bool first = true;
        for (const Node& clause : node.children) {
            if (clause.kind != NodeKind::Clause) continue;
            if (clause.size() >= 2) {
                std::string test;
                if (!expression(clause.children[0], test, 0)) {
                    refuse(clause.children[0].line,
                           "condicion sin equivalente demostrable");
                    pending(depth, clause.children[0].line, "if");
                    return;
                }
                // En Lua solo nil y false son falsos: `if x then` con un
                // numero o un objeto es verdadero. Con la capa se pregunta asi.
                if (m_options.compat && !booleanish(clause.children[0]))
                    test = "psych.truthy(" + test + ")";
                m_out.source += indent(depth) + (first ? "if (" : "else if (") +
                                test + ") {\n";
                emitBlock(clause.children[1], depth + 1);
                m_out.source += indent(depth) + "}\n";
                first = false;
                ++m_out.emitted;
                continue;
            }
            if (clause.size() == 1) {
                m_out.source += indent(depth) + "else {\n";
                emitBlock(clause.children[0], depth + 1);
                m_out.source += indent(depth) + "}\n";
                ++m_out.emitted;
            }
        }
    }

    void emitWhile(const Node& node, int depth) {
        const Node* condition = node.child(0);
        const Node* body = node.child(1);
        if (condition == nullptr || body == nullptr) return;
        std::string test;
        if (!expression(*condition, test, 0)) {
            refuse(node.line, "condicion del 'while' sin equivalente demostrable");
            pending(depth, node.line, "while");
            return;
        }
        if (m_options.compat && !booleanish(*condition)) test = "psych.truthy(" + test + ")";
        const std::string guard = loopGuard();
        if (!guard.empty()) m_out.source += indent(depth) + "{\n" + indent(depth + 1) + "var " + guard + " = 0;\n";
        const int inner = guard.empty() ? depth : depth + 1;
        m_out.source += indent(inner) + "while (" + test + ") {\n";
        if (!guard.empty()) m_out.source += indent(inner + 1) + loopGuardCheck(guard, node.line);
        emitBlock(*body, inner + 1);
        m_out.source += indent(inner) + "}\n";
        if (!guard.empty()) m_out.source += indent(depth) + "}\n";
        ++m_out.emitted;
    }

    // Un bucle que en Psych terminaba puede no terminar aqui: su condicion
    // puede depender de algo que la capa no mueve (una propiedad pendiente,
    // un valor que Psych cambiaba en otro hilo). Sin tope, congela el juego
    // entero y solo se cierra desde el Administrador de tareas. Con tope, la
    // vuelta un millon lanza un error que el guardian del hook cuenta: a la
    // tercera ese hook se apaga y el juego sigue. Solo sobre la capa.
    std::string loopGuard() {
        if (!m_options.compat) return {};
        std::string name;
        do {
            name = "fmlLoop" + std::to_string(m_loopGuardId++);
        } while (m_everLocal.count(name) || m_readNames.count(name) ||
                 m_assignedGlobals.count(name) || m_localFunctions.count(name));
        return name;
    }
    std::string loopGuardCheck(const std::string& guard, int line) const {
        return "if (++" + guard + " > 1000000) psych.runaway(" + quote(m_options.scriptName) +
               ", " + std::to_string(line) + ");\n";
    }

    // Haxe no tiene `for` con paso. Se emite como `while`, que es exactamente
    // lo que Lua hace por dentro, y con la comparacion que corresponde al signo
    // del paso. Si el paso no es un numero literal no se sabe el signo, y
    // elegir uno seria emitir un bucle que puede no terminar.
    void emitNumericFor(const Node& node, int depth) {
        const Node* name = node.child(0);
        const Node* startNode = node.child(1);
        const Node* limitNode = node.child(2);
        const Node* stepNode = node.child(3);
        const Node* body = node.child(4);
        if (name == nullptr || startNode == nullptr || limitNode == nullptr ||
            stepNode == nullptr || body == nullptr)
            return;
        double stepValue = 0.0;
        if (!literalNumber(*stepNode, &stepValue) || stepValue == 0.0) {
            refuse(node.line, "el paso del bucle no es un numero literal: no se "
                              "puede saber en que direccion avanza");
            pending(depth, node.line, "for numerico con paso variable");
            return;
        }
        std::string start, limit;
        if (!expression(*startNode, start, 0) || !expression(*limitNode, limit, 0)) {
            refuse(node.line, "los limites del bucle no tienen equivalente demostrable");
            pending(depth, node.line, "for numerico");
            return;
        }
        LocalScope scope(m_declared);
        const std::string counter = safeName(name->text);
        // Lua evalua los limites una sola vez ANTES de declarar su variable
        // de control. `for i = 1, i` no puede convertirse en `while (i <= i)`.
        // El contador interno tampoco cambia si el cuerpo asigna a `i`.
        std::string temp;
        do {
            temp = "fmlNumericFor" + std::to_string(m_numericForId++);
        } while (m_everLocal.count(temp) || m_readNames.count(temp) ||
                 m_assignedGlobals.count(temp) || m_localFunctions.count(temp));
        const std::string step = lua::luaNumberToString(stepValue);
        const std::string compare = stepValue > 0 ? " <= " : " >= ";
        const std::string guard = loopGuard();
        m_out.source += indent(depth) + "{\n";
        m_out.source += indent(depth + 1) + "var " + temp + " = {value: " + start + ", limit: " + limit + "};\n";
        if (!guard.empty()) m_out.source += indent(depth + 1) + "var " + guard + " = 0;\n";
        m_out.source += indent(depth + 1) + "while (" + temp + ".value" + compare + temp + ".limit) {\n";
        if (!guard.empty()) m_out.source += indent(depth + 2) + loopGuardCheck(guard, node.line);
        m_out.source += indent(depth + 2) + "var " + counter + " = " + temp + ".value;\n";
        m_declared.insert(name->text);
        emitBlock(*body, depth + 2);
        m_out.source += indent(depth + 2) + temp + ".value += " + step + ";\n";
        m_out.source += indent(depth + 1) + "}\n";
        m_out.source += indent(depth) + "}\n";
        ++m_out.emitted;
    }

    // Solo `ipairs` sobre una tabla de este chunk. `pairs` recorre tambien las
    // claves y su orden no esta definido en Lua; emitirlo como si fuera un
    // array cambiaria el resultado sin avisar.
    void emitGenericFor(const Node& node, int depth) {
        const Node* names = node.child(0);
        const Node* values = node.child(1);
        const Node* body = node.child(2);
        if (names == nullptr || values == nullptr || body == nullptr ||
            names->size() == 0)
            return;
        const Node* call = values->child(0);
        const std::string iterator = call != nullptr ? lua::calleeName(*call) : "";
        const Node* subject = call != nullptr ? call->child(1) : nullptr;
        const bool knownTable = subject != nullptr && subject->kind == NodeKind::Name &&
                                m_luaTables.count(subject->text) != 0;
        std::string tableText;
        // Con la capa, `pairs` sobre una tabla con claves de este script
        // recorre sus campos (`Reflect.fields`); sin saber que es un objeto,
        // sigue rechazandose.
        if (m_options.compat && iterator == "pairs" && subject != nullptr &&
            subject->kind == NodeKind::Name && m_keyedTables.count(subject->text) != 0) {
            const std::string key = safeName(names->children[0].text);
            m_out.source += indent(depth) + "for (" + key + " in Reflect.fields(" + subject->text + ")) {\n";
            if (names->size() > 1)
                m_out.source += indent(depth + 1) + "var " + safeName(names->children[1].text) +
                                " = Reflect.field(" + subject->text + ", " + key + ");\n";
            emitBlock(*body, depth + 1);
            m_out.source += indent(depth) + "}\n";
            ++m_out.emitted;
            return;
        }
        if (iterator != "ipairs" || subject == nullptr ||
            !(knownTable || (m_options.compat && expression(*subject, tableText, 0)))) {
            refuse(node.line, iterator == "pairs"
                                  ? "'pairs' no tiene orden definido en Lua: "
                                    "recorrerlo como un array cambiaria el resultado"
                                  : "iterador no demostrable; solo se emite "
                                    "'ipairs' sobre una tabla de este script");
            pending(depth, node.line, "for ... in " +
                                          (iterator.empty() ? "?" : iterator));
            return;
        }
        const std::string table = knownTable ? subject->text : tableText;
        const std::string key = safeName(names->children[0].text);
        m_out.source += indent(depth) + "for (fmlIndex in 0..." + table + ".length) {\n";
        // El indice de Lua empieza en 1 y el de Haxe en 0: la variable que ve
        // el script tiene que seguir siendo la de Lua.
        m_out.source += indent(depth + 1) + "var " + key + " = fmlIndex + 1;\n";
        if (names->size() > 1)
            m_out.source += indent(depth + 1) + "var " + safeName(names->children[1].text) +
                            " = " + table + "[fmlIndex];\n";
        emitBlock(*body, depth + 1);
        m_out.source += indent(depth) + "}\n";
        ++m_out.emitted;
    }

    void emitFunction(const Node& node, int depth) {
        LocalScope scope(m_declared);
        const Node* fn = node.child(0);
        if (fn == nullptr) return;
        if (node.text.find('.') != std::string::npos ||
            node.text.find(':') != std::string::npos) {
            refuse(node.line, "'" + node.text + "': una funcion dentro de una "
                              "tabla no tiene equivalente directo");
            pending(depth, node.line, "function " + node.text);
            return;
        }
        m_localFunctions.insert(node.text);
        const Node* params = fn->child(0);
        const Node* body = fn->child(1);
        if (params == nullptr || body == nullptr) return;
        if (fn->flag) {
            refuse(node.line, "'" + node.text + "': argumentos variables (...)");
            pending(depth, node.line, "function " + node.text + "(...)");
            return;
        }
        std::string list;
        for (std::size_t i = 0; i < params->size(); ++i) {
            if (i) list += ", ";
            // En Lua una funcion aguanta que la llamen con menos argumentos (el
            // resto es nil); en HScript eso lanza si no son opcionales.
            list += "?" + safeName(params->children[i].text);
            m_declared.insert(params->children[i].text);
        }

        // Un hook de Psych al nivel superior: nombre y firma de Codename, y el
        // preludio que reconstruye los parametros de Psych (PsychHooks.hpp).
        const psychhooks::Hook* hook = m_options.compat && depth == 0
                                           ? psychhooks::find(node.text) : nullptr;
        if (hook != nullptr) {
            for (const std::string& defined : m_out.definedHooks)
                if (defined == hook->codename) {
                    refuse(node.line, "'" + node.text + "' cae en el mismo hook de Codename ('" +
                                          hook->codename + "') que otra funcion de este archivo");
                    pending(depth, node.line, "function " + node.text);
                    return;
                }
            m_out.definedHooks.push_back(hook->codename);
            m_out.hooks.push_back({node.text, hook->codename, hook->approximate != nullptr});
            if (hook->approximate != nullptr)
                approximate(node.line, "'" + node.text + "' -> '" + hook->codename + "': " + hook->approximate);
            m_currentHook = node.text;
            m_currentHookCancellable = hook->cancellable;
            const std::string failures = "fmlHookFailures_" + std::string(hook->codename);
            m_out.source += indent(depth) + "var " + failures + " = 0;\n";
            // Sin un solo `return` en el envoltorio: en el hscript de Codename
            // 1.0.1 cada `return` es una excepcion y cuesta ~25 us; una funcion
            // que termina en una expresion devuelve su valor por ~1.5 us.
            // Medido en el motor; un hook por fotograma y script lo notaba entero.
            m_out.source += indent(depth) + "function " + hook->codename + "(" + hook->params + ") {\n";
            m_out.source += indent(depth + 1) + "var fmlHookResult = null;\n";
            m_out.source += indent(depth + 1) + "if (" + failures + " < 3) {\n";
            // Perfil de la capa (psych.profiling, con sus trazas): cuanto cuesta
            // este hook. Se lee DENTRO del try: nada de este envoltorio puede
            // lanzar fuera de el, o el error se repetiria en cada fotograma sin
            // que el tope de tres errores llegue a contarlo.
            m_out.source += indent(depth + 1) + "var fmlProfileStart = -1.0;\n";
            // hscript-improved exprReturn captura los errores de funciones
            // anidadas y los entrega al handler antes del try exterior. Hay
            // que quitarlo durante ESTA llamada para que el error se propague.
            // El cuerpo es una closure: sus returns no saltan la restauracion.
            m_out.source += indent(depth + 1) + "var fmlHookInterp = __script__.interp;\n";
            m_out.source += indent(depth + 1) + "var fmlHookErrorHandler = fmlHookInterp.errorHandler;\n";
            m_out.source += indent(depth + 1) + "fmlHookInterp.errorHandler = null;\n";
            m_out.source += indent(depth + 1) + "try {\n";
            m_out.source += indent(depth + 2) + "if (psych.profiling == true) fmlProfileStart = psych.clock();\n";
            // v1.0.1 no captura los argumentos de un hook externo al crear
            // closures (Interp EFunction filtra depth == 0). Pasarlos de forma
            // explicita evita perder tag/elapsed/event en timers y tweens.
            m_out.source += indent(depth + 2) + "var fmlHookBody = function(" + hook->params + ") {\n";
            if (hook->cancellable) m_out.source += indent(depth + 1) + "var fmlHookEvent = event;\n";
            m_out.source += indent(depth + 1) + "psych.hook(" + quote(node.text) + ", " + quote(m_options.scriptName) + ");\n";
            if (std::string(hook->codename) == "create" && m_hoistTopLevel)
                m_out.source += indent(depth + 1) + "fmlTopLevel();\n";
            if (hook->prelude != nullptr) {
                std::string prelude = hook->prelude;
                const std::string indexCall = "psych.noteIndex(note)";
                const auto at = prelude.find(indexCall);
                std::set<std::string> indexNames = {"id"};
                if (params->size() > 0) indexNames.insert(params->children[0].text);
                // Most hit hooks only animate or change health. Constructing
                // the Psych note view for every sustain, even when the body
                // never reads its index, dominates the interpreted runtime.
                if (at != std::string::npos && !referencesAny(*body, indexNames, true))
                    prelude.replace(at, indexCall.size(), "-1 /* unused index */");
                m_out.source += indent(depth + 1) + prelude + "\n";
            }
            // Snapshot before binding user names: function onEvent(value2,
            // name, value1) is legal Lua and must not overwrite later inputs.
            const auto arguments = psychhooks::arguments(*hook);
            if (params->size() > 0)
                m_out.source += indent(depth + 1) + "var fmlPsychArguments = [" + arguments + "];\n";
            const std::size_t argumentCount = arguments.empty() ? 0 :
                1 + static_cast<std::size_t>(std::count(arguments.begin(), arguments.end(), ','));
            for (std::size_t i = 0; i < params->size(); ++i) {
                const std::string p = safeName(params->children[i].text);
                m_out.source += indent(depth + 1) + "var " + p + " = " +
                    (i < argumentCount ? "fmlPsychArguments[" + std::to_string(i) + "]" : "null") + ";\n";
            }
            emitFunctionBody(*body, depth + 1);
            m_out.source += indent(depth + 2) + "};\n";
            m_out.source += indent(depth + 2) + "fmlHookResult = fmlHookBody(" + hook->params + ");\n";
            m_out.source += indent(depth + 1) + "} catch (fmlHookError:Dynamic) {\n";
            m_out.source += indent(depth + 2) + failures + "++;\n";
            m_out.source += indent(depth + 2) + "trace(" + quote("[FML] error hook " +
                node.text + " " + m_options.scriptName + " (hook line " + std::to_string(node.line) +
                "): ") + " + Std.string(fmlHookError) + \" [\" + " + failures + " + \"/3]\");\n";
            m_out.source += indent(depth + 2) + "if (" + failures + " >= 3) trace(" +
                quote("[FML] disabled hook " + node.text + " " + m_options.scriptName +
                      ": 3 runtime errors; other hooks remain active") + ");\n";
            m_out.source += indent(depth + 1) + "}\n";
            m_out.source += indent(depth + 1) + "fmlHookInterp.errorHandler = fmlHookErrorHandler;\n";
            m_out.source += indent(depth + 1) + "if (fmlProfileStart >= 0) psych.profile(" +
                quote(m_options.scriptName) + ", " + quote(node.text) + ", fmlProfileStart);\n";
            m_out.source += indent(depth + 1) + "}\n";
            m_out.source += indent(depth + 1) + "fmlHookResult;\n";
            m_out.source += indent(depth) + "}\n\n";
            m_currentHook.clear();
            m_currentHookCancellable = false;
            ++m_out.emitted;
            return;
        }

        m_out.source += indent(depth) + "function " + safeName(node.text) + "(" + list + ") {\n";
        emitFunctionBody(*body, depth + 1);
        m_out.source += indent(depth) + "}\n\n";
        ++m_out.emitted;
    }

    // -- cuerpos de funcion sin `return` -------------------------------------
    // En el hscript de Codename 1.0.1 `return` se implementa con una excepcion:
    // ~25 us cada uno, frente a ~1.5 us si la funcion termina en una expresion
    // (su valor es el del bloque). Medido en el motor. Con la capa, el cuerpo
    // de una funcion se emite asi:
    //   - un `return x` al final pasa a ser la expresion final `(x);`;
    //   - `if c then ... return end` seguido de mas sentencias pasa a
    //     `if (c) { ... } else { resto }` cuando todas sus ramas salen;
    //   - una funcion que en Lua no devuelve nada termina en `null;`, para que
    //     quien use su valor siga viendo nil y no el de la ultima asignacion.
    // Un `return` dentro de un bucle, o en una rama que no sale entera, se
    // queda como esta: solo cuesta mas, no cambia lo que hace.
    void emitFunctionBody(const Node& block, int depth) {
        if (!m_options.compat) {
            emitBlock(block, depth);
            return;
        }
        emitTailStatements(block, 0, depth);
    }

    static bool endsInReturn(const Node& block) {
        return block.size() > 0 && block.children.back().kind == NodeKind::Return;
    }

    // Un `if` sin `else` cuyas ramas acaban todas en `return`.
    static bool exitingIfWithoutElse(const Node& node) {
        if (node.kind != NodeKind::If || node.size() == 0) return false;
        for (const Node& clause : node.children) {
            if (clause.kind != NodeKind::Clause || clause.size() != 2) return false;
            if (!endsInReturn(clause.children[1])) return false;
        }
        return true;
    }

    void emitTailStatements(const Node& block, std::size_t from, int depth) {
        LocalScope scope(m_declared);
        const std::size_t count = block.size();
        for (std::size_t i = from; i < count; ++i) {
            const Node& statement = block.children[i];
            const bool last = i + 1 == count;
            if (statement.kind == NodeKind::Return) {
                emitTailReturn(statement, depth);
                return;
            }
            if (statement.kind == NodeKind::If && (last || exitingIfWithoutElse(statement)) &&
                emitTailIf(statement, depth, last ? nullptr : &block, i + 1))
                return;
            emitStatement(statement, depth);
        }
        // Lua devuelve nil al caer al final de la funcion.
        m_out.source += indent(depth) + "null;\n";
    }

    // El `if` como ultima sentencia del cuerpo, cada rama con su valor. `rest`
    // son las sentencias que siguen a un `if` sin `else` que sale en todas sus
    // ramas: van en su `else`. Falso si no se pudo (entonces se emite normal).
    bool emitTailIf(const Node& node, int depth, const Node* rest, std::size_t restFrom) {
        std::vector<std::string> tests;
        for (const Node& clause : node.children) {
            if (clause.kind != NodeKind::Clause) return false;
            if (clause.size() >= 2) {
                std::string test;
                if (!expression(clause.children[0], test, 0)) return false;
                if (!booleanish(clause.children[0])) test = "psych.truthy(" + test + ")";
                tests.push_back(test);
            }
        }
        std::size_t at = 0;
        bool hasElse = false;
        // Se cuenta como emitIf: una por rama del Lua (el `else` sintetico no).
        for (const Node& clause : node.children) {
            if (clause.size() >= 2) {
                m_out.source += indent(depth) + (at == 0 ? "if (" : "else if (") + tests[at] + ") {\n";
                emitTailStatements(clause.children[1], 0, depth + 1);
                m_out.source += indent(depth) + "}\n";
                ++at;
                ++m_out.emitted;
            } else if (clause.size() == 1) {
                m_out.source += indent(depth) + "else {\n";
                emitTailStatements(clause.children[0], 0, depth + 1);
                m_out.source += indent(depth) + "}\n";
                hasElse = true;
                ++m_out.emitted;
            }
        }
        if (!hasElse) {
            m_out.source += indent(depth) + "else {\n";
            if (rest != nullptr) emitTailStatements(*rest, restFrom, depth + 1);
            else m_out.source += indent(depth + 1) + "null;\n";
            m_out.source += indent(depth) + "}\n";
        }
        return true;
    }

    // `return` en posicion final: el valor como expresion, sin excepcion.
    void emitTailReturn(const Node& node, int depth) {
        const Node* values = node.child(0);
        if (values == nullptr || values->size() == 0) {
            m_out.source += indent(depth) + "null;\n";
            ++m_out.emitted;
            return;
        }
        if (values->size() > 1) {
            refuse(node.line, "retorno multiple: Haxe devuelve un solo valor");
            pending(depth, node.line, "return con varios valores");
            m_out.source += indent(depth) + "null;\n";
            return;
        }
        const Node& value = values->children[0];
        if (value.kind == NodeKind::Name && (isFunctionStop(value.text) || isFunctionContinue(value.text))) {
            if (isFunctionStop(value.text) && !m_currentHook.empty()) {
                if (m_currentHookCancellable) {
                    m_out.source += indent(depth) + "fmlHookEvent.cancel();\n";
                } else {
                    approximate(node.line, "'" + value.text + "' en '" + m_currentHook +
                        "': Codename no consulta el retorno de este hook, asi que no corta nada");
                    m_out.source += indent(depth) + "// " + value.text + ": Codename no lo consulta aqui\n";
                }
                m_out.source += indent(depth) + "null;\n";
            } else {
                m_out.source += indent(depth) + quote(value.text) + ";\n";
            }
            ++m_out.emitted;
            return;
        }
        std::string text;
        if (!expression(value, text, 0)) {
            refuse(node.line, "no hay equivalente demostrable para el valor devuelto");
            pending(depth, node.line, "return");
            m_out.source += indent(depth) + "null;\n";
            return;
        }
        terminatePreviousBlock();
        m_out.source += indent(depth) + "(" + text + ");\n";
        ++m_out.emitted;
    }

    // hscript lee `if (c) {...}` seguido de `(x)` como UNA llamada:
    // `if (c) {...}(x)` (parseExprNext tras parseStructure, `Parser.hx:381`).
    // Un `;` tras la llave cierra la sentencia (parseFullExpr lo consume).
    // Se salta el espacio y las lineas que solo son comentario.
    void terminatePreviousBlock() {
        std::string& source = m_out.source;
        std::size_t end = source.size();
        while (end > 0) {
            while (end > 0 && std::isspace(static_cast<unsigned char>(source[end - 1]))) --end;
            if (end == 0) return;
            const std::size_t lineStart = source.rfind('\n', end - 1);
            const std::size_t from = lineStart == std::string::npos ? 0 : lineStart + 1;
            std::size_t first = from;
            while (first < end && std::isspace(static_cast<unsigned char>(source[first]))) ++first;
            if (source.compare(first, 2, "//") != 0) break;
            end = from;
        }
        if (end > 0 && source[end - 1] == '}') source.insert(end, ";");
    }

    // -- el nivel superior con la capa --------------------------------------
    // La capa se carga la ULTIMA (raiz `songs/`, `PlayState.hx:754`), asi que
    // cuando corre el nivel superior de un script de cancion `psych` aun no
    // existe. Las declaraciones se quedan arriba -son variables del script,
    // que las funciones ven-, y todo lo que llama a algo se iza a
    // `fmlTopLevel()`, que `create` invoca lo primero. Se conserva el orden.
    void runCompat(const Node& block) {
        // Primera pasada: hay algo que izar?
        std::set<std::string> hoistedNames;
        for (const Node& statement : block.children) {
            if (statement.kind == NodeKind::FunctionDecl) continue;
            if (statement.kind == NodeKind::Local && localIsSafe(statement, hoistedNames)) continue;
            if (statement.kind == NodeKind::Assign && assignIsSafe(statement, hoistedNames)) continue;
            if (statement.kind == NodeKind::Local) {
                const Node* names = statement.child(0);
                if (names != nullptr) for (const Node& n : names->children) hoistedNames.insert(n.text);
            }
            m_hoistTopLevel = true;
        }
        m_out.source += "var fmlPsychScript = true;\n";
        // Los globales del script, declarados a null: en Lua un global sin
        // definir es nil y asignarlo lo crea; en HScript leer uno no declarado
        // lanza. Esto reproduce la semantica de Lua (globales por archivo).
        std::set<std::string> declareGlobals;
        for (const std::string& name : m_assignedGlobals)
            if (!knownName(name) && m_everLocal.count(name) == 0) declareGlobals.insert(name);
        for (const std::string& name : m_readNames)
            if (!knownName(name) && m_everLocal.count(name) == 0 && m_localFunctions.count(name) == 0)
                declareGlobals.insert(name);
        if (!declareGlobals.empty()) {
            m_out.source += "// Globales de este script (en Lua serian nil hasta asignarse).\n";
            for (const std::string& name : declareGlobals) m_out.source += "var " + name + " = null;\n";
        }
        m_out.source += "\n";
        hoistedNames.clear();
        std::string hoisted;
        bool hasCreate = false;
        for (const Node& statement : block.children) {
            if (statement.kind == NodeKind::FunctionDecl) {
                if (psychhooks::find(statement.text) != nullptr &&
                    std::string(psychhooks::find(statement.text)->codename) == "create")
                    hasCreate = true;
                emitStatement(statement, 0);
                continue;
            }
            if (statement.kind == NodeKind::Assign && assignIsSafe(statement, hoistedNames)) {
                // `x = 0` al nivel superior: una global del script, sin llamar a nada.
                emitStatement(statement, 0);
                continue;
            }
            if (statement.kind == NodeKind::Local) {
                if (localIsSafe(statement, hoistedNames)) { emitStatement(statement, 0); continue; }
                // La declaracion arriba, la inicializacion izada.
                const Node* names = statement.child(0);
                const Node* values = statement.child(1);
                if (names == nullptr) continue;
                for (std::size_t i = 0; i < names->size(); ++i) {
                    const std::string& name = names->children[i].text;
                    hoistedNames.insert(name);
                    m_out.source += "var " + name + " = null;\n";
                    const Node* value = values != nullptr ? values->child(i) : nullptr;
                    if (value == nullptr) continue;
                    if (value->kind == NodeKind::Table) {
                        m_luaTables.insert(name);
                        if (!value->children.empty() && value->children[0].size() == 2) m_keyedTables.insert(name);
                    }
                    std::string text;
                    const std::string before = m_out.source;
                    m_out.source.clear();
                    if (!expression(*value, text, 0)) {
                        refuse(statement.line, "'" + name + "': el valor no tiene equivalente demostrable");
                        pending(1, statement.line, "local " + name);
                    } else {
                        m_out.source += indent(1) + name + " = " + text + ";\n";
                        ++m_out.emitted;
                    }
                    hoisted += m_out.source;
                    m_out.source = before;
                    ++m_out.hoisted;
                }
                continue;
            }
            const std::string before = m_out.source;
            m_out.source.clear();
            emitStatement(statement, 1);
            hoisted += m_out.source;
            m_out.source = before;
            ++m_out.hoisted;
        }
        if (m_hoistTopLevel) {
            m_out.source += "// Lo que el Lua hacia al nivel superior. La capa `psych` no existe\n"
                            "// todavia cuando este archivo se carga (`PlayState.hx:754`), asi\n"
                            "// que corre lo primero dentro de `create`, en el mismo orden.\n"
                            "function fmlTopLevel() {\n" + hoisted + "}\n\n";
            if (!hasCreate) {
                m_out.definedHooks.push_back("create");
                m_out.source += "function create() {\n    psych.hook(\"onCreate\", " +
                                quote(m_options.scriptName) + ");\n    fmlTopLevel();\n}\n\n";
            }
        }
    }

    // Los nombres que NO son un global del script: variables por defecto del
    // motor (Script.hx:37-125 de Codename), lo que publica la capa
    // (PsychCompat) y las palabras del lenguaje. Leer uno de estos no es un
    // global de Lua; declararlo lo sombrearia.
    static bool knownName(const std::string& name) {
        static const std::set<std::string> known = {
            // lenguaje y esenciales
            "this", "super", "null", "true", "false", "trace", "psych",
            "disableScript", "importScript", "state", "window", "__script__",
            // clases y helpers por defecto de Codename (Script.hx)
            "Std", "Math", "Reflect", "StringTools", "Json", "Xml", "Type", "Date",
            "Lambda", "Sys", "BlendMode", "Assets", "Application", "Main",
            "FlxG", "FlxSprite", "FlxBasic", "FlxCamera", "FlxEase", "FlxTween",
            "FlxSound", "FlxAssets", "FlxMath", "FlxGroup", "FlxTypedGroup",
            "FlxSpriteGroup", "FlxTypeText", "FlxText", "FlxTimer", "FlxPoint",
            "FlxAxes", "FlxColor", "FlxTweenType", "FlxTextBorderStyle", "FlxTextAlign",
            "engine", "ModState", "ModSubState", "PlayState", "GameOverSubstate",
            "HealthIcon", "HudCamera", "Note", "Strum", "StrumLine", "Character",
            "Boyfriend", "PauseSubstate", "PauseSubState", "FreeplayState",
            "MainMenuState", "StoryMenuState", "TitleState", "Options", "Paths",
            "Conductor", "FunkinShader", "CustomShader", "FunkinText", "FlxAnimate",
            "FunkinSprite", "Alphabet", "Flags", "CoolUtil", "IniUtil", "XMLUtil",
            "ZipUtil", "MarkdownUtil", "EngineUtil", "ThreadUtil", "MemoryUtil",
            "BitmapUtil", "TranslationUtil", "translate",
            // los globales de Psych que publica la capa (PsychCompat.cpp)
            "psychReady", "curBpm", "crochet", "stepCrochet", "curSection",
            "curDecStep", "curDecBeat", "songPos", "songLength", "songName",
            "difficultyName", "mustHitSection", "altAnim", "gfSection",
            "healthGainMult", "healthLossMult", "screenWidth", "screenHeight",
            "buildTarget", "currentModDirectory", "botPlay", "middlescroll",
            "hideHud", "cameraZoomOnBeat", "shadersEnabled", "inGameOver",
            "boyfriendName", "dadName", "gfName", "Function_Stop", "Function_Continue",
            "Function_StopLua", "Function_StopHScript", "Function_StopAll",
            "bpm", "scrollSpeed", "curStep", "curBeat", "difficulty", "songNameShort",
            "combo", "score", "misses", "hits", "deaths", "rating", "ratingName",
            "ratingFC", "startedCountdown", "seenCutscene", "downscroll",
            "isStoryMode", "playbackRate", "health", "curStage", "version",
            "flashingLights", "hasVocals", "healthBarAlpha", "totalPlayed",
            "totalNotesHit", "lowQuality", "framerate", "ghostTapping", "noteOffset",
            "instakillOnMiss", "practice", "cameraSpeed",
        };
        return known.count(name) != 0;
    }

    // Recorre TODO el arbol: junta los nombres asignados como bare `x = ...`
    // (globales que el script escribe) y todos los que se leen, y aparte los
    // que en algun sitio son locales/parametros/bucle (que no hay que declarar
    // como globales). El global a declarar es lo asignado o leido menos lo
    // local, lo conocido y las funciones del propio script.
    void collectNames(const Node& node) {
        switch (node.kind) {
            case NodeKind::Local: {
                const Node* names = node.child(0);
                if (names != nullptr) for (const Node& n : names->children) m_everLocal.insert(n.text);
                break;
            }
            case NodeKind::NumericFor:
                if (node.child(0) != nullptr) m_everLocal.insert(node.child(0)->text);
                break;
            case NodeKind::GenericFor: {
                const Node* names = node.child(0);
                if (names != nullptr) for (const Node& n : names->children) m_everLocal.insert(n.text);
                break;
            }
            case NodeKind::Function: {
                const Node* params = node.child(0);
                if (params != nullptr) for (const Node& n : params->children) m_everLocal.insert(n.text);
                break;
            }
            case NodeKind::FunctionDecl:
                m_everLocal.insert(node.text);   // el nombre de la funcion no es un global-dato
                break;
            case NodeKind::Assign: {
                const Node* targets = node.child(0);
                if (targets != nullptr) for (const Node& t : targets->children)
                    if (t.kind == NodeKind::Name) m_assignedGlobals.insert(t.text);
                break;
            }
            case NodeKind::Name:
                m_readNames.insert(node.text);
                break;
            default: break;
        }
        for (const Node& child : node.children) collectNames(child);
    }

    // Una asignacion de nivel superior (`x = 0`, una global del script) se
    // queda arriba con la misma regla, si sus destinos son nombres simples.
    bool assignIsSafe(const Node& statement, const std::set<std::string>& hoistedNames) const {
        const Node* targets = statement.child(0);
        const Node* values = statement.child(1);
        if (targets == nullptr || values == nullptr) return false;
        for (const Node& target : targets->children)
            if (target.kind != NodeKind::Name || hoistedNames.count(target.text) != 0) return false;
        for (const Node& value : values->children) {
            if (containsCall(value)) return false;
            if (referencesAny(value, hoistedNames)) return false;
        }
        return true;
    }

    // Una declaracion de nivel superior se queda arriba si su valor no llama
    // a nada ni depende de algo izado: literales, tablas de literales y
    // funciones (cuyo cuerpo corre despues).
    bool localIsSafe(const Node& statement, const std::set<std::string>& hoistedNames) const {
        const Node* values = statement.child(1);
        if (values == nullptr) return true;
        for (const Node& value : values->children) {
            if (containsCall(value)) return false;
            if (referencesAny(value, hoistedNames)) return false;
        }
        return true;
    }

    const EmitOptions&    m_options;
    EmitResult&           m_out;
    std::set<std::string> m_luaTables;       // tablas declaradas AQUI: base 1
    std::set<std::string> m_keyedTables;     // tablas con claves: objetos anonimos
    std::set<std::string> m_localFunctions;  // funciones de este mismo script
    std::set<std::string> m_declared;        // locales y parametros vistos (pueden guardar funciones)
    std::set<std::string> m_assignedGlobals; // bare `x = ...` en cualquier scope
    std::set<std::string> m_readNames;       // todo Name leido
    std::set<std::string> m_everLocal;       // nombres locales/parametros/bucle/funcion en algun sitio
    std::string           m_currentHook;     // el hook de Psych que se esta emitiendo
    bool                  m_currentHookCancellable = false;
    bool                  m_hoistTopLevel = false;
    unsigned              m_numericForId = 0;
    unsigned              m_loopGuardId = 0;

public:
    // Si hace falta el mapa de sonidos por etiqueta al principio del archivo.
    bool m_needsSoundRegistry = false;
    // Si se emitio alguna posicion forzada de camara. Psych ademas levanta
    // `isCameraOnForcedPos` y Codename no tiene esa bandera, asi que hay una
    // diferencia que decir.
    bool m_forcedCameraEmitted = false;
    bool m_needsTweenRegistry = false;
    bool m_needsTimerRegistry = false;
};

}  // namespace

EmitResult emitHscript(const lua::Chunk& chunk, const EmitOptions& options) {
    EmitResult out;
    out.source =
        options.compat
            ? "// Generado por Funkin Mod Lab desde un script de Psych Engine, sobre la\n"
              "// capa de compatibilidad `songs/fml-psych-compat.hx` (`psych.*`).\n"
              "// Lo que la capa no implementa sale como psych.pending(nombre, args):\n"
              "// traza una vez en el juego y no rompe. Cada hueco lleva la linea del\n"
              "// original; se puede terminar a mano.\n\n"
            : "// Generado por Funkin Mod Lab desde un script de Psych Engine.\n"
              "// Lo que no tiene equivalente demostrable queda como PENDIENTE con su\n"
              "// linea del original: se ve, y se puede terminar a mano.\n\n";
    const std::string header = out.source;
    out.source.clear();
    Emitter emitter(options, out);
    emitter.run(chunk.root);
    // El registro va DELANTE de todo, porque las cuatro funciones de sonido lo
    // usan y en HScript una variable tiene que existir antes de tocarla.
    // Un resumen arriba del todo: cuanto salio, cuanto ya vive en el XML y
    // cuanto queda. Quien abre el archivo generado quiere saber eso antes de
    // leer nada.
    char summary[320];
    std::snprintf(summary, sizeof summary,
                  "// %d sentencias emitidas | %d ya en el XML del escenario | "
                  "%d pendientes\n"
                  "// Cada PENDIENTE lleva la linea del original, el codigo tal "
                  "cual estaba\n// y, cuando se sabe, que usar en Codename.\n\n",
                  out.emitted, out.declarative, out.refused);
    out.source = header + summary +
                 (emitter.m_needsTweenRegistry
                      ? "// Psych guarda los tweens por etiqueta para poder "
                        "cancelarlos, y\n// arrancar uno con una etiqueta usada "
                        "cancela el anterior\n// (`LuaUtils.hx:304`).\n"
                        "var fmlTweens:Map<String, FlxTween> = new Map();\n\n"
                      : "") +
                 (emitter.m_needsTimerRegistry
                      ? "// Lo mismo con los temporizadores (`FunkinLua.hx:667`).\n"
                        "var fmlTimers:Map<String, FlxTimer> = new Map();\n\n"
                      : "") +
                 (emitter.m_needsSoundRegistry
                      ? "// Psych guarda los sonidos por etiqueta para poder pararlos\n"
                        "// despues (`FunkinLua.hx:1305-1335`). Sin este registro,\n"
                        "// `stopSound` no tendria a que sonido referirse.\n"
                        "var fmlSounds:Map<String, FlxSound> = new Map();\n\n"
                      : "") +
                 out.source;
    if (emitter.m_forcedCameraEmitted) {
        PortDiagnostic d;
        d.cause = PortCause::MissingCapability;
        d.resolution = PortResolution::Approximate;
        d.path = options.path;
        d.detail = "la camara se coloca igual que en Psych, pero alli el evento "
                   "levanta ademas `isCameraOnForcedPos` para que la seccion "
                   "siguiente no la mueva, y Codename no tiene esa bandera: la "
                   "posicion se pone y el siguiente cambio de seccion la puede "
                   "recuperar";
        out.pending.push_back(std::move(d));
        ++out.refused;
    }
    // Lo que el lector de sintaxis no supo leer tambien es un pendiente del
    // porteo, y se arrastra en vez de quedarse en el chunk que nadie mira.
    out.pending.insert(out.pending.end(), chunk.pending.begin(), chunk.pending.end());
    out.complete = out.pending.empty() && chunk.complete;
    return out;
}

}  // namespace hscript
}  // namespace fml
