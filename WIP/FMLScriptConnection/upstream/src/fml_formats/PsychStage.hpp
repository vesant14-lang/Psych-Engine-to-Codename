// Psych 0.7 and 1.0.4 stage conversion, audited against StageData/PlayState.
// 0.7 stages use creation scripts; 1.0.4 can ALSO declare JSON objects.
// This reader never executes Lua/HScript. It recovers declarative creation,
// literal arithmetic/tables and drawing order, and reports approximations.
// 'directory' selects a Psych asset library; it is not a Codename image prefix.
// Native Haxe/compiled stage logic and runtime modcharts require a port.
// Stage camera offsets map to camxoffset/camyoffset on character markers.
#pragma once

#include "../fml_core/Diagnostics.hpp"
#include "../fml_core/PortDiagnostic.hpp"
#include "../fml_core/Model.hpp"

#include <string>
#include <vector>

namespace fml {

struct PsychStageImport {
    UniversalStage stage;
    // Cuantos objetos traia el JSON y cuantos se convirtieron. Distinto de
    // `stage.objects.size()`: las marcas de personaje tambien son objetos.
    int  objectCount = 0;
    int  spriteCount = 0;
    bool hasObjects = false;     // false = el JSON solo trae posiciones y zoom
    // Camera offsets are also emitted on the Codename stage markers.
    Vec2 cameraBoyfriend, cameraOpponent, cameraGirlfriend;
    bool hasCameraOffsets = false;
    float cameraSpeed = 1.0f;
    bool  hidesGirlfriend = false;
    std::string directory;       // asset library/currentLevel, NOT an image prefix
    std::vector<std::string> warnings;
    std::vector<std::string> images;   // rutas que el stage pide, sin resolver
    // Cuantos sprites salieron del `.lua` en vez del JSON, y cuantas llamadas
    // del script no se supieron leer. Es la diferencia entre «el stage esta
    // vacio» y «el stage esta vacio Y no sabemos por que».
    int  fromScript = 0;
    int  scriptCallsIgnored = 0;
    bool scriptHadBranches = false;   // hubo una condicion sin demostrar
    // Cuanto se pudo desplegar del script y cuanto no. La diferencia entre un
    // escenario de 7 objetos y uno de 114 esta aqui, y tiene que poder mirarse.
    int  loopsExpanded = 0;
    int  loopsSkipped = 0;
    int  branchesUnknown = 0;
    bool scriptTruncated = false;
    // Lo que queda por resolver, con su linea del original. Un pendiente no
    // desaparece del informe: es el criterio de terminacion del plan.
    PortDiagnostics pending;
    // Un stage COMPILADO en el motor no tiene decorado en archivos: si se
    // reconstruyo, de donde salieron las piezas ("ejecutable", "carpeta") y
    // la prueba, para poder decirlo en cada lista y en cada informe.
    std::string reconstruction;
    std::string reconstructionNote;
};

// Lee un stage de Psych. `id` es el nombre del fichero sin extension, que es
// como el chart lo nombra.
//
// `scriptText` es el `.lua` del stage, si lo tiene. NO es un extra: de los 159
// stages de Psych que hay en disco NINGUNO trae `objects`, asi que sin leer el
// script el resultado es un escenario VACIO -zoom y las tres marcas- por muy
// bien que este el JSON. El script monta el decorado con llamadas
// declarativas (`makeLuaSprite`, `addLuaSprite`, `setScrollFactor`...) y de
// ahi salen los sprites, su orden y si van delante o detras de los personajes.
// JSON objects and declarative Lua creation are combined. No untrusted script
// is executed; runtime callbacks and unsupported expressions are reported.
Result<PsychStageImport> importPsychStage(const std::string& jsonText,
                                          const std::string& id,
                                          const std::string& scriptText = "",
                                          const std::string& songName = "");

// Corrige el hueco del jugador por la `position` de SU personaje.
//
// Los dos motores no llaman posicion a lo mismo. Psych suma `position` del
// personaje a la posicion del MUNDO -`PlayState.hx:757-758`,
// `char.x += positionArray[0]`- y no la refleja nunca. Codename mete ese numero
// en `globalOffset`, que va al offset del SPRITE y SI se refleja cuando el
// personaje esta en un lado distinto de aquel para el que se dibujo
// (`Character.hx:304`: `(isPlayer != playerOffsets) ? globalOffset.x
// : -globalOffset.x`). El hueco del jugador viene marcado como volteado por
// defecto (`Stage.hx:getDefaultPos`, `flip: true`) y FML nunca escribe
// `isPlayer` en el personaje (`PsychCharacter.cpp:177-178`), asi que ahi
// -y SOLO ahi- el empujon sale con el signo cambiado: el personaje acaba
// desviado el DOBLE de su propio offset.
//
// Se corrige en la POSE del escenario porque es el unico sitio de Codename
// donde un numero de posicion no se refleja (`Stage.hx:prepareCharacter` ->
// `char.setPosition`). No se toca el XML del personaje a proposito: asi los
// que ya estan instalados siguen valiendo y una importacion suelta de
// personaje no cambia de sitio. La Y no se toca: esa nunca se refleja.
void foldPlayerOffset(PsychStageImport& imported, const Vec2& playerPosition);

// Emite el `<stage>` de Codename equivalente.
std::string emitCodenameStage(const PsychStageImport& imported);

}  // namespace fml
