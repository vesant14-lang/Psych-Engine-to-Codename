// fml_formats — La capa de compatibilidad de Psych sobre Codename, como texto
// que el importador instala en `songs/fml-psych-compat.hx`.
//
// EL CUERPO vive en `psych-compat/fml-psych-compat.hx` -HScript que se lee y
// se prueba como tal- y se embebe en `PsychCompatScript.cpp` (generado con
// `tools/embed_compat.py`; `fml_psychsourcecheck` comprueba que los dos no se
// hayan separado). Aqui se le pone la cabecera con version y huella, el bloque
// de opciones que el usuario eligio al importar y los alias que eligio para
// funciones de un fork.
//
// LA CABECERA es lo que permite regenerar sin pisar: `// fml-psych-compat v1
// sha256=<huella del resto>`. Si la huella del archivo instalado coincide
// con su contenido, es nuestro y se puede reescribir; si no, alguien lo edito
// y se conserva.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fml {
namespace psychcompat {

constexpr int kVersion = 2;

struct Options {
    bool verbose = false;      // trazas [FML] hook/pending en la consola del juego
    bool windowReal = false;   // setPropertyFromClass('openfl.Lib', ...) toca la ventana de verdad
    // Alias elegidos por el usuario para funciones de un fork
    // (`psych-scripts/api-map.txt`: `makeLuaWiiText = makeLuaText`): pares
    // (alias, destino en psych.*). Van al final del nivel superior de la
    // capa, asi existen para cualquier `create`; son Aproximado y se quitan
    // regenerando sin ellos.
    std::vector<std::pair<std::string, std::string>> aliases;
};

// El HScript del cuerpo, tal cual esta en el .hx (sin cabecera ni opciones).
const std::string& body();

// El archivo entero listo para instalar.
std::string emit(const Options& options);

// Que hay instalado.
struct Installed {
    bool ours = false;     // lleva nuestra cabecera
    int  version = 0;
    bool edited = false;   // la huella no coincide con el contenido: alguien lo toco
    Options options;       // lo que declara (verbose, windowReal, alias)
};
Installed inspect(const std::string& text);

// Ruta virtual donde se instala.
inline const char* installPath() { return "songs/fml-psych-compat.hx"; }

}  // namespace psychcompat
}  // namespace fml
