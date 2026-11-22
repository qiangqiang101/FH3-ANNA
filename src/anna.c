/* FH3 ANNA: ANNA's menu for every mod (include/fh3anna.h), with her Car list (Auto Drive, Lights,
 * Doors), Auto Drive and the car's lights and doors. An Extra Options plugin:
 * mods\Forza Horizon 3\ANNA\plugins\ANNA.dll, settings in mods\Forza Horizon 3\ANNA\ANNA.ini. */
#include "anna.h"

#include <stdarg.h>
#include <stdio.h>

#define ANNA_VERSION "1.0.0"

const FH3PluginContext *host;
unsigned char *image;
static wchar_t ini_path[MAX_PATH];

void anna_log(const char *format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    host->log_utf8(line);
}

int anna_option(const wchar_t *section, const wchar_t *key, int fallback) {
    return (int)GetPrivateProfileIntW(section, key, fallback, ini_path);
}

void anna_option_text(const wchar_t *section, const wchar_t *key, const wchar_t *fallback, wchar_t *out,
                      unsigned capacity) {
    GetPrivateProfileStringW(section, key, fallback, out, capacity, ini_path);
}

/* ---- Settings ------------------------------------------------------------------------------------ */
static const char default_ini[] =
    "; FH3 ANNA: ANNA's menu (D-pad Down while driving) with Car > Auto Drive, Lights and Doors.\r\n"
    "; Changes apply when the game starts.\r\n"
    "[Autodrive]\r\n"
    "; The game's AI drives your car along the route set on the map or by ANNA. Throttle, brake or\r\n"
    "; steering hands it back.\r\n"
    "Enabled=%d\r\n"
    "; Keyboard toggle: F1..F12, a letter or digit, or a virtual-key code such as 0x71.\r\n"
    "Key=%ls\r\n"
    "; Controller toggle, pressed together: UP DOWN LEFT RIGHT START BACK L3 R3 LB RB A B X Y.\r\n"
    "; D-pad Down belongs to ANNA, so the default is both sticks clicked.\r\n"
    "Buttons=%ls\r\n"
    "; List Auto Drive in ANNA's Car list.\r\n"
    "InAnna=%d\r\n"
    "; While ANNA drives, this button switches the cinematic camera on and off: slow orbiting shots of the\r\n"
    "; car, the HUD hidden behind black bars. A button's name (LB RB A B X Y VIEW MENU L3 R3 UP LEFT\r\n"
    "; RIGHT), or None for no cinematic camera.\r\n"
    "Cinematic=%ls\r\n"
    "; The cinematic shots' field of view, in percent of the game's: below 100 is narrower (from\r\n"
    "; further away), above 100 wider. 20 to 200.\r\n"
    "CinematicFOV=70\r\n"
    "\r\n"
    "[Car]\r\n"
    "; ANNA's Car list: Lights (headlights, hazard lights, left/right indicators) and Doors (open and\r\n"
    "; close the roof, hood, trunk and doors the car animates, or all at once). Parts open only while\r\n"
    "; stationary; the throttle closes them.\r\n"
    "Menu=%d\r\n"
    "; Hold D-pad Left/Right for 1 s to turn that indicator on or off (a tap still changes the radio\r\n"
    "; station). Indicators turn off once a turn is made and the steering is centred.\r\n"
    "IndicatorsOnDpad=%d\r\n";

/* A new ANNA.ini keeps the settings these had in FH3_ExtraOptions.ini ([Vehicle]), next to azimuth.exe
   three folders up. */
static void write_default_ini(const wchar_t *mod_directory) {
    if (GetFileAttributesW(ini_path) != INVALID_FILE_ATTRIBUTES) return;
    wchar_t old[MAX_PATH];
    lstrcpynW(old, mod_directory, MAX_PATH);
    for (int up = 0; up < 3; ++up) {
        wchar_t *slash = NULL;
        for (wchar_t *p = old; *p; ++p)
            if (*p == L'\\' || *p == L'/') slash = p;
        if (slash) *slash = 0;
    }
    int migrated = lstrlenW(old) + 24 < MAX_PATH;
    if (migrated) lstrcatW(old, L"\\FH3_ExtraOptions.ini");
    migrated = migrated && GetFileAttributesW(old) != INVALID_FILE_ATTRIBUTES;
    const wchar_t *from = migrated ? old : L"";
    wchar_t key[64] = L"F2", buttons[64] = L"L3+R3";
    if (migrated) {
        GetPrivateProfileStringW(L"Vehicle", L"AutodriveKey", L"F2", key, 64, from);
        GetPrivateProfileStringW(L"Vehicle", L"AutodriveButtons", L"L3+R3", buttons, 64, from);
    }
#define OLD(name, fallback) (migrated ? (int)GetPrivateProfileIntW(L"Vehicle", name, fallback, from) : fallback)
    char text[sizeof(default_ini) + 256];
    int length = snprintf(text, sizeof(text), default_ini, OLD(L"Autodrive", 1) != 0, key, buttons,
                          OLD(L"AutodriveInAnna", 1) != 0, OLD(L"AutodriveCinematic", 1) ? L"LB" : L"None",
                          OLD(L"CarPartsInAnna", 1) != 0, OLD(L"IndicatorsOnDpad", 1) != 0);
#undef OLD
    HANDLE file = CreateFileW(ini_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || length <= 0) return;
    DWORD written;
    WriteFile(file, text, (DWORD)length, &written, NULL);
    CloseHandle(file);
    anna_log(migrated ? "ANNA: ANNA.ini written with the settings from FH3_ExtraOptions.ini [Vehicle]."
                      : "ANNA: ANNA.ini written with the default settings.");
}

/* ---- Input --------------------------------------------------------------------------------------- */
typedef DWORD(WINAPI *GetPadState)(DWORD, PadState *);
static GetPadState get_pad_state;

int pad_state(DWORD pad, PadState *state) {
    if (!get_pad_state) {
        HMODULE xinput = LoadLibraryExW(L"xinput1_4.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        get_pad_state = xinput ? (GetPadState)(void *)GetProcAddress(xinput, "XInputGetState") : NULL;
        if (!get_pad_state) return 0;
    }
    return get_pad_state(pad, state) == ERROR_SUCCESS;
}

static int window_focused(void) {
    DWORD process = 0;
    HWND window = GetForegroundWindow();
    return window && (GetWindowThreadProcessId(window, &process), process == GetCurrentProcessId());
}

int focused_key(int key) { return window_focused() && (GetAsyncKeyState(key) & 0x8000) != 0; }

void *player_car(void) { return host->player_car ? host->player_car() : NULL; }

void *driven_car(void) {
    void *car = player_car();
    return car ? car : drive_car();
}

/* ---- The API for other mods ---------------------------------------------------------------------- */
static int api_is_open(void) { return menu_is_open(); }
static int api_is_autodriving(void) { return drive_active(); }
static const FH3AnnaApi api = {sizeof(FH3AnnaApi), 1, menu_add_entry, api_is_open, api_is_autodriving};
static int ready, active;

__declspec(dllexport) const FH3AnnaApi *FH3ANNA_GetApi(uint32_t min_version) {
    return ready && min_version <= api.version ? &api : NULL;
}

/* ---- Plugin entry -------------------------------------------------------------------------------- */
static int car_shown(void *user, const char *key) {
    (void)user;
    (void)key;
    return driven_car() != NULL;
}

static const char *car_label(void *user, const char *key) {
    (void)user;
    (void)key;
    return text_get("Car");
}

__declspec(dllexport) BOOL WINAPI FH3PluginInitialize(const FH3PluginContext *context) {
    if (!context || context->api_version < 1 || !context->hook_call || !context->patch_bytes) return FALSE;
    host = context;
    image = context->game_image;
    if (lstrlenW(context->mod_directory) + 10 >= MAX_PATH) return FALSE;
    /* An Extra Options with ANNA built in (0.6.0 and older) has already put its own jump in the padding
       at 0xaa0101 that ANNA's menu uses: ANNA leaves the game to it rather than hook the same code twice. */
    static const unsigned char padding[12] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
    unsigned char bytes[12];
    if (!memory_read(image + 0xaa0101, bytes, sizeof(bytes)) || !memory_equal(bytes, padding, sizeof(bytes))) {
        anna_log("ANNA " ANNA_VERSION ": this Extra Options has ANNA built in; update Extra Options to use this plugin.");
        return TRUE;
    }
    lstrcpyW(ini_path, context->mod_directory);
    lstrcatW(ini_path, L"\\ANNA.ini");
    write_default_ini(context->mod_directory);
    int menu = menu_prepare();
    if (!menu) anna_log("ANNA: the game's ANNA code differs; her menu is unavailable.");
    FH3AnnaEntry car = {sizeof(car)};
    car.key = FH3ANNA_LIST_CAR;
    car.label = "Car";
    car.label_now = car_label;
    car.shown = car_shown;
    menu_add_entry(&car);
    drive_prepare();
    car_prepare(anna_option(L"Car", L"Menu", 1), anna_option(L"Car", L"IndicatorsOnDpad", 1));
    ready = menu;
    active = 1;
    anna_log(menu ? "ANNA " ANNA_VERSION ": menu ready; other mods add entries through fh3anna.h."
                  : "ANNA " ANNA_VERSION ": Auto Drive and lights only.");
    return TRUE;
}

__declspec(dllexport) void WINAPI FH3PluginUpdate(const FH3PluginFrame *frame) {
    if (!active) return;
    float dt = frame && frame->size >= sizeof(FH3PluginFrame) ? frame->delta_seconds : 0.0f;
    menu_frame();
    drive_frame(dt);
    car_frame();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
