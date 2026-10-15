// fml_formats — De los archivos de comportamiento de un bundle de Psych a lo
// que se INSTALA en Codename, y donde.
//
// HASTA HOY el importador conservaba todo en `psych-scripts/` (Codename no
// lee nada de ahi) y, como mucho, dejaba una traduccion al lado como
// borrador. Esto decide, archivo por archivo y por CONTRATO del motor, a que
// sitio ejecutable va cada uno y lo traduce sobre la capa de compatibilidad
// (PsychCompat, HscriptEmit en modo de capa):
//
//   data/<cancion>/*.lua      -> songs/<id>/scripts/<archivo>.hx   (PlayState.hx:753)
//   scripts/*.lua (globales)  -> songs/<id>/scripts/<archivo>.hx, solo si la
//                                cancion los usa (dependencia, nombre de la
//                                cancion en el script) o el usuario lo marca
//   stages/<stage>.lua        -> data/stages/<stage>.hx             (Stage.hx:76-80)
//   characters/<x>.lua        -> songs/fml-psych-chars/<x>.hx (la capa los mete
//                                en el pack tras el escenario, como Psych)
//   custom_events/<N>.lua     -> data/events/<N>.hx + <N>.json      (EventsData.hx:155)
//   custom_notetypes/<T>.lua  -> data/notes/<T>.hx                  (PlayState.hx:797)
//
// Un `.txt` de evento sin `.lua` o un tipo de nota de 0 bytes es
// comportamiento COMPILADO en el fork: se dice como tal, no como "sin script".
// Un `.hx` de Psych (HScript de otro motor) se conserva; su traduccion es de
// otra etapa.
//
// "Activo con pendientes" (DESIGN §58): lo traducido se instala aunque tenga
// llamadas sin equivalencia; cada una sale como `psych.pending` y aqui con su
// archivo:linea. Solo se bloquea lo que no parsea.
//
// Es una funcion pura sobre rutas y texto: la usan el importador al escribir y
// las reglas del corpus al medir, con el mismo resultado.
#pragma once

#include "../fml_core/PortDiagnostic.hpp"
#include "HscriptEmit.hpp"
#include "PsychScriptDependencies.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fml {
namespace psychplan {

struct Binding { std::string source, difficulty; };

struct Options {
    std::string songId;          // carpeta de destino: songs/<id>
    std::string psychSongName;   // como Psych nombra la cancion (SONG.song)
    bool activate = true;        // false: todo se conserva, nada se instala
    bool includeGlobals = false; // activar todos los globales del mod en esta cancion
    std::vector<Binding> manual; // archivos asociados a mano (y a una dificultad)
    // Claves ya instaladas por otras canciones (para los globales compartidos).
    std::vector<std::string> alreadyInstalled;
    std::vector<std::string> disconnectedKeys; // library matches explicitly declined
    std::map<std::string, std::string> sourceText; // verified archived sources, no disk dependency
};

struct Script {
    std::string source;       // ruta real
    std::string key;          // clave de Psych (data/<cancion>/x.lua)
    std::string role;         // cancion, global, escenario, personaje, evento, nota, hscript, otro
    std::string destination;  // ruta virtual del activo; vacia = no se instala
    std::string difficulty;   // si va atado a una dificultad
    std::string status;       // Activado, Aproximado, Conservado, Detectado, Bloqueado
    std::string reason;       // por que no se activa, o la nota
    std::string psychName;    // nombre del evento o del tipo de nota
    bool active = false;
    std::string hscript;
    std::vector<hscript::EmitResult::Hook>        hooks;
    std::vector<hscript::EmitResult::PendingCall> pendingCalls;
    PortDiagnostics diagnostics;
    int approximations = 0;
    // Otros archivos que acompanan al activo (el .json del evento).
    std::vector<std::pair<std::string, std::string>> extras;
};

struct Plan {
    std::vector<Script> scripts;
    int active = 0, approximate = 0, conserved = 0, detected = 0, blocked = 0;
    int pendingCalls = 0;
    std::vector<std::string> pendingNames;   // sin repetir, ordenados
    std::vector<std::string> hooks;          // hooks de Codename definidos, sin repetir
    int customEvents = 0, customNotes = 0, modcharts = 0;
    // Clave de Psych (con y sin extension, y el tallo) -> ruta instalada. Va a
    // songs/<id>/fml-psych-scripts.json para addLuaScript/isRunning.
    std::map<std::string, std::string> scriptMap;
};

// `files`: pares (ruta real, clave) del bundle. `dependencies`: las que
// encontro collectBundle (para saber que globales pide la cancion).
Plan planBehavior(const std::vector<std::pair<std::string, std::string>>& files,
                  const std::vector<psych::ScriptDependency>& dependencies,
                  const Options& options);

// El json de `data/events/<Nombre>.json` para un evento de Psych: dos
// valores de texto, como los escribe su editor.
std::string eventSchemaJson();

// El json de `songs/<id>/fml-psych-scripts.json`.
std::string scriptMapJson(const Plan& plan);

}  // namespace psychplan
}  // namespace fml
