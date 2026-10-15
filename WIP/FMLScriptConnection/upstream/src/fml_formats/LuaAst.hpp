// fml_formats — Lua 5.1 leido de verdad: tokens, arbol y linea de origen.
//
// POR QUE EXISTE ESTO. Hasta ahora el decorado se recuperaba con expresiones
// regulares (`PsychStage.cpp: expandStageTables`), y eso soporta exactamente
// UNA forma de bucle:
//
//     for i = 0, #TABLA - 1 do  ...  TABLA[i + 1][N]  ...  end
//
// Medido sobre 400 scripts de 7 mods del corpus mas las fuentes archivadas de
// lo ya importado: **139 bucles, y cero encajan**. Been Patient escribe
// `for i = 1, 24` -numerico, sin tabla-; Boxing Match WG escribe
// `for i = 1, #finalPhaseObjects` y entra por una local (`local obj = T[i]`,
// luego `obj[2]`). Ninguna de las dos formas la ve una regex sin convertirse
// en otro parser peor. Ver `tools/portscan.py` y `PORTEO_ASISTIDO_PLAN.md` §0.2.
//
// LO QUE ESTE MODULO NO HACE. No ejecuta nada, no resuelve nombres y no
// traduce. Solo convierte texto en arbol. Quien decida que significa
// `makeLuaSprite` va encima, no aqui: asi el mismo arbol sirve para recuperar
// un stage, para construir el flujo de la cancion y para traducir a HScript,
// sin que ninguno de los tres tenga que volver a leer Lua.
//
// SIN EXCEPCIONES (DESIGN §1.3). Lo que no se sabe leer sale como
// `PortDiagnostic` con su linea, y el arbol se queda con lo que si se leyo.
#pragma once

#include "../fml_core/Diagnostics.hpp"
#include "../fml_core/PortDiagnostic.hpp"

#include <functional>
#include <string>
#include <vector>

namespace fml {
namespace lua {

enum class NodeKind {
    // --- expresiones ---
    Nil, True, False,
    Number,        // number
    String,        // text = valor ya sin comillas ni escapes
    Vararg,        // ...
    Name,          // text
    Index,         // [objeto, clave]   a.b se guarda como a["b"]
    Call,          // [callee, args...]
    MethodCall,    // [objeto, args...]   text = nombre del metodo
    Function,      // [params(NameList), cuerpo(Block)]
    Table,         // [Field...]
    Field,         // [clave, valor] si tiene clave; [valor] si es de array
    Binary,        // text = operador; [izquierda, derecha]
    Unary,         // text = operador; [operando]

    // --- sentencias ---
    Block,         // [sentencias...]
    Local,         // [nombres(NameList), valores(ExprList)]
    Assign,        // [destinos(ExprList), valores(ExprList)]
    ExprStat,      // [expresion]   una llamada usada como sentencia
    If,            // [Clause...]
    Clause,        // [condicion, Block] o [Block] para el `else`
    While,         // [condicion, Block]
    Repeat,        // [Block, condicion]
    NumericFor,    // [Name, inicio, limite, paso, Block]  el paso SIEMPRE esta
    GenericFor,    // [nombres(NameList), valores(ExprList), Block]
    Return,        // [ExprList]
    Break,
    Do,            // [Block]
    FunctionDecl,  // text = nombre completo (a.b.c o a:b); [Function]

    // --- auxiliares ---
    NameList, ExprList,
    Bad,           // algo que no se supo leer; su linea ya esta en el informe
};

const char* toString(NodeKind kind);

struct Node {
    NodeKind    kind = NodeKind::Nil;
    std::string text;
    double      number = 0.0;
    int         line = 0;
    // `true` en `Function` si declara `...`; en `String` si venia de [[ ]].
    bool        flag = false;
    std::vector<Node> children;

    const Node* child(std::size_t index) const {
        return index < children.size() ? &children[index] : nullptr;
    }
    std::size_t size() const { return children.size(); }
};

struct Chunk {
    Node            root;          // siempre un Block, aunque este vacio
    PortDiagnostics pending;       // lo que no se supo leer, con su linea
    // `false` si algo quedo sin leer. No significa que el arbol este vacio:
    // significa que lo que hay es incompleto y no se puede presentar como fiel.
    bool complete = true;
};

// Lee un script de Lua. `path` solo se copia a los diagnosticos.
//
// Nunca falla del todo: un archivo ilegible devuelve un `Chunk` con `complete`
// en false y el motivo situado. Un `Result::fail` queda para lo que impide
// hasta empezar (fuente vacia).
Result<Chunk> parseLua(const std::string& source, const std::string& path = "");

// Utilidades que necesita cualquiera que recorra el arbol. Estan aqui para que
// no las reescriba cada consumidor con criterios distintos.

// Como imprime Lua un numero al concatenar: `1` y no `1.000000`. De ahi
// salen los nombres de tag (`'bar'..i` -> `bar1`), asi que es un hecho del
// lenguaje y no del evaluador.
std::string luaNumberToString(double value);

// Nombre plano de lo que se llama: `makeLuaSprite`, `obj.setX`, `a.b.c`.
// Vacio si el llamado no es un nombre resoluble estaticamente.
std::string calleeName(const Node& call);

// Recorrido en profundidad. `visit` devuelve false para no bajar por ese nodo.
void walk(const Node& node, const std::function<bool(const Node&)>& visit);

}  // namespace lua
}  // namespace fml
