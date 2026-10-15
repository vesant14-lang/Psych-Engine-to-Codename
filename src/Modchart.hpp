#pragma once
// A modchart as data: shaders on cameras and a timeline of actions on shader
// uniforms, receptors, cameras and scroll speed. It is read from what Psych
// mods ship (ModchartLoader's modchart.xml timelines; Lua scripts, replayed
// over the song with LuaLite), evaluated for the preview and written to
// Codename as an HScript that plays the same timeline.
#include "Catalog.hpp"
#include "LuaLite.hpp"
#include "Log.hpp"
#include "../../third_party/pugixml.hpp"
#include <chrono>
#include <atomic>
#include <future>
#include <mutex>
#include <cstring>

namespace pi {
enum class ModTarget { Shader, Receptor, Camera, Scroll };
struct ModShader {
    std::string name, effect;                 // instance name, effect (.frag name)
    fs::path frag, vert;
    bool game = false, hud = false;           // cameras it sits on
    int order = 0;                            // when it was put on a camera: filters apply in that order
    std::map<std::string, float> initial;     // uniforms at the start
};
struct ModAction {
    double step = 0, length = 0;              // song steps; length 0 sets at once
    ModTarget target = ModTarget::Shader;
    std::string object;                       // shader name; "game"/"hud"; receptor "L<line>:<index>" (index -1: whole line)
    std::string property;                     // uniform; x/y/angle/alpha/scaleX/scaleY; zoom/defaultZoom/angle/alpha/x/y; speed
    float to = 0, from = 0; bool hasFrom = false;
    std::string ease = "linear";
    std::string origin;                       // where it came from, for reports
};
struct Modchart {
    std::vector<ModShader> shaders;
    std::vector<ModAction> actions;
    std::vector<fs::path> sources;
    std::vector<std::string> notes;           // what could not be read
    std::vector<std::pair<std::string, long long>> timing;   // script -> ms spent reading it
    bool empty() const { return shaders.empty() && actions.empty(); }
    size_t count(ModTarget t) const { size_t n = 0; for (const auto& a : actions) if (a.target == t) ++n; return n; }
};

// --- easing (FlxEase) ---------------------------------------------------------
inline const std::vector<std::string>& modEaseNames() {
    static const std::vector<std::string> names = {"linear",
        "quadIn","quadOut","quadInOut","cubeIn","cubeOut","cubeInOut","quartIn","quartOut","quartInOut","quintIn","quintOut","quintInOut",
        "smoothStepIn","smoothStepOut","smoothStepInOut","smootherStepIn","smootherStepOut","smootherStepInOut",
        "sineIn","sineOut","sineInOut","bounceIn","bounceOut","bounceInOut","circIn","circOut","circInOut",
        "expoIn","expoOut","expoInOut","backIn","backOut","backInOut","elasticIn","elasticOut","elasticInOut"};
    return names;
}
// Psych accepts any case ('circinout', 'CircInOut'): the canonical FlxEase name.
inline std::string modEaseName(const std::string& raw) {
    auto want = lower(raw); for (const auto& n : modEaseNames()) if (lower(n) == want) return n; return "linear";
}
inline float modEase(const std::string& name, float t) {
    t = std::clamp(t, 0.0f, 1.0f); const float PI = 3.14159265f;
    auto bounceOut = [](float x) { const float b1 = 0.363636363636f, b2 = 0.727272727273f, b3 = 0.545454545455f, b4 = 0.909090909091f, b5 = 0.818181818182f, b6 = 0.954545454545f;
        if (x < b1) return 7.5625f * x * x; if (x < b2) { x -= b3; return 7.5625f * x * x + .75f; } if (x < b4) { x -= b5; return 7.5625f * x * x + .9375f; } x -= b6; return 7.5625f * x * x + .984375f; };
    auto smoothStep = [](float x) { return x * x * (3 - 2 * x); };
    auto smootherStep = [](float x) { return x * x * x * (x * (x * 6 - 15) + 10); };
    const float ELASTIC_AMPLITUDE = 1, ELASTIC_PERIOD = .4f;
    if (name == "linear") return t;
    if (name == "quadIn") return t * t; if (name == "quadOut") return -t * (t - 2); if (name == "quadInOut") return t <= .5f ? t * t * 2 : 1 - (--t) * t * 2;
    if (name == "cubeIn") return t * t * t; if (name == "cubeOut") { t -= 1; return 1 + t * t * t; } if (name == "cubeInOut") return t <= .5f ? t * t * t * 4 : 1 + (t - 1) * (t - 1) * (t - 1) * 4;
    if (name == "quartIn") return t * t * t * t; if (name == "quartOut") { t -= 1; return 1 - t * t * t * t; }
    if (name == "quartInOut") { if (t <= .5f) return t * t * t * t * 8; t = t * 2 - 2; return (1 - t * t * t * t) / 2 + .5f; }
    if (name == "quintIn") return t * t * t * t * t; if (name == "quintOut") { t -= 1; return t * t * t * t * t + 1; }
    if (name == "quintInOut") { t *= 2; if (t < 1) return t * t * t * t * t / 2; t -= 2; return (t * t * t * t * t + 2) / 2; }
    if (name == "smoothStepIn") return 2 * smoothStep(t / 2); if (name == "smoothStepOut") return 2 * smoothStep(t / 2 + .5f) - 1; if (name == "smoothStepInOut") return smoothStep(t);
    if (name == "smootherStepIn") return 2 * smootherStep(t / 2); if (name == "smootherStepOut") return 2 * smootherStep(t / 2 + .5f) - 1; if (name == "smootherStepInOut") return smootherStep(t);
    if (name == "sineIn") return -std::cos(PI / 2 * t) + 1; if (name == "sineOut") return std::sin(PI / 2 * t); if (name == "sineInOut") return -std::cos(PI * t) / 2 + .5f;
    if (name == "bounceIn") return 1 - bounceOut(1 - t); if (name == "bounceOut") return bounceOut(t);
    if (name == "bounceInOut") return t < .5f ? (1 - bounceOut(1 - 2 * t)) / 2 : (1 + bounceOut(2 * t - 1)) / 2;
    if (name == "circIn") return -(std::sqrt(1 - t * t) - 1); if (name == "circOut") return std::sqrt(1 - (t - 1) * (t - 1));
    if (name == "circInOut") return t <= .5f ? (std::sqrt(1 - t * t * 4) - 1) / -2 : (std::sqrt(1 - (t * 2 - 2) * (t * 2 - 2)) + 1) / 2;
    if (name == "expoIn") return std::pow(2.0f, 10 * (t - 1)); if (name == "expoOut") return -std::pow(2.0f, -10 * t) + 1;
    if (name == "expoInOut") return t < .5f ? std::pow(2.0f, 10 * (t * 2 - 1)) / 2 : (-std::pow(2.0f, -10 * (t * 2 - 1)) + 2) / 2;
    const float s = 1.70158f;
    if (name == "backIn") return t * t * (2.70158f * t - s); if (name == "backOut") { t -= 1; return 1 - t * t * (-2.70158f * t - s); }
    if (name == "backInOut") { t *= 2; if (t < 1) return t * t * (2.70158f * t - s) / 2; t -= 1; return (1 - (t - 1) * (t - 1) * (-2.70158f * (t - 1) - s)) / 2 + .5f; }
    if (name == "elasticIn") { t -= 1; return -(ELASTIC_AMPLITUDE * std::pow(2.0f, 10 * t) * std::sin((t - (ELASTIC_PERIOD / (2 * PI) * std::asin(1 / ELASTIC_AMPLITUDE))) * (2 * PI) / ELASTIC_PERIOD)); }
    if (name == "elasticOut") return ELASTIC_AMPLITUDE * std::pow(2.0f, -10 * t) * std::sin((t - (ELASTIC_PERIOD / (2 * PI) * std::asin(1 / ELASTIC_AMPLITUDE))) * (2 * PI) / ELASTIC_PERIOD) + 1;
    if (name == "elasticInOut") {
        if (t < .5f) return -.5f * (std::pow(2.0f, 10 * (t -= .5f)) * std::sin((t - (ELASTIC_PERIOD / 4)) * (2 * PI) / ELASTIC_PERIOD));
        return std::pow(2.0f, -10 * (t -= .5f)) * std::sin((t - (ELASTIC_PERIOD / 4)) * (2 * PI) / ELASTIC_PERIOD) * .5f + 1;
    }
    return t;
}

// --- ModchartLoader's modchart.xml --------------------------------------------
// <Init>: initShader (name, shader = effect), setCameraShader (camera game/hud),
// setShaderProperty; <Events>: tweenShaderProperty (step, time in steps,
// startValue, value, ease), setShaderProperty with a step. The effect's
// .frag/.vert come from shaders/ in the mod; its .txt lists uniform defaults.
inline void readModchartXml(const fs::path& xml, const Files& files, Modchart& out) {
    pugi::xml_document doc; if (!doc.load_file(xml.c_str())) { out.notes.push_back(xml.filename().u8string() + ": XML ilegible"); return; }
    out.sources.push_back(xml);
    auto shader = [&](const std::string& name) -> ModShader* { for (auto& s : out.shaders) if (s.name == name) return &s; return nullptr; };
    auto findShaderFile = [&](const std::string& effect, const char* ext) -> fs::path {
        for (const auto& p : files.paths) if (lower(p.filename().u8string()) == lower(effect + ext) && lower(p.parent_path().filename().u8string()) == "shaders") return p;
        return {};
    };
    size_t unknown = 0; int placed = (int)out.shaders.size();
    for (auto ev : doc.select_nodes("//Event")) {
        auto n = ev.node(); std::string type = n.attribute("type").as_string(), name = n.attribute("name").as_string();
        bool init = std::string(n.parent().name()) == "Init";
        if (type == "initShader") {
            if (shader(name)) continue;
            ModShader s; s.name = name; s.effect = n.attribute("shader").as_string();
            s.frag = findShaderFile(s.effect, ".frag"); s.vert = findShaderFile(s.effect, ".vert");
            if (s.frag.empty()) out.notes.push_back("shader " + s.effect + ": no está en shaders/ del mod");
            auto txt = findShaderFile(s.effect, ".txt");
            if (!txt.empty()) { std::istringstream in(read(txt)); std::string key; float v; while (in >> key >> v) s.initial[key] = v; }
            out.shaders.push_back(s);
        } else if (type == "setCameraShader") {
            if (auto* s = shader(name)) { std::string cam = lower(n.attribute("camera").as_string()); if (!s->game && !s->hud) s->order = placed++; if (cam == "hud") s->hud = true; else s->game = true; }
        } else if (type == "setShaderProperty" || type == "tweenShaderProperty") {
            std::string prop = n.attribute("property").as_string(); float value = n.attribute("value").as_float();
            if (init) { if (auto* s = shader(name)) s->initial[prop] = value; continue; }
            ModAction a; a.target = ModTarget::Shader; a.object = name; a.property = prop; a.to = value;
            a.step = n.attribute("step").as_double(); a.origin = xml.filename().u8string();
            if (type == "tweenShaderProperty") {
                a.length = n.attribute("time").as_double(); a.ease = modEaseName(n.attribute("ease").as_string("linear"));
                if (n.attribute("startValue")) { a.from = n.attribute("startValue").as_float(); a.hasFrom = true; }
            }
            out.actions.push_back(a);
        } else ++unknown;
    }
    if (unknown) out.notes.push_back(xml.filename().u8string() + ": " + std::to_string(unknown) + " eventos de tipo desconocido");
}

// --- Characters by note type ----------------------------------------------------
// {'character','Type'} pairs in a song's own scripts (Wii Funkin's
// PlayerChars.lua: {'spongemii','Darnote1'}): that character sings the notes
// whose type contains Type, and with "-duet" the line's own characters sing
// them too. Kept when the character is in the mod once and some note type of
// the chart contains Type. The preview and the export read the same pairs.
struct ScriptedSinger { std::string type; int character = -1; };
inline std::vector<ScriptedSinger> scriptedTypeSingers(const Catalog& c, const Difficulty& d) {
    std::set<std::string> types; for (const auto& n : d.chart.notes) if (!n.type.empty()) types.insert(n.type);
    std::vector<ScriptedSinger> out; if (types.empty()) return out;
    auto unique = [&](const std::string& id) { int found = -1; for (int i = 0; i < (int)c.characters.size(); ++i) if (lower(c.characters[i].imported.character.id) == lower(id)) { if (found >= 0) return -1; found = i; } return found; };
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(d.path.parent_path(), ec)) {
        auto ext = lower(entry.path().extension().u8string());
        if ((ext != ".lua" && ext != ".hx") || entry.file_size(ec) > 512u * 1024u) continue;
        const auto text = read(entry.path());
        // {'a','b'} by hand: short quoted words, no regex over a whole script.
        for (size_t at = text.find('{'); at != std::string::npos; at = text.find('{', at + 1)) {
            size_t i = at + 1; std::string words[2]; bool ok = true;
            for (int w = 0; w < 2 && ok; ++w) {
                while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
                if (i >= text.size() || (text[i] != '\'' && text[i] != '"')) { ok = false; break; }
                const char q = text[i]; const size_t end = text.find(q, i + 1);
                if (end == std::string::npos || end - i - 1 == 0 || end - i - 1 > 64 || text.find('\n', i) < end) { ok = false; break; }
                words[w] = text.substr(i + 1, end - i - 1); i = end + 1;
                while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
                if (w == 0) { if (i >= text.size() || text[i] != ',') ok = false; else ++i; }
                else if (i >= text.size() || text[i] != '}') ok = false;
            }
            if (!ok) continue;
            const int index = unique(words[0]); if (index < 0) continue;
            if (std::none_of(types.begin(), types.end(), [&](const std::string& t) { return t.find(words[1]) != std::string::npos; })) continue;
            if (std::any_of(out.begin(), out.end(), [&](const ScriptedSinger& s) { return s.type == words[1]; })) continue;
            out.push_back({words[1], index});
        }
    }
    return out;
}

// --- Psych Lua, replayed -------------------------------------------------------
// The script runs as Psych would call it over the whole song: onCreate,
// onCreatePost and onSongStart at step 0, then for every step onUpdate,
// onUpdatePost, onStepHit (and onBeatHit, onSectionHit), and each chart note
// through goodNoteHit/opponentNoteHit at its time, each chart event through
// onEvent at its time, and onTweenCompleted / onTimerCompleted when a tween or
// a runTimer of the script ends (scripts chain effects that way: a camera that
// floats back and forth, a zoom that becomes the default when it arrives).
// What the script does to receptors, cameras, camera shaders and scroll speed
// is recorded with the step it happened at. Getters answer from what was
// recorded so far; everything else in the Psych API does nothing. Positions
// are Psych's (STRUM_X 42 + 50 + 112 * i, + 640 for the player, y 50) and
// become offsets from the receptor's place.
struct LuaReplayLimits { long long stepsPerCall = 200000; size_t actions = 60000; double seconds = 3; const std::atomic<bool>* cancel = nullptr; };
// A script that never touches receptors, cameras or scroll speed has no
// modchart to replay: reading it costs nothing.
inline bool luaMayModchart(const std::string& source) {
    static const char* calls[] = {"noteTween", "Strums", "strumLineNotes", "doTweenZoom", "doTweenAngle", "doTweenAlpha", "doTweenX", "doTweenY",
                                  "camHUD", "camGame", "defaultCamZoom", "songSpeed", "Change Scroll Speed", "setFilters", "setShaderFloat"};
    for (const char* c : calls) if (source.find(c) != std::string::npos) return true;
    return false;
}
class PsychLuaReplay {
public:
    PsychLuaReplay(const fml::UniversalChart& chart, const fml::TimeMap& time, Modchart& out, const std::string& origin, const Files* files = nullptr)
        : chart(chart), time(time), out(out), origin(origin), files(files) {
        // Psych's lines: the first opponent and the first player strumline.
        for (size_t i = 0; i < chart.strumLines.size(); ++i) {
            if (chart.strumLines[i].type == 0 && opponentLine < 0) opponentLine = (int)i;
            if (chart.strumLines[i].type == 1 && playerLine < 0) playerLine = (int)i;
        }
        keys = opponentLine >= 0 ? std::max(1, chart.strumLines[opponentLine].keyCount) : 4;
        install();
    }
    void run(const std::string& source, const LuaReplayLimits& limits = {}) {
        const auto started = std::chrono::steady_clock::now();
        lua.budget = limits.stepsPerCall * 4;
        try { lua.load(source); } catch (const std::exception& e) { out.notes.push_back(origin + ": " + e.what()); return; }
        double lastMs = 0; for (const auto& n : chart.notes) lastMs = std::max(lastMs, n.timeMs + n.sustainMs);
        const int lastStep = (int)std::ceil(time.beatAt(lastMs + 2000) * stepsPerBeat());
        auto once = [&](const char* name, std::vector<lua::Value> args) {
            if (out.actions.size() >= limits.actions) return;
            lua.budget = limits.stepsPerCall;
            try { lua.callGlobal(name, std::move(args)); }
            catch (const lua::Budget&) { ++overBudget[name]; }
            catch (const std::exception& e) { ++failed[std::string(name) + ": " + e.what()]; }
        };
        // triggerEvent reaches onEvent once the call that fired it returns.
        auto callback = [&](const char* name, std::vector<lua::Value> args) {
            once(name, std::move(args));
            for (int guard = 0; !triggered.empty() && guard < 32; ++guard) {
                auto args2 = triggered.front(); triggered.erase(triggered.begin()); once("onEvent", std::move(args2));
            }
            triggered.clear();
        };
        // Tweens and timers of the script that end before `untilMs`, in order.
        auto completions = [&](double untilMs, int s) {
            for (int guard = 0; guard < 256; ++guard) {
                std::string tag; double at = untilMs; bool timer = false;
                for (const auto& t : tweens) if (t.second <= at) { at = t.second; tag = t.first; }
                for (const auto& t : timers) if (t.second.next <= at && (tag.empty() || t.second.next < at)) { at = t.second.next; tag = t.first; timer = true; }
                if (tag.empty()) break;
                stepNow = std::max<double>(s, time.beatAt(at) * stepsPerBeat()); setTime(s);
                if (!timer) { tweens.erase(tag); callback("onTweenCompleted", {lua::Value::string(tag)}); continue; }
                auto& t = timers[tag]; ++t.done;
                const int loops = t.loops, left = loops > 0 ? loops - t.done : 0;
                if (loops > 0 && left <= 0) timers.erase(tag); else t.next += std::max(1.0, t.interval);
                callback("onTimerCompleted", {lua::Value::string(tag), lua::Value::number(loops), lua::Value::number(left)});
            }
        };
        setTime(0);
        for (const char* name : {"onCreate", "onCreatePost", "onCountdownStarted", "onSongStart"}) callback(name, {});
        creating = false;
        size_t note = 0, event = 0;
        std::vector<size_t> order(chart.notes.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return chart.notes[a].timeMs < chart.notes[b].timeMs; });
        std::vector<size_t> events(chart.events.size()); for (size_t i = 0; i < events.size(); ++i) events[i] = i;
        std::stable_sort(events.begin(), events.end(), [&](size_t a, size_t b) { return chart.events[a].timeMs < chart.events[b].timeMs; });
        for (int s = 0; s <= lastStep; ++s) {
            if (limits.cancel && *limits.cancel) break;
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() > limits.seconds) { out.notes.push_back(origin + ": se cortó la lectura en el paso " + std::to_string(s) + " por tiempo"); break; }
            if (out.actions.size() >= limits.actions) { out.notes.push_back(origin + ": límite de " + std::to_string(limits.actions) + " acciones"); break; }
            completions(time.msAtBeat(s / (double)stepsPerBeat()), s);
            stepNow = s; setTime(s);
            const double elapsed = time.stepMsAt(time.msAtBeat(s / (double)stepsPerBeat())) / 1000.0;
            // Per-frame code that leaves no modchart in its first 256 steps
            // (camera smoothing, HUD followers) stops being called: it is
            // most of the cost and none of the result.
            if (!skipUpdate) {
                const size_t before = out.actions.size();
                callback("onUpdate", {lua::Value::number(elapsed)});
                callback("onUpdatePost", {lua::Value::number(elapsed)});
                updateActions += out.actions.size() - before;
                if (s >= 256 && updateActions == 0) skipUpdate = true;
            }
            callback("onStepHit", {});
            if (s % stepsPerBeat() == 0) callback("onBeatHit", {});
            if (s % (stepsPerBeat() * 4) == 0) callback("onSectionHit", {});
            const double endMs = time.msAtBeat((s + 1) / (double)stepsPerBeat());
            // Chart events of this step: onEvent(name, value1, value2, strumTime).
            for (; event < events.size() && chart.events[events[event]].timeMs < endMs; ++event) {
                const auto& e = chart.events[events[event]];
                stepNow = std::max<double>(s, time.beatAt(e.timeMs) * stepsPerBeat()); setTime(s);
                callback("onEvent", {lua::Value::string(e.name), lua::Value::string(e.params.size() > 0 ? e.params[0] : ""),
                                     lua::Value::string(e.params.size() > 1 ? e.params[1] : ""), lua::Value::number(e.timeMs)});
            }
            stepNow = s;
            // Notes of this step, at their own time.
            for (; note < order.size() && chart.notes[order[note]].timeMs < endMs; ++note) {
                const auto& n = chart.notes[order[note]];
                stepNow = time.beatAt(n.timeMs) * stepsPerBeat(); setTime(s);
                const bool player = n.strumLine >= 0 && (size_t)n.strumLine < chart.strumLines.size() && chart.strumLines[n.strumLine].type == 1;
                callback(player ? "goodNoteHit" : "opponentNoteHit",
                         {lua::Value::number((double)order[note]), lua::Value::number(n.direction % std::max(1, keys)), lua::Value::string(n.type), lua::Value::boolean(false)});
            }
            stepNow = s;
        }
        for (const auto& f : failed) out.notes.push_back(origin + ": " + f.first + " (" + std::to_string(f.second) + " veces)");
        for (const auto& f : overBudget) out.notes.push_back(origin + ": " + f.first + " superó el límite de pasos " + std::to_string(f.second) + " veces");
    }
    std::map<std::string, int> unknownCalls() const { return lua.missingCalls; }
    // Psych starts defaultCamZoom and camGame.zoom at the stage's defaultZoom.
    // Unknown, a script adding to it (Wii's Better Zoom) read nil and wrote 0.
    void setStageZoom(float zoom) { absolute["defaultCamZoom"] = zoom; absolute["cam.game.zoom"] = zoom; }
private:
    const fml::UniversalChart& chart; const fml::TimeMap& time; Modchart& out; std::string origin;
    lua::Interpreter lua; int opponentLine = -1, playerLine = -1, keys = 4; double stepNow = 0;
    bool skipUpdate = false; size_t updateActions = 0;
    const Files* files = nullptr;
    bool creating = true;                                 // inside onCreate ... onSongStart
    std::map<std::string, double> tweens;                 // tag -> when it ends (ms)
    struct Timer { double next = 0, interval = 0; int loops = 1, done = 0; };
    std::map<std::string, Timer> timers;
    std::vector<std::vector<lua::Value>> triggered;       // triggerEvent, for onEvent
    std::map<std::string, std::string> spriteShader;      // Lua sprite -> shader name
    std::set<std::string> lateShaders, badValues;
    double msNow() const { return time.msAtBeat(stepNow / stepsPerBeat()); }
    void startTag(const std::string& tag, double seconds) { if (!tag.empty()) tweens[tag] = msNow() + std::max(0.0, seconds) * 1000.0; }
    std::map<std::string, float> state;   // current values, for getters and from-values
    std::map<std::string, int> failed, overBudget;
    int stepsPerBeat() const { return std::max(1, chart.stepsPerBeat); }

    void setTime(int step) {
        const double beat = (double)step / stepsPerBeat(), ms = time.msAtBeat(beat);
        lua.setGlobal("curStep", lua::Value::number(step)); lua.setGlobal("curBeat", lua::Value::number(std::floor(beat)));
        lua.setGlobal("curSection", lua::Value::number(std::floor(beat / 4))); lua.setGlobal("curDecStep", lua::Value::number(stepNow)); lua.setGlobal("curDecBeat", lua::Value::number(stepNow / stepsPerBeat()));
        lua.setGlobal("songPosition", lua::Value::number(ms)); lua.setGlobal("bpm", lua::Value::number(time.bpmAt(ms)));
        lua.setGlobal("crochet", lua::Value::number(60000.0 / time.bpmAt(ms))); lua.setGlobal("stepCrochet", lua::Value::number(time.stepMsAt(ms)));
    }
    double durationSteps(double seconds) const {
        const double ms = time.msAtBeat(stepNow / stepsPerBeat());
        return (time.beatAt(ms + seconds * 1000.0) - stepNow / stepsPerBeat()) * stepsPerBeat();
    }
    // strumLineNotes index -> line and index in it.
    bool receptor(int note, int& line, int& index) const {
        if (note < keys) { line = opponentLine; index = note; } else { line = playerLine; index = note - keys; }
        return line >= 0;
    }
    float defaultX(int line, int index) const { return 42 + 50 + 112.0f * index + (line == playerLine ? 640 : 0); }
    float defaultY() const { return 50; }
    float current(const std::string& key, float fallback) const { auto f = state.find(key); return f == state.end() ? fallback : f->second; }
    void record(ModTarget target, const std::string& object, const std::string& property, float to, double seconds, const std::string& ease) {
        // A value Codename cannot take never reaches the script: a camera at
        // zoom 0 runs it out of memory (Mii Funkin', 14-oct).
        const bool zoom = target == ModTarget::Camera && (property == "zoom" || property == "defaultZoom");
        if (!std::isfinite(to) || (zoom && (to < 0.05f || to > 20.0f))) {
            if (badValues.insert(property).second) out.notes.push_back(origin + ": " + property + " inválido (" + std::to_string(to) + "), se ignora");
            return;
        }
        ModAction a; a.target = target; a.object = object; a.property = property; a.to = to; a.step = stepNow;
        a.length = seconds > 0 ? durationSteps(seconds) : 0; a.ease = modEaseName(ease); a.origin = origin;
        // The same tween fired again on every frame of a beat (onUpdate) is one
        // tween, and one towards the value the channel already reached does
        // nothing (Opponentarrowshide hides the arrows on every frame). Not
        // the camera zoom: the game eases it back to the default by itself,
        // so every bump counts. Another script moving the channel in between
        // makes it a new action.
        for (auto it = out.actions.rbegin(); it != out.actions.rend() && it - out.actions.rbegin() < 256; ++it)
            if (it->target == target && it->object == object && it->property == property) {
                if (it->origin != origin) break;
                if (it->to == a.to && std::abs(it->length - a.length) < 1e-3 && it->ease == a.ease && a.step - it->step < std::max(1.0, a.length)) return;
                if (it->to == a.to && a.step >= it->step + it->length && !(target == ModTarget::Camera && property == "zoom")) return;
                break;
            }
        state[object + "." + property] = to;
        out.actions.push_back(a);
    }
    // Psych's absolute receptor values become offsets / multipliers.
    void receptorValue(int note, const std::string& prop, float value, double seconds, const std::string& ease) {
        int line, index; if (note < 0 || note >= keys * 2 || !receptor(note, line, index)) return;
        const std::string object = "L" + std::to_string(line) + ":" + std::to_string(index);
        std::string p = prop; float v = value;
        if (p == "x") v = value - defaultX(line, index); else if (p == "y") v = value - defaultY();
        else if (p == "scale.x") { p = "scaleX"; v = value / .7f; } else if (p == "scale.y") { p = "scaleY"; v = value / .7f; }
        else if (p == "visible") { p = "alpha"; v = value != 0 ? 1.0f : 0.0f; }
        else if (p != "angle" && p != "alpha") return;
        absolute[object + "." + prop] = value;
        record(ModTarget::Receptor, object, p, v, seconds, ease);
    }
    std::map<std::string, float> absolute;   // Psych-side values, for getPropertyFromGroup
    float receptorGet(int note, const std::string& prop) const {
        int line, index; if (!receptor(note, line, index)) return 0;
        auto f = absolute.find("L" + std::to_string(line) + ":" + std::to_string(index) + "." + prop);
        if (f != absolute.end()) return f->second;
        if (prop == "x") return defaultX(line, index); if (prop == "y") return defaultY();
        if (prop == "alpha" || prop == "visible") return 1; if (prop == "scale.x" || prop == "scale.y") return .7f; return 0;
    }
    // A member past the end of its group does not exist in Psych and the call
    // does nothing (Ballistic loops opponentStrums from 0 to 7).
    int groupIndex(const std::string& group, int i) const {
        if (i < 0) return -1;
        if (group == "playerStrums") return i < keys ? keys + i : -1;
        if (group == "opponentStrums") return i < keys ? i : -1;
        if (group == "strumLineNotes") return i < keys * 2 ? i : -1;
        return -1;
    }
    // Which camera a call names, as Psych resolves it (FunkinLua.hx:554-576).
    // doTweenZoom takes a camera name and falls back to camGame; the other
    // doTween* take an object, so only camGame/camHUD themselves are cameras.
    // Light It Up (Wii) fades a black square with doTweenAlpha: read as
    // camGame it left the whole game black in Codename.
    static std::string cameraOf(const std::string& name, bool zoom) {
        const std::string n = lower(name);
        if (n == "camhud" || (zoom && n == "hud")) return "hud";
        if (n == "camgame" || (zoom && n == "game")) return "game";
        if (n == "camother" || n == "other") return "";
        return zoom ? "game" : "";
    }
    void cameraValue(const std::string& cam, const std::string& prop, float value, double seconds, const std::string& ease) {
        const std::string object = cameraOf(cam, prop == "zoom");
        if (object.empty()) return;
        if (prop != "zoom" && prop != "angle" && prop != "alpha" && prop != "x" && prop != "y") return;
        absolute["cam." + object + "." + prop] = value;
        record(ModTarget::Camera, object, prop, value, seconds, ease);
    }
    // Psych mods put a shader on a camera with runHaxeCode:
    //   var shader0 = game.createRuntimeShader("vcr");   (or the shader of a
    //   sprite: setSpriteShader('img', 'vcr'), game.getLuaObject("img").shader)
    //   game.camGame.setFilters([new ShaderFilter(shader0)]);
    // The camera, the shader and the sprite whose uniforms setShaderFloat
    // changes are read from that code; the rest of the Haxe is not run. Only
    // shaders put on while the song is created: one put on halfway through
    // would need its own timeline and is reported instead.
    // Read with plain scanning, not std::regex: MSVC's regex recurses per
    // character and a long runHaxeCode block overflowed the stack (Rev-Mixed).
    void haxeCode(const std::string& code) {
        const size_t n = code.size();
        auto space = [&](size_t i) { while (i < n && std::isspace((unsigned char)code[i])) ++i; return i; };
        auto word = [&](size_t& i) { size_t b = i; while (i < n && (std::isalnum((unsigned char)code[i]) || code[i] == '_')) ++i; return code.substr(b, i - b); };
        auto quoted = [&](size_t& i, std::string& out) {   // "..." or '...' at i
            if (i >= n || (code[i] != '"' && code[i] != '\'')) return false;
            const char q = code[i]; const size_t close = code.find(q, i + 1); if (close == std::string::npos) return false;
            out = code.substr(i + 1, close - i - 1); i = close + 1; return true;
        };
        auto starts = [&](size_t i, const char* text) { return code.compare(i, std::strlen(text), text) == 0; };
        // game.getLuaObject("name") at i: the name, i past the closing parenthesis.
        auto luaObject = [&](size_t& i, std::string& name) {
            if (!starts(i, "game.getLuaObject(")) return false;
            size_t k = space(i + std::strlen("game.getLuaObject(")); if (!quoted(k, name)) return false;
            k = space(k); if (k >= n || code[k] != ')') return false; i = k + 1; return true;
        };
        std::map<std::string, std::string> strings, shaders;
        for (size_t at = code.find("var "); at != std::string::npos; at = code.find("var ", at + 4)) {
            size_t i = space(at + 4); const auto name = word(i); if (name.empty()) continue;
            i = space(i); if (i < n && code[i] == ':') { i = space(i + 1); word(i); i = space(i); }
            if (i >= n || code[i] != '=' || (i + 1 < n && code[i + 1] == '=')) continue;
            i = space(i + 1); std::string value;
            if (quoted(i, value)) { strings[name] = value; continue; }
            if (!starts(i, "game.createRuntimeShader(")) continue;
            i = space(i + std::strlen("game.createRuntimeShader("));
            if (quoted(i, value)) shaders[name] = value; else { const auto ref = word(i); auto f = strings.find(ref); if (f != strings.end()) shaders[name] = f->second; }
        }
        for (size_t at = code.find("game.getLuaObject("); at != std::string::npos; at = code.find("game.getLuaObject(", at + 1)) {
            size_t i = at; std::string sprite; if (!luaObject(i, sprite) || !starts(i, ".shader")) continue;
            i = space(i + std::strlen(".shader")); if (i >= n || code[i] != '=' || (i + 1 < n && code[i + 1] == '=')) continue;
            i = space(i + 1); const auto ref = word(i); auto f = shaders.find(ref); if (f != shaders.end()) spriteShader[sprite] = f->second;
        }
        for (const char* camera : {"camGame", "camHUD", "camOther"}) {
            const std::string call = std::string("game.") + camera + ".setFilters(";
            for (size_t at = code.find(call); at != std::string::npos; at = code.find(call, at + 1)) {
                size_t open = space(at + call.size()); if (open >= n || code[open] != '[') continue;
                const size_t close = code.find(']', open); if (close == std::string::npos) continue;
                const std::string list = code.substr(open + 1, close - open - 1);
                // setFilters replaces the camera's list (vcr.lua, then vcrshader.lua).
                if (creating && std::string(camera) != "camOther") clearCamera(std::string(camera) == "camHUD");
                for (size_t f = list.find("ShaderFilter("); f != std::string::npos; f = list.find("ShaderFilter(", f + 1)) {
                    size_t i = f + std::strlen("ShaderFilter("); while (i < list.size() && std::isspace((unsigned char)list[i])) ++i;
                    std::string instance, effect;
                    if (list.compare(i, std::strlen("game.getLuaObject("), "game.getLuaObject(") == 0) {
                        size_t q = i + std::strlen("game.getLuaObject("); while (q < list.size() && std::isspace((unsigned char)list[q])) ++q;
                        if (q < list.size() && (list[q] == '"' || list[q] == '\'')) { const size_t e = list.find(list[q], q + 1); if (e != std::string::npos) instance = list.substr(q + 1, e - q - 1); }
                        auto found = spriteShader.find(instance); if (found != spriteShader.end()) effect = found->second;
                    } else {
                        size_t b = i; while (i < list.size() && (std::isalnum((unsigned char)list[i]) || list[i] == '_')) ++i;
                        auto found = shaders.find(list.substr(b, i - b)); if (found != shaders.end()) { effect = found->second; instance = effect; }
                    }
                    if (!effect.empty()) cameraShader(instance, effect, camera);
                }
            }
        }
    }
    void clearCamera(bool hud) {
        for (auto& s : out.shaders) (hud ? s.hud : s.game) = false;
        std::set<std::string> gone; for (const auto& s : out.shaders) if (!s.game && !s.hud) gone.insert(s.name);
        if (gone.empty()) return;
        out.shaders.erase(std::remove_if(out.shaders.begin(), out.shaders.end(), [&](const ModShader& s) { return gone.count(s.name) > 0; }), out.shaders.end());
        out.actions.erase(std::remove_if(out.actions.begin(), out.actions.end(), [&](const ModAction& a) { return a.target == ModTarget::Shader && gone.count(a.object) > 0; }), out.actions.end());
    }
    void cameraShader(const std::string& instance, const std::string& effect, const std::string& camera) {
        if (camera == "camOther") return;
        if (!creating) {
            if (lateShaders.insert(effect).second) out.notes.push_back(origin + ": el shader " + effect + " se pone en la cámara a mitad de canción; no se convierte");
            return;
        }
        ModShader* s = nullptr; for (auto& existing : out.shaders) if (existing.name == instance) s = &existing;
        if (!s) {
            ModShader made; made.name = instance; made.effect = effect;
            if (files) for (const auto& p : files->paths) if (lower(p.parent_path().filename().u8string()) == "shaders") {
                if (lower(p.filename().u8string()) == lower(effect + ".frag")) made.frag = p;
                if (lower(p.filename().u8string()) == lower(effect + ".vert")) made.vert = p;
            }
            if (made.frag.empty()) out.notes.push_back("shader " + effect + ": no está en shaders/ del mod");
            int order = 0; for (const auto& other : out.shaders) order = std::max(order, other.order + 1);
            made.order = order; out.shaders.push_back(made); s = &out.shaders.back();
        }
        (camera == "camHUD" ? s->hud : s->game) = true;
    }
    // setShaderFloat on the sprite that holds a camera shader moves its uniform.
    // iTime is left to the shader's own clock (Codename's timeShaders).
    void shaderValue(const std::string& object, const std::string& uniform, float value) {
        if (uniform == "iTime") return;
        for (const auto& s : out.shaders) if (s.name == object) { record(ModTarget::Shader, object, uniform, value, 0, "linear"); return; }
    }
    void install() {
        using lua::Value; using A = std::vector<Value>&;
        auto num = [](A a, size_t i, double d = 0) { return i < a.size() && a[i].kind != Value::Nil ? lua::Interpreter::toNumber(a[i]) : d; };
        auto str = [](A a, size_t i) { return i < a.size() ? lua::Interpreter::toString(a[i]) : std::string(); };
        lua.setGlobal("downscroll", Value::boolean(false)); lua.setGlobal("middlescroll", Value::boolean(false));
        lua.setGlobal("screenWidth", Value::number(1280)); lua.setGlobal("screenHeight", Value::number(720));
        lua.setGlobal("songName", Value::string(chart.songId)); lua.setGlobal("songLength", Value::number(0));
        lua.setGlobal("scrollSpeed", Value::number(chart.speed)); lua.setGlobal("playbackRate", Value::number(1));
        // A normal game, not a showcase: Rev-Mixed hides the HUD and the
        // receptors when botPlay is on.
        lua.setGlobal("botPlay", Value::boolean(false)); lua.setGlobal("mustHitSection", Value::boolean(false));
        for (int i = 0; i < keys; ++i) {
            lua.setGlobal("defaultPlayerStrumX" + std::to_string(i), Value::number(42 + 50 + 112.0 * i + 640));
            lua.setGlobal("defaultOpponentStrumX" + std::to_string(i), Value::number(42 + 50 + 112.0 * i));
            lua.setGlobal("defaultPlayerStrumY" + std::to_string(i), Value::number(50));
            lua.setGlobal("defaultOpponentStrumY" + std::to_string(i), Value::number(50));
        }
        auto tween = [&](const char* name, const char* prop) {
            lua.setNative(name, [this, prop, num, str](A a) { startTag(str(a, 0), num(a, 3)); receptorValue((int)num(a, 1), prop, (float)num(a, 2), num(a, 3), str(a, 4)); return std::vector<Value>{}; });
        };
        tween("noteTweenX", "x"); tween("noteTweenY", "y"); tween("noteTweenAngle", "angle"); tween("noteTweenAlpha", "alpha");
        lua.setNative("setPropertyFromGroup", [this, num, str](A a) {
            int idx = groupIndex(str(a, 0), (int)num(a, 1)); if (idx < 0) return std::vector<Value>{};
            float v = a.size() > 3 && a[3].kind == Value::Bool ? (a[3].b ? 1.0f : 0.0f) : (float)num(a, 3);
            receptorValue(idx, str(a, 2), v, 0, "linear"); return std::vector<Value>{}; });
        lua.setNative("getPropertyFromGroup", [this, num, str](A a) {
            int idx = groupIndex(str(a, 0), (int)num(a, 1)); if (idx < 0) return std::vector<Value>{Value{}};
            auto prop = str(a, 2); if (prop == "downScroll") return std::vector<Value>{Value::boolean(false)};
            return std::vector<Value>{Value::number(receptorGet(idx, prop))}; });
        auto camTween = [&](const char* name, const char* prop) {
            lua.setNative(name, [this, prop, num, str](A a) { startTag(str(a, 0), num(a, 3)); cameraValue(str(a, 1), prop, (float)num(a, 2), num(a, 3), str(a, 4)); return std::vector<Value>{}; });
        };
        camTween("doTweenZoom", "zoom"); camTween("doTweenAngle", "angle"); camTween("doTweenAlpha", "alpha"); camTween("doTweenX", "x"); camTween("doTweenY", "y");
        lua.setNative("setProperty", [this, num, str](A a) {
            auto path = str(a, 0); auto dot = path.find('.'); float v = (float)num(a, 1);
            if (path == "defaultCamZoom") { absolute["defaultCamZoom"] = v; record(ModTarget::Camera, "game", "defaultZoom", v, 0, "linear"); }
            else if (path == "songSpeed") { absolute["songSpeed"] = v; record(ModTarget::Scroll, "", "speed", v, 0, "linear"); }
            else if (dot != std::string::npos) {
                auto obj = lower(path.substr(0, dot)), prop = path.substr(dot + 1);
                if (obj == "camhud" || obj == "camgame") cameraValue(obj, prop, v, 0, "linear");
            }
            return std::vector<Value>{}; });
        lua.setNative("getProperty", [this, str](A a) {
            auto path = str(a, 0); auto dot = path.find('.');
            if (path == "defaultCamZoom" || path == "songSpeed") { auto f = absolute.find(path); return std::vector<Value>{f == absolute.end() ? Value{} : Value::number(f->second)}; }
            if (dot != std::string::npos) {
                auto obj = lower(path.substr(0, dot)), prop = path.substr(dot + 1);
                if (obj == "camhud" || obj == "camgame") { auto f = absolute.find("cam." + std::string(obj == "camhud" ? "hud" : "game") + "." + prop);
                    if (f != absolute.end()) return std::vector<Value>{Value::number(f->second)};
                    return std::vector<Value>{Value::number(prop == "zoom" || prop == "alpha" ? 1.0 : 0.0)}; }
            }
            return std::vector<Value>{Value{}}; });
        lua.setNative("triggerEvent", [this, num, str](A a) {
            if (lower(str(a, 0)) == "change scroll speed") {
                double mult = num(a, 1, 1), seconds = num(a, 2, 0);
                record(ModTarget::Scroll, "", "speed", (float)(chart.speed * mult), seconds, "linear");
            }
            if (triggered.size() < 32) triggered.push_back({Value::string(str(a, 0)), Value::string(str(a, 1)), Value::string(str(a, 2)), Value::number(msNow())});
            return std::vector<Value>{}; });
        // Tweens this reader does not record still end, for onTweenCompleted.
        for (const char* name : {"doTweenColor", "startTween"})
            lua.setNative(name, [this, num, str](A a) { startTag(str(a, 0), num(a, 3)); return std::vector<Value>{}; });
        lua.setNative("cancelTween", [this, str](A a) { tweens.erase(str(a, 0)); return std::vector<Value>{}; });
        lua.setNative("runTimer", [this, num, str](A a) {
            Timer t; t.interval = std::max(0.0, num(a, 1, 1)) * 1000.0; t.next = msNow() + t.interval; t.loops = (int)num(a, 2, 1);
            timers[str(a, 0)] = t; return std::vector<Value>{}; });
        lua.setNative("cancelTimer", [this, str](A a) { timers.erase(str(a, 0)); return std::vector<Value>{}; });
        // Camera shaders (see haxeCode).
        lua.setNative("initLuaShader", [](A) { return std::vector<Value>{Value::boolean(true)}; });
        lua.setNative("setSpriteShader", [this, str](A a) { spriteShader[str(a, 0)] = str(a, 1); return std::vector<Value>{Value::boolean(true)}; });
        lua.setNative("runHaxeCode", [this, str](A a) { haxeCode(str(a, 0)); return std::vector<Value>{}; });
        lua.setNative("setShaderFloat", [this, num, str](A a) { shaderValue(str(a, 0), str(a, 1), (float)num(a, 2)); return std::vector<Value>{Value::boolean(true)}; });
        {
            Value os; os.kind = Value::Tab; os.t = std::make_shared<lua::Table>();
            auto clock = [this](A) { return std::vector<Value>{Value::number(msNow() / 1000.0)}; };
            for (const char* name : {"$clock", "$time"}) { Value f; f.kind = Value::Nat; f.native = std::make_shared<lua::Native>(clock); os.t->fields[name] = f; }
            lua.setGlobal("os", os);
        }
        lua.setNative("getRandomInt", [num](A a) { return std::vector<Value>{Value::number(std::floor((num(a, 0) + num(a, 1)) / 2))}; });
        lua.setNative("getRandomFloat", [num](A a) { return std::vector<Value>{Value::number((num(a, 0) + num(a, 1)) / 2)}; });
        lua.setNative("getRandomBool", [](A) { return std::vector<Value>{Value::boolean(false)}; });
    }
};

// The Lua scripts Psych runs during a song, in its order (PlayState.hx:434,
// 464, 574-576, then data/<song>/): every .lua in scripts/ of the shared
// assets, of mods/, of global mods and of the song's mod folder; the stage's
// stages/<stage>.lua; one custom_notetypes/<type>.lua per note type and one
// custom_events/<event>.lua per event the chart uses; and the song's folder.
// A named script is looked up in the song's mod folder first, then in the
// rest (Paths.modFolders). A mod folder is mods/, mods/<mod>/ (only the
// song's own, or one whose pack.json says runsGlobally), assets/ or
// assets/shared/, or a folder loaded as the mod itself.
inline std::vector<fs::path> psychSongScripts(const Files& files, const Difficulty& d) {
    std::vector<std::string> parts; for (const auto& part : d.path) parts.push_back(lower(part.u8string()));
    std::string songMod;
    for (size_t i = 0; i + 1 < parts.size(); ++i) if (parts[i] == "mods" && parts[i + 1] != "data" && parts[i + 1] != "songs") songMod = parts[i + 1];
    std::map<std::string, bool> globalMods;
    auto modRoot = [&](const fs::path& folder, int& rank) {
        const auto name = lower(folder.filename().u8string()), parent = lower(folder.parent_path().filename().u8string());
        if (name == "mods") { rank = 1; return true; }
        if (parent == "mods") {
            if (name == songMod) { rank = 0; return true; }
            auto known = globalMods.find(name);
            if (known == globalMods.end()) {
                bool global = false; auto pack = folder / "pack.json";
                if (fml::psych::file(pack)) { try { global = json::parse(read(pack)).value("runsGlobally", false); } catch (...) {} }
                known = globalMods.emplace(name, global).first;
            }
            rank = 1; return known->second;
        }
        if ((name == "shared" && parent == "assets") || name == "assets") { rank = 2; return true; }
        for (const auto& root : files.roots) if (lower(root.lexically_normal().generic_u8string()) == lower(folder.lexically_normal().generic_u8string())) { rank = 0; return true; }
        return false;
    };
    // key ("scripts", "stages/x.lua"...) -> candidates with their rank
    std::vector<std::pair<int, fs::path>> global; std::map<std::string, std::vector<std::pair<int, fs::path>>> named;
    for (const auto& p : files.paths) {
        if (lower(p.extension().u8string()) != ".lua") continue;
        const auto dir = p.parent_path(); const auto kind = lower(dir.filename().u8string()); int rank = 0;
        if (kind != "scripts" && kind != "stages" && kind != "custom_notetypes" && kind != "custom_events") continue;
        if (!modRoot(dir.parent_path(), rank)) continue;
        if (kind == "scripts") global.push_back({rank, p}); else named[kind + "/" + lower(p.filename().u8string())].push_back({rank, p});
    }
    std::vector<fs::path> out; std::set<std::string> seen;
    auto add = [&](const fs::path& p) { if (seen.insert(lower(p.lexically_normal().generic_u8string())).second) out.push_back(p); };
    std::stable_sort(global.begin(), global.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    for (const auto& g : global) add(g.second);
    auto first = [&](const std::string& key) { auto f = named.find(lower(key)); if (f == named.end()) return;
        auto best = std::min_element(f->second.begin(), f->second.end(), [](const auto& a, const auto& b) { return a.first < b.first; }); add(best->second); };
    first("stages/" + d.chart.stage + ".lua");
    std::set<std::string> types, events;
    for (const auto& n : d.chart.notes) if (!n.type.empty()) types.insert(n.type);
    for (const auto& e : d.chart.events) if (!e.name.empty()) events.insert(e.name);
    for (const auto& t : types) first("custom_notetypes/" + t + ".lua");
    for (const auto& e : events) first("custom_events/" + e + ".lua");
    std::error_code ec; std::vector<fs::path> own;
    for (const auto& entry : fs::directory_iterator(d.path.parent_path(), ec)) if (entry.is_regular_file(ec) && lower(entry.path().extension().u8string()) == ".lua") own.push_back(entry.path());
    std::sort(own.begin(), own.end()); for (const auto& p : own) add(p);
    return out;
}
// Everything that makes a song's modchart: modchart.xml, and every Lua
// script Psych runs for the song, replayed against the chart.
inline Modchart songModchart(const Files& files, const Difficulty& d, const std::atomic<bool>* cancel = nullptr) {
    Modchart out; std::error_code ec;
    const auto folder = d.path.parent_path();
    auto xml = folder / "modchart.xml"; if (fml::psych::file(xml)) readModchartXml(xml, files, out);
    fml::TimeMap time; time.build(d.chart);
    const auto scripts = psychSongScripts(files, d);
    // The stage's defaultZoom (stages/<stage>.json), 0.9 without one
    // (StageData.dummy).
    float stageZoom = 0.9f;
    for (const auto& path : files.paths)
        if (lower(path.parent_path().filename().u8string()) == "stages" && lower(path.filename().u8string()) == lower(d.chart.stage + ".json")) {
            try { auto j = json::parse(read(path)); if (j.contains("defaultZoom") && j["defaultZoom"].is_number()) stageZoom = j["defaultZoom"].get<float>(); } catch (...) {}
            break;
        }
    for (const auto& script : scripts) {
        if (fs::file_size(script, ec) > 1024u * 1024u) { out.notes.push_back(script.filename().u8string() + ": demasiado grande"); continue; }
        if (cancel && *cancel) break;
        const auto source = read(script); if (!luaMayModchart(source)) continue;
        const size_t before = out.actions.size(); const auto started = std::chrono::steady_clock::now();
        LuaReplayLimits limits; limits.cancel = cancel;
        PsychLuaReplay replay(d.chart, time, out, script.filename().u8string(), &files); replay.setStageZoom(stageZoom); replay.run(source, limits);
        out.timing.push_back({script.filename().u8string() + " (" + std::to_string(out.actions.size() - before) + " acciones)", logs::since(started)});
        if (out.actions.size() > before) out.sources.push_back(script);
    }
    std::stable_sort(out.actions.begin(), out.actions.end(), [](const ModAction& a, const ModAction& b) { return a.step < b.step; });
    return out;
}

// --- evaluation ----------------------------------------------------------------
// The value of every channel at a step: the last action that started wins;
// a tween eases from where the previous one left the channel.
class ModchartPlayer {
public:
    struct Channel { ModTarget target; std::string object, property; std::vector<size_t> actions; std::vector<float> from; float base = 0; bool hasBase = false; };
    void prepare(const Modchart& m) {
        chart = &m; channels.clear(); index.clear();
        for (size_t i = 0; i < m.actions.size(); ++i) {
            const auto& a = m.actions[i]; auto key = channelKey(a.target, a.object, a.property);
            auto found = index.find(key);
            if (found == index.end()) { found = index.emplace(key, channels.size()).first; Channel c; c.target = a.target; c.object = a.object; c.property = a.property; c.hasBase = defaultValue(m, c, c.base); channels.push_back(c); }
            channels[found->second].actions.push_back(i);
        }
        for (auto& c : channels) {
            c.from.resize(c.actions.size());
            for (size_t k = 0; k < c.actions.size(); ++k) {
                const auto& a = m.actions[c.actions[k]];
                c.from[k] = a.hasFrom ? a.from : k == 0 ? c.base : valueOf(c, k - 1, a.step);
            }
        }
    }
    // Calls fn(channel, value) for every channel that has a value at `step`.
    template<class Fn> void evaluate(double step, Fn fn) const {
        if (!chart) return;
        for (const auto& c : channels) {
            auto it = std::upper_bound(c.actions.begin(), c.actions.end(), step, [&](double s, size_t i) { return s < chart->actions[i].step; });
            if (it == c.actions.begin()) { if (c.hasBase) fn(c, c.base); continue; }
            fn(c, valueOf(c, (size_t)(it - c.actions.begin()) - 1, step));
        }
    }
    const std::vector<Channel>& all() const { return channels; }
    static std::string channelKey(ModTarget t, const std::string& o, const std::string& p) { return std::to_string((int)t) + "|" + o + "|" + p; }
private:
    const Modchart* chart = nullptr; std::vector<Channel> channels; std::map<std::string, size_t> index;
    float valueOf(const Channel& c, size_t k, double step) const {
        const auto& a = chart->actions[c.actions[k]];
        if (a.length <= 0 || step >= a.step + a.length) return a.to;
        const float t = (float)((step - a.step) / a.length);
        return c.from[k] + (a.to - c.from[k]) * modEase(a.ease, t);
    }
    static bool defaultValue(const Modchart& m, const Channel& c, float& out) {
        if (c.target == ModTarget::Shader) { for (const auto& s : m.shaders) if (s.name == c.object) { auto f = s.initial.find(c.property); if (f != s.initial.end()) { out = f->second; return true; } } return false; }
        if (c.target == ModTarget::Receptor) { out = c.property == "alpha" || c.property == "scaleX" || c.property == "scaleY" ? 1.0f : 0.0f; return true; }
        return false;   // cameras and scroll speed start where the song has them
    }
};

// The evaluated state at one instant, as the preview draws it.
struct ModFrame {
    struct Receptor { float x = 0, y = 0, angle = 0, alpha = 1, scaleX = 1, scaleY = 1; };
    std::map<std::string, std::map<std::string, float>> shaders;   // shader -> uniform -> value
    std::map<std::pair<int, int>, Receptor> receptors;           // (line, index); index -1 is the whole line
    std::map<std::string, float> game, hud;                      // camera properties that were set
    bool hasSpeed = false; float speed = 0;
    bool any() const { return !shaders.empty() || !receptors.empty() || !game.empty() || !hud.empty() || hasSpeed; }
    // A receptor's own values with its line's on top: offsets add, alpha and
    // scale multiply.
    Receptor receptor(int line, int index) const {
        Receptor r; auto whole = receptors.find({line, -1}), own = receptors.find({line, index});
        for (auto f : {whole, own}) if (f != receptors.end()) {
            r.x += f->second.x; r.y += f->second.y; r.angle += f->second.angle;
            r.alpha *= f->second.alpha; r.scaleX *= f->second.scaleX; r.scaleY *= f->second.scaleY;
        }
        return r;
    }
};
inline ModFrame evaluateModchart(const ModchartPlayer& player, double step) {
    ModFrame f;
    player.evaluate(step, [&](const ModchartPlayer::Channel& c, float v) {
        switch (c.target) {
            case ModTarget::Shader: f.shaders[c.object][c.property] = v; break;
            case ModTarget::Receptor: {
                int line = 0, index = 0; if (std::sscanf(c.object.c_str(), "L%d:%d", &line, &index) != 2) break;
                auto& r = f.receptors[{line, index}];
                if (c.property == "x") r.x = v; else if (c.property == "y") r.y = v; else if (c.property == "angle") r.angle = v;
                else if (c.property == "alpha") r.alpha = v; else if (c.property == "scaleX") r.scaleX = v; else if (c.property == "scaleY") r.scaleY = v;
                break;
            }
            case ModTarget::Camera: (c.object == "hud" ? f.hud : f.game)[c.property] = v; break;
            case ModTarget::Scroll: f.hasSpeed = true; f.speed = v; break;
        }
    });
    return f;
}
// A song's modchart is read once: replaying Lua over a whole song is not
// free, and the preview, the readiness check and the export all ask for it.
struct ModchartCache { std::mutex mutex; std::map<std::string, std::shared_ptr<const Modchart>> entries; };
inline ModchartCache& modchartCache() { static ModchartCache cache; return cache; }
inline std::string modchartKey(const Difficulty& d) { return d.path.u8string() + "|" + std::to_string(d.chart.notes.size()) + "|" + std::to_string(d.chart.strumLines.size()); }
// Already read, or null: lets the preview ask without waiting.
inline std::shared_ptr<const Modchart> peekSongModchart(const Difficulty& d) {
    auto& cache = modchartCache(); std::lock_guard<std::mutex> lock(cache.mutex);
    auto f = cache.entries.find(modchartKey(d)); return f == cache.entries.end() ? nullptr : f->second;
}
inline std::shared_ptr<const Modchart> cachedSongModchart(const Files& files, const Difficulty& d, const std::atomic<bool>* cancel = nullptr) {
    auto& cache = modchartCache(); const auto key = modchartKey(d);
    { std::lock_guard<std::mutex> lock(cache.mutex); auto f = cache.entries.find(key); if (f != cache.entries.end()) return f->second; }
    const auto started = std::chrono::steady_clock::now();
    auto made = std::make_shared<const Modchart>(songModchart(files, d, cancel));
    if (cancel && *cancel) return made;   // incomplete: not cached
    if (!made->empty()) logs::info("Modchart de " + d.path.parent_path().filename().u8string() + ": " + std::to_string(made->actions.size()) + " acciones, " +
                                   std::to_string(made->shaders.size()) + " shaders, " + std::to_string(logs::since(started)) + " ms");
    std::lock_guard<std::mutex> lock(cache.mutex); if (cache.entries.size() > 256) cache.entries.clear(); cache.entries[key] = made; return made;
}
inline std::string modchartSummary(const Modchart& m) {
    std::string out = std::to_string(m.actions.size()) + " acciones";
    if (!m.shaders.empty()) out += ", " + std::to_string(m.shaders.size()) + " shaders";
    if (auto n = m.count(ModTarget::Receptor)) out += ", receptores " + std::to_string(n);
    if (auto n = m.count(ModTarget::Camera)) out += ", cámara " + std::to_string(n);
    if (auto n = m.count(ModTarget::Scroll)) out += ", scroll " + std::to_string(n);
    return out;
}

// --- Codename -------------------------------------------------------------------
// One HScript per song that plays the timeline: CustomShaders on camGame /
// camHUD with their starting uniforms, receptors moved from their own place,
// cameras and scroll speed, each action at its step with the FlxEase of the
// source. A new tween on a channel cancels the one before, like Psych tags.
inline std::string codenameModchartScript(const Modchart& m, const std::string& songTitle) {
    std::ostringstream o; o.setf(std::ios::fixed); o.precision(4);
    auto quote = [](const std::string& s) { std::string q = "\""; for (char c : s) { if (c == '"' || c == '\\') q += '\\'; q += c; } return q + "\""; };
    o << "// Modchart de " << songTitle << ", convertido desde Psych por Psych Importer (FML).\n";
    o << "// Fuentes:"; for (const auto& s : m.sources) o << " " << s.filename().u8string(); o << "\n";
    o << "// Receptores: x/y son desplazamientos desde su sitio en Codename. Duraciones en pasos.\n";
    o << "import funkin.backend.shaders.CustomShader;\nimport flixel.tweens.FlxEase;\nimport flixel.tweens.FlxTween;\nimport haxe.ds.StringMap;\n\n";
    o << "var eases = [";
    for (size_t i = 0; i < modEaseNames().size(); ++i) o << (i ? ", " : "") << "FlxEase." << modEaseNames()[i];
    o << "];\n";
    std::map<std::string, int> shaderIndex; for (size_t i = 0; i < m.shaders.size(); ++i) shaderIndex[m.shaders[i].name] = (int)i;
    auto easeIndex = [](const std::string& e) { const auto& n = modEaseNames(); auto f = std::find(n.begin(), n.end(), e); return f == n.end() ? 0 : (int)(f - n.begin()); };
    o << "// [paso, tipo (0 shader, 1 receptor, 2 cámara, 3 scroll), objeto, propiedad, valor, desde, tiene desde, duración, ease]\n";
    // Rows as short as they can be: a song with a bounce on every note has
    // ten thousand of them (Rev-Mixed's Rejected), and Codename parses them
    // all when the song loads.
    auto num = [](double v) {
        char b[40]; std::snprintf(b, sizeof b, "%.4f", v); std::string t = b;
        while (t.size() > 1 && t.back() == '0') t.pop_back(); if (!t.empty() && t.back() == '.') t.pop_back();
        return t == "-0" ? std::string("0") : t;
    };
    o << "var actions = [\n";
    for (const auto& a : m.actions) {
        int kind = (int)a.target; std::string object;
        if (a.target == ModTarget::Shader) { auto f = shaderIndex.find(a.object); if (f == shaderIndex.end()) continue; object = std::to_string(f->second); }
        else if (a.target == ModTarget::Receptor) { int line = 0, idx = 0; if (std::sscanf(a.object.c_str(), "L%d:%d", &line, &idx) != 2) continue; object = std::to_string(line * 100 + (idx < 0 ? 99 : idx)); }
        else if (a.target == ModTarget::Camera) object = a.object == "hud" ? "1" : "0";
        else object = "0";
        o << "[" << num(a.step) << "," << kind << "," << object << "," << quote(a.property) << "," << num(a.to) << "," << num(a.hasFrom ? a.from : 0.0f) << ","
          << (a.hasFrom ? "true" : "false") << "," << num(a.length) << "," << easeIndex(a.ease) << "],\n";
    }
    o << "];\n\nvar shaders = [];\nvar timeShaders = [];\nvar base = [];\nvar values = new StringMap();\nvar tweens = new StringMap();\nvar claimed = new StringMap();\nvar cursor = 0;\n\n";
    // One name per shader and one variable per statement: the plainest
    // HScript, which every hscript build accepts.
    o << "function postCreate() {\n";
    for (size_t i = 0; i < m.shaders.size(); ++i) {
        const auto& s = m.shaders[i]; const std::string v = "shader" + std::to_string(i);
        o << "\tvar " << v << " = new CustomShader(" << quote(s.effect) << ");\n";
        for (const auto& u : s.initial) o << "\t" << v << ".hset(" << quote(u.first) << ", " << u.second << ");\n";
        if (s.game) o << "\tcamGame.addShader(" << v << ");\n"; if (s.hud) o << "\tcamHUD.addShader(" << v << ");\n";
        o << "\tshaders.push(" << v << ");\n";
        if (!s.frag.empty() && read(s.frag).find("iTime") != std::string::npos) o << "\ttimeShaders.push(" << v << ");\n";
    }
    // Base positions are where each strum ends up: in postCreate Codename's
    // intro has just put it 10 px higher at alpha 0 (StrumLine.hx:418-420).
    o << "\tfor (line in strumLines.members) {\n\t\tvar row = [];\n\t\tfor (strum in line.members) row.push([strum.x, strum.alpha == 0 ? strum.y + 10 : strum.y, strum.scale.x, strum.scale.y]);\n\t\tbase.push(row);\n\t}\n}\n\n";
    // Psych notes copy their receptor's alpha (Note.copyAlpha); Codename's do
    // not (Strum.updateNotePosition). A modchart that hides receptors would
    // leave their notes flying, so the script copies it, as Psych does.
    bool receptorAlpha = false; for (const auto& a : m.actions) if (a.target == ModTarget::Receptor && a.property == "alpha") receptorAlpha = true;
    // What the source sets at step 0 holds from the first frame, countdown
    // included (Psych scripts set it in onCreate).
    o << "function update(elapsed) {\n\tvar step = Math.max(Conductor.curStepFloat, 0);\n\twhile (cursor < actions.length && actions[cursor][0] <= step) { run(actions[cursor]); cursor++; }\n"
         "\tfor (s in timeShaders) s.hset(\"iTime\", Conductor.songPosition / 1000);\n";
    if (receptorAlpha)
        o << "\tfor (line in strumLines.members) {\n\t\tline.notes.forEach(function(n) {\n\t\t\tvar s = line.members[n.strumID];\n"
             "\t\t\tif (s != null) n.alpha = s.alpha * (n.isSustainNote ? 0.6 : 1);\n\t\t});\n\t}\n";
    o << "}\n\n";
    o << "function key(a) { return a[1] + \":\" + a[2] + \":\" + a[3]; }\n\n";
    o << "function current(a) {\n\tvar k = key(a);\n\tif (values.exists(k)) return values.get(k);\n"
         "\tif (a[1] == 1) return (a[3] == \"alpha\" || a[3] == \"scaleX\" || a[3] == \"scaleY\") ? 1 : 0;\n"
         "\tif (a[1] == 2) { var cam = a[2] == 1 ? camHUD : camGame; if (a[3] == \"defaultZoom\") return defaultCamZoom; return Reflect.getProperty(cam, a[3]); }\n"
         "\tif (a[1] == 3) return scrollSpeed;\n\treturn 0;\n}\n\n";
    o << "function write(a, v) {\n\tvalues.set(key(a), v);\n\tswitch (a[1]) {\n"
         "\t\tcase 0: shaders[a[2]].hset(a[3], v);\n"
         "\t\tcase 1:\n\t\t\tvar line = Std.int(a[2] / 100);\n\t\t\tvar only = a[2] % 100;\n"
         "\t\t\tif (line >= strumLines.length) return;\n"
         "\t\t\tvar strums = strumLines.members[line].members;\n"
         "\t\t\tfor (i in 0...strums.length) {\n\t\t\t\tif (only != 99 && only != i) continue;\n\t\t\t\tvar s = strums[i];\n\t\t\t\tvar row = line < base.length ? base[line] : null;\n\t\t\t\tvar b = row != null && i < row.length ? row[i] : null;\n"
         "\t\t\t\tif (b == null) continue;\n"
         "\t\t\t\tif (!claimed.exists(line + \":\" + i)) {\n\t\t\t\t\tclaimed.set(line + \":\" + i, true);\n\t\t\t\t\tFlxTween.cancelTweensOf(s);\n\t\t\t\t\ts.y = b[1];\n\t\t\t\t\tif (a[3] != \"alpha\") s.alpha = 1;\n\t\t\t\t}\n"
         "\t\t\t\tswitch (a[3]) {\n\t\t\t\t\tcase \"x\": s.x = b[0] + v;\n\t\t\t\t\tcase \"y\": s.y = b[1] + v;\n\t\t\t\t\tcase \"angle\": s.angle = v;\n\t\t\t\t\tcase \"alpha\": s.alpha = v;\n"
         "\t\t\t\t\tcase \"scaleX\": s.scale.x = b[2] * v;\n\t\t\t\t\tcase \"scaleY\": s.scale.y = b[3] * v;\n\t\t\t\t}\n\t\t\t}\n"
         "\t\tcase 2:\n\t\t\tvar cam = a[2] == 1 ? camHUD : camGame;\n\t\t\tif (a[3] == \"defaultZoom\") defaultCamZoom = v; else Reflect.setProperty(cam, a[3], v);\n"
         "\t\tcase 3: scrollSpeed = v;\n\t}\n}\n\n";
    o << "function run(a) {\n\tvar k = key(a);\n\tif (tweens.exists(k)) { tweens.get(k).cancel(); tweens.remove(k); }\n"
         "\tif (a[7] <= 0) { write(a, a[4]); return; }\n\tvar from = a[6] ? a[5] : current(a);\n"
         "\ttweens.set(k, FlxTween.num(from, a[4], a[7] * Conductor.stepCrochet / 1000, {ease: eases[a[8]]}, function(v) { write(a, v); }));\n}\n";
    return o.str();
}
}
