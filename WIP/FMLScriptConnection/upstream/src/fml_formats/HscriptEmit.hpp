// fml_formats — De arbol de Lua a HScript de Codename.
//
// QUE ES Y QUE NO ES. Esto NO traduce Lua: emite HScript para lo que se puede
// demostrar equivalente y **se niega, por escrito**, para lo demas. Un emisor
// que rellena los huecos con algo parecido produce un archivo que compila y
// hace otra cosa, y eso es peor que un archivo con un hueco marcado: el hueco
// se ve, la diferencia de comportamiento no.
//
// DOS MODOS. El AUTONOMO (por omision) es el de siempre: HScript directo para
// lo demostrable y `PENDIENTE` para lo demas; es lo que alimenta el informe de
// porteo. El de CAPA (`EmitOptions::compat`) supone instalada la capa de
// compatibilidad `songs/fml-psych-compat.hx` (PsychCompat.cpp), que publica
// `psych.*` con la semantica de Psych sobre Codename: cada llamada de la API
// sale como `psych.<nombre>(args)` sin exigir literales, los hooks se renombran
// con la firma que Codename entrega (PsychHooks.hpp), las sentencias de nivel
// superior se izan a `fmlTopLevel()` -la capa aun no existe cuando corre el
// nivel superior de un script de cancion, `PlayState.hx:754`- y lo que no
// tiene equivalente sale como `psych.pending("nombre", [args])`, que traza una
// vez en el juego y no rompe. Es "activo con pendientes visibles" (DESIGN §58):
// nunca codigo que hace otra cosa, y cada hueco con su archivo:linea.
//
// POR QUE SOBRE EL ARBOL Y NO SOBRE LAS LLAMADAS RESUELTAS. `LuaStatic` aplana
// -despliega bucles, resuelve valores- y eso sirve para recuperar un ESCENARIO,
// que es una foto. El comportamiento no es una foto: `if`, `while` y los hooks
// tienen que seguir siendo `if`, `while` y hooks en el destino.
//
// LAS TRAMPAS QUE ESTE MODULO SI CONOCE, porque son las que producen codigo
// silenciosamente distinto:
//
//   `..`      en Lua concatena numeros y cadenas; `+` en Haxe suma numeros.
//             Se emite `Std.string(a) + Std.string(b)`, nunca `a + b`.
//   `t[i]`    las tablas de Lua empiezan en 1 y los Array de Haxe en 0. En el
//             modo autonomo solo se ajusta el indice de las tablas que ESTE
//             chunk declara; con la capa se ajusta siempre, porque en el Lua de
//             Psych un indice numerico solo cae sobre tablas de Lua (los
//             objetos del motor no se indexan: se piden por API).
//   `and/or`  devuelven un VALOR, no un booleano. `a or 10` vale 10 si `a` es
//             nil, y en Haxe eso no es `a || 10`. Con la capa va por
//             `psych.luaOr`, que evalua los dos lados.
//   `nil`     no es `false`: en Lua solo `nil` y `false` son falsos, el 0 no.
//
// Lo que no encaje en eso sale como comentario `PENDIENTE` con su linea y como
// `PortDiagnostic`. Ver PORTEO_ASISTIDO_PLAN seccion B.
#pragma once

#include "../fml_core/PortDiagnostic.hpp"
#include "LuaAst.hpp"

#include <string>
#include <vector>

namespace fml {
namespace hscript {

struct EmitOptions {
    std::string path;          // solo para los diagnosticos
    // El Lua original. No es opcional por capricho: un pendiente que solo dice
    // "linea 34" obliga a abrir el otro archivo y contar lineas. Con el
    // original al lado, terminar a mano deja de ser una busqueda.
    std::string source;
    // Si el escenario ya salio del mismo script. Cuando es true, las llamadas
    // declarativas no se emiten como codigo -ya son el XML- y se anotan.
    // (Solo en el modo autonomo: la capa adopta los sprites del XML por nombre.)
    bool stageAlreadyEmitted = true;
    int  maxDepth = 48;
    // El modo de capa: ver la cabecera.
    bool compat = false;
    // Nombre corto del script para las trazas de la capa (`[FML] hook ...`).
    std::string scriptName;
};

struct EmitResult {
    std::string     source;
    PortDiagnostics pending;

    int emitted = 0;      // sentencias que salieron como codigo
    int declarative = 0;  // llamadas que ya viven en el XML del escenario
    int refused = 0;      // lo que quedo como PENDIENTE, con su motivo
    // `true` solo si NADA quedo pendiente. Un `false` no significa que el
    // archivo no sirva: significa que no se puede presentar como fiel.
    bool complete = false;

    // Solo en el modo de capa: los hooks reconocidos (Psych -> Codename), las
    // llamadas emitidas como `psych.pending` con su linea, y cuantas
    // sentencias de nivel superior se izaron a `fmlTopLevel()`.
    struct Hook { std::string psych, codename; bool approximate = false; };
    struct PendingCall { std::string name; int line = 0; };
    std::vector<Hook>        hooks;
    std::vector<PendingCall> pendingCalls;
    int hoisted = 0;
    // Nombres de los hooks de Codename que el archivo define (para detectar
    // dos hooks de Psych que caen en el mismo de Codename).
    std::vector<std::string> definedHooks;
};

EmitResult emitHscript(const lua::Chunk& chunk, const EmitOptions& options);

}  // namespace hscript
}  // namespace fml
