/* ANNA example: an Extra Options plugin that adds its own entries to ANNA's menu.
 *
 * Install: build ANNAExample.dll (sh build.sh) and put it in
 *   <Azimuth folder>\mods\Forza Horizon 3\ANNAExample\plugins\ANNAExample.dll
 * next to ANNA (mods\Forza Horizon 3\ANNA\plugins\ANNA.dll). In the game, press D-pad Down while
 * driving: ANNA's main list has "ANNA Example", and her Car list has "Example: Flash Message".
 * Its log lines are in FH3_ExtraOptions.log, marked PLUGIN:.
 *
 * What it shows:
 *   - finding ANNA (fh3anna_get, or fh3anna_wait on a thread when ANNA loads after this plugin)
 *   - a list of your own in ANNA's main list, with entries in it
 *   - an entry in one of ANNA's own lists (Car)
 *   - an on/off entry whose label says what it will do (label_now)
 *   - an entry shown only sometimes (shown): here, only while ANNA's Auto Drive is driving
 *   - an entry that keeps ANNA open (stays_open), its label changing each time
 *   - per-frame work in FH3PluginUpdate */
#include "fh3_plugin.h" /* Extra Options' plugin SDK (sdk/fh3_plugin.h in this repo) */
#include "fh3anna.h"    /* ANNA's API (include/fh3anna.h in this repo) */

#include <stdio.h>

static const FH3PluginContext *host;
static const FH3AnnaApi *anna;

static int party_mode;   /* the on/off entry's state */
static int pressed;      /* the counter entry's count */

static void say(const char *line) {
    if (host && host->log_utf8) host->log_utf8(line);
}

/* ---- What the entries do. Every callback runs on the game's thread: keep them short. ---- */

/* An ordinary entry: ANNA closes, then this runs on the next frame. */
static void hello(void *user, const char *key) {
    (void)user;
    char line[128];
    snprintf(line, sizeof line, "ANNA example: %s chosen.", key);
    say(line);
}

/* An on/off entry: its label (party_label) says what choosing it will do. */
static void party_toggle(void *user, const char *key) {
    (void)user;
    (void)key;
    party_mode = !party_mode;
    say(party_mode ? "ANNA example: party mode on." : "ANNA example: party mode off.");
}

static const char *party_label(void *user, const char *key) {
    (void)user;
    (void)key;
    return party_mode ? "Party Mode Off" : "Party Mode On";
}

/* A counter: stays_open keeps ANNA open on this list, and its label is asked again each time. */
static void count(void *user, const char *key) {
    (void)user;
    (void)key;
    ++pressed;
}

static const char *count_label(void *user, const char *key) {
    (void)user;
    (void)key;
    static char text[48]; /* ANNA copies the label straight away: a static buffer is fine */
    snprintf(text, sizeof text, "Pressed %d Time%s", pressed, pressed == 1 ? "" : "s");
    return text;
}

/* Shown only while ANNA's Auto Drive drives the car. */
static int while_autodriving(void *user, const char *key) {
    (void)user;
    (void)key;
    return anna && anna->is_autodriving();
}

/* ---- Adding them ---- */
static void add_entries(const FH3AnnaApi *api) {
    /* A list in ANNA's main list: no on_choose. It shows while one of its entries does. */
    FH3AnnaEntry list = {sizeof list};
    list.key = "annaexample";             /* keys are unique across all mods: start them with yours */
    list.label = "ANNA Example";
    api->add_entry(&list);

    FH3AnnaEntry entry = {sizeof entry};
    entry.key = "annaexample.hello";
    entry.parent = "annaexample";
    entry.label = "Say Hello";
    entry.on_choose = hello;
    api->add_entry(&entry);

    FH3AnnaEntry party = {sizeof party};
    party.key = "annaexample.party";
    party.parent = "annaexample";
    party.label = "Party Mode";           /* used when label_now gives nothing */
    party.label_now = party_label;
    party.on_choose = party_toggle;
    api->add_entry(&party);

    FH3AnnaEntry counter = {sizeof counter};
    counter.key = "annaexample.count";
    counter.parent = "annaexample";
    counter.label = "Count";
    counter.label_now = count_label;
    counter.on_choose = count;
    counter.stays_open = 1;
    api->add_entry(&counter);

    FH3AnnaEntry drive = {sizeof drive};
    drive.key = "annaexample.drive";
    drive.parent = "annaexample";
    drive.label = "Nice Driving, ANNA";
    drive.shown = while_autodriving;
    drive.on_choose = hello;
    api->add_entry(&drive);

    /* An entry in one of ANNA's own lists: Car (also FH3ANNA_LIST_CAR_LIGHTS, FH3ANNA_LIST_CAR_DOORS). */
    FH3AnnaEntry car = {sizeof car};
    car.key = "annaexample.flash";
    car.parent = FH3ANNA_LIST_CAR;
    car.label = "Example: Flash Message";
    car.on_choose = hello;
    api->add_entry(&car);

    say("ANNA example: entries added to ANNA's menu.");
}

/* ANNA hasn't loaded yet (mods load in loadorder.txt order): wait for her on a thread of our own. */
static DWORD WINAPI wait_for_anna(LPVOID unused) {
    (void)unused;
    const FH3AnnaApi *api = fh3anna_wait(1, 60000);
    if (!api) {
        say("ANNA example: ANNA isn't installed; nothing to add.");
        return 0;
    }
    anna = api;
    add_entries(api);
    return 0;
}

/* ---- Extra Options' plugin exports ---- */
__declspec(dllexport) BOOL WINAPI FH3PluginInitialize(const FH3PluginContext *context) {
    host = context;
    anna = fh3anna_get(1);
    if (anna) {
        add_entries(anna);
    } else {
        /* Never wait here: this runs on the game thread. */
        HANDLE thread = CreateThread(NULL, 0, wait_for_anna, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}

/* Once per frame on the game thread while driving. */
__declspec(dllexport) void WINAPI FH3PluginUpdate(const FH3PluginFrame *frame) {
    (void)frame;
    /* Per-frame work goes here, e.g. what party mode does to the car (frame->car). */
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
