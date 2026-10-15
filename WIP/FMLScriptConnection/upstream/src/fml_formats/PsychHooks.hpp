// fml_formats — Los hooks de Psych y en que hook de Codename se convierten.
//
// POR QUE UNA TABLA Y NO UN MAPA DE NOMBRES. `onCreatePost -> postCreate` es
// solo la mitad: la otra mitad es la FIRMA. Codename llama a `stepHit(curStep)`
// con un argumento y a `onEvent(event)` con uno; Psych llama a `onEvent(name,
// value1, value2)` con tres. Si la funcion traducida declara mas parametros de
// los que el motor pasa, hscript lanza "Invalid number of parameters" y el
// hook muere en silencio (`hscript/Interp.hx:1052-1058`): el script parece
// instalado y no hace nada. Asi que cada entrada declara los parametros que
// Codename entrega y un PRELUDIO que reconstruye los de Psych a partir del
// evento (`var name = ...`) antes de la primera linea del cuerpo.
//
// Y LA CANCELACION. `return Function_Stop` en Psych corta el flujo del motor
// en los hooks que lo consultan (`PlayState.hx:966,1136,1703,1986,2435`); en
// Codename el equivalente es `event.cancel()` sobre un CancellableEvent. Solo
// los hooks marcados `cancellable` lo tienen; en los demas un Function_Stop
// es un `return` sin efecto, y se dice.
//
// Las lineas de Psych son de `FNF-PsychEngine-main` y las de Codename de
// `CodenameEngine-main`; estan leidas, no supuestas.
#pragma once

#include <cstddef>
#include <string>

namespace fml {
namespace psychhooks {

struct Hook {
    const char* psych;       // como lo escribe el mod (larga y corta de Leather)
    const char* codename;    // el hook de Codename que lo dispara
    const char* params;      // la lista de parametros que Codename entrega
    // Lo que va delante del cuerpo: los parametros de Psych sacados del
    // evento. Nulo si no hace falta nada.
    const char* prelude;
    bool        cancellable; // `return Function_Stop` -> `event.cancel()`
    // Aproximado: el momento o el dato no es exactamente el de Psych. Se dice
    // en el informe, no se calla.
    const char* approximate;
};

// El orden es el de frecuencia en el corpus (onUpdate 66, onCreate 64,
// onCreatePost 61, onTimerCompleted 55, onBeatHit 45, onEvent 35...).
inline const Hook* table(std::size_t* count) {
    static const Hook kHooks[] = {
        {"onUpdate", "update", "elapsed", nullptr, false, nullptr},
        {"update", "update", "elapsed", nullptr, false, nullptr},
        {"onCreate", "create", "", nullptr, false, nullptr},
        {"create", "create", "", nullptr, false, nullptr},
        {"onCreatePost", "postCreate", "", nullptr, false, nullptr},
        {"createPost", "postCreate", "", nullptr, false, nullptr},
        {"onUpdatePost", "postUpdate", "elapsed", nullptr, false, nullptr},
        {"updatePost", "postUpdate", "elapsed", nullptr, false, nullptr},
        // Los llama la capa de compatibilidad con los argumentos de Psych, en
        // su orden (`FunkinLua.hx:672`, `:1513`).
        {"onTimerCompleted", "onTimerCompleted", "tag, loops, loopsLeft", nullptr, false, nullptr},
        {"onTweenCompleted", "onTweenCompleted", "tag, vars", nullptr, false, nullptr},
        {"onSoundFinished", "onSoundFinished", "tag", nullptr, false, nullptr},
        // Psych no pasa el beat: lo lee de la variable global. Codename lo pasa
        // y con el mismo nombre queda a la vista del cuerpo.
        {"onBeatHit", "beatHit", "curBeat", nullptr, false, nullptr},
        {"beatHit", "beatHit", "curBeat", nullptr, false, nullptr},
        {"onStepHit", "stepHit", "curStep", nullptr, false, nullptr},
        {"stepHit", "stepHit", "curStep", nullptr, false, nullptr},
        // Psych difunde `onEvent` DESPUES de ejecutar el evento
        // (`PlayState.hx:2303-2304`): eso es `onPostEvent` en Codename
        // (`PlayState.hx:1724`), y con el nombre y valores de Psych que la
        // capa saca del evento del chart.
        {"onEvent", "onPostEvent", "event",
         "if (psych.eventIsAuxiliary(event)) return; var name = psych.eventName(event); var value1 = psych.eventValue(event, 1); "
         "var value2 = psych.eventValue(event, 2); var strumTime = event.event.time;",
         false, nullptr},
        {"onSongStart", "onSongStart", "", nullptr, false, nullptr},
        // `goodNoteHit(id, noteData, noteType, isSustainNote)`
        // (`PlayState.hx:3120`): `onPlayerHit` es el hit del jugador y
        // `onDadHit` el del rival (`PlayState.hx:1997`).
        {"goodNoteHit", "onPlayerHit", "event",
         "var note = event.note; var id = psych.noteIndex(note); var noteData = event.direction; "
         "var noteType = psych.noteTypeName(event.noteType); var isSustainNote = note != null && note.isSustainNote;",
         false, nullptr},
        {"opponentNoteHit", "onDadHit", "event",
         "var note = event.note; var id = psych.noteIndex(note); var noteData = event.direction; "
         "var noteType = psych.noteTypeName(event.noteType); var isSustainNote = note != null && note.isSustainNote;",
         false, nullptr},
        {"goodNoteHitPre", "onPlayerHit", "event",
         "var note = event.note; var id = psych.noteIndex(note); var noteData = event.direction; "
         "var noteType = psych.noteTypeName(event.noteType); var isSustainNote = note != null && note.isSustainNote;",
         true, "en Codename no hay un hook previo al hit separado del hit: es el mismo instante"},
        {"opponentNoteHitPre", "onDadHit", "event",
         "var note = event.note; var id = psych.noteIndex(note); var noteData = event.direction; "
         "var noteType = psych.noteTypeName(event.noteType); var isSustainNote = note != null && note.isSustainNote;",
         true, "en Codename no hay un hook previo al hit separado del hit: es el mismo instante"},
        {"onSectionHit", "measureHit", "curSection", nullptr, false,
         "solo coincide con la seccion de Psych en compases de 4/4"},
        {"sectionHit", "measureHit", "curSection", nullptr, false,
         "solo coincide con la seccion de Psych en compases de 4/4"},
        {"onGameOverStart", "onPostGameOver", "event", nullptr, false, nullptr},
        // `noteMiss` es fallar una nota; `noteMissPress` es pulsar sin nota.
        // Codename junta las dos en `onPlayerMiss` con `event.note` nulo o no
        // (`PlayState.hx:1900`).
        {"noteMiss", "onPlayerMiss", "event",
         "if (event.note == null) return; var note = event.note; var id = psych.noteIndex(note); "
         "var noteData = event.direction; var noteType = psych.noteTypeName(event.noteType); "
         "var isSustainNote = note.isSustainNote;",
         false, nullptr},
        {"noteMissPress", "onPlayerMiss", "event",
         "if (event.note != null) return; var direction = event.direction;",
         false, nullptr},
        {"onCountdownTick", "onCountdown", "event", "var swagCounter = event.swagCounter;", false, nullptr},
        {"onCountdownStarted", "onPostStartCountdown", "", nullptr, false, nullptr},
        {"onStartCountdown", "onStartCountdown", "event", nullptr, true, nullptr},
        {"onEndSong", "onSongEnd", "event", nullptr, true, nullptr},
        {"onGameOver", "onGameOver", "event", nullptr, true, nullptr},
        {"onPause", "onGamePause", "event", nullptr, true, nullptr},
        {"onResume", "onSubstateClose", "event", nullptr, false,
         "Codename avisa al cerrar cualquier substate, no solo la pausa"},
        {"onDestroy", "destroy", "", nullptr, false, nullptr},
        // Psych avisa al ENTRAR la nota en la ventana de aparicion; Codename al
        // crearla con el chart. Lo que el mod suele hacer aqui -cambiar la
        // textura o la escala por tipo- vale igual en la creacion.
        {"onSpawnNote", "onNoteCreation", "event",
         "var note = event.note; var id = psych.noteIndex(note); var noteData = event.strumID; "
         "var noteType = psych.noteTypeName(event.noteType); var isSustainNote = note != null && note.isSustainNote; "
         "var strumTime = note != null ? note.strumTime : 0;",
         false, "Psych lo dispara al entrar la nota en pantalla y Codename al crearla con el chart"},
        {"onMoveCamera", "onCameraMove", "event", "var target = psych.cameraTargetName(event);", false, nullptr},
        {"onRecalculateRating", "onRatingUpdate", "event", nullptr, true,
         "Codename lo dispara al cambiar la calificacion, no en cada nota"},
        {"onUpdateScore", "onRatingUpdate", "event", "var miss = false;", false,
         "Codename lo dispara al cambiar la calificacion, no en cada nota"},
        {"preUpdateScore", "onRatingUpdate", "event", "var miss = false;", true,
         "Codename lo dispara al cambiar la calificacion, no en cada nota"},
        {"onSongStartPre", "onSongStart", "", nullptr, false, nullptr},
    };
    *count = sizeof(kHooks) / sizeof(kHooks[0]);
    return kHooks;
}

inline const Hook* find(const std::string& psychName) {
    std::size_t count = 0;
    const Hook* hooks = table(&count);
    for (std::size_t i = 0; i < count; ++i)
        if (psychName == hooks[i].psych) return &hooks[i];
    return nullptr;
}

// Lua binds callback arguments by position, not by the author's variable names.
inline std::string arguments(const Hook& hook) {
    const std::string name = hook.psych;
    if (name == "onEvent") return "name, value1, value2, strumTime";
    if (name == "onSpawnNote") return "id, noteData, noteType, isSustainNote, strumTime";
    if (name == "goodNoteHit" || name == "opponentNoteHit" || name == "goodNoteHitPre" ||
        name == "opponentNoteHitPre" || name == "noteMiss") return "id, noteData, noteType, isSustainNote";
    if (name == "noteMissPress") return "direction";
    if (name == "onCountdownTick") return "swagCounter";
    if (name == "onMoveCamera") return "target";
    if (name == "onUpdateScore" || name == "preUpdateScore") return "miss";
    if (hook.cancellable || std::string(hook.params) == "event" ||
        name == "onBeatHit" || name == "onStepHit" || name == "onSectionHit") return "";
    return hook.params;
}

}  // namespace psychhooks
}  // namespace fml
