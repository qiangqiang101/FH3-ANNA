// ANNA example: an FH3 Mod Platform mod that adds its own entries to ANNA's menu.
//
// Install: build ANNAExampleMod.dll (see CMakeLists.txt) and put it in fh3-mods next to azimuth.exe,
// with ANNA installed as an Extra Options plugin (mods\Forza Horizon 3\ANNA\plugins\ANNA.dll). In the
// game, press D-pad Down while driving: ANNA's main list has "ANNA Example (Mod)". Its log lines are in
// fh3-mods\platform.log; the MODS page has a row that hides its entries.
//
// What it shows:
//   - waiting for ANNA on a thread of the mod's own (platform mods start before Extra Options plugins)
//   - a list of your own, an on/off entry (label_now), a counter that keeps ANNA open (stays_open)
//   - a MODS page row (FH3 Mod Platform) that hides the entries (shown)
//
// No fh3mod_shutdown (hot reload): ANNA keeps the entries' callbacks for the whole session, so this
// DLL must stay loaded. A mod that wants hot reload should not add entries to ANNA.
#include "fh3mod.h"   // FH3 Mod Platform (platform/include/fh3mod.h)
#include "fh3anna.h"  // ANNA's API (include/fh3anna.h in the FH3-ANNA repo)

#include <windows.h>

#include <atomic>
#include <cstdio>

namespace {

const FH3ModApi* g_api   = nullptr;
FH3Mod* g_mod            = nullptr;
const FH3AnnaApi* g_anna = nullptr;

std::atomic<bool> g_shown{true};   // the MODS page row: show the entries in ANNA
bool g_lights_show       = false;  // the on/off entry's state
int g_pressed            = 0;      // the counter entry's count

void say(const char* line) { g_api->log(g_mod, line); }

// ---- What the entries do. Every callback runs on the game's thread: keep them short. ----
int shown(void*, const char*) { return g_shown.load(); }

void hello(void*, const char* key) {
    char line[128];
    std::snprintf(line, sizeof line, "%s chosen", key);
    say(line);
}

void lights_toggle(void*, const char*) {
    g_lights_show = !g_lights_show;
    say(g_lights_show ? "light show on" : "light show off");
}

const char* lights_label(void*, const char*) { return g_lights_show ? "Stop Light Show" : "Start Light Show"; }

void count(void*, const char*) { ++g_pressed; }

const char* count_label(void*, const char*) {
    static char text[48];   // ANNA copies the label straight away
    std::snprintf(text, sizeof text, "Pressed %d Time%s", g_pressed, g_pressed == 1 ? "" : "s");
    return text;
}

void add_entries(const FH3AnnaApi* anna) {
    FH3AnnaEntry list{};
    list.size  = sizeof list;
    list.key   = "annaexamplemod";   // unique across all mods: start keys with your mod's name
    list.label = "ANNA Example (Mod)";
    list.shown = shown;               // hiding the list hides everything in it
    anna->add_entry(&list);

    FH3AnnaEntry entry{};
    entry.size      = sizeof entry;
    entry.key       = "annaexamplemod.hello";
    entry.parent    = "annaexamplemod";
    entry.label     = "Say Hello";
    entry.on_choose = hello;
    anna->add_entry(&entry);

    FH3AnnaEntry lights{};
    lights.size      = sizeof lights;
    lights.key       = "annaexamplemod.lights";
    lights.parent    = "annaexamplemod";
    lights.label     = "Light Show";
    lights.label_now = lights_label;
    lights.on_choose = lights_toggle;
    anna->add_entry(&lights);

    FH3AnnaEntry counter{};
    counter.size       = sizeof counter;
    counter.key        = "annaexamplemod.count";
    counter.parent     = "annaexamplemod";
    counter.label      = "Count";
    counter.label_now  = count_label;
    counter.on_choose  = count;
    counter.stays_open = 1;
    anna->add_entry(&counter);

    say("entries added to ANNA's menu");
}

// Platform mods load before Extra Options plugins: ANNA isn't there yet when fh3mod_init runs.
DWORD WINAPI wait_for_anna(LPVOID) {
    g_anna = fh3anna_wait(1, 120000);
    if (!g_anna) {
        say("ANNA isn't installed; nothing to add");
        return 0;
    }
    add_entries(g_anna);
    return 0;
}

} // namespace

FH3MOD_EXPORT int fh3mod_init(const FH3ModApi* api) {
    if (api->api_version < 1) return 1;
    g_api = api;

    FH3ModInfo info{};
    info.size             = sizeof info;
    info.name             = "ANNAExampleMod";
    info.version          = "1.0.0";
    info.platform_version = FH3MOD_PLATFORM_VERSION;
    g_mod                 = api->register_mod(&info);
    if (!g_mod) return 1;

    // A MODS page row that hides the entries in ANNA (API 4).
    if (api->api_version >= 4) {
        FH3Setting row{};
        row.size        = sizeof row;
        row.key         = "ShowInAnna";
        row.label       = "Show in ANNA";
        row.description = "Lists ANNA Example (Mod) in ANNA's menu (D-pad Down).";
        row.value       = 1;
        row.on_change   = [](void*, const char*, int value) { g_shown = value != 0; };
        api->add_setting(g_mod, &row);
    }

    // Never wait in fh3mod_init: a thread of our own waits for ANNA.
    if (HANDLE thread = CreateThread(nullptr, 0, wait_for_anna, nullptr, 0, nullptr)) CloseHandle(thread);
    return 0;
}
