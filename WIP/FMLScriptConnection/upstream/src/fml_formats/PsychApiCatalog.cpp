#include "PsychApiCatalog.hpp"

#include <cstring>

namespace fml {
namespace psychapi {

const char* toString(Support support) {
    switch (support) {
        case Support::Declarative: return "declarativo";
        case Support::Adapted:     return "adaptado";
        case Support::Approximate: return "aproximado";
        case Support::Pending:     return "pendiente";
        case Support::NotEngine:   return "no-es-del-motor";
    }
    return "?";
}

namespace {

// Las lineas de Psych son de `FNF-PsychEngine-0.7` en las entradas de la
// primera tanda y de `FNF-PsychEngine-main` (`psychlua/`) en las de la capa de
// compatibilidad; las de Codename, de `CodenameEngine-main`. Estan abiertas y
// comprobadas una a una, no copiadas de una wiki.
//
// El septimo campo es el nombre en la capa `psych.*`; el octavo, en que se
// aparta de Psych. Sin septimo campo la capa no lo implementa.
const Entry kEntries[] = {
    // ---- lo que acaba siendo el escenario, sin una linea de codigo ---------
    // Con la capa tambien son codigo: `psych.makeLuaSprite` adopta el sprite
    // que ya este en el XML del escenario con ese nombre (`Stage.getSprite`,
    // `Stage.hx:46`) y solo crea lo que no exista.
    {"makeLuaSprite", Support::Declarative, 1, "FunkinLua.hx:949",
     "<sprite> en data/stages/<id>.xml (Stage.hx:66)",
     "crear NO es anadir: el sprite existe pero no se ve hasta addLuaSprite",
     "makeLuaSprite"},
    {"makeAnimatedLuaSprite", Support::Declarative, 1, "FunkinLua.hx:960",
     "<sprite> con sus <anim>",
     "el atlas tiene que viajar tambien; sin el, el sprite queda sin frames",
     "makeAnimatedLuaSprite"},
    {"makeGraphic", Support::Declarative, 3, "FunkinLua.hx:969",
     "<box> con ancho, alto y color",
     "el color viene sin '#' y hay que anadirselo", "makeGraphic"},
    {"addLuaSprite", Support::Declarative, 1, "FunkinLua.hx:1039",
     "posicion en el XML respecto a las marcas de personaje",
     "el segundo argumento decide delante/detras de los personajes; por "
     "omision va detras (FunkinLua.hx:1039-1050)", "addLuaSprite"},
    {"setScrollFactor", Support::Declarative, 3, "FunkinLua.hx:1028",
     "atributo scroll del <sprite>", nullptr, "setScrollFactor"},
    {"scaleObject", Support::Declarative, 3, "FunkinLua.hx:1074",
     "atributos scalex y scaley", nullptr, "scaleObject"},
    {"addAnimationByPrefix", Support::Declarative, 3, "FunkinLua.hx:973",
     "<anim> con name, anim, fps y loop", nullptr, "addAnimationByPrefix"},
    {"setObjectOrder", Support::Declarative, 2, "FunkinLua.hx:469",
     "el orden de los <sprite> en el XML",
     "el XML se lee en orden, asi que reordenar es mover la linea",
     "setObjectOrder",
     "en Codename el escenario, las marcas y los personajes van planos en "
     "`PlayState.members` (`Stage.hx:321-350`): el indice numerico de Psych no "
     "cuenta los mismos objetos"},

    // ---- lo que si tiene equivalente comprobado en codigo ------------------
    {"setProperty", Support::Adapted, 2, "ReflectionFunctions.hx:24",
     "asignacion directa: `tag.campo = valor`",
     "Stage.hx:52-53 mete CADA sprite del escenario como variable del script "
     "por su nombre, asi que la ruta de un nivel es directa. Una ruta mas "
     "profunda o hacia una clase de Psych no lo es", "setProperty"},
    {"getProperty", Support::Adapted, 1, "ReflectionFunctions.hx:18",
     "lectura directa: `tag.campo`",
     "mismo limite que setProperty: un nivel", "getProperty"},
    {"setObjectCamera", Support::Adapted, 2, "FunkinLua.hx:1151",
     "`tag.camera = camGame` o `camHUD` (PlayState.hx:315,319)",
     "Codename nombra igual camGame y camHUD, pero camHUD es HudCamera, no "
     "FlxCamera: lo que dependa de su tipo hay que mirarlo", "setObjectCamera"},
    {"getSongPosition", Support::Adapted, 0, "FunkinLua.hx:801",
     "Conductor.songPosition (Conductor.hx:57)", nullptr, "getSongPosition"},

    // ---- la familia de tweens ----------------------------------------------
    // 1451 llamadas en el corpus -`doTweenY` 556, `doTweenX` 442,
    // `doTweenAlpha` 301, `noteTweenY` 152-: el bloque descubierto mas grande.
    // Codename expone `FlxTween` y `FlxEase` a TODOS los scripts
    // (`Script.hx:62-63`), asi que hay equivalente de verdad.
    {"doTweenX", Support::Adapted, 5, "FunkinLua.hx:1505",
     "FlxTween.tween(objeto, {x: valor}, duracion, {ease: ...}) (Script.hx:62-63)",
     "el primer argumento es la ETIQUETA del tween, no el objeto: el objeto es "
     "el segundo. Confundirlos mueve otra cosa", "doTweenX"},
    {"doTweenY", Support::Adapted, 5, "FunkinLua.hx:518",
     "FlxTween.tween(objeto, {y: valor}, duracion, {ease: ...})",
     "mismo orden de argumentos que doTweenX", "doTweenY"},
    {"doTweenAngle", Support::Adapted, 5, "FunkinLua.hx:521",
     "FlxTween.tween(objeto, {angle: valor}, duracion, {ease: ...})", nullptr, "doTweenAngle"},
    {"doTweenAlpha", Support::Adapted, 5, "FunkinLua.hx:1505",
     "FlxTween.tween(objeto, {alpha: valor}, duracion, {ease: ...})", nullptr, "doTweenAlpha"},
    {"doTweenZoom", Support::Adapted, 5, "FunkinLua.hx:1505",
     "FlxTween.tween(objeto, {zoom: valor}, duracion, {ease: ...})", nullptr, "doTweenZoom"},
    {"cancelTween", Support::Adapted, 1, "LuaUtils.hx:293",
     "fmlTweens.get(tag).cancel() y se quita del registro",
     "el registro no es opcional: arrancar un tween con una etiqueta ya usada "
     "cancela el anterior (`LuaUtils.hx:304`), y sin guardarlos dos tweens se "
     "pelean por el mismo objeto", "cancelTween"},

    // ---- texto: 459 llamadas -----------------------------------------------
    // `Script.hx:71` expone `FlxText` a todos los scripts de Codename.
    {"makeLuaText", Support::Adapted, 1, "TextFunctions.hx:9",
     "new FlxText(x, y, width, texto, 16) (Script.hx:71)",
     "Psych ademas le pone vcr.ttf blanco centrado con borde negro y lo manda a "
     "camHUD (`TextFunctions.hx:12-15`). Sin eso el texto sale con otra fuente "
     "y en otra camara, asi que el formato viaja con la creacion", "makeLuaText"},
    {"addLuaText", Support::Adapted, 1, "TextFunctions.hx:152",
     "add(tag)", nullptr, "addLuaText"},
    {"setTextString", Support::Adapted, 2, "TextFunctions.hx:20",
     "tag.text = valor", nullptr, "setTextString"},
    {"setTextSize", Support::Adapted, 2, "TextFunctions.hx:30",
     "tag.size = valor", nullptr, "setTextSize"},
    {"setTextAlignment", Support::Adapted, 2, "TextFunctions.hx:97",
     "tag.alignment = LEFT | CENTER | RIGHT", nullptr, "setTextAlignment"},

    // ---- temporizadores: 178 llamadas --------------------------------------
    {"runTimer", Support::Adapted, 1, "FunkinLua.hx:666",
     "new FlxTimer().start(tiempo, callback, vueltas) (Script.hx: FlxTimer)",
     "el callback llama a `onTimerCompleted(tag, loops, loopsLeft)` -en ese "
     "orden, `FunkinLua.hx:672`- y Psych lo difunde a TODOS los scripts. En el "
     "corpus aparece 128 veces: perderlo es perder la mitad de lo que hace un "
     "temporizador", "runTimer"},
    {"cancelTimer", Support::Adapted, 1, "LuaUtils.hx:311",
     "fmlTimers.get(tag).cancel() y se quita del registro",
     "igual que cancelTween: `runTimer` cancela el que tuviera esa etiqueta "
     "(`FunkinLua.hx:667`)", "cancelTimer"},

    // ---- color: 74 llamadas ------------------------------------------------
    {"getColorFromHex", Support::Adapted, 1, "FunkinLua.hx:729",
     "FlxColor.fromString(valor) (Script.hx: FlxColor)",
     "el motor llama literalmente a `FlxColor.fromString`, asi que la "
     "equivalencia es la misma funcion", "getColorFromHex"},
    {"doTweenColor", Support::Adapted, 5, "FunkinLua.hx:530",
     "FlxTween.color(objeto, duracion, desde, hasta, {ease: ...})",
     "OJO al orden: en `FlxTween.color` la duracion va SEGUNDA, no cuarta como "
     "en `FlxTween.tween`", "doTweenColor"},

    // ---- personajes: 130 llamadas ------------------------------------------
    {"characterPlayAnim", Support::Adapted, 2, "DeprecatedFunctions.hx:33",
     "personaje.playAnim(anim, forzado) (Character.hx:297)",
     "Psych la marca DEPRECADA en su propio fuente; se adapta porque el corpus "
     "la usa 130 veces, no porque sea la forma recomendada", "characterPlayAnim"},

    // ---- lo que NO se puede resolver, y por que -----------------------------
    {"triggerEvent", Support::Adapted, 1, "FunkinLua.hx:751",
     "'Camera Follow Pos' -> camFollow.setPosition(x, y) (PlayState.hx:194,867)",
     "de las 324 llamadas del corpus, 314 son ese unico evento. Los demas "
     "nombres siguen pendientes. Diferencia dicha: Psych levanta ademas "
     "`isCameraOnForcedPos` (`PlayState.hx:1913`) para que la seccion siguiente "
     "no mueva la camara, y Codename no tiene esa bandera", "triggerEvent"},
    {"setPropertyFromClass", Support::Adapted, 2, "ReflectionFunctions.hx:51",
     "FlxG.<campo> y Conductor.<campo>; las demas clases no",
     "solo las que existen en los dos motores con la misma forma. "
     "`ClientPrefs` (100 llamadas) no tiene equivalente, y las de "
     "`GameOverSubstate` (108) son ESTATICAS en Psych "
     "(`GameOverSubstate.hx:22`) y de INSTANCIA en Codename "
     "(`GameOverSubstate.hx:18`): asignarlas como clase no haria nada",
     "setPropertyFromClass"},
    {"getPropertyFromGroup", Support::Pending, 2, "ReflectionFunctions.hx:71",
     "los grupos que nombra el corpus -unspawnNotes, strumLineNotes, notes, "
     "eventNotes- no existen en Codename, que modela las notas como "
     "`strumLines:FlxTypedGroup<StrumLine>` (`PlayState.hx:103`)",
     "135 llamadas entre las dos: no es un nombre distinto, es otro modelo",
     "getPropertyFromGroup",
     "`strumLineNotes`/`opponentStrums`/`playerStrums` se resuelven sobre "
     "`strumLines.members[0|1].members[i]` (`PlayState.hx:2281-2288`); `notes` "
     "y `unspawnNotes` sobre las notas del chart en orden de tiempo"},
    {"setPropertyFromGroup", Support::Pending, 3, "ReflectionFunctions.hx:97",
     "igual que getPropertyFromGroup", nullptr, "setPropertyFromGroup",
     "igual que getPropertyFromGroup"},
    {"noteTweenY", Support::Pending, 4, "FunkinLua.hx:561",
     "mueve un receptor por indice; hace falta la strumline resuelta",
     "152 llamadas, todas en 8 archivos: es material de modchart", "noteTweenY"},
    {"playSound", Support::Adapted, 1, "FunkinLua.hx:1305",
     "FlxG.sound.play(Paths.sound(x), volumen), guardado por etiqueta",
     "las cuatro de sonido van juntas o no van: las otras tres no reciben el "
     "sonido sino la ETIQUETA con la que este lo guardo. El motor ademas para "
     "el anterior con esa misma etiqueta antes de sonar el nuevo", "playSound"},
    {"stopSound", Support::Adapted, 1, "FunkinLua.hx:1319",
     "fmlSounds.get(tag).stop() y se quita del registro",
     "el motor exige `tag.length > 1`: una etiqueta de un solo caracter NO hace "
     "nada, y eso se reproduce", "stopSound"},
    {"pauseSound", Support::Adapted, 1, "FunkinLua.hx:1325",
     "fmlSounds.get(tag).pause()", nullptr, "pauseSound"},
    {"resumeSound", Support::Adapted, 1, "FunkinLua.hx:1330",
     "fmlSounds.get(tag).play()",
     "si: el motor llama a `play()`, no a `resume()` (`FunkinLua.hx:1332`)", "resumeSound"},

    // ---- funciones del propio mod, no del motor -----------------------------
    // Estan definidas DENTRO de los mods -`setupCrowd` en 11 archivos,
    // `ifExists` en 21, `getCamString` en 4-, asi que no son una capacidad que
    // falte: son llamadas entre archivos, y eso lo resuelve el grafo del
    // proyecto, no una entrada de catalogo. Se listan para que no aparezcan
    // como "sin catalogar" y hagan bulto donde no toca.
    {"setupCrowd", Support::Pending, 0, "(definida en el propio mod)",
     "no es API del motor: la define el mod en 11 de sus archivos",
     "se resuelve siguiendo la llamada entre archivos, no adaptandola"},
    {"ifExists", Support::Pending, 0, "(definida en el propio mod)",
     "no es API del motor: la define el mod en 21 de sus archivos", nullptr},
    {"getCamString", Support::Pending, 0, "(definida en el propio mod)",
     "no es API del motor: la define el mod en 4 de sus archivos", nullptr},
    {"getCamProperty", Support::Pending, 0, "(definida en el propio mod)",
     "no es API del motor", nullptr},
    {"makeLuaWiiText", Support::Pending, 0, "(definida en el propio mod)",
     "no es API del motor", nullptr},

    // ---- matematicas que Haxe no llama igual --------------------------------
    {"math.clamp", Support::Adapted, 3, "(helper de Lua del mod o de Psych)",
     "FlxMath.bound(valor, minimo, maximo) (Script.hx: FlxMath)",
     "`Math` de Haxe no tiene `clamp`: emitir `Math.clamp` no compilaria"},
    {"math.sin", Support::NotEngine, 1, "(builtin de Lua)", nullptr, nullptr},
    {"math.cos", Support::NotEngine, 1, "(builtin de Lua)", nullptr, nullptr},
    {"math.random", Support::NotEngine, 0, "(builtin de Lua)", nullptr,
     "`Math.random()` de Haxe da 0..1 y no acepta rango: no son la misma "
     "funcion aunque se llamen igual"},

    // ---- la cola: funciones sueltas, medidas una a una ---------------------
    {"debugPrint", Support::Adapted, 1, "FunkinLua.hx:1400",
     "trace(...)", nullptr, "debugPrint"},
    {"getRandomInt", Support::Adapted, 1, "ExtraFunctions.hx:265",
     "FlxG.random.int(min, max)",
     "Psych admite ademas una lista de exclusiones; con ella no se emite", "getRandomInt"},
    {"getRandomFloat", Support::Adapted, 1, "ExtraFunctions.hx:274",
     "FlxG.random.float(min, max)", nullptr, "getRandomFloat"},
    {"setTextFont", Support::Adapted, 2, "TextFunctions.hx:77",
     "tag.font = Paths.font(nombre)", nullptr, "setTextFont"},
    {"removeLuaSprite", Support::Adapted, 1, "FunkinLua.hx:1117",
     "remove(tag), y destroy() si el segundo argumento lo pide",
     "por omision Psych DESTRUYE el sprite (`:1117`), no solo lo saca", "removeLuaSprite"},
    {"screenCenter", Support::Adapted, 1, "FunkinLua.hx:1191",
     "objeto.screenCenter(FlxAxes.XY | X | Y)", nullptr, "screenCenter"},
    {"math.pow", Support::NotEngine, 2, "(builtin de Lua)", nullptr, nullptr},
    {"math.lerp", Support::Adapted, 3, "(helper de Lua)",
     "FlxMath.lerp(a, b, t)",
     "`Math` de Haxe no tiene `lerp`: emitirlo tal cual no compilaria"},
    {"table.insert", Support::Adapted, 2, "(builtin de Lua)",
     "array.push(valor)",
     "solo la forma de dos argumentos. Con posicion, `table.insert(t, i, v)` "
     "inserta en base 1 y seria `t.insert(i - 1, v)`: no se emite hasta poder "
     "demostrar que `t` es una tabla de este script"},

    // ---- lo que NO, y por que ----------------------------------------------
    {"addCharacterToList", Support::Pending, 1, "FunkinLua.hx:732",
     "Psych precarga un personaje en su lista; Codename los resuelve al montar "
     "el escenario", nullptr, "addCharacterToList",
     "la capa no precarga: el personaje se crea cuando `Change Character` lo pide"},
    {"addInstance", Support::Pending, 1, "ReflectionFunctions.hx:244",
     "anade por reflexion un objeto nombrado en tiempo de ejecucion", nullptr, "addInstance"},
    {"createInstance", Support::Pending, 2, "ReflectionFunctions.hx:219",
     "Type.resolveClass + Type.createInstance; si la clase no esta en Codename se dice", nullptr, "createInstance",
     "la clase se resuelve en ejecucion: si Codename no la compila, queda pendiente con su nombre"},
    {"instanceArg", Support::Pending, 1, "ReflectionFunctions.hx:261",
     "marca de argumento-objeto que la capa resuelve al llamar", nullptr, "instanceArg"},
    {"string.format", Support::Pending, 1, "(builtin de Lua)",
     "Haxe no tiene un equivalente directo de la cadena de formato de Lua",
     nullptr},
    {"string.find", Support::Pending, 2, "(builtin de Lua)",
     "`string.find` devuelve posiciones en base 1 y admite patrones; "
     "`indexOf` no es lo mismo", nullptr},

    // ---- del propio mod, no del motor --------------------------------------
    {"getIconColor", Support::Pending, 0, "(definida en el propio mod)",
     "no es API del motor", nullptr},
    {"setCamProperty", Support::Pending, 0, "(definida en el propio mod)",
     "no es API del motor", nullptr},
    {"tweenNumber", Support::Pending, 0, "(definida en el propio mod)",
     "no es API del motor", nullptr},
    {"tableCopy", Support::Pending, 0, "(definida en el propio mod)",
     "no es API del motor", nullptr},
    {"shiftCamNote", Support::Pending, 0, "(definida en el propio mod)",
     "no es API del motor", nullptr},
    {"point.new", Support::Pending, 0, "(helper del propio mod)",
     "construye un punto propio del mod, no un FlxPoint", nullptr},

    // ---- lo que se parece pero no es lo mismo ------------------------------
    {"objectPlayAnimation", Support::Approximate, 2,
     "DeprecatedFunctions.hx:19",
     "`tag.playAnim(nombre, forzado)`",
     "Psych la marca DEPRECADA en su propio fuente; el mod que la usa esta "
     "sobre una API que su motor ya no recomienda", "objectPlayAnimation"},
    {"precacheImage", Support::Approximate, 1, "FunkinLua.hx:740",
     "Codename precarga por carpeta al abrir el escenario",
     "no hay una llamada equivalente: el efecto se consigue solo, y forzarlo "
     "seria inventarse una API", "precacheImage"},
    {"precacheSound", Support::Approximate, 1, "FunkinLua.hx:743",
     "igual que precacheImage", nullptr, "precacheSound"},

    // ---- lo que NO tiene equivalente comprobado ----------------------------
    {"getObjectOrder", Support::Pending, 1, "FunkinLua.hx:455",
     "sin equivalente: el orden vive en el XML y no se consulta en ejecucion",
     "devuelve un numero que el script suele usar para recolocar; hay que "
     "leer que hace con el", "getObjectOrder",
     "la capa devuelve `members.indexOf(objeto)`, que cuenta objetos distintos "
     "de los de Psych"},
    {"getPropertyFromClass", Support::Adapted, 2, "ReflectionFunctions.hx:33",
     "FlxG.<campo> y Conductor.<campo>; las demas clases no",
     "mismo limite que setPropertyFromClass", "getPropertyFromClass"},

    // ---- lo que ni siquiera es del motor -----------------------------------
    {"type", Support::NotEngine, 1, "(builtin de Lua)", nullptr, nullptr},
    {"getfenv", Support::NotEngine, 0, "(builtin de Lua)", nullptr,
     "toca el entorno del propio interprete; no viaja a ningun sitio"},
    {"ipairs", Support::NotEngine, 1, "(builtin de Lua)", nullptr, nullptr},
    {"pairs", Support::NotEngine, 1, "(builtin de Lua)", nullptr, nullptr},
    {"tostring", Support::NotEngine, 1, "(builtin de Lua)", nullptr, nullptr},
    {"tonumber", Support::NotEngine, 1, "(builtin de Lua)", nullptr, nullptr},
    {"math.sqrt", Support::NotEngine, 1, "(builtin de Lua)", nullptr, nullptr},
    {"math.floor", Support::NotEngine, 1, "(builtin de Lua)", nullptr, nullptr},
    {"math.abs", Support::NotEngine, 1, "(builtin de Lua)", nullptr, nullptr},
    {"math.min", Support::NotEngine, 2, "(builtin de Lua)", nullptr, nullptr},
    {"math.max", Support::NotEngine, 2, "(builtin de Lua)", nullptr, nullptr},

    // ---- lo que el plan prohibe expresamente resolver inventando -----------
    {"runHaxeCode", Support::Pending, 1, "HScript.hx (implementacion propia)",
     "no se convierte: el codigo se construye en caliente",
     "el bloque literal puede pasar por el analizador Haxe; lo construido "
     "dinamicamente NO (PORTEO_ASISTIDO_PLAN seccion C)"},
    {"runHaxeFunction", Support::Pending, 1, "HScript.hx",
     "no se convierte: llama a una funcion definida en un bloque de runHaxeCode", nullptr},
    {"addHaxeLibrary", Support::Pending, 1, "HScript.hx",
     "no se convierte: nombra clases del propio Psych", nullptr},
    {"callMethod", Support::Pending, 1, "ReflectionFunctions.hx:200",
     "el metodo se resuelve por reflexion en ejecucion (Reflect.callMethod)", nullptr, "callMethod",
     "el metodo se resuelve en ejecucion: si el objeto o el metodo no existen en Codename queda pendiente con su nombre"},
    {"callMethodFromClass", Support::Pending, 2, "ReflectionFunctions.hx:215",
     "el metodo se resuelve por reflexion en ejecucion", nullptr, "callMethodFromClass",
     "igual que callMethod"},
    {"callOnLuas", Support::Adapted, 1, "FunkinLua.hx",
     "PlayState.instance.scripts.call(nombre, args) (ScriptPack.call)",
     "no es un apano: `ScriptPack.call` recorre los scripts activos y llama la "
     "funcion en cada uno, que es exactamente lo que hace `callOnLuas`. "
     "`PlayState.instance` es publico (`PlayState.hx:48`), `scripts` tambien "
     "(`:98`) y `PlayState` esta expuesto a todo script (`Script.hx:86`). Es lo "
     "que sostiene la secuencia entre archivos de Boxing Match WG", "callOnLuas"},
    {"callOnHScript", Support::Adapted, 1, "FunkinLua.hx",
     "PlayState.instance.scripts.call(nombre, args)", nullptr, "callOnHScript"},

    // ======================================================================
    // LA CAPA DE COMPATIBILIDAD (segunda tanda, 15 sep 2026). Lineas de
    // `FNF-PsychEngine-main/source/psychlua/`. `support` sigue diciendo que
    // puede el porteo autonomo (nada, salvo lo ya adaptado arriba); `compat`
    // dice que la capa lo implementa sobre Codename.
    // ======================================================================

    // ---- propiedades y reflexion -------------------------------------------
    {"setPropertyLuaSprite", Support::Pending, 3, "DeprecatedFunctions.hx:125",
     "deprecada en Psych: es setProperty sobre un sprite de Lua", nullptr, "setPropertyLuaSprite"},
    {"getPropertyLuaSprite", Support::Pending, 2, "DeprecatedFunctions.hx:110",
     "deprecada en Psych: es getProperty sobre un sprite de Lua", nullptr, "getPropertyLuaSprite"},
    {"addToGroup", Support::Pending, 2, "ReflectionFunctions.hx:136",
     "grupo.insert(pos, objeto) por reflexion", nullptr, "addToGroup"},
    {"removeFromGroup", Support::Pending, 2, "ReflectionFunctions.hx:164",
     "grupo.remove(miembro) por reflexion", nullptr, "removeFromGroup"},
    {"setVar", Support::Adapted, 2, "FunkinLua.hx:286",
     "mapa compartido de variables entre scripts", nullptr, "setVar"},
    {"getVar", Support::Adapted, 1, "FunkinLua.hx:290",
     "mapa compartido de variables entre scripts", nullptr, "getVar"},

    // ---- objetos ------------------------------------------------------------
    {"loadGraphic", Support::Pending, 2, "FunkinLua.hx:399",
     "sprite.loadGraphic(Paths.image(x), animado, ancho, alto)", nullptr, "loadGraphic"},
    {"loadFrames", Support::Pending, 2, "FunkinLua.hx:413",
     "sprite.frames = Paths.getFrames(x)", nullptr, "loadFrames"},
    {"setGraphicSize", Support::Pending, 2, "FunkinLua.hx:1040",
     "sprite.setGraphicSize(x, y) y updateHitbox", nullptr, "setGraphicSize"},
    {"updateHitbox", Support::Pending, 1, "FunkinLua.hx:1082",
     "sprite.updateHitbox()", nullptr, "updateHitbox"},
    {"setBlendMode", Support::Pending, 2, "FunkinLua.hx:1171",
     "sprite.blend = BlendMode", nullptr, "setBlendMode"},
    {"addAnimationByIndices", Support::Pending, 4, "FunkinLua.hx:984",
     "sprite.animation.addByIndices(...)", nullptr, "addAnimationByIndices"},
    {"addAnimationByIndicesLoop", Support::Pending, 4, "DeprecatedFunctions.hx:14",
     "deprecada: addAnimationByIndices con loop", nullptr, "addAnimationByIndicesLoop"},
    {"addAnimation", Support::Pending, 3, "FunkinLua.hx:980",
     "sprite.animation.add(nombre, frames, fps, loop)", nullptr, "addAnimation"},
    {"addOffset", Support::Pending, 4, "FunkinLua.hx:1004",
     "sprite.addOffset(anim, x, y)", nullptr, "addOffset"},
    {"playAnim", Support::Pending, 2, "FunkinLua.hx:988",
     "sprite.animation.play(nombre, forzado, alreves, frame) / personaje.playAnim", nullptr, "playAnim"},
    {"luaSpriteExists", Support::Pending, 1, "FunkinLua.hx:1119",
     "el registro por etiqueta de la capa", nullptr, "luaSpriteExists"},
    {"luaTextExists", Support::Pending, 1, "FunkinLua.hx:1123",
     "el registro por etiqueta de la capa", nullptr, "luaTextExists"},
    {"luaSoundExists", Support::Pending, 1, "FunkinLua.hx:1127",
     "el registro por etiqueta de la capa", nullptr, "luaSoundExists"},
    {"getMidpointX", Support::Pending, 1, "FunkinLua.hx:869", "objeto.getMidpoint().x", nullptr, "getMidpointX"},
    {"getMidpointY", Support::Pending, 1, "FunkinLua.hx:879", "objeto.getMidpoint().y", nullptr, "getMidpointY"},
    {"getGraphicMidpointX", Support::Pending, 1, "FunkinLua.hx:889", "objeto.getGraphicMidpoint().x", nullptr, "getGraphicMidpointX"},
    {"getGraphicMidpointY", Support::Pending, 1, "FunkinLua.hx:899", "objeto.getGraphicMidpoint().y", nullptr, "getGraphicMidpointY"},
    {"getScreenPositionX", Support::Pending, 1, "FunkinLua.hx:909", "objeto.getScreenPosition().x", nullptr, "getScreenPositionX"},
    {"getScreenPositionY", Support::Pending, 1, "FunkinLua.hx:919", "objeto.getScreenPosition().y", nullptr, "getScreenPositionY"},
    {"objectsOverlap", Support::Pending, 2, "FunkinLua.hx:1219", "FlxG.overlap(a, b)", nullptr, "objectsOverlap"},
    {"scaleLuaSprite", Support::Pending, 3, "DeprecatedFunctions.hx:100", "deprecada: scaleObject", nullptr, "scaleLuaSprite"},
    {"setLuaSpriteCamera", Support::Pending, 2, "DeprecatedFunctions.hx:83", "deprecada: setObjectCamera", nullptr, "setLuaSpriteCamera"},
    {"setLuaSpriteScrollFactor", Support::Pending, 3, "DeprecatedFunctions.hx:92", "deprecada: setScrollFactor", nullptr, "setLuaSpriteScrollFactor"},
    {"luaSpriteMakeGraphic", Support::Pending, 3, "DeprecatedFunctions.hx:47", "deprecada: makeGraphic", nullptr, "luaSpriteMakeGraphic"},
    {"luaSpriteAddAnimationByPrefix", Support::Pending, 3, "DeprecatedFunctions.hx:52", "deprecada: addAnimationByPrefix", nullptr, "luaSpriteAddAnimationByPrefix"},
    {"luaSpriteAddAnimationByIndices", Support::Pending, 4, "DeprecatedFunctions.hx:62", "deprecada: addAnimationByIndices", nullptr, "luaSpriteAddAnimationByIndices"},
    {"luaSpritePlayAnimation", Support::Pending, 2, "DeprecatedFunctions.hx:77", "deprecada: objectPlayAnimation", nullptr, "luaSpritePlayAnimation"},

    // ---- tweens y temporizadores ---------------------------------------------
    {"startTween", Support::Pending, 4, "FunkinLua.hx:501",
     "FlxTween.tween(objeto, valores, duracion, opciones)", nullptr, "startTween"},
    {"noteTweenX", Support::Pending, 4, "FunkinLua.hx:604", "tween sobre strumLines.members[..].members[i]", nullptr, "noteTweenX"},
    {"noteTweenAngle", Support::Pending, 4, "FunkinLua.hx:610", "tween sobre el receptor", nullptr, "noteTweenAngle"},
    {"noteTweenAlpha", Support::Pending, 4, "FunkinLua.hx:613", "tween sobre el receptor", nullptr, "noteTweenAlpha"},
    {"noteTweenDirection", Support::Pending, 4, "FunkinLua.hx:616", "tween sobre `direction` del receptor", nullptr, "noteTweenDirection",
     "Codename no tiene `direction` en el receptor: la capa gira el receptor (angle)"},

    // ---- texto ---------------------------------------------------------------
    {"removeLuaText", Support::Pending, 1, "TextFunctions.hx:189", "remove(tag) y destroy", nullptr, "removeLuaText"},
    {"setTextWidth", Support::Pending, 2, "TextFunctions.hx:42", "tag.fieldWidth = valor", nullptr, "setTextWidth"},
    {"setTextHeight", Support::Pending, 2, "TextFunctions.hx:53", "tag.height = valor", nullptr, "setTextHeight"},
    {"setTextAutoSize", Support::Pending, 2, "TextFunctions.hx:64", "tag.autoSize = valor", nullptr, "setTextAutoSize"},
    {"setTextBorder", Support::Pending, 3, "TextFunctions.hx:75", "tag.borderSize / borderColor / borderStyle", nullptr, "setTextBorder"},
    {"setTextColor", Support::Pending, 2, "TextFunctions.hx:90", "tag.color = FlxColor.fromString", nullptr, "setTextColor"},
    {"setTextItalic", Support::Pending, 2, "TextFunctions.hx:112", "tag.italic = valor", nullptr, "setTextItalic"},
    {"getTextString", Support::Pending, 1, "TextFunctions.hx:144", "tag.text", nullptr, "getTextString"},
    {"getTextSize", Support::Pending, 1, "TextFunctions.hx:154", "tag.size", nullptr, "getTextSize"},
    {"getTextFont", Support::Pending, 1, "TextFunctions.hx:164", "tag.font", nullptr, "getTextFont"},
    {"getTextWidth", Support::Pending, 1, "TextFunctions.hx:174", "tag.fieldWidth", nullptr, "getTextWidth"},

    // ---- personajes ----------------------------------------------------------
    {"characterDance", Support::Pending, 1, "FunkinLua.hx:929", "personaje.dance()", nullptr, "characterDance"},
    {"getCharacterX", Support::Pending, 1, "FunkinLua.hx:773", "personaje.x", nullptr, "getCharacterX"},
    {"getCharacterY", Support::Pending, 1, "FunkinLua.hx:793", "personaje.y", nullptr, "getCharacterY"},
    {"setCharacterX", Support::Pending, 2, "FunkinLua.hx:783", "personaje.x = valor", nullptr, "setCharacterX"},
    {"setCharacterY", Support::Pending, 2, "FunkinLua.hx:803", "personaje.y = valor", nullptr, "setCharacterY"},

    // ---- camara --------------------------------------------------------------
    {"cameraShake", Support::Pending, 3, "FunkinLua.hx:837", "camara.shake(intensidad, duracion)", nullptr, "cameraShake"},
    {"cameraFlash", Support::Pending, 3, "FunkinLua.hx:841", "camara.flash(color, duracion, null, forzado)", nullptr, "cameraFlash"},
    {"cameraFade", Support::Pending, 3, "FunkinLua.hx:844", "camara.fade(color, duracion, false, null, forzado)", nullptr, "cameraFade"},
    {"cameraSetTarget", Support::Pending, 1, "FunkinLua.hx:813",
     "curCameraTarget = strumline del personaje (`PlayState.hx:509`)", nullptr, "cameraSetTarget"},
    {"setCameraFollowPoint", Support::Pending, 2, "FunkinLua.hx:826",
     "camFollow.setPosition con curCameraTarget = -1 (`PlayState.hx:1621-1630`)", nullptr, "setCameraFollowPoint"},
    {"addCameraFollowPoint", Support::Pending, 2, "FunkinLua.hx:828", "camFollow.x/y += valor", nullptr, "addCameraFollowPoint"},
    {"setCameraScroll", Support::Pending, 2, "FunkinLua.hx:825", "FlxG.camera.scroll.set(x, y)", nullptr, "setCameraScroll"},
    {"addCameraScroll", Support::Pending, 2, "FunkinLua.hx:827", "FlxG.camera.scroll.x/y += valor", nullptr, "addCameraScroll"},
    {"getCameraFollowX", Support::Pending, 0, "FunkinLua.hx:834", "camFollow.x", nullptr, "getCameraFollowX"},
    {"getCameraFollowY", Support::Pending, 0, "FunkinLua.hx:835", "camFollow.y", nullptr, "getCameraFollowY"},
    {"getCameraScrollX", Support::Pending, 0, "FunkinLua.hx:832", "FlxG.camera.scroll.x", nullptr, "getCameraScrollX"},
    {"getCameraScrollY", Support::Pending, 0, "FunkinLua.hx:833", "FlxG.camera.scroll.y", nullptr, "getCameraScrollY"},

    // ---- audio ---------------------------------------------------------------
    {"playMusic", Support::Pending, 1, "FunkinLua.hx:1314", "FlxG.sound.playMusic(Paths.music(x), volumen, loop)", nullptr, "playMusic"},
    {"soundFadeIn", Support::Pending, 2, "FunkinLua.hx:1384", "sonido.fadeIn(duracion, desde, hasta)", nullptr, "soundFadeIn"},
    {"soundFadeOut", Support::Pending, 2, "FunkinLua.hx:1398", "sonido.fadeOut(duracion, hasta)", nullptr, "soundFadeOut"},
    {"soundFadeCancel", Support::Pending, 1, "FunkinLua.hx:1412", "sonido.fadeTween.cancel()", nullptr, "soundFadeCancel"},
    {"getSoundVolume", Support::Pending, 1, "FunkinLua.hx:1426", "sonido.volume", nullptr, "getSoundVolume"},
    {"setSoundVolume", Support::Pending, 2, "FunkinLua.hx:1440", "sonido.volume = valor", nullptr, "setSoundVolume"},
    {"getSoundTime", Support::Pending, 1, "FunkinLua.hx:1457", "sonido.time", nullptr, "getSoundTime"},
    {"setSoundTime", Support::Pending, 2, "FunkinLua.hx:1466", "sonido.time = valor", nullptr, "setSoundTime"},
    {"getSoundPitch", Support::Pending, 1, "FunkinLua.hx:1482", "sonido.pitch", nullptr, "getSoundPitch"},
    {"setSoundPitch", Support::Pending, 2, "FunkinLua.hx:1492", "sonido.pitch = valor", nullptr, "setSoundPitch"},
    {"precacheMusic", Support::Pending, 1, "FunkinLua.hx:721", "FlxG.sound.load(Paths.music(x))", nullptr, "precacheMusic"},
    {"musicFadeIn", Support::Pending, 1, "DeprecatedFunctions.hx:143", "deprecada: inst.fadeIn", nullptr, "musicFadeIn"},
    {"musicFadeOut", Support::Pending, 1, "DeprecatedFunctions.hx:148", "deprecada: inst.fadeOut", nullptr, "musicFadeOut"},

    // ---- puntuacion y salud ---------------------------------------------------
    {"setHealth", Support::Pending, 1, "FunkinLua.hx:696", "health = valor (`PlayState.hx:266`)", nullptr, "setHealth"},
    {"addHealth", Support::Pending, 1, "FunkinLua.hx:697", "health += valor", nullptr, "addHealth"},
    {"getHealth", Support::Pending, 0, "FunkinLua.hx:698", "health", nullptr, "getHealth"},
    {"addScore", Support::Pending, 1, "FunkinLua.hx:672", "songScore += valor (`PlayState.hx:324`)", nullptr, "addScore"},
    {"setScore", Support::Pending, 1, "FunkinLua.hx:684", "songScore = valor", nullptr, "setScore"},
    {"addMisses", Support::Pending, 1, "FunkinLua.hx:676", "misses += valor (`PlayState.hx:328`)", nullptr, "addMisses"},
    {"setMisses", Support::Pending, 1, "FunkinLua.hx:688", "misses = valor", nullptr, "setMisses"},
    {"addHits", Support::Pending, 1, "FunkinLua.hx:680", "sin contador propio en Codename", nullptr, "addHits",
     "Codename cuenta aciertos por calificacion (`hits`), no un total: la capa lleva el suyo"},
    {"setHits", Support::Pending, 1, "FunkinLua.hx:692", "igual que addHits", nullptr, "setHits",
     "igual que addHits"},
    {"setRatingPercent", Support::Pending, 1, "FunkinLua.hx:847", "sin equivalente directo: la precision se calcula", nullptr, "setRatingPercent",
     "Codename recalcula la precision de las notas: la capa la guarda para el texto de la HUD de Psych"},
    {"setRatingName", Support::Pending, 1, "FunkinLua.hx:851", "igual que setRatingPercent", nullptr, "setRatingName",
     "igual que setRatingPercent"},
    {"setRatingFC", Support::Pending, 1, "FunkinLua.hx:855", "igual que setRatingPercent", nullptr, "setRatingFC",
     "igual que setRatingPercent"},
    {"updateScoreText", Support::Pending, 0, "FunkinLua.hx:859", "updateRatingStuff() (`PlayState.hx:1408`)", nullptr, "updateScoreText"},
    {"setHealthBarColors", Support::Pending, 2, "FunkinLua.hx:1132", "healthBar.createFilledBar(izq, der)", nullptr, "setHealthBarColors"},
    {"setTimeBarColors", Support::Pending, 2, "FunkinLua.hx:1141", "la barra de tiempo es de la capa", nullptr, "setTimeBarColors",
     "Codename no trae barra de tiempo: la crea la capa al primer acceso"},

    // ---- flujo ---------------------------------------------------------------
    {"startCountdown", Support::Pending, 0, "FunkinLua.hx:732", "PlayState.instance.startCountdown() (`PlayState.hx:1023`)", nullptr, "startCountdown"},
    {"endSong", Support::Pending, 0, "FunkinLua.hx:736", "PlayState.instance.endSong() (`PlayState.hx:1775`)", nullptr, "endSong"},
    {"restartSong", Support::Pending, 0, "FunkinLua.hx:741", "FlxG.resetState()", nullptr, "restartSong"},
    {"exitSong", Support::Pending, 0, "FunkinLua.hx:747", "FlxG.switchState(FreeplayState o StoryMenuState)", nullptr, "exitSong"},
    {"startVideo", Support::Pending, 1, "FunkinLua.hx:1282",
     "VideoCutscene / FlxVideoSprite; llega en la etapa de cinematicas", nullptr},
    {"startDialogue", Support::Pending, 1, "FunkinLua.hx:1242",
     "DialogueCutscene con dialogue.xml; llega en la etapa de cinematicas", nullptr},
    {"openCustomSubstate", Support::Pending, 1, "CustomSubstate.hx:14", "sin equivalente en Codename", nullptr},
    {"closeCustomSubstate", Support::Pending, 0, "CustomSubstate.hx:15", "sin equivalente en Codename", nullptr},
    {"insertToCustomSubstate", Support::Pending, 1, "CustomSubstate.hx:16", "sin equivalente en Codename", nullptr},
    {"close", Support::Pending, 0, "FunkinLua.hx (close)", "disableScript() del propio script", nullptr, "close",
     "la capa desactiva el script que llama; Psych ademas lo destruye"},

    // ---- comunicacion entre scripts ------------------------------------------
    {"callOnScripts", Support::Pending, 1, "FunkinLua.hx (callOnScripts)", "PlayState.instance.scripts.call(nombre, args)", nullptr, "callOnScripts"},
    {"callScript", Support::Pending, 2, "FunkinLua.hx:252", "scripts.getByPath(x).call(nombre, args)", nullptr, "callScript"},
    {"addLuaScript", Support::Pending, 1, "FunkinLua.hx:294",
     "importScript sobre el .hx instalado con ese nombre (`ScriptPack.hx:70`)", nullptr, "addLuaScript"},
    {"addHScript", Support::Pending, 1, "FunkinLua.hx:311", "igual que addLuaScript", nullptr, "addHScript"},
    {"removeLuaScript", Support::Pending, 1, "FunkinLua.hx:332", "el script se desactiva (`active = false`)", nullptr, "removeLuaScript"},
    {"removeHScript", Support::Pending, 1, "FunkinLua.hx:352", "igual que removeLuaScript", nullptr, "removeHScript"},
    {"isRunning", Support::Pending, 1, "FunkinLua.hx:265", "scripts.contains(ruta instalada)", nullptr, "isRunning"},
    {"getRunningScripts", Support::Pending, 0, "FunkinLua.hx:212", "los nombres de los scripts activos del pack", nullptr, "getRunningScripts"},

    // ---- sistema y utilidades ---------------------------------------------------
    {"getRandomBool", Support::Pending, 0, "ExtraFunctions.hx:246", "FlxG.random.bool(probabilidad)", nullptr, "getRandomBool"},
    {"getColorFromName", Support::Pending, 1, "FunkinLua.hx:702", "FlxColor.fromString", nullptr, "getColorFromName"},
    {"getColorFromString", Support::Pending, 1, "FunkinLua.hx:703", "FlxColor.fromString", nullptr, "getColorFromString"},
    {"stringStartsWith", Support::Pending, 2, "ExtraFunctions.hx:212", "StringTools.startsWith", nullptr, "stringStartsWith"},
    {"stringEndsWith", Support::Pending, 2, "ExtraFunctions.hx:215", "StringTools.endsWith", nullptr, "stringEndsWith"},
    {"stringSplit", Support::Pending, 2, "ExtraFunctions.hx:218", "cadena.split(separador)", nullptr, "stringSplit"},
    {"stringTrim", Support::Pending, 1, "ExtraFunctions.hx:221", "StringTools.trim", nullptr, "stringTrim"},
    {"checkFileExists", Support::Pending, 1, "ExtraFunctions.hx:151", "Assets.exists(Paths.getPath(x))", nullptr, "checkFileExists"},
    {"getTextFromFile", Support::Pending, 1, "ExtraFunctions.hx:194", "Assets.getText(Paths.getPath(x))", nullptr, "getTextFromFile"},
    {"getModSetting", Support::Pending, 1, "(ModSettings)", "sin ajustes de mod en Codename", nullptr, "getModSetting",
     "la capa devuelve el valor por defecto que el mod declare en data/settings.json, o null"},
    {"keyJustPressed", Support::Pending, 1, "ExtraFunctions.hx:61", "FlxG.keys.justPressed.<tecla>", nullptr, "keyJustPressed"},
    {"keyPressed", Support::Pending, 1, "ExtraFunctions.hx:72", "FlxG.keys.pressed.<tecla>", nullptr, "keyPressed"},
    {"keyReleased", Support::Pending, 1, "ExtraFunctions.hx:83", "FlxG.keys.justReleased.<tecla>", nullptr, "keyReleased"},
    {"keyboardJustPressed", Support::Pending, 1, "ExtraFunctions.hx:17", "FlxG.keys.justPressed.<tecla>", nullptr, "keyboardJustPressed"},
    {"keyboardPressed", Support::Pending, 1, "ExtraFunctions.hx:18", "FlxG.keys.pressed.<tecla>", nullptr, "keyboardPressed"},
    {"keyboardReleased", Support::Pending, 1, "ExtraFunctions.hx:19", "FlxG.keys.justReleased.<tecla>", nullptr, "keyboardReleased"},
    {"mouseClicked", Support::Pending, 0, "FunkinLua.hx:619", "FlxG.mouse.justPressed", nullptr, "mouseClicked"},
    {"mousePressed", Support::Pending, 0, "FunkinLua.hx:630", "FlxG.mouse.pressed", nullptr, "mousePressed"},
    {"mouseReleased", Support::Pending, 0, "FunkinLua.hx:641", "FlxG.mouse.justReleased", nullptr, "mouseReleased"},
    {"getMouseX", Support::Pending, 0, "FunkinLua.hx:860", "FlxG.mouse.getScreenPosition(camara).x", nullptr, "getMouseX"},
    {"getMouseY", Support::Pending, 0, "FunkinLua.hx:864", "FlxG.mouse.getScreenPosition(camara).y", nullptr, "getMouseY"},
    {"initSaveData", Support::Pending, 1, "ExtraFunctions.hx:96", "FlxSave propio con prefijo", nullptr, "initSaveData"},
    {"flushSaveData", Support::Pending, 1, "ExtraFunctions.hx:108", "save.flush()", nullptr, "flushSaveData"},
    {"getDataFromSave", Support::Pending, 2, "ExtraFunctions.hx:117", "Reflect.field(save.data, campo)", nullptr, "getDataFromSave"},
    {"setDataFromSave", Support::Pending, 3, "ExtraFunctions.hx:130", "Reflect.setField(save.data, campo, valor)", nullptr, "setDataFromSave"},

    // ---- shaders: se marcan adaptados solo tras verificarse en el motor -------
    {"initLuaShader", Support::Pending, 1, "ShaderFunctions.hx (initLuaShader)", "new CustomShader(nombre) (`Script.hx:105`)", nullptr, "initLuaShader",
     "pendiente de verificar en el motor real"},
    {"setSpriteShader", Support::Pending, 2, "ShaderFunctions.hx (setSpriteShader)", "sprite.shader = shader", nullptr, "setSpriteShader",
     "pendiente de verificar en el motor real"},
    {"removeSpriteShader", Support::Pending, 1, "ShaderFunctions.hx:50", "sprite.shader = null", nullptr, "removeSpriteShader"},
    {"setShaderFloat", Support::Pending, 3, "ShaderFunctions.hx:211", "shader.<uniforme> = valor", nullptr, "setShaderFloat"},
    {"setShaderFloatArray", Support::Pending, 3, "ShaderFunctions.hx:226", "shader.<uniforme> = valores", nullptr, "setShaderFloatArray"},
    {"setShaderInt", Support::Pending, 3, "ShaderFunctions.hx:181", "shader.<uniforme> = valor", nullptr, "setShaderInt"},
    {"setShaderIntArray", Support::Pending, 3, "ShaderFunctions.hx:196", "shader.<uniforme> = valores", nullptr, "setShaderIntArray"},
    {"setShaderBool", Support::Pending, 3, "ShaderFunctions.hx:151", "shader.<uniforme> = valor", nullptr, "setShaderBool"},
    {"setShaderBoolArray", Support::Pending, 3, "ShaderFunctions.hx:166", "shader.<uniforme> = valores", nullptr, "setShaderBoolArray"},
    {"getShaderFloat", Support::Pending, 2, "ShaderFunctions.hx:121", "shader.<uniforme>", nullptr, "getShaderFloat"},
    {"getShaderInt", Support::Pending, 2, "ShaderFunctions.hx:93", "shader.<uniforme>", nullptr, "getShaderInt"},
    {"getShaderBool", Support::Pending, 2, "ShaderFunctions.hx:65", "shader.<uniforme>", nullptr, "getShaderBool"},
    {"setShaderSampler2D", Support::Pending, 3, "ShaderFunctions.hx:243", "shader.<uniforme> = bitmap", nullptr},

    // ---- ventana (decision del usuario: simulada salvo que se marque) ----------
    // Va por setPropertyFromClass('openfl.Lib', 'application.window.x', v):
    // la capa lo desvia a `psych.windowSet`, que solo toca `window`
    // (`Script.hx:27`) si la importacion activo los efectos reales.
};

const std::size_t kCount = sizeof(kEntries) / sizeof(kEntries[0]);

}  // namespace

const Entry* find(const std::string& name) {
    for (std::size_t i = 0; i < kCount; ++i)
        if (name == kEntries[i].name) return &kEntries[i];
    return nullptr;
}

std::size_t  size() { return kCount; }
const Entry* at(std::size_t index) { return index < kCount ? &kEntries[index] : nullptr; }

Coverage measure(const std::vector<std::pair<std::string, int>>& counted) {
    Coverage out;
    for (const auto& entry : counted) {
        const int times = entry.second;
        out.calls += times;
        const Entry* found = find(entry.first);
        if (found == nullptr) { out.unknown += times; continue; }
        switch (found->support) {
            case Support::Declarative: out.declarative += times; break;
            case Support::Adapted:     out.adapted += times; break;
            case Support::Approximate: out.approximate += times; break;
            case Support::Pending:     out.pending += times; break;
            case Support::NotEngine:   out.notEngine += times; break;
        }
    }
    return out;
}

Coverage measureWithCompat(const std::vector<std::pair<std::string, int>>& counted) {
    Coverage out;
    for (const auto& entry : counted) {
        const int times = entry.second;
        out.calls += times;
        const Entry* found = find(entry.first);
        if (found == nullptr) { out.unknown += times; continue; }
        if (found->compat != nullptr) {
            if (found->compatNote != nullptr) out.approximate += times;
            else out.adapted += times;
            continue;
        }
        switch (found->support) {
            case Support::NotEngine:   out.notEngine += times; break;
            case Support::Adapted:     out.adapted += times; break;
            case Support::Declarative: out.declarative += times; break;
            case Support::Approximate: out.approximate += times; break;
            case Support::Pending:     out.pending += times; break;
        }
    }
    return out;
}

bool pendingFor(const std::string& name, int line, const std::string& path,
                PortDiagnostic* out) {
    const Entry* found = find(name);
    if (found != nullptr && (found->support == Support::Declarative ||
                             found->support == Support::Adapted ||
                             found->support == Support::NotEngine))
        return false;

    if (out == nullptr) return true;
    out->path = path;
    out->line = line;
    out->affected.push_back(name);
    if (found == nullptr) {
        out->cause = PortCause::MissingCapability;
        out->resolution = PortResolution::ProvideSource;
        out->detail = "'" + name + "': sin equivalente comprobado en el "
                      "catalogo de compatibilidad";
        return true;
    }
    out->cause = PortCause::MissingCapability;
    out->resolution = found->support == Support::Approximate
                          ? PortResolution::Approximate
                          : PortResolution::ChooseAdapter;
    out->detail = std::string("'") + found->name + "': " +
                  (found->codename != nullptr ? found->codename
                                              : "sin equivalente comprobado") +
                  " (" + found->psychSource + ")";
    return true;
}

bool pendingForCompat(const std::string& name, int line, const std::string& path,
                      PortDiagnostic* out) {
    const Entry* found = find(name);
    if (found != nullptr && found->support == Support::NotEngine) return false;
    if (found != nullptr && found->compat != nullptr && found->compatNote == nullptr) return false;
    if (out == nullptr) return true;
    out->path = path;
    out->line = line;
    out->affected.push_back(name);
    out->cause = PortCause::MissingCapability;
    if (found == nullptr) {
        out->resolution = PortResolution::ProvideSource;
        out->detail = "'" + name + "': sin equivalente en la capa de compatibilidad; "
                      "se emite como psych.pending y se dice al ejecutarse";
        return true;
    }
    if (found->compat != nullptr) {
        // La capa lo hace, pero no exactamente como Psych: se dice.
        out->resolution = PortResolution::Approximate;
        out->detail = std::string("'") + found->name + "': " + found->compatNote +
                      " (" + found->psychSource + ")";
        return true;
    }
    out->resolution = found->support == Support::Approximate
                          ? PortResolution::Approximate
                          : PortResolution::ChooseAdapter;
    out->detail = std::string("'") + found->name + "': " +
                  (found->codename != nullptr ? found->codename
                                              : "sin equivalente comprobado") +
                  " (" + found->psychSource + "); se emite como psych.pending";
    return true;
}

}  // namespace psychapi
}  // namespace fml
