// GENERADO por tools/embed_compat.py desde psych-compat/fml-psych-compat.hx.
// No se edita a mano: se edita el .hx y se vuelve a generar.
// sha256 del .hx: b30456aa5755499fd04ee1467823f732645238b92711937f91dc8d0b57a2c574
#include <cstddef>

namespace fml {
namespace psychcompat {

namespace {
const char* const kParts[] = {
R"fmlhx(// LA CAPA DE COMPATIBILIDAD DE PSYCH SOBRE CODENAME. La escribe Funkin Mod Lab.
//
// QUE ES. Los scripts que el importador trae de Psych Engine llaman a la API
// de Psych (`setProperty`, `makeLuaSprite`, `doTweenX`, `runTimer`...). Esta
// capa publica UN nombre, `psych`, con esas funciones implementadas sobre el
// motor de Codename con la semantica de Psych: el mismo orden de argumentos,
// las mismas etiquetas, los mismos callbacks (`onTimerCompleted`,
// `onTweenCompleted`) difundidos a todos los scripts. Los traducidos solo la
// llaman: `psych.setProperty("defaultCamZoom", 0.8)`.
//
// DONDE VIVE. En `songs/`, que Codename carga en toda cancion
// (`PlayState.hx:754`). Se carga la ULTIMA, asi que un script de cancion no
// puede usarla en su nivel superior: el importador iza ese codigo a `create`.
//
// CUANDO NO HACE NADA. Si ningun script del pack declara
// `fmlPsychScript = true`, se desactiva en `create` (`disableScript`,
// `Script.hx:287`): una cancion nativa del mod no paga nada.
//
// LO QUE NO SABE HACER lo dice: `psych.pending("nombre", args)` traza
// `[FML] pending nombre` UNA vez por nombre en la consola del juego y devuelve
// null. Nunca hace otra cosa en silencio.
//
// Cada funcion cita `psychlua/*.hx` de Psych (FNF-PsychEngine-main) y el
// destino en Codename (CodenameEngine-main). ESTO ES GENERADO: se regenera al
// importar si es nuestra version; si lo editas, se conserva.

import flixel.text.FlxTextBorderStyle;
import haxe.Timer;

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
var fmlActive = false;
// Perfil por hook, encendido con las trazas de la capa (`fmlVerbose`): cuanto
// cuesta cada hook de cada script traducido, sumado en ventanas de cinco
// segundos y escrito en la consola del juego. Apagado cuesta un booleano por
// llamada. Es lo que dice por que una cancion importada va a 20 FPS.
public var fmlProfiling = false;
var fmlProfileTotals:Map<String, Float> = [];
var fmlProfileCalls:Map<String, Int> = [];
var fmlProfileWindowStart = -1.0;
var fmlProfileFrames = 0;
var fmlCameraForced = false;
var fmlCameraSectionTarget = 1;
var fmlObjects:Map<String, Dynamic> = [];   // etiqueta -> sprite o texto de Lua
var fmlTweens:Map<String, Dynamic> = [];    // etiqueta -> FlxTween (comun a todos los scripts, como en Psych)
var fmlTimers:Map<String, Dynamic> = [];    // etiqueta -> FlxTimer
var fmlSounds:Map<String, Dynamic> = [];    // etiqueta -> FlxSound
var fmlVars:Map<String, Dynamic> = [];      // setVar / getVar
var fmlPendingSeen:Map<String, Bool> = [];
var fmlWarningSeen:Map<String, Bool> = [];
var fmlHookSeen:Map<String, Bool> = [];
var fmlDeferred:Array<Dynamic> = [];        // escrituras sobre objetos que aun no existen en create
var fmlHudFront:Array<Dynamic> = [];        // sprites de camHUD anadidos en create: van sobre la HUD del motor
var fmlCamOther = null;
var fmlHudObjects:Map<String, Dynamic> = []; // timeBar, timeTxt, timeBarBG, botplayTxt: se crean al primer acceso
var fmlNotesCache:Array<Dynamic> = null;
var fmlNotesCacheFrame = -1;
var fmlSpawnedNotesCache:Array<Dynamic> = null;
var fmlSpawnedNotesCacheFrame = -1;
var fmlIndexedNote = null;
var fmlFrame = 0;
var fmlHits = 0;
var fmlRatingPercent = 0.0;
var fmlRatingName = "?";
var fmlRatingFC = "";
var fmlSave = null;
var fmlSkipCountdown = false;
var fmlPostCreated = false;
var fmlInGameOver = false;
var fmlScriptsMap:Map<String, String> = [];  // clave de Psych -> ruta instalada (fml-psych-scripts.json)
var fmlModSettings = null;

// Las variables globales que Psych mantiene y Codename no tiene con ese
// nombre. Publicas: las ve todo script del pack sin copiarlas una a una
// (`Interp.hx:650`). Las que Codename ya tiene con el mismo nombre
// (`curBeat`, `curStep`, `startedCountdown`, `seenCutscene`, `difficulty`,
// `isStoryMode`, `downscroll`, `health`) NO se publican: sombrearian el campo
// del motor para todos los scripts, incluidos los nativos del mod.
public var psychReady = false;
public var curBpm:Float = 0;
public var crochet:Float = 0;
public var stepCrochet:Float = 0;
public var curSection:Int = 0;
public var curDecStep:Float = 0;
public var curDecBeat:Float = 0;
public var songPos:Float = 0;
public var songLength:Float = 0;
public var songName:String = "";
public var difficultyName:String = "";
public var mustHitSection:Bool = false;
public var altAnim:Bool = false;
public var gfSection:Bool = false;
public var healthGainMult:Float = 1;
public var healthLossMult:Float = 1;
public var screenWidth:Int = 1280;
public var screenHeight:Int = 720;
public var buildTarget:String = "windows";
public var currentModDirectory:String = "";
public var botPlay:Bool = false;
public var middlescroll:Bool = false;
public var hideHud:Bool = false;
public var cameraZoomOnBeat:Bool = true;
public var shadersEnabled:Bool = true;
public var inGameOver:Bool = false;
public var boyfriendName:String = "";
public var dadName:String = "";
public var gfName:String = "";
// Los que Codename tiene con el mismo nombre PERO que un script de v1.0.1 no
// resuelve como variable suelta: se publican y se refrescan cada frame, igual
// que Psych los mantiene (`FunkinLua.hx:87-209`, `PlayState.hx:3209-3274`).
// Escribir `health = x` en un script toca este espejo, no el motor -como en
// Psych-; para cambiar la salud de verdad se usa setProperty/psych.setHealth.
public var bpm:Float = 0;
public var scrollSpeed:Float = 1;
public var curStep:Int = 0;
public var curBeat:Int = 0;
public var difficulty:Int = 0;
public var songNameShort:String = "";
public var combo:Int = 0;
public var score:Int = 0;
public var misses:Int = 0;
public var hits:Int = 0;
public var deaths:Int = 0;
public var rating:Float = 0;
public var ratingName:String = "?";
public var ratingFC:String = "";
public var startedCountdown:Bool = false;
public var seenCutscene:Bool = false;
public var downscroll:Bool = false;
public var isStoryMode:Bool = false;
public var playbackRate:Float = 1;
public var health:Float = 1;
public var curStage:String = "";
public var version:String = "0.7.3";
public var flashingLights:Bool = true;
public var hasVocals:Bool = true;
public var healthBarAlpha:Float = 1;
public var totalPlayed:Int = 0;
public var totalNotesHit:Float = 0;
public var lowQuality:Bool = false;
public var framerate:Int = 60;
public var ghostTapping:Bool = true;
public var noteOffset:Float = 0;
public var instakillOnMiss:Bool = false;
public var practice:Bool = false;
public var cameraSpeed:Float = 1;
public var Function_Stop:String = "Function_Stop";
public var Function_Continue:String = "Function_Continue";
public var Function_StopLua:String = "Function_StopLua";
public var Function_StopHScript:String = "Function_StopHScript";
public var Function_StopAll:String = "Function_StopAll";

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------
function fmlGame() PlayState.instance;

function fmlTrace(?text:String) trace("[FML] " + text);

// RENDIMIENTO. En el hscript de Codename 1.0.1 cada `return` se implementa con
// una excepcion: ~25 us por llamada, frente a ~1.5 us de una funcion que
// termina en una expresion (el valor del bloque es su ultima expresion).
// Medido dentro del motor. Las funciones que se llaman en cada fotograma
// (propiedades, tweens, tipos, avisos) se escriben por eso sin `return`: con
// `if/else` como expresion y el valor al final. Las demas pueden usarlo.

function fmlWarn(?text:String) {
    // Una dependencia ausente debe decirse, no bloquear el motor imprimiendo
    // el mismo aviso desde cada beat o fotograma de un script activo.
    if (fmlVerbose && !fmlWarningSeen.exists(text)) {
        fmlWarningSeen.set(text, true);
        trace("[FML] aviso: " + text);
    }
}

// Lo que Psych no tiene y Codename tampoco: se dice una vez y se sigue.
function fmlPending(?name:String, ?args:Dynamic):Dynamic {
    if (!fmlPendingSeen.exists(name)) {
        fmlPendingSeen.set(name, true);
        trace("[FML] pending " + name);
    }
    null;
}

function fmlHook(?name:String, ?script:String) {
    if (fmlVerbose) {
        var key = script + ":" + name;
        if (!fmlHookSeen.exists(key)) {
            fmlHookSeen.set(key, true);
            trace("[FML] hook " + name + " " + script);
        }
    }
}

function fmlClock():Float Timer.stamp();

// Un bucle traducido que paso el millon de vueltas: en Psych terminaba, aqui
// su condicion depende de algo que no cambia. Se corta con un error que el
// guardian del hook cuenta (a la tercera apaga ese hook) en vez de congelar el
// juego entero hasta cerrarlo desde el Administrador de tareas.
function fmlRunaway(?script:String, ?line:Int) {
    throw "[FML] bucle cortado tras 1000000 vueltas en " + script + ":" + line +
        " (su condicion no cambia aqui como en Psych)";
}

// Lo llama el envoltorio de cada hook traducido cuando el perfil esta activo.
function fmlProfile(?script:String, ?hook:String, ?start:Float) {
    var key = hook + " " + script;
    var ms = (Timer.stamp() - start) * 1000.0;
    fmlProfileTotals.set(key, (fmlProfileTotals.exists(key) ? fmlProfileTotals.get(key) : 0.0) + ms);
    fmlProfileCalls.set(key, (fmlProfileCalls.exists(key) ? fmlProfileCalls.get(key) : 0) + 1);
}

// Con el perfil activo, cada funcion de `psych` se mide tambien por dentro:
// dice que parte de la API (propiedades, eventos, tweens, notas) se come el
// tiempo de los hooks. Su tiempo ya esta incluido en el del hook que la llama.
// El envoltorio no usa `return` (ver RENDIMIENTO): con uno, cada llamada
// medida pagaba una excepcion que el perfil atribuia a la API.
function fmlProfileApi() {
    for (field in Reflect.fields(psych)) {
        var original = Reflect.field(psych, field);
        if (Reflect.isFunction(original) && field != "clock" && field != "profile" && field != "hook" &&
            field != "truthy" && field != "luaOr" && field != "luaAnd") {
            var name = "psych." + field;
            Reflect.setField(psych, field, Reflect.makeVarArgs(function(args:Array<Dynamic>) {
                var start = Timer.stamp();
                var result = Reflect.callMethod(null, original, args);
                fmlProfile(name, "api", start);
                result;
            }));
        }
    }
}

function fmlProfileReport(now:Float) {
    var seconds = now - fmlProfileWindowStart;
    var keys = [];
    for (key in fmlProfileTotals.keys()) keys.push(key);
    keys.sort(function(a, b) {
        var d = fmlProfileTotals.get(b) - fmlProfileTotals.get(a);
        d > 0 ? 1 : (d < 0 ? -1 : 0);
    });
    // El tiempo de la API ya esta dentro del de cada hook que la llama: el
    // total solo suma hooks y la actualizacion de la capa.
    var total = 0.0;
    for (key in keys) if (!StringTools.startsWith(key, "api ")) total += fmlProfileTotals.get(key);
    var frames = fmlProfileFrames > 0 ? fmlProfileFrames : 1;
    trace("[FML] profile " + Std.int(seconds * 10) / 10 + " s, " + fmlProfileFrames + " frames (" +
        Std.int(fmlProfileFrames / seconds) + " fps); hooks " + Std.int(total) + " ms = " +
        Std.int(total / frames * 100) / 100 + " ms/frame");
    var shown = 0;
    for (key in keys) {
        if (shown++ < 20)
            trace("[FML] profile   " + Std.int(fmlProfileTotals.get(key) * 10) / 10 + " ms  x" +
                fmlProfileCalls.get(key) + "  " + key);
    }
    fmlProfileTotals.clear();
    fmlProfileCalls.clear();
    fmlProfileFrames = 0;
    fmlProfileWindowStart = now;
}

// Deteccion de tipo SIN `Type` ni nombres de tipo. El build (12) es Codename
// v1.0.1 y su sandbox de scripts no expone `Type` ni `String`/`Array`/`Float`
// como variables (solo clases del motor, como HudCamera), asi que
// `Type.typeof` y `Std.isOfType(x, Float)` fallan con "Unknown variable". Se
)fmlhx",
R"fmlhx(// deduce con `Reflect` (que si esta) y por operaciones que existen.
//
// COSTE. `Reflect.isObject` es verdadero para objetos, clases, cadenas y
// arrays, y falso para numeros, booleanos y funciones: descarta sin tocar el
// valor. La version anterior sumaba `valor + 0.0` a cualquier cosa, y en hxcpp
// eso convierte un objeto o un array ENTERO en texto (un array de notas, cada
// nota con su toString): era la mayor parte de cada getPropertyFromGroup.
function fmlIsNumber(?value:Dynamic):Bool {
    var ok = false;
    if (value != null && value != true && value != false && !Reflect.isObject(value) && !Reflect.isFunction(value))
        try { ok = (value + 0.0) == value; } catch (e:Dynamic) { ok = false; }
    ok;
}
function fmlIsString(?value:Dynamic):Bool {
    // Una cadena es un objeto con charAt y sin push (un array tiene los dos
    // campos... no: no tiene charAt; un sprite no tiene ninguno).
    var ok = false;
    if (value != null && Reflect.isObject(value))
        try { ok = value.charAt != null && value.push == null; } catch (e:Dynamic) { ok = false; }
    ok;
}
function fmlIsArray(?value:Dynamic):Bool {
    // Un array responde a push/pop; una cadena, un objeto anonimo o un sprite, no.
    var ok = false;
    if (value != null && Reflect.isObject(value))
        try { ok = value.push != null && value.pop != null && value.length != null; } catch (e:Dynamic) { ok = false; }
    ok;
}

// En Lua solo nil y false son falsos.
function fmlTruthy(?value:Dynamic):Bool value != null && value != false;
function fmlLuaOr(?a:Dynamic, ?b:Dynamic):Dynamic fmlTruthy(a) ? a : b;
function fmlLuaAnd(?a:Dynamic, ?b:Dynamic):Dynamic fmlTruthy(a) ? b : a;
function fmlLuaType(?value:Dynamic):String {
    if (value == null) "nil"
    else if (value == true || value == false) "boolean"
    else if (fmlIsNumber(value)) "number"
    else if (fmlIsString(value)) "string"
    else if (Reflect.isFunction(value)) "function"
    else "table";
}

// tonumber de Lua: nil para lo que no es un numero.
function fmlToNumber(?value:Dynamic):Dynamic {
    if (value == null) null
    else if (fmlIsNumber(value)) value
    else {
        var parsed = Std.parseFloat(StringTools.trim(Std.string(value)));
        Math.isNaN(parsed) ? null : parsed;
    }
}

function fmlNum(?value:Dynamic, ?fallback:Float):Float {
    if (value == null) fallback
    else if (fmlIsNumber(value)) value
    else {
        var parsed = Std.parseFloat(Std.string(value));
        Math.isNaN(parsed) ? fallback : parsed;
    }
}

function fmlBool(?value:Dynamic, ?fallback:Bool):Bool {
    if (value == null) fallback
    else if (value == true || value == false) value
    else {
        var text = Std.string(value).toLowerCase();
        text == "true" ? true : (text == "false" ? false : fallback);
    }
}

// Reproduce LuaUtils.getTweenEaseByString (`LuaUtils.hx:334-374`), incluida su
// rareza: smoothstepout devuelve smoothStepInOut. La tabla se arma una vez y
// cada nombre ya resuelto se recuerda: un tween por fotograma la reconstruia.
var fmlEaseNames:Map<String, String> = [
    "backin" => "backIn", "backinout" => "backInOut", "backout" => "backOut",
    "bouncein" => "bounceIn", "bounceinout" => "bounceInOut", "bounceout" => "bounceOut",
    "circin" => "circIn", "circinout" => "circInOut", "circout" => "circOut",
    "cubein" => "cubeIn", "cubeinout" => "cubeInOut", "cubeout" => "cubeOut",
    "elasticin" => "elasticIn", "elasticinout" => "elasticInOut", "elasticout" => "elasticOut",
    "expoin" => "expoIn", "expoinout" => "expoInOut", "expoout" => "expoOut",
    "quadin" => "quadIn", "quadinout" => "quadInOut", "quadout" => "quadOut",
    "quartin" => "quartIn", "quartinout" => "quartInOut", "quartout" => "quartOut",
    "quintin" => "quintIn", "quintinout" => "quintInOut", "quintout" => "quintOut",
    "sinein" => "sineIn", "sineinout" => "sineInOut", "sineout" => "sineOut",
    "smoothstepin" => "smoothStepIn", "smoothstepinout" => "smoothStepInOut", "smoothstepout" => "smoothStepInOut",
    "smootherstepin" => "smootherStepIn", "smootherstepinout" => "smootherStepInOut", "smootherstepout" => "smootherStepOut"
];
var fmlEaseCache:Map<String, Dynamic> = [];
function fmlEase(?name:Dynamic) {
    var ease = FlxEase.linear;
    if (name != null) {
        var raw = Std.string(name);
        if (fmlEaseCache.exists(raw)) ease = fmlEaseCache.get(raw);
        else {
            var key = StringTools.replace(raw.toLowerCase(), " ", "");
            if (fmlEaseNames.exists(key)) {
                var f = Reflect.field(FlxEase, fmlEaseNames.get(key));
                if (f != null) ease = f;
            }
            fmlEaseCache.set(raw, ease);
        }
    }
    ease;
}

function fmlColor(?value:Dynamic):Int {
    if (value == null) 0xFFFFFFFF
    else if (fmlIsNumber(value)) Std.int(value)
    else {
        var text = Std.string(value);
        // Psych admite "FF0000" y "0xFFFF0000" (`FunkinLua.hx:704`, FlxColor.fromString).
        if (text.length == 6) text = "#" + text;
        var color = FlxColor.fromString(text);
        color == null ? 0xFFFFFFFF : color;
    }
}

// openfl.display.BlendMode como cadena: es lo que hace LuaUtils.blendModeFromString.
function fmlBlend(?name:Dynamic) name == null ? null : Std.string(name).toLowerCase();

// ---------------------------------------------------------------------------
// Raices y propiedades: setProperty/getProperty (`ReflectionFunctions.hx:19-70`)
// ---------------------------------------------------------------------------
// `name[idx]` como en LuaUtils.getVarInArray (`LuaUtils.hx:83`).
function fmlGetVar(?instance:Dynamic, ?variable:String):Dynamic {
    var result:Dynamic = null;
    if (instance != null) {
        if (variable.indexOf("[") >= 0) {
            var parts = variable.split("[");
            result = fmlGetVar(instance, parts[0]);
            for (i in 1...parts.length) {
                if (result != null) {
                    var index = parts[i].substr(0, parts[i].length - 1);
                    var number = Std.parseInt(index);
                    result = number != null ? result[number] : Reflect.getProperty(result, index);
                }
            }
        }
        else if (instance is Map) result = instance.get(variable);
        else try { result = Reflect.getProperty(instance, variable); } catch (e:Dynamic) { result = null; }
    }
    result;
}

function fmlSetVar(?instance:Dynamic, ?variable:String, ?value:Dynamic):Dynamic {
    if (instance != null) {
        if (variable.indexOf("[") >= 0) {
            var parts = variable.split("[");
            var target = fmlGetVar(instance, parts[0]);
            for (i in 1...parts.length) {
                if (target != null) {
                    var index = parts[i].substr(0, parts[i].length - 1);
                    var number = Std.parseInt(index);
                    if (i >= parts.length - 1) {
                        if (number != null) target[number] = value; else Reflect.setProperty(target, index, value);
                    } else {
                        target = number != null ? target[number] : Reflect.getProperty(target, index);
                    }
                }
            }
        }
        else if (instance is Map) instance.set(variable, value);
        // Lua deja escribir cualquier campo; v1.0.1 lanza "Invalid field" si el
        // objeto no lo tiene. Se intenta y, si no, se ignora como haria Lua.
        else try { Reflect.setProperty(instance, variable, value); } catch (e:Dynamic) {}
    }
    value;
}

// El sprite del XML del escenario con ese nombre, si lo hay (`Stage.hx:46`).
function fmlStageSprite(?tag:String):Dynamic {
    var game = fmlGame();
    (game == null || game.stage == null) ? null : game.stage.getSprite(tag);
}

function fmlCharacter(?name:Dynamic):Dynamic {
    var game = fmlGame();
    var key = name == null ? "" : Std.string(name).toLowerCase();
    if (game == null) null
    else if (key == "bf" || key == "boyfriend" || key == "0" || key == "boyfriendgroup") game.boyfriend
    else if (key == "dad" || key == "opponent" || key == "1" || key == "dadgroup") game.dad
    else if (key == "gf" || key == "girlfriend" || key == "2" || key == "gfgroup") game.gf
    else null;
}

// Los receptores como Psych los numera: 0-3 el rival, 4-7 el jugador
// (`PlayState.hx:2281-2288` de Codename: cpuStrums / playerStrums). Una vez
// por fotograma: los tweens de notas lo piden varias veces por fotograma. No
// se recuerda vacio, porque en create los receptores aun no existen.
var fmlStrumCache:Array<Dynamic> = null;
var fmlStrumCacheFrame = -1;
function fmlStrumArray():Array<Dynamic> {
    if (fmlStrumCache == null || fmlStrumCacheFrame != fmlFrame) {
        var game = fmlGame();
        var out:Array<Dynamic> = [];
        if (game != null) {
            if (game.cpuStrums != null) for (s in game.cpuStrums.members) out.push(s);
            if (game.playerStrums != null) for (s in game.playerStrums.members) out.push(s);
        }
        fmlStrumCache = out.length > 0 ? out : null;
        fmlStrumCacheFrame = fmlFrame;
        if (fmlStrumCache == null) out else fmlStrumCache;
    }
    else fmlStrumCache;
}

// Las notas del chart, en orden de tiempo, de todas las strumlines. Psych las
// parte en `notes` (en pantalla) y `unspawnNotes` (por salir); Codename las
// tiene todas en `strumLine.notes` (NoteGroup). Se rehace una vez por frame.
// Orden por tiempo. Sin `return`: el comparador se llama N*log(N) veces y cada
// `return` es una excepcion en este hscript (miles por llamada con un chart
// entero; era el tiron al pulsar notas de algunos scripts).
function fmlByStrumTime(a:Dynamic, b:Dynamic):Int
    a.strumTime < b.strumTime ? -1 : (a.strumTime > b.strumTime ? 1 : 0);

function fmlAllNotes():Array<Dynamic> {
    if (fmlNotesCache == null || fmlNotesCacheFrame != fmlFrame) {
        var out:Array<Dynamic> = [];
        var game = fmlGame();
        if (game != null && game.strumLines != null)
            for (line in game.strumLines.members)
                if (line != null && line.notes != null)
                    for (note in line.notes.members) if (note != null) out.push(note);
        out.sort(fmlByStrumTime);
        fmlNotesCache = out;
        fmlNotesCacheFrame = fmlFrame;
    }
    fmlNotesCache;
}

function fmlUnspawnNotes():Array<Dynamic> {
    var out:Array<Dynamic> = [];
    var window = Conductor.songPosition + 3000;
    for (note in fmlAllNotes()) if (note.strumTime > window) out.push(note);
    out;
}

// Se rehace en cada nota acertada (fmlNoteIndex). Medido en el motor
// (knockout, ~50 notas visibles): el `sort` nativo con el comparador de
// arriba cuesta ~400 us; fundir a mano las strumlines, que ya vienen en orden,
// ~600 us. En este hscript cuenta el trabajo interpretado por elemento, no la
// complejidad: el sort solo llama al comparador; la fusion interpreta cada paso.
function fmlSpawnedNotes():Array<Dynamic> {
    if (fmlSpawnedNotesCache == null || fmlSpawnedNotesCacheFrame != fmlFrame) {
        fmlSpawnedNotesCache = [];
        var game = fmlGame();
        if (game != null && game.strumLines != null)
            for (line in game.strumLines.members) {
                if (line != null && line.notes != null) {
                    var group = line.notes;
                    // NoteGroup ordena descendente: recorrer desde el final solo
                    // hasta el limite visible. NO llamar forEach aqui: comparte el
                    // indice interno con update y un callback de hit lo
                    // corromperia por reentrada. Sin break/continue (excepciones).
                    var at = group.length - 1;
                    var window = Conductor.songPosition + group.limit;
                    while (at >= 0) {
                        var note = group.members[at--];
                        if (note != null && note.exists) {
)fmlhx",
R"fmlhx(                            // El hit precede a la retirada; wasGoodHit no debe ocultarlo.
                            if (note.strumTime > window) at = -1;
                            else fmlSpawnedNotesCache.push(note);
                        }
                    }
                }
            }
        fmlSpawnedNotesCache.sort(fmlByStrumTime);
        fmlSpawnedNotesCacheFrame = fmlFrame;
    }
    fmlSpawnedNotesCache;
}

// La raiz de una ruta de setProperty. Nulo si no existe TODAVIA (ver
// fmlLateRoot) o no existe.
// Sin `return`: el `switch` es una expresion y su valor es el del caso. Cada
// `case` es una comparacion interpretada: un nombre que no esta en la tabla
// (una etiqueta del escenario, un campo del juego como `health`) va directo.
var fmlRootNames:Map<String, Bool> = [
    "this" => true, "game" => true, "instance" => true,
    "boyfriend" => true, "bf" => true, "boyfriendGroup" => true,
    "dad" => true, "opponent" => true, "dadGroup" => true,
    "gf" => true, "girlfriend" => true, "gfGroup" => true,
    "camGame" => true, "camHUD" => true, "camOther" => true, "camFollow" => true,
    "strumLineNotes" => true, "opponentStrums" => true, "playerStrums" => true,
    "notes" => true, "unspawnNotes" => true, "eventNotes" => true,
    "timeBar" => true, "timeTxt" => true, "timeBarBG" => true, "botplayTxt" => true,
    "SONG" => true, "inst" => true, "vocals" => true, "FlxG" => true, "Conductor" => true
];
function fmlRoot(?name:String):Dynamic {
    var game = PlayState.instance;
    if (fmlObjects.exists(name)) fmlObjects.get(name)
    else if (fmlHudObjects.exists(name)) fmlHudObjects.get(name)
    else if (!fmlRootNames.exists(name)) {
        var stage = fmlStageSprite(name);
        if (stage != null) stage
        else if (game != null && Reflect.hasField(game, name)) Reflect.getProperty(game, name)
        else null;
    }
    else switch (name) {
        case "this", "game", "instance": game;
        case "boyfriend", "bf", "boyfriendGroup": game.boyfriend;
        case "dad", "opponent", "dadGroup": game.dad;
        case "gf", "girlfriend", "gfGroup": game.gf;
        case "camGame": game.camGame;
        case "camHUD": game.camHUD;
        case "camOther": fmlCamOtherGet();
        case "camFollow": game.camFollow;
        case "strumLineNotes": fmlStrumArray();
        case "opponentStrums": game.cpuStrums != null ? game.cpuStrums.members : null;
        case "playerStrums": game.playerStrums != null ? game.playerStrums.members : null;
        case "notes": fmlSpawnedNotes();
        case "unspawnNotes": fmlUnspawnNotes();
        case "eventNotes": game.events;
        case "timeBar", "timeTxt", "timeBarBG", "botplayTxt": fmlHudObject(name);
        case "SONG": fmlSongView();
        case "inst": game.inst;
        case "vocals": game.vocals;
        case "FlxG": FlxG;
        case "Conductor": Conductor;
        default: null;
    }
}

// Lo que en Psych existe en onCreate y en Codename se crea despues de
// `create` (`PlayState.hx:883-914`, `:859-865`).
function fmlLateRoot(?name:String):Bool {
    !fmlPostCreated && (name == "healthBar" || name == "healthBarBG" || name == "iconP1" || name == "iconP2" ||
        name == "scoreTxt" || name == "missesTxt" || name == "accuracyTxt" || name == "strumLineNotes" ||
        name == "opponentStrums" || name == "playerStrums" || name == "notes" || name == "unspawnNotes");
}

// SONG como lo ve un script de Psych (`SONG.song`, `SONG.bpm`, `SONG.speed`...).
function fmlSongView():Dynamic {
    var game = PlayState.instance;
    var song = PlayState.SONG;
    var view:Dynamic = {
        song: song.meta != null ? song.meta.name : "",
        bpm: song.meta != null ? song.meta.bpm : Conductor.bpm,
        speed: game != null ? game.scrollSpeed : 1,
        stage: song.stage,
        player1: game != null && game.boyfriend != null ? game.boyfriend.curCharacter : "",
        player2: game != null && game.dad != null ? game.dad.curCharacter : "",
        gfVersion: game != null && game.gf != null ? game.gf.curCharacter : "",
        needsVoices: song.meta != null ? song.meta.needsVoices : true,
        notes: [],
        events: game != null ? game.events : []
    };
    view;
}

// Los nombres que fmlSetSpecial y fmlGetSpecial conocen. getProperty('health')
// es de las llamadas mas frecuentes y no es especial: sin esta tabla recorria
// las ~30 comparaciones interpretadas de cada switch para nada.
var fmlSpecialNames:Map<String, Bool> = [
    "cameraSpeed" => true, "songSpeed" => true, "skipCountdown" => true, "hideHud" => true,
    "botPlay" => true, "cpuControlled" => true, "isCameraOnForcedPos" => true,
    "healthGainMult" => true, "healthLossMult" => true,
    "ratingPercent" => true, "ratingName" => true, "ratingFC" => true,
    "camZoomingDecay" => true, "camZoomingMult" => true, "camZoomingBop" => true,
    "playbackRate" => true, "boyfriendName" => true, "dadName" => true, "gfName" => true,
    "camFollowPos" => true, "songLength" => true, "curSection" => true, "curDecStep" => true,
    "curDecBeat" => true, "songPos" => true, "mustHitSection" => true, "isDead" => true,
    "unspawnNotes" => true, "notes" => true, "strumLineNotes" => true,
    "opponentStrums" => true, "playerStrums" => true
];

// Campos de PlayState de Psych que en Codename se llaman de otra forma o no
// existen. Devuelve true si lo resolvio.
function fmlSetSpecial(?name:String, ?value:Dynamic):Bool {
    var game = PlayState.instance;
    if (!fmlSpecialNames.exists(name)) false
    else switch (name) {
        case "cameraSpeed": {
            // Psych: camFollowPos se acerca a cameraSpeed*2.4*elapsed; Codename:
            // FlxG.camera.follow(camFollow, LOCKON, 0.04) (`PlayState.hx:867`, `Flags.hx:211`).
            FlxG.camera.followLerp = 0.04 * fmlNum(value, 1);
            true;
        }
        case "songSpeed": { game.scrollSpeed = fmlNum(value, game.scrollSpeed); true; }
        case "skipCountdown": {
            fmlSkipCountdown = fmlBool(value, false);
            // `introLength = 0` antes de createPost: sin cuenta atras, la
            // cancion arranca al frame siguiente (`PlayState.hx:1030-1039`).
            if (fmlSkipCountdown && !game.startedCountdown) game.introLength = 0;
            else if (fmlSkipCountdown && game.startTimer != null && !game.startTimer.finished) {
                game.startTimer.cancel();
                Conductor.songPosition = Conductor.songOffset;
            }
            true;
        }
        case "hideHud": { hideHud = fmlBool(value, false); if (game.camHUD != null) game.camHUD.visible = !hideHud; true; }
        case "botPlay", "cpuControlled": { botPlay = fmlBool(value, false); if (game.playerStrums != null) game.playerStrums.cpu = botPlay; true; }
        case "isCameraOnForcedPos": {
            if (game.curCameraTarget >= 0) fmlCameraSectionTarget = game.curCameraTarget;
            fmlCameraForced = fmlBool(value, false);
            if (fmlCameraForced) game.curCameraTarget = -1;
            else if (game.curCameraTarget < 0) game.curCameraTarget = fmlSectionTarget();
            true;
        }
        case "healthGainMult": { healthGainMult = fmlNum(value, 1); true; }
        case "healthLossMult": { healthLossMult = fmlNum(value, 1); true; }
        case "ratingPercent": { fmlRatingPercent = fmlNum(value, 0); true; }
        case "ratingName": { fmlRatingName = Std.string(value); true; }
        case "ratingFC": { fmlRatingFC = Std.string(value); true; }
        case "camZoomingDecay": { game.camGameZoomLerp = fmlNum(value, 1) * 0.05; game.camHUDZoomLerp = game.camGameZoomLerp; true; }
        case "camZoomingMult": { game.camZoomingStrength = fmlNum(value, 1) * 0.015; true; }
        case "camZoomingBop": true;
        case "playbackRate": { fmlWarn("playbackRate no se puede cambiar desde un script en Codename"); true; }
        case "boyfriendName", "dadName", "gfName": true;
        case "camFollowPos": true;
        default: false;
    }
}

function fmlGetSpecial(?name:String):Dynamic {
    var game = PlayState.instance;
    if (!fmlSpecialNames.exists(name)) "__fml_none__"
    else switch (name) {
        case "cameraSpeed": FlxG.camera.followLerp / 0.04;
        case "songSpeed": game.scrollSpeed;
        case "skipCountdown": fmlSkipCountdown;
        case "hideHud": hideHud;
        case "botPlay", "cpuControlled": game.playerStrums != null ? game.playerStrums.cpu : botPlay;
        case "isCameraOnForcedPos": game.curCameraTarget < 0;
        case "healthGainMult": healthGainMult;
        case "healthLossMult": healthLossMult;
        case "ratingPercent": fmlRatingPercent;
        case "ratingName": fmlRatingName;
        case "ratingFC": fmlRatingFC;
        case "playbackRate": 1;
        case "songLength": game.inst != null ? game.inst.length : 0;
        case "curSection": curSection;
        case "curDecStep": curDecStep;
        case "curDecBeat": curDecBeat;
        case "songPos": Conductor.songPosition;
        case "mustHitSection": mustHitSection;
        case "boyfriendName": game.boyfriend != null ? game.boyfriend.curCharacter : "";
        case "dadName": game.dad != null ? game.dad.curCharacter : "";
        case "gfName": game.gf != null ? game.gf.curCharacter : "";
        case "camZoomingDecay": game.camGameZoomLerp / 0.05;
        case "camZoomingMult": game.camZoomingStrength / 0.015;
        case "camZoomingBop": true;
        case "isDead": fmlInGameOver;
        case "unspawnNotes": fmlUnspawnNotes();
        case "notes": fmlSpawnedNotes();
        case "strumLineNotes": fmlStrumArray();
        case "opponentStrums": game.cpuStrums != null ? game.cpuStrums.members : null;
        case "playerStrums": game.playerStrums != null ? game.playerStrums.members : null;
        default: "__fml_none__";
    }
}

// A que strumline mira la seccion: en Codename el chart lleva los eventos de
// camara; sin uno, el jugador.
function fmlSectionTarget():Int fmlCameraSectionTarget;

// camFollowPos: en Psych es el objeto que la camara sigue con lerp; su x/y son
// el centro actual de la camara. Escribirlo mueve la camara de golpe.
function fmlCamFollowPosGet(?field:String):Dynamic {
    if (field == "x") FlxG.camera.scroll.x + FlxG.camera.width * 0.5
    else if (field == "y") FlxG.camera.scroll.y + FlxG.camera.height * 0.5
    else null;
}

function fmlCamFollowPosSet(?field:String, ?value:Dynamic) {
    var game = fmlGame();
    if (field == "x") { FlxG.camera.scroll.x = fmlNum(value, 0) - FlxG.camera.width * 0.5; if (game.camFollow != null) game.camFollow.x = fmlNum(value, 0); }
    if (field == "y") { FlxG.camera.scroll.y = fmlNum(value, 0) - FlxG.camera.height * 0.5; if (game.camFollow != null) game.camFollow.y = fmlNum(value, 0); }
}

function fmlFlushDeferred() {
    if (fmlDeferred.length > 0) {
        var pending = fmlDeferred;
        fmlDeferred = [];
        for (entry in pending) fmlSetProperty(entry.path, entry.value);
    }
}

// Las dos funciones mas llamadas de toda la capa (decenas de veces por
// fotograma en un mod como Rev-Mixed): sin `return`, sin break/continue.
function fmlSetProperty(?path:Dynamic, ?value:Dynamic):Dynamic {
    if (path != null) {
        var text = Std.string(path);
        if (fmlDeferred.length > 0 && fmlPostCreated) fmlFlushDeferred();
        var split = text.split(".");
        var rootName = split[0];
        if (split.length == 1) {
            if (!fmlSetSpecial(rootName, value)) {
                if (fmlObjects.exists(rootName)) fmlObjects.set(rootName, value);
                else {
                    var game = PlayState.instance;
                    if (game != null) fmlSetVar(game, rootName, value);
                }
            }
        }
        else if (rootName == "camFollowPos" && split.length == 2) fmlCamFollowPosSet(split[1], value);
        else {
            var root = fmlRoot(rootName);
            if (root == null) {
)fmlhx",
R"fmlhx(                if (fmlLateRoot(rootName)) fmlDeferred.push({path: text, value: value});
                else fmlWarn("setProperty: no existe " + text);
            } else {
                var obj = root;
                var i = 1;
                while (i < split.length - 1 && obj != null) {
                    obj = fmlGetVar(obj, split[i]);
                    i++;
                }
                if (obj == null) fmlWarn("setProperty: no existe " + text);
                else fmlSetVar(obj, split[split.length - 1], value);
            }
        }
    }
    value;
}

function fmlGetProperty(?path:Dynamic):Dynamic {
    var result:Dynamic = null;
    if (path != null) {
        var text = Std.string(path);
        if (fmlDeferred.length > 0 && fmlPostCreated) fmlFlushDeferred();
        var split = text.split(".");
        var rootName = split[0];
        if (split.length == 1) {
            var special = fmlGetSpecial(rootName);
            if (special != "__fml_none__") result = special;
            else {
                result = fmlRoot(rootName);
                if (result == null) {
                    var game = PlayState.instance;
                    if (game != null) result = fmlGetVar(game, rootName);
                }
            }
        }
        else if (rootName == "camFollowPos" && split.length == 2) result = fmlCamFollowPosGet(split[1]);
        else {
            result = fmlRoot(rootName);
            if (result == null) {
                if (fmlLateRoot(rootName)) fmlWarn("getProperty en create sobre " + rootName + ": en Codename aun no existe (se crea tras create)");
            } else {
                var i = 1;
                while (i < split.length && result != null) {
                    result = fmlGetVar(result, split[i]);
                    i++;
                }
            }
        }
    }
    result;
}

// Recorre `a.b.c` desde `obj`: nulo en cuanto falta un tramo.
function fmlGetPath(?obj:Dynamic, ?path:String):Dynamic {
    var split = path.split(".");
    var i = 0;
    while (i < split.length && obj != null) {
        obj = fmlGetVar(obj, split[i]);
        i++;
    }
    obj;
}

// Escribe el ultimo tramo de `a.b.c` desde `obj`, como LuaUtils.setVarInArray
// tras recorrer la ruta. Sin objeto intermedio no escribe nada.
function fmlSetPath(?obj:Dynamic, ?path:String, ?value:Dynamic):Dynamic {
    var split = path.split(".");
    var i = 0;
    while (i < split.length - 1 && obj != null) {
        obj = fmlGetVar(obj, split[i]);
        i++;
    }
    if (obj != null) fmlSetVar(obj, split[split.length - 1], value);
    value;
}

// `Type` no existe en el sandbox de v1.0.1 (ver fmlIsNumber): pedir otra clase
// por nombre lanzaba "Unknown variable" y cortaba el hook en esa linea (un
// `getPropertyFromClass('Main', ...)` en onCreate se llevaba el resto de
// onCreate). Sin clase se devuelve nulo y lo dice fmlPending, como Psych.
function fmlResolveClass(?name:String):Dynamic {
    var cls:Dynamic = null;
    try { cls = Type.resolveClass(name); } catch (e:Dynamic) { cls = null; }
    cls;
}

// Clases: solo las que existen en los dos motores con la misma forma
// (`ReflectionFunctions.hx:34-70`).
function fmlClass(?name:Dynamic):Dynamic {
    var key = Std.string(name);
    switch (key.split(".").pop()) {
        case "FlxG": FlxG;
        case "Conductor": Conductor;
        case "PlayState": PlayState;
        case "GameOverSubstate": GameOverSubstate;
        case "Paths": Paths;
        case "FlxMath": FlxMath;
        case "Math": Math;
        case "Std": Std;
        case "FlxTween": FlxTween;
        case "FlxEase": FlxEase;
        case "FlxColor": FlxColor;
        case "Note": Note;
        case "Character": Character;
        default: fmlResolveClass(key);
    }
}

// Estaticas en Psych (`GameOverSubstate.hx:22`), de instancia en Codename (`:18`).
function fmlSetGameOverField(?path:String, ?value:Dynamic) {
    var game = PlayState.instance;
    var field = path.split(".")[0];
    if (field == "characterName") { if (game.boyfriend != null) game.boyfriend.gameOverCharacter = Std.string(value); }
    else if (field == "deathSoundName") game.lossSFX = Std.string(value);
    else if (field == "loopSoundName") game.gameOverSong = Std.string(value);
    else if (field == "endSoundName") game.retrySFX = Std.string(value);
    else if (GameOverSubstate.instance == null) fmlPending("setPropertyFromClass(GameOverSubstate." + path + ")", [value]);
    else fmlSetPath(GameOverSubstate.instance, path, value);
}

// Sin `return` (ver RENDIMIENTO): hay scripts que la llaman cada fotograma
// (mover la ventana, leer el raton).
function fmlSetPropertyFromClass(?className:Dynamic, ?variable:Dynamic, ?value:Dynamic):Dynamic {
    var key = Std.string(className);
    var path = Std.string(variable);
    if (key == "openfl.Lib" || key == "Lib") fmlWindowSet(path, value)
    else if (key == "ClientPrefs" || key == "backend.ClientPrefs") {
        fmlWarn("setPropertyFromClass(ClientPrefs): las opciones de Codename se leen, no se escriben desde un script");
        value;
    }
    else if (key == "PlayState" || key == "states.PlayState") fmlSetProperty(path, value)
    else if (key == "GameOverSubstate" || key == "substates.GameOverSubstate") { fmlSetGameOverField(path, value); value; }
    else {
        var cls = fmlClass(key);
        if (cls == null) fmlPending("setPropertyFromClass(" + key + ")", [variable, value]) else fmlSetPath(cls, path, value);
    }
}

function fmlGetPropertyFromClass(?className:Dynamic, ?variable:Dynamic):Dynamic {
    var key = Std.string(className);
    var path = Std.string(variable);
    if (key == "openfl.Lib" || key == "Lib") fmlWindowGet(path)
    else if (key == "ClientPrefs" || key == "backend.ClientPrefs") fmlClientPrefs(path)
    else if (key == "PlayState" || key == "states.PlayState") fmlGetProperty(path)
    else if (key == "GameOverSubstate" || key == "substates.GameOverSubstate") fmlGetPath(GameOverSubstate.instance, path)
    else {
        var cls = fmlClass(key);
        if (cls == null) fmlPending("getPropertyFromClass(" + key + ")", [variable]) else fmlGetPath(cls, path);
    }
}

// ClientPrefs.data.<x> -> Options.<x>. Solo lectura, y solo lo que existe.
function fmlClientPrefs(?path:String):Dynamic {
    var field = path.split(".").pop();
    switch (field) {
        case "downScroll": Options.downscroll;
        case "middleScroll": false;
        case "ghostTapping": Options.ghostTapping;
        case "camZooms": Options.camZoomOnBeat;
        case "shaders": Options.gameplayShaders;
        case "lowQuality": Options.lowMemoryMode;
        case "antialiasing": Options.antialiasing;
        case "framerate": Options.framerate;
        case "hideHud": hideHud;
        case "noteOffset": Options.songOffset;
        case "flashing": Options.flashingMenu;
        case "splashAlpha": 0.6;
        case "healthBarAlpha": 1;
        case "noReset": false;
        case "gameplaySettings": {
            var settings:Dynamic = {scrollspeed: 1, scrolltype: "multiplicative", songspeed: 1, healthgain: 1, healthloss: 1, instakill: false, practice: false, botplay: botPlay};
            settings;
        }
        default: {
            var value = Reflect.getProperty(Options, field);
            if (value == null) fmlPending("getPropertyFromClass(ClientPrefs." + path + ")", []) else value;
        }
    }
}

// Grupos (`ReflectionFunctions.hx:72-135`): `strumLineNotes`, `notes`,
// `unspawnNotes`, `opponentStrums`, `playerStrums` y cualquier array del juego.
// Los bucles de modchart llaman a estas tres por receptor y fotograma.
function fmlGroupMember(?group:Dynamic, ?index:Dynamic):Dynamic {
    var i = Std.int(fmlNum(index, 0));
    var list:Dynamic = fmlRoot(Std.string(group));
    if (list != null && !fmlIsArray(list)) list = Reflect.getProperty(list, "members");
    (list != null && i >= 0 && i < list.length) ? list[i] : null;
}

function fmlGetPropertyFromGroup(?group:Dynamic, ?index:Dynamic, ?variable:Dynamic):Dynamic
    fmlGetPath(fmlGroupMember(group, index), Std.string(variable));

function fmlSetPropertyFromGroup(?group:Dynamic, ?index:Dynamic, ?variable:Dynamic, ?value:Dynamic):Dynamic {
    var member = fmlGroupMember(group, index);
    if (member != null) fmlSetPath(member, Std.string(variable), value);
    else if (fmlLateRoot(Std.string(group))) fmlWarn("setPropertyFromGroup(" + group + ") en create: los receptores se crean tras create en Codename");
    value;
}

function fmlAddToGroup(?group:Dynamic, ?tag:Dynamic, ?index:Dynamic):Dynamic {
    var list:Dynamic = fmlRoot(Std.string(group));
    var obj = fmlObject(tag);
    if (list == null || obj == null) return null;
    if (index == null || fmlNum(index, -1) < 0) list.add(obj); else list.insert(Std.int(fmlNum(index, 0)), obj);
    return obj;
}

function fmlRemoveFromGroup(?group:Dynamic, ?index:Dynamic, ?dontDestroy:Dynamic):Dynamic {
    var list:Dynamic = fmlRoot(Std.string(group));
    var member = fmlGroupMember(group, index);
    if (list == null || member == null) return null;
    list.remove(member, true);
    if (!fmlBool(dontDestroy, false)) member.destroy();
    return null;
}

// ---- reflexion (`ReflectionFunctions.hx:200-306`) --------------------------
// `instanceArg('tag')` marca un argumento que es un objeto por etiqueta; se
// resuelve al llamar, como hace Psych con su prefijo.
function fmlInstanceArg(?name:Dynamic, ?className:Dynamic):String {
    var out = "##fmlInstance::" + Std.string(name);
    if (className != null) out += "::" + Std.string(className);
    out;
}

// callMethod y las suyas, sin `return` (ver RENDIMIENTO): los scripts de
// Psych 1.0 las usan donde antes iba setProperty.
function fmlParseInstance(?arg:Dynamic):Dynamic {
    var result:Dynamic = arg;
    if (arg != null && fmlIsArray(arg)) {
        var out:Array<Dynamic> = [];
        for (v in arg) out.push(fmlParseInstance(v));
        result = out;
    }
    else if (arg != null && fmlIsString(arg) && StringTools.startsWith(arg, "##fmlInstance::")) {
        var text:String = arg;
        var rest = text.substr(15);
        var last = rest.lastIndexOf("::");
        var path = last >= 0 ? rest.substr(0, last) : rest;
        var base:Dynamic = last >= 0 ? fmlResolveClass(rest.substr(last + 2)) : null;
        var split = path.split(".");
        if (base == null) {
            base = fmlObject(split[0]);
            if (base == null) base = fmlRoot(split[0]);
            split.shift();
        }
        var i = 0;
        while (i < split.length && base != null) {
            base = fmlGetVar(base, split[i]);
            i++;
        }
        result = base;
    }
    result;
}

function fmlCallOn(?parent:Dynamic, ?path:String, ?args:Dynamic, ?what:String):Dynamic {
    var split = path.split(".");
    var obj:Dynamic = parent;
    var i = 0;
    while (i < split.length - 1 && obj != null) {
        obj = fmlGetVar(obj, split[i]);
        i++;
    }
    var method = obj == null ? null : Reflect.getProperty(obj, split[split.length - 1]);
    if (method == null || !Reflect.isFunction(method)) fmlPending(what + "(" + path + ")", args)
    else {
        var list:Array<Dynamic> = args == null ? [] : (fmlIsArray(args) ? fmlParseInstance(args) : [fmlParseInstance(args)]);
        Reflect.callMethod(obj, method, list);
    }
}

function fmlCallMethod(?funcToRun:Dynamic, ?args:Dynamic):Dynamic {
    var path = StringTools.trim(Std.string(funcToRun));
    var split = path.split(".");
    var tagged = fmlObjects.get(StringTools.trim(split[0]));
    if (tagged != null && split.length > 1) { split.shift(); fmlCallOn(tagged, split.join("."), args, "callMethod"); }
    else fmlCallOn(PlayState.instance, path, args, "callMethod");
}

function fmlCallMethodFromClass(?className:Dynamic, ?funcToRun:Dynamic, ?args:Dynamic):Dynamic {
    var cls = fmlClass(className);
)fmlhx",
R"fmlhx(    if (cls == null) fmlPending("callMethodFromClass(" + Std.string(className) + ")", args)
    else fmlCallOn(cls, StringTools.trim(Std.string(funcToRun)), args, "callMethodFromClass");
}

function fmlCreateInstance(?variableToSave:Dynamic, ?className:Dynamic, ?args:Dynamic):Bool {
    var key = StringTools.replace(StringTools.trim(Std.string(variableToSave)), ".", "");
    if (fmlObjects.exists(key)) { fmlWarn("createInstance: " + key + " ya existe"); return false; }
    var cls = fmlResolveClass(Std.string(className));
    if (cls == null) { fmlPending("createInstance(" + Std.string(className) + ")", args); return false; }
    var list:Array<Dynamic> = args == null ? [] : (fmlIsArray(args) ? fmlParseInstance(args) : [fmlParseInstance(args)]);
    var obj:Dynamic = null;
    try { obj = Type.createInstance(cls, list); } catch (e:Dynamic) { fmlWarn("createInstance(" + Std.string(className) + "): " + Std.string(e)); }
    if (obj == null) return false;
    fmlObjects.set(key, obj);
    return true;
}

function fmlAddInstance(?objectName:Dynamic, ?inFront:Dynamic):Dynamic {
    var obj = fmlObjects.get(Std.string(objectName));
    if (obj == null) { fmlWarn("addInstance: no existe " + Std.string(objectName)); return null; }
    var game = fmlGame();
    if (game.members.indexOf(obj) >= 0) return obj;
    if (fmlBool(inFront, false)) game.add(obj);
    else { var at = fmlLowestCharacterIndex(); if (at < 0) game.add(obj); else game.insert(at, obj); }
    return obj;
}

// ---------------------------------------------------------------------------
// Objetos: sprites y textos por etiqueta (`FunkinLua.hx:937-1230`)
// ---------------------------------------------------------------------------
// Lo busca casi toda la API (textos, tweens, animaciones): sin `return`.
function fmlObject(?tag:Dynamic):Dynamic {
    var result:Dynamic = null;
    if (tag != null) {
        var key = Std.string(tag);
        if (fmlObjects.exists(key)) result = fmlObjects.get(key);
        else if (fmlHudObjects.exists(key)) result = fmlHudObjects.get(key);
        // Un nombre con punto es una ruta (`'boyfriend.scale'`), como en tweenPrepare.
        else if (key.indexOf(".") >= 0) result = fmlGetProperty(key);
        else {
            result = fmlRoot(key);
            if (result == null) result = fmlCharacter(key);
        }
    }
    result;
}

function fmlRemoveObject(?tag:String) {
    if (!fmlObjects.exists(tag)) return;
    var old = fmlObjects.get(tag);
    fmlObjects.remove(tag);
    if (old == null) return;
    var game = fmlGame();
    if (game != null && game.members.indexOf(old) >= 0 && fmlStageSprite(tag) == null) game.remove(old, true);
}

function fmlMakeLuaSprite(?tag:Dynamic, ?image:Dynamic, ?x:Dynamic, ?y:Dynamic):Dynamic {
    var key = Std.string(tag);
    // El sprite que ya esta en el XML del escenario con ese nombre se adopta,
    // no se duplica: el importador escribio el XML desde este mismo Lua.
    var existing = fmlStageSprite(key);
    if (existing != null) { fmlObjects.set(key, existing); return existing; }
    fmlRemoveObject(key);
    var sprite = new FunkinSprite(fmlNum(x, 0), fmlNum(y, 0));
    if (image != null && Std.string(image) != "") sprite.loadGraphic(Paths.image(Std.string(image)));
    sprite.antialiasing = Options.antialiasing;
    fmlObjects.set(key, sprite);
    return sprite;
}

function fmlMakeAnimatedLuaSprite(?tag:Dynamic, ?image:Dynamic, ?x:Dynamic, ?y:Dynamic, ?spriteType:Dynamic):Dynamic {
    var key = Std.string(tag);
    var existing = fmlStageSprite(key);
    if (existing != null) { fmlObjects.set(key, existing); return existing; }
    fmlRemoveObject(key);
    var sprite = new FunkinSprite(fmlNum(x, 0), fmlNum(y, 0));
    if (image != null && Std.string(image) != "") sprite.loadSprite(Paths.image(Std.string(image), null, true));
    sprite.antialiasing = Options.antialiasing;
    fmlObjects.set(key, sprite);
    return sprite;
}

function fmlMakeGraphic(?tag:Dynamic, ?width:Dynamic, ?height:Dynamic, ?color:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite == null) return null;
    sprite.makeGraphic(Std.int(fmlNum(width, 256)), Std.int(fmlNum(height, 256)), fmlColor(color == null ? "FFFFFF" : color));
    return sprite;
}

function fmlLoadGraphic(?tag:Dynamic, ?image:Dynamic, ?gridX:Dynamic, ?gridY:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite == null || image == null) return null;
    var gx = Std.int(fmlNum(gridX, 0));
    var gy = Std.int(fmlNum(gridY, 0));
    sprite.loadGraphic(Paths.image(Std.string(image)), gx > 0 || gy > 0, gx, gy);
    return sprite;
}

function fmlLoadFrames(?tag:Dynamic, ?image:Dynamic, ?spriteType:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite == null || image == null) return null;
    sprite.frames = Paths.getFrames(Std.string(image));
    return sprite;
}

// El orden de Psych: detras de los personajes (insert antes del grupo mas
// bajo) o delante de todo (`FunkinLua.hx:1025-1043`). En Codename los
// personajes estan planos en `members` (`Stage.hx:321-350`).
function fmlLowestCharacterIndex():Int {
    var game = fmlGame();
    var lowest = -1;
    for (character in [game.gf, game.dad, game.boyfriend]) {
        if (character == null) continue;
        var at = game.members.indexOf(character);
        if (at >= 0 && (lowest < 0 || at < lowest)) lowest = at;
    }
    return lowest;
}

function fmlAddLuaSprite(?tag:Dynamic, ?inFront:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite == null) return null;
    var game = fmlGame();
    if (game.members.indexOf(sprite) >= 0) return sprite;   // ya esta (el XML, o dos addLuaSprite)
    var front = fmlBool(inFront, false);
    if (fmlInGameOver && GameOverSubstate.instance != null) { GameOverSubstate.instance.add(sprite); return sprite; }
    if (front) {
        game.add(sprite);
        // Anadido en create sobre camHUD: la HUD del motor se anade despues
        // (`PlayState.hx:886-914`) y quedaria encima; se vuelve a poner arriba
        // en postCreate.
        if (!fmlPostCreated && sprite.cameras != null && sprite.cameras.length > 0 && sprite.cameras[0] == game.camHUD) fmlHudFront.push(sprite);
    } else {
        var at = fmlLowestCharacterIndex();
        if (at < 0) game.add(sprite); else game.insert(at, sprite);
    }
    return sprite;
}

function fmlRemoveLuaSprite(?tag:Dynamic, ?destroy:Dynamic, ?group:Dynamic):Dynamic {
    var key = Std.string(tag);
    var sprite = fmlObjects.get(key);
    if (sprite == null) return null;
    var game = fmlGame();
    if (game.members.indexOf(sprite) >= 0) game.remove(sprite, true);
    if (fmlBool(destroy, true)) { sprite.destroy(); fmlObjects.remove(key); }
    return null;
}

function fmlLuaSpriteExists(?tag:Dynamic):Bool {
    var obj = fmlObjects.get(Std.string(tag));
    obj != null && !(obj is FlxText);
}
function fmlLuaTextExists(?tag:Dynamic):Bool {
    var obj = fmlObjects.get(Std.string(tag));
    obj != null && (obj is FlxText);
}
function fmlLuaSoundExists(?tag:Dynamic):Bool fmlSounds.exists(fmlSoundTag(tag));

// De aqui a fmlObjectsOverlap, sin `return` (ver RENDIMIENTO): el sprite nulo
// cae por el `if` y se devuelve tal cual, que es lo que devolvia el `return null`.
function fmlSetGraphicSize(?tag:Dynamic, ?x:Dynamic, ?y:Dynamic, ?updateHitbox:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null) {
        sprite.setGraphicSize(Std.int(fmlNum(x, 0)), Std.int(fmlNum(y, 0)));
        if (fmlBool(updateHitbox, true)) sprite.updateHitbox();
    }
    sprite;
}

function fmlScaleObject(?tag:Dynamic, ?x:Dynamic, ?y:Dynamic, ?updateHitbox:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null) {
        sprite.scale.set(fmlNum(x, 1), fmlNum(y, 1));
        if (fmlBool(updateHitbox, true)) sprite.updateHitbox();
    }
    sprite;
}

function fmlUpdateHitbox(?tag:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null) sprite.updateHitbox();
    sprite;
}

function fmlScreenCenter(?tag:Dynamic, ?axes:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null) {
        var key = axes == null ? "xy" : Std.string(axes).toLowerCase();
        if (key == "x") sprite.screenCenter(FlxAxes.X);
        else if (key == "y") sprite.screenCenter(FlxAxes.Y);
        else sprite.screenCenter(FlxAxes.XY);
    }
    sprite;
}

function fmlCamOtherGet():Dynamic {
    if (fmlCamOther == null) {
        // Psych: una camara transparente por encima de camHUD (`PlayState.hx` de Psych, camOther).
        fmlCamOther = new FlxCamera();
        fmlCamOther.bgColor = 0;
        FlxG.cameras.add(fmlCamOther, false);
    }
    fmlCamOther;
}

function fmlCameraByName(?name:Dynamic):Dynamic {
    var game = PlayState.instance;
    var key = name == null ? "game" : Std.string(name).toLowerCase();
    if (key == "hud" || key == "camhud") game.camHUD
    else if (key == "other" || key == "camother") fmlCamOtherGet()
    else game.camGame != null ? game.camGame : FlxG.camera;
}

function fmlSetObjectCamera(?tag:Dynamic, ?camera:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null) sprite.cameras = [fmlCameraByName(camera)];
    sprite;
}

function fmlSetObjectOrder(?tag:Dynamic, ?position:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null) {
        var game = PlayState.instance;
        game.remove(sprite, true);
        var at = Std.int(fmlNum(position, 0));
        if (at < 0) at = 0;
        if (at > game.members.length) at = game.members.length;
        game.insert(at, sprite);
    }
    sprite;
}

function fmlGetObjectOrder(?tag:Dynamic):Int {
    var sprite = fmlObject(tag);
    sprite == null ? -1 : PlayState.instance.members.indexOf(sprite);
}

function fmlSetScrollFactor(?tag:Dynamic, ?x:Dynamic, ?y:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null) sprite.scrollFactor.set(fmlNum(x, 1), fmlNum(y, 1));
    sprite;
}

function fmlSetBlendMode(?tag:Dynamic, ?blend:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null) sprite.blend = fmlBlend(blend);
    sprite;
}

function fmlAddAnimationByPrefix(?tag:Dynamic, ?name:Dynamic, ?prefix:Dynamic, ?framerate:Dynamic, ?loop:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null && sprite.animation != null) {
        sprite.animation.addByPrefix(Std.string(name), Std.string(prefix), Std.int(fmlNum(framerate, 24)), fmlBool(loop, true));
        if (sprite.animation.curAnim == null) sprite.animation.play(Std.string(name), true);
        sprite;
    }
    else null;
}

function fmlIndices(?value:Dynamic):Array<Int> {
    var out:Array<Int> = [];
    if (value != null) {
        if (fmlIsArray(value)) for (v in value) out.push(Std.int(fmlNum(v, 0)));
        else for (part in Std.string(value).split(",")) { var n = Std.parseInt(StringTools.trim(part)); if (n != null) out.push(n); }
    }
    out;
}

function fmlAddAnimationByIndices(?tag:Dynamic, ?name:Dynamic, ?prefix:Dynamic, ?indices:Dynamic, ?framerate:Dynamic, ?loop:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null && sprite.animation != null) {
        sprite.animation.addByIndices(Std.string(name), Std.string(prefix), fmlIndices(indices), "", Std.int(fmlNum(framerate, 24)), fmlBool(loop, false));
        if (sprite.animation.curAnim == null) sprite.animation.play(Std.string(name), true);
        sprite;
    }
    else null;
}

function fmlAddAnimationByIndicesLoop(?tag:Dynamic, ?name:Dynamic, ?prefix:Dynamic, ?indices:Dynamic, ?framerate:Dynamic):Dynamic
    fmlAddAnimationByIndices(tag, name, prefix, indices, framerate, true);

function fmlAddAnimation(?tag:Dynamic, ?name:Dynamic, ?frames:Dynamic, ?framerate:Dynamic, ?loop:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null && sprite.animation != null) {
)fmlhx",
R"fmlhx(        sprite.animation.add(Std.string(name), fmlIndices(frames), Std.int(fmlNum(framerate, 24)), fmlBool(loop, true));
        if (sprite.animation.curAnim == null) sprite.animation.play(Std.string(name), true);
        sprite;
    }
    else null;
}

function fmlAddOffset(?tag:Dynamic, ?anim:Dynamic, ?x:Dynamic, ?y:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null && (sprite is FunkinSprite)) sprite.addOffset(Std.string(anim), fmlNum(x, 0), fmlNum(y, 0));
    sprite;
}

function fmlPlayAnim(?tag:Dynamic, ?name:Dynamic, ?forced:Dynamic, ?reverse:Dynamic, ?startFrame:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    if (sprite != null) {
        var anim = Std.string(name);
        var force = fmlBool(forced, false);
        // Character.playAnim(anim, force, NONE) no se corta por el beat
        // (`Character.hx:179-191`): no hace falta specialAnim.
        if (sprite is Character) sprite.playAnim(anim, force);
        else if (sprite is FunkinSprite) sprite.playAnim(anim, force, "NONE", fmlBool(reverse, false), Std.int(fmlNum(startFrame, 0)));
        else if (sprite.animation != null) sprite.animation.play(anim, force, fmlBool(reverse, false), Std.int(fmlNum(startFrame, 0)));
    }
    sprite;
}

function fmlObjectPlayAnimation(?tag:Dynamic, ?name:Dynamic, ?forced:Dynamic, ?startFrame:Dynamic):Dynamic
    fmlPlayAnim(tag, name, forced, false, startFrame);

function fmlGetMidpointX(?tag:Dynamic):Float { var s = fmlObject(tag); s == null ? 0 : s.getMidpoint().x; }
function fmlGetMidpointY(?tag:Dynamic):Float { var s = fmlObject(tag); s == null ? 0 : s.getMidpoint().y; }
function fmlGetGraphicMidpointX(?tag:Dynamic):Float { var s = fmlObject(tag); s == null ? 0 : s.getGraphicMidpoint().x; }
function fmlGetGraphicMidpointY(?tag:Dynamic):Float { var s = fmlObject(tag); s == null ? 0 : s.getGraphicMidpoint().y; }
function fmlGetScreenPositionX(?tag:Dynamic, ?camera:Dynamic):Float { var s = fmlObject(tag); s == null ? 0 : s.getScreenPosition(null, fmlCameraByName(camera)).x; }
function fmlGetScreenPositionY(?tag:Dynamic, ?camera:Dynamic):Float { var s = fmlObject(tag); s == null ? 0 : s.getScreenPosition(null, fmlCameraByName(camera)).y; }
function fmlObjectsOverlap(?a:Dynamic, ?b:Dynamic):Bool {
    var oa = fmlObject(a); var ob = fmlObject(b);
    oa != null && ob != null && FlxG.overlap(oa, ob);
}

// ---- texto (`TextFunctions.hx`) -------------------------------------------
function fmlMakeLuaText(?tag:Dynamic, ?text:Dynamic, ?width:Dynamic, ?x:Dynamic, ?y:Dynamic):Dynamic {
    var key = Std.string(tag);
    fmlRemoveObject(key);
    var label = new FlxText(fmlNum(x, 0), fmlNum(y, 0), Std.int(fmlNum(width, 0)), text == null ? "" : Std.string(text), 16);
    // vcr.ttf blanco centrado con borde negro y en camHUD (`TextFunctions.hx:12-15`).
    label.setFormat(Paths.font("vcr.ttf"), 16, FlxColor.WHITE, "center", FlxTextBorderStyle.OUTLINE, FlxColor.BLACK);
    label.cameras = [fmlGame().camHUD];
    label.scrollFactor.set();
    label.borderSize = 2;
    fmlObjects.set(key, label);
    return label;
}

function fmlAddLuaText(?tag:Dynamic):Dynamic {
    var label = fmlObject(tag);
    if (label == null) return null;
    var game = fmlGame();
    if (game.members.indexOf(label) < 0) {
        game.add(label);
        if (!fmlPostCreated) fmlHudFront.push(label);
    }
    return label;
}

function fmlRemoveLuaText(?tag:Dynamic, ?destroy:Dynamic):Dynamic {
    var key = Std.string(tag);
    var label = fmlObjects.get(key);
    if (label == null) return null;
    var game = fmlGame();
    if (game.members.indexOf(label) >= 0) game.remove(label, true);
    if (fmlBool(destroy, true)) { label.destroy(); fmlObjects.remove(key); }
    return null;
}

function fmlText(?tag:Dynamic):Dynamic fmlObject(tag);

// Los setters de FlxText que rehacen el grafico del texto en cada llamada
// (`font`, `size`, `alignment` pasan por updateDefaultFormat) solo se tocan si
// el valor cambia: hay scripts que los llaman en CADA fotograma con el mismo
// valor, y en Psych cuesta lo mismo pero va compilado. `text`, `color` y el
// borde ya se protegen solos en FlxText.
function fmlSetTextString(?tag:Dynamic, ?text:Dynamic):Dynamic { var t = fmlText(tag); if (t != null) t.text = text == null ? "" : Std.string(text); t; }
function fmlSetTextSize(?tag:Dynamic, ?size:Dynamic):Dynamic {
    var t = fmlText(tag);
    if (t != null) {
        var value = Std.int(fmlNum(size, 16));
        if (t.size != value) t.size = value;
    }
    t;
}
function fmlSetTextWidth(?tag:Dynamic, ?width:Dynamic):Dynamic { var t = fmlText(tag); if (t != null) t.fieldWidth = fmlNum(width, 0); t; }
function fmlSetTextHeight(?tag:Dynamic, ?height:Dynamic):Dynamic { var t = fmlText(tag); if (t != null) t.height = fmlNum(height, 0); t; }
function fmlSetTextAutoSize(?tag:Dynamic, ?value:Dynamic):Dynamic { var t = fmlText(tag); if (t != null) t.autoSize = fmlBool(value, true); t; }
function fmlSetTextBorder(?tag:Dynamic, ?size:Dynamic, ?color:Dynamic, ?style:Dynamic):Dynamic {
    var t = fmlText(tag);
    if (t != null) {
        t.borderSize = fmlNum(size, 1);
        if (color != null) t.borderColor = fmlColor(color);
        var key = style == null ? "outline" : Std.string(style).toLowerCase();
        if (key == "shadow") t.borderStyle = FlxTextBorderStyle.SHADOW;
        else if (key == "outline_fast" || key == "outlinefast") t.borderStyle = FlxTextBorderStyle.OUTLINE_FAST;
        else if (key == "none") t.borderStyle = FlxTextBorderStyle.NONE;
        else t.borderStyle = FlxTextBorderStyle.OUTLINE;
    }
    t;
}
function fmlSetTextColor(?tag:Dynamic, ?color:Dynamic):Dynamic { var t = fmlText(tag); if (t != null) t.color = fmlColor(color); t; }
// FlxText.font devuelve el NOMBRE de la fuente, no la ruta: para saber si ya
// esta puesta se recuerda por etiqueta que ruta se puso, en que objeto, y que
// nombre quedo (si otro codigo la cambio, el nombre ya no coincide).
var fmlTextFonts:Map<String, Dynamic> = [];
function fmlSetTextFont(?tag:Dynamic, ?font:Dynamic):Dynamic {
    var t = fmlText(tag);
    if (t != null && font != null) {
        var key = Std.string(tag);
        var path = Paths.font(Std.string(font));
        var last = fmlTextFonts.get(key);
        if (last == null || last.obj != t || last.path != path || last.name != t.font) {
            t.font = path;
            fmlTextFonts.set(key, {obj: t, path: path, name: t.font});
        }
    }
    t;
}
function fmlSetTextItalic(?tag:Dynamic, ?italic:Dynamic):Dynamic { var t = fmlText(tag); if (t != null) t.italic = fmlBool(italic, false); t; }
function fmlSetTextAlignment(?tag:Dynamic, ?alignment:Dynamic):Dynamic {
    var t = fmlText(tag);
    if (t != null) {
        var key = alignment == null ? "left" : Std.string(alignment).toLowerCase();
        var value = key == "right" ? "right" : key == "center" ? "center" : "left";
        if (t.alignment != value) t.alignment = value;
    }
    t;
}
function fmlGetTextString(?tag:Dynamic):String { var t = fmlText(tag); t == null ? "" : t.text; }
function fmlGetTextSize(?tag:Dynamic):Int { var t = fmlText(tag); t == null ? 0 : t.size; }
function fmlGetTextFont(?tag:Dynamic):String { var t = fmlText(tag); t == null ? "" : t.font; }
function fmlGetTextWidth(?tag:Dynamic):Float { var t = fmlText(tag); t == null ? 0 : t.fieldWidth; }

// ---- los objetos de HUD que Psych trae y Codename no ---------------------
function fmlHudObject(?name:String):Dynamic {
    var obj:Dynamic = fmlHudObjects.get(name);
    var game = PlayState.instance;
    if (obj == null && game != null && game.camHUD != null) {
        switch (name) {
            case "timeBarBG":
                obj = new FlxSprite(0, Options.downscroll ? FlxG.height - 44 : 19).makeGraphic(400, 19, 0xFF000000);
                obj.screenCenter(FlxAxes.X);
            case "timeBar":
                var bg = fmlHudObject("timeBarBG");
                obj = new FlxSprite(bg.x + 4, bg.y + 4).makeGraphic(392, 11, 0xFFFFFFFF);
            case "timeTxt":
                obj = new FlxText(42, Options.downscroll ? FlxG.height - 44 : 19, 400, "", 32);
                obj.setFormat(Paths.font("vcr.ttf"), 32, FlxColor.WHITE, "center", FlxTextBorderStyle.OUTLINE, FlxColor.BLACK);
                obj.screenCenter(FlxAxes.X);
                obj.borderSize = 2;
            case "botplayTxt":
                obj = new FlxText(400, Options.downscroll ? FlxG.height - 120 : 80, FlxG.width - 800, "BOTPLAY", 32);
                obj.setFormat(Paths.font("vcr.ttf"), 32, FlxColor.WHITE, "center", FlxTextBorderStyle.OUTLINE, FlxColor.BLACK);
                obj.borderSize = 1.25;
                obj.visible = false;
        }
        if (obj != null) {
            obj.scrollFactor.set();
            obj.cameras = [game.camHUD];
            game.add(obj);
            fmlHudObjects.set(name, obj);
        }
    }
    obj;
}

// createFilledBar crea graficos nuevos por cada par de colores: un script que
// lo llama en cada fotograma o nota con los mismos colores solo gastaba
// memoria. Se repite solo si cambian los colores o la barra.
var fmlHealthBarColors = "";
var fmlHealthBarColored = null;
function fmlSetHealthBarColors(?left:Dynamic, ?right:Dynamic):Dynamic {
    var game = PlayState.instance;
    if (game.healthBar == null) fmlDeferredCall(function() fmlSetHealthBarColors(left, right));
    else {
        var l = fmlColor(left);
        var r = fmlColor(right);
        var key = l + "," + r;
        if (key != fmlHealthBarColors || fmlHealthBarColored != game.healthBar) {
            fmlHealthBarColors = key;
            fmlHealthBarColored = game.healthBar;
            game.healthBar.createFilledBar(l, r);
            game.healthBar.updateBar();
        }
    }
    null;
}

function fmlSetTimeBarColors(?left:Dynamic, ?right:Dynamic):Dynamic {
    var bar = fmlHudObject("timeBar");
    var bg = fmlHudObject("timeBarBG");
    if (bar != null) bar.color = fmlColor(left);
    if (bg != null) bg.color = fmlColor(right);
    null;
}

var fmlDeferredCalls:Array<Dynamic> = [];
function fmlDeferredCall(?f:Dynamic) fmlDeferredCalls.push(f);

// ---------------------------------------------------------------------------
// Tweens y temporizadores (`FunkinLua.hx:501-670`, `LuaUtils.hx:293-320`)
// ---------------------------------------------------------------------------
// Tweens y temporizadores sin `return` (ver RENDIMIENTO): los modcharts lanzan
// varios por beat, y cada uno pasaba por cuatro o cinco `return`.
function fmlCancelTween(?tag:Dynamic):Dynamic {
    var key = Std.string(tag);
    if (fmlTweens.exists(key)) {
        var tween = fmlTweens.get(key);
        fmlTweens.remove(key);
        if (tween != null) tween.cancel();
    }
    null;
}

function fmlCancelTimer(?tag:Dynamic):Dynamic {
    var key = Std.string(tag);
    if (fmlTimers.exists(key)) {
        var timer = fmlTimers.get(key);
        fmlTimers.remove(key);
        if (timer != null) timer.cancel();
    }
    null;
}

// tweenPrepare (`LuaUtils.hx:393`): cancela la etiqueta y resuelve el objeto.
function fmlTweenTarget(?vars:Dynamic):Dynamic {
    var path = Std.string(vars);
    path.indexOf(".") < 0 ? fmlObject(path) : fmlGetProperty(path);
}

function fmlStartTweenOn(?tag:Dynamic, ?target:Dynamic, ?values:Dynamic, ?duration:Dynamic, ?ease:Dynamic, ?varsName:String):Dynamic {
    var tween:Dynamic = null;
    if (target == null) fmlWarn("tween " + tag + ": no existe " + varsName);
    else {
        var key = tag == null ? null : Std.string(tag);
        if (key != null) fmlCancelTween(key);
        var options:Dynamic = {ease: fmlEase(ease)};
        if (key != null) {
            options.onComplete = function(twn) {
                if (fmlTweens.get(key) == twn) fmlTweens.remove(key);
                PlayState.instance.scripts.call("onTweenCompleted", [key, varsName]);
            };
        }
        tween = FlxTween.tween(target, values, fmlNum(duration, 1), options);
)fmlhx",
R"fmlhx(        if (key != null) fmlTweens.set(key, tween);
    }
    tween;
}

function fmlDoTween(?field:String, ?tag:Dynamic, ?vars:Dynamic, ?value:Dynamic, ?duration:Dynamic, ?ease:Dynamic):Dynamic {
    var target = fmlTweenTarget(vars);
    var values = {};
    Reflect.setField(values, field, fmlNum(value, 0));
    fmlStartTweenOn(tag, target, values, duration, ease, Std.string(vars));
}

function fmlDoTweenX(?tag, ?vars, ?value, ?duration, ?ease) fmlDoTween("x", tag, vars, value, duration, ease);
function fmlDoTweenY(?tag, ?vars, ?value, ?duration, ?ease) fmlDoTween("y", tag, vars, value, duration, ease);
function fmlDoTweenAngle(?tag, ?vars, ?value, ?duration, ?ease) fmlDoTween("angle", tag, vars, value, duration, ease);
function fmlDoTweenAlpha(?tag, ?vars, ?value, ?duration, ?ease) fmlDoTween("alpha", tag, vars, value, duration, ease);
function fmlDoTweenZoom(?tag, ?vars, ?value, ?duration, ?ease):Dynamic {
    var target = fmlTweenTarget(vars);
    var game = fmlGame();
    // Un zoom sobre camGame con camZooming activo tiene que mover tambien el
    // valor al que la camara vuelve, o el siguiente frame lo deshace
    // (`PlayState.hx:1671-1677` de Codename hace lo mismo en su evento).
    var isGame = target == game.camGame || target == FlxG.camera;
    var isHud = target == game.camHUD;
    var key = tag == null ? null : Std.string(tag);
    if (key != null) fmlCancelTween(key);
    var tween:Dynamic = null;
    if (target != null) {
        var finalZoom = fmlNum(value, 1);
        var options:Dynamic = {ease: fmlEase(ease)};
        options.onUpdate = function(twn) {
            if (isGame) game.defaultCamZoom = target.zoom;
            else if (isHud) game.defaultHudZoom = target.zoom;
        };
        options.onComplete = function(twn) {
            if (isGame) game.defaultCamZoom = finalZoom;
            else if (isHud) game.defaultHudZoom = finalZoom;
            if (key != null) {
                if (fmlTweens.get(key) == twn) fmlTweens.remove(key);
                game.scripts.call("onTweenCompleted", [key, Std.string(vars)]);
            }
        };
        tween = FlxTween.tween(target, {zoom: finalZoom}, fmlNum(duration, 1), options);
        if (key != null) fmlTweens.set(key, tween);
    }
    tween;
}

function fmlDoTweenColor(?tag:Dynamic, ?vars:Dynamic, ?color:Dynamic, ?duration:Dynamic, ?ease:Dynamic):Dynamic {
    var target = fmlTweenTarget(vars);
    var tween:Dynamic = null;
    if (target != null) {
        var key = tag == null ? null : Std.string(tag);
        if (key != null) fmlCancelTween(key);
        var options:Dynamic = {ease: fmlEase(ease)};
        if (key != null) options.onComplete = function(twn) {
            if (fmlTweens.get(key) == twn) fmlTweens.remove(key);
            PlayState.instance.scripts.call("onTweenCompleted", [key, Std.string(vars)]);
        };
        // En FlxTween.color la duracion va SEGUNDA (`FlxTween.color(sprite, duration, from, to, options)`).
        tween = FlxTween.color(target, fmlNum(duration, 1), target.color, fmlColor(color), options);
        if (key != null) fmlTweens.set(key, tween);
    }
    tween;
}

// startTween(tag, vars, values, duration, options) (`FunkinLua.hx:501`).
function fmlStartTween(?tag:Dynamic, ?vars:Dynamic, ?values:Dynamic, ?duration:Dynamic, ?options:Dynamic):Dynamic {
    var target = fmlTweenTarget(vars);
    var tween:Dynamic = null;
    if (target != null && values != null) {
        var key = tag == null ? null : Std.string(tag);
        if (key != null) fmlCancelTween(key);
        var tweenOptions:Dynamic = {};
        if (options != null) {
            if (Reflect.hasField(options, "ease")) tweenOptions.ease = fmlEase(Reflect.field(options, "ease"));
            if (Reflect.hasField(options, "startDelay")) tweenOptions.startDelay = fmlNum(Reflect.field(options, "startDelay"), 0);
            if (Reflect.hasField(options, "loopDelay")) tweenOptions.loopDelay = fmlNum(Reflect.field(options, "loopDelay"), 0);
            if (Reflect.hasField(options, "type")) {
                var type = Std.string(Reflect.field(options, "type")).toLowerCase();
                tweenOptions.type = type == "pingpong" ? FlxTween.PINGPONG : type == "looping" ? FlxTween.LOOPING : type == "backward" ? FlxTween.BACKWARD : FlxTween.ONESHOT;
            }
        }
        var done = key != null ? key : "";
        tweenOptions.onComplete = function(twn) {
            if (key != null && fmlTweens.get(key) == twn) fmlTweens.remove(key);
            PlayState.instance.scripts.call("onTweenCompleted", [done, Std.string(vars)]);
        };
        // Los valores llegan como objeto {x: 1, alpha: 0}: numeros dentro.
        var clean = {};
        for (field in Reflect.fields(values)) Reflect.setField(clean, field, fmlNum(Reflect.field(values, field), 0));
        tween = FlxTween.tween(target, clean, fmlNum(duration, 1), tweenOptions);
        if (key != null) fmlTweens.set(key, tween);
    }
    tween;
}

// noteTween*(tag, index, value, duration, ease) sobre los receptores 0-7. El
// indice da la vuelta como en Psych (`note % strumLineNotes.length`,
// `FunkinLua.hx:1706`): el 8 es el 0.
function fmlNoteTween(?field:String, ?tag:Dynamic, ?index:Dynamic, ?value:Dynamic, ?duration:Dynamic, ?ease:Dynamic):Dynamic {
    var strums = fmlStrumArray();
    var i = Std.int(fmlNum(index, 0));
    if (strums.length > 0) i = i % strums.length;
    if (i < 0 || i >= strums.length) { fmlWarn("noteTween: receptor " + i + " fuera de rango"); null; }
    else {
        var values = {};
        Reflect.setField(values, field, fmlNum(value, 0));
        fmlStartTweenOn(tag, strums[i], values, duration, ease, "strumLineNotes[" + i + "]");
    }
}
function fmlNoteTweenX(?tag, ?index, ?value, ?duration, ?ease) fmlNoteTween("x", tag, index, value, duration, ease);
function fmlNoteTweenY(?tag, ?index, ?value, ?duration, ?ease) fmlNoteTween("y", tag, index, value, duration, ease);
function fmlNoteTweenAngle(?tag, ?index, ?value, ?duration, ?ease) fmlNoteTween("angle", tag, index, value, duration, ease);
function fmlNoteTweenAlpha(?tag, ?index, ?value, ?duration, ?ease) fmlNoteTween("alpha", tag, index, value, duration, ease);
function fmlNoteTweenDirection(?tag, ?index, ?value, ?duration, ?ease) fmlNoteTween("angle", tag, index, value, duration, ease);

// runTimer(tag, time, loops): `onTimerCompleted(tag, loops, loopsLeft)`
// difundido a todos los scripts (`FunkinLua.hx:655-668`).
function fmlRunTimer(?tag:Dynamic, ?time:Dynamic, ?loops:Dynamic):Dynamic {
    var key = Std.string(tag);
    fmlCancelTimer(key);
    var count = Std.int(fmlNum(loops, 1));
    var timer = new FlxTimer();
    fmlTimers.set(key, timer);
    timer.start(fmlNum(time, 1), function(tmr) {
        if (tmr.finished && fmlTimers.get(key) == tmr) fmlTimers.remove(key);
        PlayState.instance.scripts.call("onTimerCompleted", [key, tmr.loops, tmr.loopsLeft]);
    }, count);
    key;
}

// Psych congela TODOS los tweens y temporizadores al pausar; Codename solo el
// de la cuenta atras (`PlayState.hx:1218-1229`). Sin esto, una intro con
// runTimer se desincroniza al pausar.
function fmlPauseRegistries(?paused:Bool) {
    for (key in fmlTweens.keys()) { var t = fmlTweens.get(key); if (t != null) t.active = !paused; }
    for (key in fmlTimers.keys()) { var t = fmlTimers.get(key); if (t != null) t.active = !paused; }
    for (key in fmlSounds.keys()) { var s = fmlSounds.get(key); if (s != null) { if (paused) s.pause(); else if (s.playing == false && fmlSoundWasPlaying.get(key) == true) s.resume(); } }
}
var fmlSoundWasPlaying:Map<String, Bool> = [];

// ---------------------------------------------------------------------------
// Sonido (`FunkinLua.hx:1314-1500`)
// ---------------------------------------------------------------------------
// El motor quita los puntos de la etiqueta (`FunkinLua.hx:1307`).
function fmlSoundTag(?tag:Dynamic):String tag == null ? "" : StringTools.replace(Std.string(tag), ".", "");

function fmlPlaySound(?sound:Dynamic, ?volume:Dynamic, ?tag:Dynamic):Dynamic {
    var vol = fmlNum(volume, 1);
    var key = fmlSoundTag(tag);
    if (key == "") FlxG.sound.play(Paths.sound(Std.string(sound)), vol)
    else {
        if (fmlSounds.exists(key)) { var old = fmlSounds.get(key); if (old != null) old.stop(); fmlSounds.remove(key); }
        var played = FlxG.sound.play(Paths.sound(Std.string(sound)), vol, false, null, true, function() {
            if (fmlSounds.get(key) != null) fmlSounds.remove(key);
            PlayState.instance.scripts.call("onSoundFinished", [key]);
        });
        fmlSounds.set(key, played);
        played;
    }
}

function fmlPlayMusic(?sound:Dynamic, ?volume:Dynamic, ?loop:Dynamic):Dynamic {
    FlxG.sound.playMusic(Paths.music(Std.string(sound)), fmlNum(volume, 1), fmlBool(loop, false));
    null;
}

// El motor exige tag.length > 1: una etiqueta de un caracter no hace nada.
function fmlSoundByTag(?tag:Dynamic):Dynamic {
    var key = fmlSoundTag(tag);
    key.length <= 1 ? null : fmlSounds.get(key);
}

function fmlStopSound(?tag:Dynamic):Dynamic { var s = fmlSoundByTag(tag); if (s != null) { s.stop(); fmlSounds.remove(fmlSoundTag(tag)); } null; }
function fmlPauseSound(?tag:Dynamic):Dynamic { var s = fmlSoundByTag(tag); if (s != null) s.pause(); null; }
function fmlResumeSound(?tag:Dynamic):Dynamic { var s = fmlSoundByTag(tag); if (s != null) s.play(); null; }
function fmlSoundFadeIn(?tag:Dynamic, ?duration:Dynamic, ?fromValue:Dynamic, ?toValue:Dynamic):Dynamic {
    var s = fmlSoundTag(tag) == "" ? FlxG.sound.music : fmlSoundByTag(tag);
    if (s != null) s.fadeIn(fmlNum(duration, 1), fmlNum(fromValue, 0), fmlNum(toValue, 1));
    null;
}
function fmlSoundFadeOut(?tag:Dynamic, ?duration:Dynamic, ?toValue:Dynamic):Dynamic {
    var s = fmlSoundTag(tag) == "" ? FlxG.sound.music : fmlSoundByTag(tag);
    if (s != null) s.fadeOut(fmlNum(duration, 1), fmlNum(toValue, 0));
    null;
}
function fmlSoundFadeCancel(?tag:Dynamic):Dynamic {
    var s = fmlSoundTag(tag) == "" ? FlxG.sound.music : fmlSoundByTag(tag);
    if (s != null && s.fadeTween != null) s.fadeTween.cancel();
    null;
}
function fmlGetSoundVolume(?tag:Dynamic):Float { var s = fmlSoundTag(tag) == "" ? FlxG.sound.music : fmlSoundByTag(tag); s == null ? 0 : s.volume; }
function fmlSetSoundVolume(?tag:Dynamic, ?value:Dynamic):Dynamic { var s = fmlSoundTag(tag) == "" ? FlxG.sound.music : fmlSoundByTag(tag); if (s != null) s.volume = fmlNum(value, 1); null; }
function fmlGetSoundTime(?tag:Dynamic):Float { var s = fmlSoundTag(tag) == "" ? FlxG.sound.music : fmlSoundByTag(tag); s == null ? 0 : s.time; }
function fmlSetSoundTime(?tag:Dynamic, ?value:Dynamic):Dynamic { var s = fmlSoundTag(tag) == "" ? FlxG.sound.music : fmlSoundByTag(tag); if (s != null) s.time = fmlNum(value, 0); null; }
function fmlGetSoundPitch(?tag:Dynamic):Float { var s = fmlSoundTag(tag) == "" ? FlxG.sound.music : fmlSoundByTag(tag); s == null ? 1 : s.pitch; }
function fmlSetSoundPitch(?tag:Dynamic, ?value:Dynamic):Dynamic { var s = fmlSoundTag(tag) == "" ? FlxG.sound.music : fmlSoundByTag(tag); if (s != null) s.pitch = fmlNum(value, 1); null; }
function fmlPrecacheSound(?name:Dynamic):Dynamic { if (name != null) FlxG.sound.load(Paths.sound(Std.string(name))); null; }
function fmlPrecacheMusic(?name:Dynamic):Dynamic { if (name != null) FlxG.sound.load(Paths.music(Std.string(name))); null; }
function fmlPrecacheImage(?name:Dynamic):Dynamic null;
function fmlMusicFadeIn(?duration:Dynamic, ?fromValue:Dynamic, ?toValue:Dynamic):Dynamic { if (FlxG.sound.music != null) FlxG.sound.music.fadeIn(fmlNum(duration, 1), fmlNum(fromValue, 0), fmlNum(toValue, 1)); null; }
function fmlMusicFadeOut(?duration:Dynamic, ?toValue:Dynamic):Dynamic { if (FlxG.sound.music != null) FlxG.sound.music.fadeOut(fmlNum(duration, 1), fmlNum(toValue, 0)); null; }

// ---------------------------------------------------------------------------
)fmlhx",
R"fmlhx(// Personajes y camara (`FunkinLua.hx:773-935`, `DeprecatedFunctions.hx:33`)
// ---------------------------------------------------------------------------
function fmlCharacterPlayAnim(?character:Dynamic, ?anim:Dynamic, ?forced:Dynamic):Dynamic {
    var who = fmlCharacter(character);
    // Psych ademas pone specialAnim = true para que el beat no lo corte; en
    // Codename playAnim(anim, force, NONE) ya no se corta (`Character.hx:179-191`).
    if (who != null) who.playAnim(Std.string(anim), fmlBool(forced, false));
    who;
}
function fmlCharacterDance(?character:Dynamic):Dynamic { var who = fmlCharacter(character); if (who != null) who.dance(); who; }
function fmlGetCharacterX(?character:Dynamic):Float { var who = fmlCharacter(character); who == null ? 0 : who.x; }
function fmlGetCharacterY(?character:Dynamic):Float { var who = fmlCharacter(character); who == null ? 0 : who.y; }
function fmlSetCharacterX(?character:Dynamic, ?value:Dynamic):Dynamic { var who = fmlCharacter(character); if (who != null) who.x = fmlNum(value, 0); who; }
function fmlSetCharacterY(?character:Dynamic, ?value:Dynamic):Dynamic { var who = fmlCharacter(character); if (who != null) who.y = fmlNum(value, 0); who; }
function fmlAddCharacterToList(?name:Dynamic, ?type:Dynamic):Dynamic null;

function fmlCameraShake(?camera:Dynamic, ?intensity:Dynamic, ?duration:Dynamic):Dynamic {
    fmlCameraByName(camera).shake(fmlNum(intensity, 0.05), fmlNum(duration, 0.5));
    null;
}
function fmlCameraFlash(?camera:Dynamic, ?color:Dynamic, ?duration:Dynamic, ?forced:Dynamic):Dynamic {
    fmlCameraByName(camera).flash(fmlColor(color), fmlNum(duration, 1), null, fmlBool(forced, false));
    null;
}
function fmlCameraFade(?camera:Dynamic, ?color:Dynamic, ?duration:Dynamic, ?forced:Dynamic, ?fadeIn:Dynamic):Dynamic {
    fmlCameraByName(camera).fade(fmlColor(color), fmlNum(duration, 1), fmlBool(fadeIn, false), null, fmlBool(forced, false));
    null;
}
// La primera strumline que tiene a ese personaje (sin break: ver RENDIMIENTO).
function fmlCameraSetTarget(?target:Dynamic):Dynamic {
    var game = PlayState.instance;
    var key = target == null ? "" : Std.string(target).toLowerCase();
    var who = fmlCharacter(key);
    if (who != null) {
        var i = 0;
        var lines = game.strumLines.members;
        while (i < lines.length) {
            var line = lines[i];
            if (line != null && line.characters != null && line.characters.indexOf(who) >= 0) {
                game.curCameraTarget = i;
                game.moveCamera();
                i = lines.length;
            }
            else i++;
        }
    }
    null;
}
function fmlSetCameraFollowPoint(?x:Dynamic, ?y:Dynamic):Dynamic {
    var game = PlayState.instance;
    // Forzada: moveCamera() sobrescribe camFollow cada frame salvo con
    // curCameraTarget == -1 (`PlayState.hx:1515`).
    game.curCameraTarget = -1;
    game.camFollow.setPosition(fmlNum(x, 0), fmlNum(y, 0));
    null;
}
function fmlAddCameraFollowPoint(?x:Dynamic, ?y:Dynamic):Dynamic { var game = fmlGame(); game.curCameraTarget = -1; game.camFollow.x += fmlNum(x, 0); game.camFollow.y += fmlNum(y, 0); null; }
function fmlSetCameraScroll(?x:Dynamic, ?y:Dynamic):Dynamic { FlxG.camera.scroll.set(fmlNum(x, 0), fmlNum(y, 0)); null; }
function fmlAddCameraScroll(?x:Dynamic, ?y:Dynamic):Dynamic { FlxG.camera.scroll.x += fmlNum(x, 0); FlxG.camera.scroll.y += fmlNum(y, 0); null; }
function fmlGetCameraFollowX():Float fmlGame().camFollow.x;
function fmlGetCameraFollowY():Float fmlGame().camFollow.y;
function fmlGetCameraScrollX():Float FlxG.camera.scroll.x;
function fmlGetCameraScrollY():Float FlxG.camera.scroll.y;

// ---------------------------------------------------------------------------
// Eventos: triggerEvent y los eventos de Psych que Codename no trae
// (`states/PlayState.hx:2061-2305` de Psych)
// ---------------------------------------------------------------------------
function fmlEventFloat(?value:Dynamic):Null<Float> {
    var parsed:Dynamic = null;
    if (value != null) {
        var f = Std.parseFloat(Std.string(value));
        if (!Math.isNaN(f)) parsed = f;
    }
    parsed;
}

// Devuelve true si el evento es de Psych y se ejecuto aqui. Sin `return`: el
// valor es el del caso (triggerEvent puede llegar varias veces por beat).
function fmlRunPsychEvent(?name:String, ?value1:Dynamic, ?value2:Dynamic):Bool {
    var game = PlayState.instance;
    var v1 = value1 == null ? "" : Std.string(value1);
    var v2 = value2 == null ? "" : Std.string(value2);
    var f1 = fmlEventFloat(value1);
    var f2 = fmlEventFloat(value2);
    switch (name) {
        case "Hey!": {
            var value = 2;
            var key = StringTools.trim(v1.toLowerCase());
            if (key == "bf" || key == "boyfriend" || key == "0") value = 0;
            else if (key == "gf" || key == "girlfriend" || key == "1") value = 1;
            if (value != 0 && game.gf != null) game.gf.playAnim("cheer", true);
            if (value != 1 && game.boyfriend != null) game.boyfriend.playAnim("hey", true);
            true;
        }
        case "Set GF Speed": {
            if (f1 == null || f1 < 1) f1 = 1;
            game.gfSpeed = Math.round(f1);
            true;
        }
        case "Add Camera Zoom": {
            if (Options.camZoomOnBeat && FlxG.camera.zoom < 1.35) {
                FlxG.camera.zoom += f1 == null ? 0.015 : f1;
                game.camHUD.zoom += f2 == null ? 0.03 : f2;
            }
            true;
        }
        case "Play Animation": {
            var who = game.dad;
            var key = StringTools.trim(v2.toLowerCase());
            if (key == "bf" || key == "boyfriend") who = game.boyfriend;
            else if (key == "gf" || key == "girlfriend") who = game.gf;
            else if (f2 != null && Math.round(f2) == 1) who = game.boyfriend;
            else if (f2 != null && Math.round(f2) == 2) who = game.gf;
            if (who != null) who.playAnim(v1, true);
            true;
        }
        case "Camera Follow Pos": {
            if (game.curCameraTarget >= 0) fmlCameraSectionTarget = game.curCameraTarget;
            fmlCameraForced = f1 != null || f2 != null;
            if (f1 != null || f2 != null) {
                game.curCameraTarget = -1;
                game.camFollow.setPosition(f1 == null ? 0 : f1, f2 == null ? 0 : f2);
            } else if (game.curCameraTarget < 0) {
                game.curCameraTarget = fmlSectionTarget();
            }
            true;
        }
        case "Alt Idle Animation": {
            var who = game.dad;
            var key = StringTools.trim(v1.toLowerCase());
            if (key == "gf" || key == "girlfriend") who = game.gf;
            else if (key == "boyfriend" || key == "bf") who = game.boyfriend;
            else { var n = Std.parseInt(v1); if (n == 1) who = game.boyfriend; else if (n == 2) who = game.gf; }
            if (who != null) who.idleSuffix = v2;
            true;
        }
        case "Screen Shake": {
            var targets = [game.camGame, game.camHUD];
            var values = [v1, v2];
            for (i in 0...2) {
                var split = values[i].split(",");
                var duration = split.length > 0 ? fmlNum(StringTools.trim(split[0]), 0) : 0;
                var intensity = split.length > 1 ? fmlNum(StringTools.trim(split[1]), 0) : 0;
                if (duration > 0 && intensity != 0 && targets[i] != null) targets[i].shake(intensity, duration);
            }
            true;
        }
        case "Change Character": { fmlChangeCharacter(v1, v2); true; }
        case "Change Scroll Speed": {
            if (f1 == null) f1 = 1;
            if (f2 == null) f2 = 0;
            var base = PlayState.SONG.scrollSpeed != null ? PlayState.SONG.scrollSpeed : game.scrollSpeed;
            var target = base * f1;
            if (f2 <= 0) game.scrollSpeed = target;
            else FlxTween.tween(game, {scrollSpeed: target}, f2, {ease: FlxEase.linear});
            true;
        }
        case "Set Property": {
            var trimmed = StringTools.trim(v2);
            var trueValue:Dynamic = trimmed;
            if (trimmed == "true" || trimmed == "false") trueValue = trimmed == "true";
            else if (f2 != null) trueValue = f2;
            fmlSetProperty(v1, trueValue);
            true;
        }
        case "Play Sound": { FlxG.sound.play(Paths.sound(v1), f2 == null ? 1 : f2); true; }
        default: false;
    }
}

// Change Character: cambia el personaje de la strumline conservando posicion,
// camara e icono (Psych `PlayState.hx:2185-2260`).
function fmlChangeCharacter(?value1:String, ?value2:String) {
    var game = fmlGame();
    var key = StringTools.trim(value1.toLowerCase());
    var type = 0;
    if (key == "gf" || key == "girlfriend") type = 2;
    else if (key == "dad" || key == "opponent") type = 1;
    else { var n = Std.parseInt(key); if (n != null) type = n; }
    var old:Dynamic = type == 0 ? game.boyfriend : type == 1 ? game.dad : game.gf;
    if (old == null || old.curCharacter == value2) return;
    var line:Dynamic = null;
    var index = -1;
    for (l in game.strumLines.members) if (l != null && l.characters != null) { var at = l.characters.indexOf(old); if (at >= 0) { line = l; index = at; } }
    if (line == null) return;
    var fresh = new Character(old.x, old.y, value2, old.isPlayer);
    // applyCharStuff coloca el personaje en su marca del escenario y lo mete
    // en el estado (`Stage.hx:342-350`).
    game.stage.applyCharStuff(fresh, type == 0 ? "boyfriend" : type == 1 ? "dad" : "girlfriend", index);
    fresh.alpha = old.alpha;
    fresh.visible = old.visible;
    game.remove(old, true);
    line.characters[index] = fresh;
    var icon = fresh.icon != null ? fresh.icon : fresh.curCharacter;
    if (type == 0 && game.iconP1 != null) game.iconP1.setIcon(icon);
    if (type == 1 && game.iconP2 != null) game.iconP2.setIcon(icon);
    old.destroy();
}

function fmlTriggerEvent(?name:Dynamic, ?value1:Dynamic, ?value2:Dynamic):Dynamic {
    var key = name == null ? "" : Std.string(name);
    // Un evento de Codename por nombre (Camera Zoom, Camera Flash...) o un
    // evento custom con su script en data/events/: por el motor, para que
    // onEvent/onPostEvent lo vean con su nombre.
    var event = {name: key, time: Conductor.songPosition, params: [value1, value2],
        psych: {name: key, value1: value1, value2: value2, native: false}};
    PlayState.instance.executeEvent(event);
    null;
}

// El nombre y los valores de Psych de un evento del chart: el importador los
// deja en `event.psych` cuando renombra el evento (`psych: {name, value1, value2}`).
// Se piden en cada evento de cada script: sin `return`.
function fmlEventName(?event:Dynamic):String {
    if (event == null || event.event == null) ""
    else {
        var raw = event.event;
        (Reflect.hasField(raw, "psych") && raw.psych != null && raw.psych.name != null) ? raw.psych.name : raw.name;
    }
}

function fmlEventValue(?event:Dynamic, ?which:Int):Dynamic {
    var result:Dynamic = null;
    if (event != null && event.event != null) {
        var raw = event.event;
        if (Reflect.hasField(raw, "psych") && raw.psych != null) result = which == 1 ? raw.psych.value1 : raw.psych.value2;
        else if (raw.params != null && raw.params.length >= which) {
            var value = raw.params[which - 1];
            if (value != null) result = Std.string(value);
        }
    }
    result;
}

function fmlEventIsAuxiliary(?event:Dynamic):Bool {
    if (event == null || event.event == null) false
    else {
        var raw = event.event;
        Reflect.hasField(raw, "psych") && raw.psych != null &&
            Reflect.hasField(raw.psych, "auxiliary") && raw.psych.auxiliary == true;
    }
}

function fmlCameraTargetName(?event:Dynamic):String {
    var game = PlayState.instance;
    if (event == null || event.strumLine == null) ""
    else {
)fmlhx",
R"fmlhx(        var line = event.strumLine;
        if (line.characters != null && line.characters.indexOf(game.boyfriend) >= 0) "boyfriend"
        else if (line.characters != null && line.characters.indexOf(game.dad) >= 0) "dad"
        else if (line.characters != null && line.characters.indexOf(game.gf) >= 0) "gf"
        else line.opponentSide ? "dad" : "boyfriend";
    }
}

function fmlNoteIndex(?note:Dynamic):Int {
    if (note == null) -1
    else {
        // El id de Psych pertenece a notes, no a todo el chart. Releer cuando
        // cambia la nota del callback refleja altas/bajas en el mismo frame;
        // los distintos scripts del mismo callback comparten la vista.
        if (fmlIndexedNote != note) {
            fmlIndexedNote = note;
            fmlSpawnedNotesCacheFrame = -1;
        }
        fmlSpawnedNotes().indexOf(note);
    }
}

function fmlNoteTypeName(?name:Dynamic):String name == null ? "" : Std.string(name);

// ---------------------------------------------------------------------------
// Flujo, puntuacion, comunicacion, sistema
// ---------------------------------------------------------------------------
function fmlStartCountdown():Dynamic { fmlGame().startCountdown(); true; }
function fmlEndSong():Dynamic { fmlGame().endSong(); true; }
function fmlRestartSong():Dynamic { FlxG.resetState(); true; }
function fmlExitSong():Dynamic {
    if (PlayState.isStoryMode) FlxG.switchState(new StoryMenuState()); else FlxG.switchState(new FreeplayState());
    true;
}
// El close() de Psych apaga el script que llama; la capa no sabe cual es,
// asi que el emisor traduce close() -> disableScript() en el propio script.
// Aqui solo queda por si algo llama psych.close() de forma indirecta.
function fmlClose():Dynamic fmlPending("close", []);

function fmlSetHealth(?value:Dynamic):Dynamic { fmlGame().health = fmlNum(value, 1); null; }
function fmlAddHealth(?value:Dynamic):Dynamic { fmlGame().health += fmlNum(value, 0); null; }
function fmlGetHealth():Float fmlGame().health;
function fmlAddScore(?value:Dynamic):Dynamic { fmlGame().songScore += Std.int(fmlNum(value, 0)); fmlUpdateScoreText(); null; }
function fmlSetScore(?value:Dynamic):Dynamic { fmlGame().songScore = Std.int(fmlNum(value, 0)); fmlUpdateScoreText(); null; }
function fmlAddMisses(?value:Dynamic):Dynamic { fmlGame().misses += Std.int(fmlNum(value, 0)); fmlUpdateScoreText(); null; }
function fmlSetMisses(?value:Dynamic):Dynamic { fmlGame().misses = Std.int(fmlNum(value, 0)); fmlUpdateScoreText(); null; }
function fmlAddHits(?value:Dynamic):Dynamic { fmlHits += Std.int(fmlNum(value, 0)); null; }
function fmlSetHits(?value:Dynamic):Dynamic { fmlHits = Std.int(fmlNum(value, 0)); null; }
function fmlSetRatingPercent(?value:Dynamic):Dynamic { fmlRatingPercent = fmlNum(value, 0); null; }
function fmlSetRatingName(?value:Dynamic):Dynamic { fmlRatingName = Std.string(value); null; }
function fmlSetRatingFC(?value:Dynamic):Dynamic { fmlRatingFC = Std.string(value); null; }
function fmlUpdateScoreText():Dynamic { var game = fmlGame(); if (game.updateRatingStuff != null) game.updateRatingStuff(); null; }

function fmlSetVarShared(?name:Dynamic, ?value:Dynamic):Dynamic { fmlVars.set(Std.string(name), value); value; }
function fmlGetVarShared(?name:Dynamic):Dynamic fmlVars.get(Std.string(name));

function fmlCallOnScripts(?name:Dynamic, ?args:Dynamic):Dynamic {
    var list:Array<Dynamic> = args == null ? [] : (fmlIsArray(args) ? args : [args]);
    PlayState.instance.scripts.call(Std.string(name), list);
    null;
}
function fmlCallScript(?script:Dynamic, ?name:Dynamic, ?args:Dynamic):Dynamic {
    var target = fmlFindScript(script);
    var list:Array<Dynamic> = args == null ? [] : (fmlIsArray(args) ? args : [args]);
    target != null ? target.call(Std.string(name), list) : fmlCallOnScripts(name, args);
}

// La ruta instalada de un script de Psych (`scripts/foo`, `foo.lua`...):
// fml-psych-scripts.json de la cancion, y si no, los sitios donde el
// importador escribe.
function fmlScriptPath(?name:Dynamic):String {
    var key = Std.string(name);
    if (StringTools.endsWith(key, ".lua")) key = key.substr(0, key.length - 4);
    if (StringTools.endsWith(key, ".hx")) key = key.substr(0, key.length - 3);
    if (fmlScriptsMap.exists(key)) return fmlScriptsMap.get(key);
    var stem = key.split("/").pop();
    if (fmlScriptsMap.exists(stem)) return fmlScriptsMap.get(stem);
    var song = PlayState.SONG.meta != null ? PlayState.SONG.meta.name : "";
    for (candidate in ["songs/" + song + "/scripts/" + stem, "songs/" + stem, "songs/fml-psych-chars/" + stem, "data/stages/" + stem, "data/events/" + stem, "data/notes/" + stem]) {
        if (Assets.exists(Paths.script(candidate))) return candidate;
    }
    return "";
}

function fmlFindScript(?name:Dynamic):Dynamic {
    var path = fmlScriptPath(name);
    if (path == "") return null;
    var full = Paths.script(path);
    return fmlGame().scripts.getByPath(full);
}

function fmlAddLuaScript(?name:Dynamic, ?ignoreAlreadyRunning:Dynamic):Dynamic {
    var path = fmlScriptPath(name);
    if (path == "") { fmlWarn("addLuaScript: no hay un script instalado para " + name); return null; }
    var full = Paths.script(path);
    var pack = fmlGame().scripts;
    if (pack.contains(full)) {
        var existing = pack.getByPath(full);
        if (existing != null) existing.active = true;
        return existing;
    }
    var script = pack.importScript(path);
    if (script != null && fmlPostCreated) { script.call("create", []); script.call("postCreate", []); }
    else if (script != null) script.call("create", []);
    return script;
}

function fmlRemoveLuaScript(?name:Dynamic):Dynamic {
    var script = fmlFindScript(name);
    if (script != null) script.active = false;
    null;
}

function fmlIsRunning(?name:Dynamic):Bool {
    var script = fmlFindScript(name);
    script != null && script.active;
}

function fmlGetRunningScripts():Array<String> {
    var out:Array<String> = [];
    for (s in PlayState.instance.scripts.scripts) if (s.active) out.push(s.fileName);
    out;
}

function fmlDebugPrint(?a:Dynamic, ?b:Dynamic, ?c:Dynamic, ?d:Dynamic):Dynamic {
    var text = Std.string(a);
    if (b != null) text += " " + Std.string(b);
    if (c != null) text += " " + Std.string(c);
    if (d != null) text += " " + Std.string(d);
    trace(text);
    null;
}

function fmlGetRandomInt(?min:Dynamic, ?max:Dynamic, ?exclude:Dynamic):Int {
    var lo = Std.int(fmlNum(min, 0));
    var hi = Std.int(fmlNum(max, 0x7FFFFFFF));
    var excluded:Array<Int> = [];
    if (exclude != null) for (v in Std.string(exclude).split(",")) { var n = Std.parseInt(StringTools.trim(v)); if (n != null) excluded.push(n); }
    FlxG.random.int(lo, hi, excluded);
}
function fmlGetRandomFloat(?min:Dynamic, ?max:Dynamic, ?exclude:Dynamic):Float
    FlxG.random.float(fmlNum(min, 0), fmlNum(max, 1));
function fmlGetRandomBool(?chance:Dynamic):Bool FlxG.random.bool(fmlNum(chance, 50));
function fmlGetColorFromHex(?value:Dynamic):Int fmlColor(value);
function fmlStringStartsWith(?text:Dynamic, ?prefix:Dynamic):Bool StringTools.startsWith(Std.string(text), Std.string(prefix));
function fmlStringEndsWith(?text:Dynamic, ?suffix:Dynamic):Bool StringTools.endsWith(Std.string(text), Std.string(suffix));
function fmlStringSplit(?text:Dynamic, ?separator:Dynamic):Array<String> Std.string(text).split(Std.string(separator));
function fmlStringTrim(?text:Dynamic):String StringTools.trim(Std.string(text));
// Cadenas y math de Lua: los scripts de texto las llaman cada fotograma
// (string.format en el marcador). Sin `return` ni break.
function fmlStringSub(?text:Dynamic, ?from:Dynamic, ?to:Dynamic):String {
    // string.sub en base 1 y con negativos desde el final.
    var s = Std.string(text);
    var i = Std.int(fmlNum(from, 1));
    var j = to == null ? s.length : Std.int(fmlNum(to, s.length));
    if (i < 0) i = s.length + i + 1;
    if (j < 0) j = s.length + j + 1;
    if (i < 1) i = 1;
    if (j > s.length) j = s.length;
    i > j ? "" : s.substr(i - 1, j - i + 1);
}
function fmlStringRep(?text:Dynamic, ?count:Dynamic):String {
    var out = "";
    var n = Std.int(fmlNum(count, 0));
    for (i in 0...n) out += Std.string(text);
    out;
}
function fmlStringFind(?text:Dynamic, ?pattern:Dynamic):Dynamic {
    var at = Std.string(text).indexOf(Std.string(pattern));
    at < 0 ? null : at + 1;
}
function fmlStringGsub(?text:Dynamic, ?pattern:Dynamic, ?replacement:Dynamic):String
    StringTools.replace(Std.string(text), Std.string(pattern), Std.string(replacement));
function fmlStringFormat(?format:Dynamic, ?a:Dynamic, ?b:Dynamic, ?c:Dynamic, ?d:Dynamic):String {
    // Solo %s, %d y %f sin anchura: lo que el corpus usa. El primer valor
    // nulo, o la primera marca que ya no esta, terminan el formato.
    var out = Std.string(format);
    var values = [a, b, c, d];
    var k = 0;
    while (k < values.length) {
        var value = values[k];
        var at = -1;
        if (value != null)
            for (spec in ["%s", "%d", "%i", "%f", "%.2f", "%.1f", "%.0f"]) { var p = out.indexOf(spec); if (p >= 0 && (at < 0 || p < at)) at = p; }
        if (at < 0) k = values.length;
        else {
            var spec = out.substr(at, out.indexOf("f", at) == at + 3 ? 4 : 2);
            var text = Std.string(value);
            if (spec == "%d" || spec == "%i" || spec == "%.0f") text = Std.string(Math.round(fmlNum(value, 0)));
            else if (spec == "%.2f") text = Std.string(Math.round(fmlNum(value, 0) * 100) / 100);
            else if (spec == "%.1f") text = Std.string(Math.round(fmlNum(value, 0) * 10) / 10);
            out = out.substr(0, at) + text + out.substr(at + spec.length);
            k++;
        }
    }
    out;
}
function fmlTableConcat(?list:Dynamic, ?separator:Dynamic):String {
    var parts:Array<String> = [];
    if (list != null) for (v in list) parts.push(Std.string(v));
    parts.join(separator == null ? "" : Std.string(separator));
}
function fmlMathRandom(?a:Dynamic, ?b:Dynamic):Float {
    if (a == null) Math.random()
    else if (b == null) FlxG.random.int(1, Std.int(fmlNum(a, 1)))
    else FlxG.random.int(Std.int(fmlNum(a, 1)), Std.int(fmlNum(b, 1)));
}
function fmlCheckFileExists(?path:Dynamic, ?absolute:Dynamic):Bool {
    var key = Std.string(path);
    Assets.exists(Paths.getPath(key)) || Assets.exists(key);
}
function fmlGetTextFromFile(?path:Dynamic, ?ignoreMods:Dynamic):String {
    var full = Paths.getPath(Std.string(path));
    Assets.exists(full) ? Assets.getText(full) : "";
}
function fmlGetModSetting(?name:Dynamic, ?modName:Dynamic):Dynamic {
    if (fmlModSettings == null) {
        fmlModSettings = {};
        var path = Paths.getPath("data/settings.json");
        if (Assets.exists(path)) {
            try {
                var parsed = Json.parse(Assets.getText(path));
                for (field in Reflect.fields(parsed)) {
                    var entry = Reflect.field(parsed, field);
                    if (entry != null && Reflect.hasField(entry, "value")) Reflect.setField(fmlModSettings, field, Reflect.field(entry, "value"));
                    else if (entry != null && Reflect.hasField(entry, "defaultValue")) Reflect.setField(fmlModSettings, field, Reflect.field(entry, "defaultValue"));
                }
            } catch (e:Dynamic) {}
        }
    }
    var key = Std.string(name);
    Reflect.hasField(fmlModSettings, key) ? Reflect.field(fmlModSettings, key) : fmlPending("getModSetting(" + key + ")", []);
}

function fmlKeyName(?key:Dynamic):String Std.string(key).toUpperCase();
function fmlKeyJustPressed(?key:Dynamic):Bool Reflect.field(FlxG.keys.justPressed, fmlKeyName(key)) == true;
function fmlKeyPressed(?key:Dynamic):Bool Reflect.field(FlxG.keys.pressed, fmlKeyName(key)) == true;
function fmlKeyReleased(?key:Dynamic):Bool Reflect.field(FlxG.keys.justReleased, fmlKeyName(key)) == true;
function fmlMouseClicked(?button:Dynamic):Bool FlxG.mouse.justPressed;
)fmlhx",
R"fmlhx(function fmlMousePressed(?button:Dynamic):Bool FlxG.mouse.pressed;
function fmlMouseReleased(?button:Dynamic):Bool FlxG.mouse.justReleased;
function fmlGetMouseX(?camera:Dynamic):Float FlxG.mouse.getScreenPosition(fmlCameraByName(camera)).x;
function fmlGetMouseY(?camera:Dynamic):Float FlxG.mouse.getScreenPosition(fmlCameraByName(camera)).y;

// Save data (`ExtraFunctions.hx:96-140`): un FlxSave con prefijo del mod.
function fmlSaveBind(?name:Dynamic, ?folder:Dynamic):Dynamic {
    var save = new flixel.util.FlxSave();
    save.bind("fml-psych-" + Std.string(name), folder == null ? "funkin-mod-lab" : Std.string(folder));
    fmlSave = save;
    save;
}
function fmlInitSaveData(?name:Dynamic, ?folder:Dynamic):Dynamic { fmlSaveBind(name, folder); null; }
function fmlFlushSaveData(?name:Dynamic):Dynamic { if (fmlSave != null) fmlSave.flush(); null; }
function fmlGetDataFromSave(?name:Dynamic, ?field:Dynamic, ?defaultValue:Dynamic):Dynamic {
    if (fmlSave == null) fmlSaveBind(name, null);
    var value = Reflect.field(fmlSave.data, Std.string(field));
    value == null ? defaultValue : value;
}
function fmlSetDataFromSave(?name:Dynamic, ?field:Dynamic, ?value:Dynamic):Dynamic {
    if (fmlSave == null) fmlSaveBind(name, null);
    Reflect.setField(fmlSave.data, Std.string(field), value);
    null;
}

// ---- shaders (`ShaderFunctions.hx`) ---------------------------------------
// setShaderFloat("x", "iTime", t) va en onUpdate de casi todo shader: sin `return`.
var fmlShaders:Map<String, Dynamic> = [];
function fmlInitLuaShader(?name:Dynamic, ?glslVersion:Dynamic):Bool {
    var key = Std.string(name);
    var ok = fmlShaders.exists(key);
    if (!ok) {
        try {
            fmlShaders.set(key, new CustomShader(key));
            ok = true;
        } catch (e:Dynamic) {
            fmlWarn("initLuaShader(" + key + "): " + Std.string(e));
        }
    }
    ok;
}
function fmlSetSpriteShader(?tag:Dynamic, ?shader:Dynamic):Bool {
    var sprite = fmlObject(tag);
    var key = Std.string(shader);
    var ok = sprite != null && (fmlShaders.exists(key) || fmlInitLuaShader(key, null));
    if (ok) sprite.shader = fmlShaders.get(key);
    ok;
}
function fmlRemoveSpriteShader(?tag:Dynamic):Bool {
    var sprite = fmlObject(tag);
    if (sprite != null) sprite.shader = null;
    sprite != null;
}
function fmlShaderOf(?tag:Dynamic):Dynamic {
    var sprite = fmlObject(tag);
    (sprite != null && sprite.shader != null) ? sprite.shader : fmlShaders.get(Std.string(tag));
}
// Los uniformes de un shader de Codename no son campos: FunkinShader es un
// IHScriptCustomBehaviour y los lee y escribe por hget/hset
// (`FunkinShader.hx:525-548`). Reflect.setProperty no los ve y lanzaba
// "Invalid field:iTime" en cada fotograma: con VS Matt, 1 800 lineas por
// segundo en la consola y el juego congelado.
function fmlSetShaderValue(?tag:Dynamic, ?uniform:Dynamic, ?value:Dynamic):Bool {
    var shader = fmlShaderOf(tag);
    var ok = false;
    if (shader != null) {
        try { shader.hset(Std.string(uniform), value); ok = true; }
        catch (e:Dynamic) { fmlWarn("setShader " + Std.string(uniform) + ": " + Std.string(e)); }
    }
    ok;
}
function fmlSetShaderFloat(?tag, ?uniform, ?value) fmlSetShaderValue(tag, uniform, fmlNum(value, 0));
function fmlSetShaderInt(?tag, ?uniform, ?value) fmlSetShaderValue(tag, uniform, Std.int(fmlNum(value, 0)));
function fmlSetShaderBool(?tag, ?uniform, ?value) fmlSetShaderValue(tag, uniform, fmlBool(value, false));
function fmlSetShaderFloatArray(?tag, ?uniform, ?values) fmlSetShaderValue(tag, uniform, values);
function fmlSetShaderIntArray(?tag, ?uniform, ?values) fmlSetShaderValue(tag, uniform, values);
function fmlSetShaderBoolArray(?tag, ?uniform, ?values) fmlSetShaderValue(tag, uniform, values);
function fmlGetShaderValue(?tag:Dynamic, ?uniform:Dynamic):Dynamic {
    var shader = fmlShaderOf(tag);
    var value:Dynamic = null;
    if (shader != null) {
        try { value = shader.hget(Std.string(uniform)); }
        catch (e:Dynamic) { fmlWarn("getShader " + Std.string(uniform) + ": " + Std.string(e)); }
    }
    value;
}

// ---- ventana (decision: simulada salvo que la importacion active la real) --
// Los scripts de temblor de ventana la mueven en cada fotograma: sin `return`.
function fmlWindowSet(?path:String, ?value:Dynamic):Dynamic {
    var field = path.split(".").pop();
    if (!fmlWindowReal) {
        if (!fmlPendingSeen.exists("window")) { fmlPendingSeen.set("window", true); trace("[FML] window simulada: " + path + " (activa los efectos de ventana reales al importar)"); }
        value;
    }
    else switch (field) {
        case "x": { window.x = Std.int(fmlNum(value, window.x)); value; }
        case "y": { window.y = Std.int(fmlNum(value, window.y)); value; }
        case "width": { window.width = Std.int(fmlNum(value, window.width)); value; }
        case "height": { window.height = Std.int(fmlNum(value, window.height)); value; }
        case "title": { window.title = Std.string(value); value; }
        case "borderless": { window.borderless = fmlBool(value, false); value; }
        case "fullscreen": { window.fullscreen = fmlBool(value, false); value; }
        default: fmlPending("openfl.Lib." + path, [value]);
    }
}
function fmlWindowGet(?path:String):Dynamic {
    switch (path.split(".").pop()) {
        case "x": window.x;
        case "y": window.y;
        case "width": window.width;
        case "height": window.height;
        case "title": window.title;
        default: fmlPending("openfl.Lib." + path, []);
    }
}

// ---------------------------------------------------------------------------
// Las variables globales de Psych, cada frame
// ---------------------------------------------------------------------------
function fmlRefreshGlobals() {
    var game = PlayState.instance;
    if (game != null) fmlRefreshGlobalsFrom(game);
}

function fmlRefreshGlobalsFrom(game:Dynamic) {
    curBpm = Conductor.bpm;
    crochet = Conductor.crochet;
    stepCrochet = Conductor.stepCrochet;
    curSection = Conductor.curMeasure;
    curDecStep = Conductor.curStepFloat;
    curDecBeat = Conductor.curBeatFloat;
    songPos = Conductor.songPosition;
    if (game.inst != null) songLength = game.inst.length;
    mustHitSection = game.curCameraTarget == 1;
    inGameOver = fmlInGameOver;
    if (game.playerStrums != null) botPlay = game.playerStrums.cpu;
    bpm = Conductor.bpm;
    scrollSpeed = game.scrollSpeed;
    curStep = Conductor.curStep;
    curBeat = Conductor.curBeat;
    combo = game.combo;
    score = game.songScore;
    misses = game.misses;
    health = game.health;
    startedCountdown = game.startedCountdown;
    seenCutscene = PlayState.seenCutscene;
    downscroll = Options.downscroll;
    isStoryMode = PlayState.isStoryMode;
    curStage = PlayState.SONG.stage;
    if (PlayState.SONG.meta != null) songNameShort = PlayState.SONG.meta.name;
    ratingName = fmlRatingName;
    ratingFC = fmlRatingFC;
    rating = fmlRatingPercent;
    lowQuality = Options.lowMemoryMode;
    framerate = Options.framerate;
    ghostTapping = Options.ghostTapping;
    cameraSpeed = FlxG.camera.followLerp / 0.04;
}

// Un error DENTRO de una funcion de la capa, llamada desde un hook traducido,
// no llega al guardian de ese hook: hscript-improved lo entrega al
// errorHandler de ESTE script (exprReturn) y la funcion devuelve null. El
// hook sigue como si nada y en el siguiente fotograma se repite: fue el
// cuelgue de VS Matt (un shader, 1 800 lineas por segundo en la consola).
// Cada error distinto se imprime tres veces y luego se dice que se calla.
// Sin clausuras (v1.0.1 no captura locales de una funcion que llama el motor)
// y sin poder lanzar: un error aqui volveria a este mismo manejador.
var fmlErrorCounts:Map<String, Int> = [];
var fmlOriginalErrorHandler:Dynamic = null;
function fmlLimitedErrorHandler(error:Dynamic) {
    try {
        var key = Std.string(error);
        var count = fmlErrorCounts.exists(key) ? fmlErrorCounts.get(key) : 0;
        fmlErrorCounts.set(key, count + 1);
        if (count < 3) fmlOriginalErrorHandler(error);
        else if (count == 3) trace("[FML] este error se repite en cada llamada; se deja de mostrar: " + key);
    } catch (e:Dynamic) {}
}
function fmlLimitRepeatedErrors() {
    var interp = __script__.interp;
    if (interp.errorHandler != null && fmlOriginalErrorHandler == null) {
        fmlOriginalErrorHandler = interp.errorHandler;
        interp.errorHandler = fmlLimitedErrorHandler;
    }
}

// ---------------------------------------------------------------------------
// Hooks de la propia capa
// ---------------------------------------------------------------------------
function create() {
    var game = fmlGame();
    fmlActive = false;
    for (s in game.scripts.scripts) {
        if (s == null || !s.active) continue;
        if (s.get("fmlPsychScript") == true) { fmlActive = true; break; }
    }
    if (!fmlActive) {
        // Una cancion nativa del mod: esta capa no hace nada y no cuesta nada.
        disableScript();
        return;
    }
    fmlLimitRepeatedErrors();
    // Los scripts de personaje de Psych (`characters/<x>.lua`) son scripts
    // de PlayState (`startCharacterScripts`): van en el pack, detras del
    // escenario y delante de la cancion.
    fmlLoadCharacterScripts();
    fmlLoadScriptsMap();
    songName = PlayState.SONG.meta != null ? PlayState.SONG.meta.name : "";
    songNameShort = songName;
    difficultyName = PlayState.difficulty;
    screenWidth = FlxG.width;
    screenHeight = FlxG.height;
    cameraZoomOnBeat = Options.camZoomOnBeat;
    shadersEnabled = Options.gameplayShaders;
    boyfriendName = game.boyfriend != null ? game.boyfriend.curCharacter : "";
    dadName = game.dad != null ? game.dad.curCharacter : "";
    gfName = game.gf != null ? game.gf.curCharacter : "";
    if (!PlayState.isStoryMode) game.playCutscenes = true;
    fmlRefreshGlobals();
    psychReady = true;
    fmlProfiling = fmlVerbose;
    psych.profiling = fmlProfiling;
    if (fmlProfiling) fmlProfileApi();
    if (fmlVerbose) trace("[FML] compat activa: " + songName);
}

function fmlLoadCharacterScripts() {
    var game = fmlGame();
    var seen:Map<String, Bool> = [];
    var at = 1;
    for (line in game.strumLines.members) {
        if (line == null || line.characters == null) continue;
        for (character in line.characters) {
            if (character == null || seen.exists(character.curCharacter)) continue;
            seen.set(character.curCharacter, true);
            var path = "songs/fml-psych-chars/" + character.curCharacter;
            var full = Paths.script(path);
            if (!Assets.exists(full) || game.scripts.contains(full)) continue;
            var script = Script.create(full);
            if (script == null) continue;
            game.scripts.insert(at++, script);
            script.load();
        }
    }
}

function fmlLoadScriptsMap() {
    var song = PlayState.SONG.meta != null ? PlayState.SONG.meta.name : "";
    var path = Paths.getPath("songs/" + song + "/fml-psych-scripts.json");
    if (!Assets.exists(path)) return;
    try {
        var parsed = Json.parse(Assets.getText(path));
        for (field in Reflect.fields(parsed)) fmlScriptsMap.set(field, Reflect.field(parsed, field));
    } catch (e:Dynamic) {}
}

function postCreate() {
    fmlPostCreated = true;
    var game = fmlGame();
    // La HUD del motor se anadio despues de create (`PlayState.hx:886-914`):
    // lo que el script puso "delante" en create vuelve arriba del todo.
    for (sprite in fmlHudFront) if (game.members.indexOf(sprite) >= 0) { game.remove(sprite, true); game.add(sprite); }
    fmlHudFront = [];
    fmlFlushDeferred();
    var calls = fmlDeferredCalls;
    fmlDeferredCalls = [];
    for (f in calls) f();
    fmlRefreshGlobals();
}

// Lo que la capa hace en cada fotograma tampoco puede fallar sin tope: un
)fmlhx",
R"fmlhx(// error aqui se repetiria sesenta veces por segundo en la consola del juego.
var fmlUpdateFailures = 0;
function update(elapsed:Float) {
    fmlFrame++;
    // Sin `return` (ver RENDIMIENTO): dentro de un try, cada uno eran dos
    // excepciones por fotograma.
    if (fmlUpdateFailures < 3) {
        try {
            if (!fmlProfiling) fmlRefreshGlobals();
            else {
                var start = Timer.stamp();
                if (fmlProfileWindowStart < 0) fmlProfileWindowStart = start;
                fmlProfileFrames++;
                fmlRefreshGlobals();
                fmlProfile("fml-psych-compat", "globals", start);
                if (start - fmlProfileWindowStart >= 5) fmlProfileReport(start);
            }
        } catch (e:Dynamic) {
            fmlUpdateFailures++;
            trace("[FML] error en la actualizacion de la capa: " + Std.string(e) + " [" + fmlUpdateFailures + "/3]");
            if (fmlUpdateFailures >= 3) trace("[FML] las variables globales de Psych dejan de actualizarse en esta cancion");
        }
    }
}

function onSubstateOpen(event) {
    var game = fmlGame();
    if (game.paused) {
        for (key in fmlSounds.keys()) { var s = fmlSounds.get(key); fmlSoundWasPlaying.set(key, s != null && s.playing); }
        fmlPauseRegistries(true);
    }
}

function onSubstateClose(event) {
    var game = fmlGame();
    if (game.paused) fmlPauseRegistries(false);
}

function onGameOver(event) {
    fmlInGameOver = true;
    inGameOver = true;
}

function onSongEnd(event) {
    fmlRefreshGlobals();
}

function destroy() {
    for (key in fmlTweens.keys()) { var t = fmlTweens.get(key); if (t != null) t.cancel(); }
    for (key in fmlTimers.keys()) { var t = fmlTimers.get(key); if (t != null) t.cancel(); }
    for (key in fmlSounds.keys()) { var s = fmlSounds.get(key); if (s != null) s.stop(); }
    fmlTweens.clear(); fmlTimers.clear(); fmlSounds.clear(); fmlObjects.clear(); fmlHudObjects.clear();
}

// El evento de Psych que Codename no trae (Screen Shake, Hey!, Set GF Speed,
// Play Sound, Set Property, Change Character, Add Camera Zoom, Play
// Animation, Alt Idle Animation, Camera Follow Pos con nombre de Psych):
// se ejecuta aqui con sus valores; el resto sigue por el motor.
// Sin `return` (ver RENDIMIENTO): corre en cada evento del chart.
function onEvent(event) {
    var name = fmlActive ? fmlEventName(event) : "";
    if (name != "") {
        var raw = event.event;
        // Section events still advance while the camera is forced; on release,
        // resume the CURRENT section rather than always jumping to boyfriend.
        if (raw.name == "Camera Movement" && raw.params != null && raw.params.length > 0) {
            fmlCameraSectionTarget = raw.params[0];
            if (fmlCameraForced) {
                var held = Reflect.copy(raw); held.name = "Unknown"; event.event = held;
            }
        }
        else if (!(Reflect.hasField(raw, "psych") && raw.psych != null &&
            (!Reflect.hasField(raw.psych, "native") || raw.psych.native == true) &&
            name != "Camera Follow Pos" && name != "Alt Idle Animation")) {
            var v1 = fmlEventValue(event, 1);
            var v2 = fmlEventValue(event, 2);
            if (fmlRunPsychEvent(name, v1, v2)) {
                // Cancelling here also suppresses Codename's onPostEvent, so translated
                // Lua onEvent callbacks never ran. Use a native no-op and keep the
                // original payload; do not mutate the chart's shared event object.
                var handled = Reflect.copy(raw);
                handled.name = "Unknown";
                handled.psych = {name: name, value1: v1, value2: v2, native: false};
                event.event = handled;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// La API, bajo un solo nombre
// ---------------------------------------------------------------------------
public var psych = {
    ready: true,
    verbose: fmlVerbose,
    windowReal: fmlWindowReal,
    pending: fmlPending, hook: fmlHook, clock: fmlClock, profile: fmlProfile, profiling: false,
    runaway: fmlRunaway,
    truthy: fmlTruthy, luaOr: fmlLuaOr, luaAnd: fmlLuaAnd, luaType: fmlLuaType, toNumber: fmlToNumber,
    tableConcat: fmlTableConcat, stringSub: fmlStringSub, stringRep: fmlStringRep, stringFormat: fmlStringFormat,
    stringFind: fmlStringFind, stringGsub: fmlStringGsub, mathRandom: fmlMathRandom, ease: fmlEase,
    eventName: fmlEventName, eventValue: fmlEventValue, eventIsAuxiliary: fmlEventIsAuxiliary, cameraTargetName: fmlCameraTargetName,
    noteIndex: fmlNoteIndex, noteTypeName: fmlNoteTypeName, object: fmlObject, allNotes: fmlAllNotes,
    // propiedades
    setProperty: fmlSetProperty, getProperty: fmlGetProperty,
    setPropertyFromClass: fmlSetPropertyFromClass, getPropertyFromClass: fmlGetPropertyFromClass,
    setPropertyFromGroup: fmlSetPropertyFromGroup, getPropertyFromGroup: fmlGetPropertyFromGroup,
    setPropertyLuaSprite: fmlSetProperty, getPropertyLuaSprite: fmlGetProperty,
    addToGroup: fmlAddToGroup, removeFromGroup: fmlRemoveFromGroup,
    setVar: fmlSetVarShared, getVar: fmlGetVarShared,
    callMethod: fmlCallMethod, callMethodFromClass: fmlCallMethodFromClass, createInstance: fmlCreateInstance,
    addInstance: fmlAddInstance, instanceArg: fmlInstanceArg,
    // objetos
    makeLuaSprite: fmlMakeLuaSprite, makeAnimatedLuaSprite: fmlMakeAnimatedLuaSprite, makeGraphic: fmlMakeGraphic,
    loadGraphic: fmlLoadGraphic, loadFrames: fmlLoadFrames, addLuaSprite: fmlAddLuaSprite, removeLuaSprite: fmlRemoveLuaSprite,
    luaSpriteExists: fmlLuaSpriteExists, luaTextExists: fmlLuaTextExists, luaSoundExists: fmlLuaSoundExists,
    setGraphicSize: fmlSetGraphicSize, scaleObject: fmlScaleObject, updateHitbox: fmlUpdateHitbox, screenCenter: fmlScreenCenter,
    setObjectCamera: fmlSetObjectCamera, setObjectOrder: fmlSetObjectOrder, getObjectOrder: fmlGetObjectOrder,
    setScrollFactor: fmlSetScrollFactor, setBlendMode: fmlSetBlendMode,
    addAnimationByPrefix: fmlAddAnimationByPrefix, addAnimationByIndices: fmlAddAnimationByIndices,
    addAnimationByIndicesLoop: fmlAddAnimationByIndicesLoop, addAnimation: fmlAddAnimation, addOffset: fmlAddOffset,
    playAnim: fmlPlayAnim, objectPlayAnimation: fmlObjectPlayAnimation,
    getMidpointX: fmlGetMidpointX, getMidpointY: fmlGetMidpointY, getGraphicMidpointX: fmlGetGraphicMidpointX, getGraphicMidpointY: fmlGetGraphicMidpointY,
    getScreenPositionX: fmlGetScreenPositionX, getScreenPositionY: fmlGetScreenPositionY, objectsOverlap: fmlObjectsOverlap,
    scaleLuaSprite: fmlScaleObject, setLuaSpriteCamera: fmlSetObjectCamera, setLuaSpriteScrollFactor: fmlSetScrollFactor,
    luaSpriteMakeGraphic: fmlMakeGraphic, luaSpriteAddAnimationByPrefix: fmlAddAnimationByPrefix,
    luaSpriteAddAnimationByIndices: fmlAddAnimationByIndices, luaSpritePlayAnimation: fmlObjectPlayAnimation,
    // texto
    makeLuaText: fmlMakeLuaText, addLuaText: fmlAddLuaText, removeLuaText: fmlRemoveLuaText,
    setTextString: fmlSetTextString, setTextSize: fmlSetTextSize, setTextWidth: fmlSetTextWidth, setTextHeight: fmlSetTextHeight,
    setTextAutoSize: fmlSetTextAutoSize, setTextBorder: fmlSetTextBorder, setTextColor: fmlSetTextColor, setTextFont: fmlSetTextFont,
    setTextItalic: fmlSetTextItalic, setTextAlignment: fmlSetTextAlignment,
    getTextString: fmlGetTextString, getTextSize: fmlGetTextSize, getTextFont: fmlGetTextFont, getTextWidth: fmlGetTextWidth,
    setHealthBarColors: fmlSetHealthBarColors, setTimeBarColors: fmlSetTimeBarColors,
    // tweens y temporizadores
    doTweenX: fmlDoTweenX, doTweenY: fmlDoTweenY, doTweenAngle: fmlDoTweenAngle, doTweenAlpha: fmlDoTweenAlpha,
    doTweenZoom: fmlDoTweenZoom, doTweenColor: fmlDoTweenColor, startTween: fmlStartTween, cancelTween: fmlCancelTween,
    noteTweenX: fmlNoteTweenX, noteTweenY: fmlNoteTweenY, noteTweenAngle: fmlNoteTweenAngle, noteTweenAlpha: fmlNoteTweenAlpha,
    noteTweenDirection: fmlNoteTweenDirection, runTimer: fmlRunTimer, cancelTimer: fmlCancelTimer,
    // sonido
    playSound: fmlPlaySound, playMusic: fmlPlayMusic, stopSound: fmlStopSound, pauseSound: fmlPauseSound, resumeSound: fmlResumeSound,
    soundFadeIn: fmlSoundFadeIn, soundFadeOut: fmlSoundFadeOut, soundFadeCancel: fmlSoundFadeCancel,
    getSoundVolume: fmlGetSoundVolume, setSoundVolume: fmlSetSoundVolume, getSoundTime: fmlGetSoundTime, setSoundTime: fmlSetSoundTime,
    getSoundPitch: fmlGetSoundPitch, setSoundPitch: fmlSetSoundPitch,
    precacheImage: fmlPrecacheImage, precacheSound: fmlPrecacheSound, precacheMusic: fmlPrecacheMusic,
    musicFadeIn: fmlMusicFadeIn, musicFadeOut: fmlMusicFadeOut,
    // personajes y camara
    characterPlayAnim: fmlCharacterPlayAnim, characterDance: fmlCharacterDance,
    getCharacterX: fmlGetCharacterX, getCharacterY: fmlGetCharacterY, setCharacterX: fmlSetCharacterX, setCharacterY: fmlSetCharacterY,
    addCharacterToList: fmlAddCharacterToList,
    cameraShake: fmlCameraShake, cameraFlash: fmlCameraFlash, cameraFade: fmlCameraFade, cameraSetTarget: fmlCameraSetTarget,
    setCameraFollowPoint: fmlSetCameraFollowPoint, addCameraFollowPoint: fmlAddCameraFollowPoint,
    setCameraScroll: fmlSetCameraScroll, addCameraScroll: fmlAddCameraScroll,
    getCameraFollowX: fmlGetCameraFollowX, getCameraFollowY: fmlGetCameraFollowY, getCameraScrollX: fmlGetCameraScrollX, getCameraScrollY: fmlGetCameraScrollY,
    // eventos y flujo
    triggerEvent: fmlTriggerEvent, startCountdown: fmlStartCountdown, endSong: fmlEndSong, restartSong: fmlRestartSong, exitSong: fmlExitSong, close: fmlClose,
    getSongPosition: function() Conductor.songPosition,
    setHealth: fmlSetHealth, addHealth: fmlAddHealth, getHealth: fmlGetHealth,
    addScore: fmlAddScore, setScore: fmlSetScore, addMisses: fmlAddMisses, setMisses: fmlSetMisses, addHits: fmlAddHits, setHits: fmlSetHits,
    setRatingPercent: fmlSetRatingPercent, setRatingName: fmlSetRatingName, setRatingFC: fmlSetRatingFC, updateScoreText: fmlUpdateScoreText,
    // comunicacion
    callOnLuas: fmlCallOnScripts, callOnHScript: fmlCallOnScripts, callOnScripts: fmlCallOnScripts, callScript: fmlCallScript,
    addLuaScript: fmlAddLuaScript, addHScript: fmlAddLuaScript, removeLuaScript: fmlRemoveLuaScript, removeHScript: fmlRemoveLuaScript,
    isRunning: fmlIsRunning, getRunningScripts: fmlGetRunningScripts, debugPrint: fmlDebugPrint,
    // sistema
    getRandomInt: fmlGetRandomInt, getRandomFloat: fmlGetRandomFloat, getRandomBool: fmlGetRandomBool,
    getColorFromHex: fmlGetColorFromHex, getColorFromName: fmlGetColorFromHex, getColorFromString: fmlGetColorFromHex,
    stringStartsWith: fmlStringStartsWith, stringEndsWith: fmlStringEndsWith, stringSplit: fmlStringSplit, stringTrim: fmlStringTrim,
    checkFileExists: fmlCheckFileExists, getTextFromFile: fmlGetTextFromFile, getModSetting: fmlGetModSetting,
    keyJustPressed: fmlKeyJustPressed, keyPressed: fmlKeyPressed, keyReleased: fmlKeyReleased,
    keyboardJustPressed: fmlKeyJustPressed, keyboardPressed: fmlKeyPressed, keyboardReleased: fmlKeyReleased,
    mouseClicked: fmlMouseClicked, mousePressed: fmlMousePressed, mouseReleased: fmlMouseReleased, getMouseX: fmlGetMouseX, getMouseY: fmlGetMouseY,
    initSaveData: fmlInitSaveData, flushSaveData: fmlFlushSaveData, getDataFromSave: fmlGetDataFromSave, setDataFromSave: fmlSetDataFromSave,
    // shaders
    initLuaShader: fmlInitLuaShader, setSpriteShader: fmlSetSpriteShader, removeSpriteShader: fmlRemoveSpriteShader,
    setShaderFloat: fmlSetShaderFloat, setShaderFloatArray: fmlSetShaderFloatArray, setShaderInt: fmlSetShaderInt,
    setShaderIntArray: fmlSetShaderIntArray, setShaderBool: fmlSetShaderBool, setShaderBoolArray: fmlSetShaderBoolArray,
    getShaderFloat: fmlGetShaderValue, getShaderInt: fmlGetShaderValue, getShaderBool: fmlGetShaderValue,
    // ventana
    windowSet: fmlWindowSet, windowGet: fmlWindowGet
};
)fmlhx",
};
}  // namespace

const char* const* compatScriptParts(std::size_t* count) {
    *count = sizeof(kParts) / sizeof(kParts[0]);
    return kParts;
}

}  // namespace psychcompat
}  // namespace fml
