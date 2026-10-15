// fml_core
//Archivo por Eluri14

#pragma once

#include <string>
#include <vector>

namespace fml {

// Por que quedo pendiente. No es una escala de gravedad: son causas distintas
// con herramientas distintas.
enum class PortCause {
    UnsupportedSyntax,   // el lenguaje trae algo que el analizador no lee
    UnresolvedValue,     // no se puede demostrar el valor (ruta construida, etc.)
    MissingCapability,   // la API no tiene equivalente comprobado en el destino
    MissingResource,     // el archivo que pide no esta en las fuentes
    MissingSource,       // el comportamiento solo existe compilado
    AmbiguousOverride,   // dos bibliotecas aportan lo mismo y hay que elegir
    DynamicCode,         // el codigo se construye en caliente y no es analizable
};

inline const char* toString(PortCause cause) {
    switch (cause) {
        case PortCause::UnsupportedSyntax: return "unsupported-syntax";
        case PortCause::UnresolvedValue:   return "unresolved-value";
        case PortCause::MissingCapability: return "missing-capability";
        case PortCause::MissingResource:   return "missing-resource";
        case PortCause::MissingSource:     return "missing-source";
        case PortCause::AmbiguousOverride: return "ambiguous-override";
        case PortCause::DynamicCode:       return "dynamic-code";
    }
    return "unknown";
}


enum class PortResolution {
    None,
    PickFile,        // elegir el archivo o la carpeta que falta
    ProvideSource,   // aportar el fuente Haxe del fork
    ChooseAdapter,   // elegir una equivalencia conocida entre varias
    Approximate,     // hay una aproximacion; apagada de inicio y marcada
    EditByHand,      // lo generado esta escrito y se puede seguir a mano
};

inline const char* toString(PortResolution resolution) {
    switch (resolution) {
        case PortResolution::None:          return "none";
        case PortResolution::PickFile:      return "pick-file";
        case PortResolution::ProvideSource: return "provide-source";
        case PortResolution::ChooseAdapter: return "choose-adapter";
        case PortResolution::Approximate:   return "approximate";
        case PortResolution::EditByHand:    return "edit-by-hand";
    }
    return "unknown";
}

struct PortDiagnostic {
    PortCause      cause = PortCause::UnsupportedSyntax;
    PortResolution resolution = PortResolution::None;
    std::string    path;        // el archivo ORIGINAL
    int            line = 0;    // 1 = primera linea; 0 = no se sabe
    int            column = 0;
    std::string    detail;      // que se encontro, en una linea
    // A quien deja sin servicio: tags de objeto, nombres de fase, personajes.
    std::vector<std::string> affected;
};

// Un pendiente NUNCA desaparece del informe (criterio de terminacion del plan
// de porteo). Esta lista es la que se entrega, y solo se le anade.
using PortDiagnostics = std::vector<PortDiagnostic>;

}  // namespace fml
