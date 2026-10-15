// fml_formats — Que significa en Codename cada llamada de la API Lua de Psych.
//
// POR QUE UN CATALOGO Y NO UN MAPA DE NOMBRES. Un mapa de nombres dice
// `setScrollFactor` -> `scrollFactor.set`, y eso basta hasta que los argumentos
// no coinciden: entonces produce codigo que compila y hace otra cosa. Cada
// entrada de aqui declara ademas cuantos argumentos hace falta, DONDE esta
// leido -archivo:linea de Psych y de Codename- y que nivel de soporte tiene.
// Sin cita no entra: una equivalencia que nadie puede comprobar es una
// suposicion con formato de tabla.
//
// EL ORDEN LO DECIDIO LA MEDICION, no la intuicion. `fml_luastaticcheck --api`
// sobre el corpus da 213 scripts, 89 funciones distintas y 7413 llamadas, con
// una concentracion brutal: tres funciones son el 59% de las llamadas, nueve
// son el 80% y veinte son el 90%. Este catalogo cubre esa cabeza; el resto
// sale como pendiente CON SU NOMBRE, que es lo que el plan de porteo pide.
//
// DOS MUNDOS, UNA TABLA. `support` describe el porteo AUTONOMO: lo que el
// emisor puede escribir como HScript directo sin ayuda (es el informe de
// porteo, que se niega por escrito ante lo que no puede demostrar). `compat`
// describe la CAPA DE COMPATIBILIDAD (`songs/fml-psych-compat.hx`,
// PsychCompat.cpp): el nombre bajo el que `psych.*` implementa la llamada con
// la semantica de Psych sobre Codename. Con la capa, una llamada se emite como
// `psych.<compat>(args)` sin exigir literales; sin ella, como hasta ahora.
// `compatNote` dice en que la capa se aparta de Psych, si se aparta.
//
// LO QUE NO SE HACE. `callMethod`, `runHaxeCode` y `addHaxeLibrary` no reciben
// equivalencia generica: se resuelven contra una capacidad conocida o el fuente
// aportado, o quedan pendientes (PORTEO_ASISTIDO_PLAN seccion C).
#pragma once

#include "../fml_core/PortDiagnostic.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fml {
namespace psychapi {

// Que se puede hacer con la llamada al portarla. No es una escala de calidad:
// son destinos distintos, y `Declarative` no es "mejor" que `Adapted`, es otra
// cosa -acaba en el XML del escenario en vez de en codigo-.
enum class Support {
    Declarative,   // se convierte en dato del escenario; no genera codigo
    Adapted,       // hay equivalente comprobado en Codename
    Approximate,   // hay algo parecido que NO es lo mismo; eleccion explicita
    Pending,       // sin equivalente comprobado: sale como pendiente con nombre
    NotEngine,     // no es API del motor: builtin de Lua o helper del propio mod
};

const char* toString(Support support);

// A que se traduce, y por que se puede afirmar.
struct Entry {
    const char* name;         // el nombre tal cual lo escribe el mod
    Support     support;
    int         minArgs;      // por debajo de esto la llamada esta incompleta
    const char* psychSource;  // archivo:linea de Psych 0.7
    const char* codename;     // el equivalente, o por que no lo hay
    const char* note;         // lo que hay que saber para no equivocarse
    // La capa de compatibilidad: nombre en `psych.*`, o nulo si la capa no lo
    // implementa (entonces sale como `psych.pending(nombre, args)`).
    const char* compat = nullptr;
    // En que se aparta la capa de Psych. Nulo = reproduce el motor.
    const char* compatNote = nullptr;
};

// Nulo si el nombre no esta en el catalogo. Un nombre desconocido NO es un
// error: es un pendiente que hay que decir, y decirlo es trabajo del llamante.
const Entry* find(const std::string& name);

std::size_t  size();
const Entry* at(std::size_t index);

// Cobertura sobre un inventario medido: pares (nombre, cuantas veces se llama).
// Cuenta LLAMADAS, no funciones distintas, porque lo que decide el trabajo es
// cuanto del corpus queda cubierto, no cuantas casillas se rellenan.
struct Coverage {
    int calls = 0;
    int declarative = 0;
    int adapted = 0;
    int approximate = 0;
    int pending = 0;
    int notEngine = 0;
    int unknown = 0;        // ni siquiera esta en el catalogo

    // Lo que se puede portar sin intervencion: declarativo + adaptado.
    int covered() const { return declarative + adapted; }
};

Coverage measure(const std::vector<std::pair<std::string, int>>& counted);

// La misma cuenta con la capa de compatibilidad delante: lo que la capa
// implementa cuenta como adaptado (o aproximado si se aparta); lo demas como
// pendiente o ajeno al motor.
Coverage measureWithCompat(const std::vector<std::pair<std::string, int>>& counted);

// El pendiente que corresponde a una llamada, listo para el informe. Devuelve
// false cuando la llamada no deja pendiente (declarativa, adaptada o ajena al
// motor).
bool pendingFor(const std::string& name, int line, const std::string& path,
                PortDiagnostic* out);

// Lo mismo con la capa: no deja pendiente lo que la capa implementa ni lo
// ajeno al motor; lo aproximado se dice como aproximado.
bool pendingForCompat(const std::string& name, int line, const std::string& path,
                      PortDiagnostic* out);

}  // namespace psychapi
}  // namespace fml
