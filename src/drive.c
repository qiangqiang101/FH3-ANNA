/* Auto Drive: the game's AI drives the player's car (0x18b7040(car, 1); the car's driver mode at
 * +0x7398 is then 0), along the route set on the map or by ANNA. A key or pad chord (or ANNA's Car >
 * Auto Drive) toggles it; throttle, brake or steering hand the car back.
 *
 * Cinematic camera: ForzaMultiCam (singleton at 0x4e6caf0) holds its camera groups as 64-byte entries
 * at +0x1a8 (camera at +0x30) and the current group on a list at +0x1d0 (count +0x1f0, back() via
 * 0x447250). 0x18f0f40(manager, group) switches group; group 13 is CarRelativeCams. CarRelativeCams
 * (Camera::CTSCCamera) keeps its shots at [+0x1b8]: mode (+0) and 0x5b0-byte entries (+8 begin, +0x10
 * end) with label byte +0x20 and start time +0x28. In mode 3 each shot starts at its index and the
 * update picks the shot under the time at +0x890, so writing the time selects a shot. Labels 17 and 19
 * are the slow orbiting shots FH2 used. The anim player at +0x1c0 has the playing shot config at +0x1a8
 * (entry +0x80) and its keys (64 bytes, time first) at +0x1b0..+0x1b8.
 *
 * As FH2 ANNA does it: while ANNA drives, the cinematic camera's button ([Autodrive] Cinematic, LB)
 * switches it on and off, with the HUD hidden behind a letterbox. For the first seconds of a drive a
 * prompt at the bottom says so, then that ANNA (D-pad Down) ends the drive. The shots' field of view
 * is the camera's FOV getter (CarRelativeCams' vtable +0x78: the shot camera it wraps at +0x1a8, else
 * its own +0x2b0), scaled by [Autodrive] CinematicFOV percent while the cinematic camera is on.
 *
 * The white flash between cameras is the game's own, as States::ResetCameraToPlayer gives it (0xd7a381):
 * 0x1941ff0(view, 3, 0.6 s) on the player's view, 0x1924bc0(0x1724f70([0x4e5bb48], 0), 0). It flashes
 * as the cinematic camera starts and stops and as it cuts from one shot to the next; a flash already
 * playing ([view+0xe9]) is left to finish. */
#include "anna.h"

typedef void (*SetAIDriving)(void *, unsigned char);
typedef void (*SetCameraGroup)(void *, int);
typedef int *(*CameraGroupBack)(void *);
enum { CAMERA_MANAGER = 0x4e6caf0, CAMERA_SET_GROUP = 0x18f0f40, CAMERA_GROUP_BACK = 0x447250,
       SET_AI_DRIVING = 0x18b7040 };
enum { CINEMATIC_GROUP = 13, SHOT_SIZE = 0x5b0 };

static int enabled;
static WORD cinematic_button = 0x100; /* XInput's LB; 0: no cinematic camera */
static const char *cinematic_name = "LB";
static int cinematic_was_down = 1, fov_percent = 70, prompt = -1;
typedef float (*CameraFov)(void *);
static CameraFov camera_fov;
static void *autodrive_car;
static WORD toggle_buttons = PAD_LEFT_THUMB | PAD_RIGHT_THUMB;
static int toggle_key = VK_F2;
static int key_was_down = 1, input_armed;
static volatile LONG requested;
static int cinematic_previous = -1, cinematic_tried, cinematic_label;
static float elapsed, shot_elapsed, shot_length;
static double cinematic_saved_time;
static unsigned shot_seed = 0x9e3779b9u;

static int driver_mode(void *car) {
    void *holder, *driver;
    int mode;
    if (!memory_read_pointer((unsigned char *)car + 0x7378, &holder) || !memory_read_pointer(holder, &driver) ||
        !memory_read((unsigned char *)car + 0x7398, &mode, sizeof(mode)))
        return -1;
    return mode;
}

static void hud_hidden(int mode) {
    if (host->api_version >= 2 && host->size >= offsetof(FH3PluginContext, set_hud_hidden) + sizeof(void *) &&
        host->set_hud_hidden)
        host->set_hud_hidden(mode);
}

/* The prompt at the bottom of the HUD: a translated text whose "{}" is the button's picture. */
static void show_prompt(const char *english, const char *button) {
    if (host->api_version < 2 || host->size < offsetof(FH3PluginContext, set_hud_prompt) + sizeof(void *) ||
        !host->set_hud_prompt)
        return;
    if (!english) {
        host->set_hud_prompt(NULL, NULL, NULL);
        return;
    }
    wchar_t text[192], *after = L"";
    if (MultiByteToWideChar(CP_UTF8, 0, text_get(english), -1, text, ARRAY_COUNT(text)) <= 0) return;
    for (wchar_t *at = text; at[0]; ++at)
        if (at[0] == L'{' && at[1] == L'}') {
            at[0] = 0;
            after = at + 2;
            break;
        }
    /* The picture brings its own gaps. */
    size_t length = wcslen(text);
    while (length && text[length - 1] == L' ') text[--length] = 0;
    while (*after == L' ') ++after;
    host->set_hud_prompt(text, button, after);
}

/* Which prompt the drive shows now: 0 the cinematic camera's, 1 ANNA's, 2 none. */
static void prompt_tick(int wanted) {
    if (wanted == prompt) return;
    prompt = wanted;
    if (wanted == 0) show_prompt("Press {} to activate Cinematic Camera", cinematic_name);
    else if (wanted == 1) show_prompt("Ask ANNA {} to disable Auto Drive", "DPadDown");
    else show_prompt(NULL, NULL);
}

static int cinematic_down(void) {
    for (DWORD pad = 0; cinematic_button && pad < 4; ++pad) {
        PadState state;
        if (pad_state(pad, &state) && (state.buttons & cinematic_button)) return 1;
    }
    return 0;
}

/* CarRelativeCams' field of view, narrower (or wider) while the cinematic camera is on. */
static int cinematic_on(void);
static float cinematic_fov(void *camera) {
    float fov = camera_fov(camera);
    static int logged;
    if (cinematic_on() && fov_percent != 100 && float_is_finite(fov) && fov > 0.0f) {
        if (!logged) {
            logged = 1;
            anna_log("AUTODRIVE: cinematic shot field of view %.3f -> %.3f.", (double)fov, (double)(fov * fov_percent / 100.0f));
        }
        fov = fov * (float)fov_percent / 100.0f;
    }
    return fov;
}

static int toggle_chord(void) {
    for (DWORD pad = 0; pad < 4; ++pad) {
        PadState state;
        if (pad_state(pad, &state) && (state.buttons & toggle_buttons) == toggle_buttons) return 1;
    }
    return 0;
}

static int same_name(const wchar_t *text, size_t length, const char *name) {
    size_t i = 0;
    for (; i < length && name[i]; ++i) {
        wchar_t c = text[i] >= L'a' && text[i] <= L'z' ? text[i] - 32 : text[i];
        if (c != (wchar_t)name[i]) return 0;
    }
    return i == length && !name[i];
}

/* "L3+R3", "LB+Y", ... ; 0 when any part is unknown. */
static WORD parse_buttons(const wchar_t *text) {
    static const struct { const char *name; WORD bit; } names[] = {
        {"UP", 0x1}, {"DOWN", 0x2}, {"LEFT", 0x4}, {"RIGHT", 0x8}, {"MENU", 0x10},
        {"START", 0x10}, {"VIEW", 0x20}, {"BACK", 0x20}, {"L3", 0x40}, {"R3", 0x80},
        {"LB", 0x100}, {"RB", 0x200}, {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000},
        {"Y", 0x8000}};
    WORD mask = 0;
    while (*text) {
        while (*text == L' ' || *text == L'+') ++text;
        size_t length = 0;
        while (text[length] && text[length] != L'+' && text[length] != L' ') ++length;
        if (!length) break;
        WORD bit = 0;
        for (unsigned i = 0; i < ARRAY_COUNT(names) && !bit; ++i)
            if (same_name(text, length, names[i].name)) bit = names[i].bit;
        if (!bit) return 0;
        mask |= bit;
        text += length;
    }
    return mask;
}

/* "F2", "G", "7" or a virtual-key code such as "0x71"; 0 when unknown. */
static int parse_key(const wchar_t *text) {
    size_t length = 0;
    while (text[length]) ++length;
    if (length > 2 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
        int value = 0;
        for (size_t i = 2; i < length; ++i) {
            wchar_t c = text[i];
            int digit = c >= L'0' && c <= L'9'   ? c - L'0'
                        : c >= L'a' && c <= L'f' ? c - L'a' + 10
                        : c >= L'A' && c <= L'F' ? c - L'A' + 10
                                                 : -1;
            if (digit < 0 || value > 0xff) return 0;
            value = value * 16 + digit;
        }
        return value > 0 && value < 0xff ? value : 0;
    }
    if (length == 1) {
        wchar_t c = text[0] >= L'a' && text[0] <= L'z' ? text[0] - 32 : text[0];
        return (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ? (int)c : 0;
    }
    if ((length == 2 || length == 3) && (text[0] == L'F' || text[0] == L'f')) {
        int number = text[1] - L'0';
        if (length == 3) number = number * 10 + (text[2] - L'0');
        return number >= 1 && number <= 12 && text[1] >= L'0' && text[1] <= L'9' &&
                       (length == 2 || (text[2] >= L'0' && text[2] <= L'9'))
                   ? VK_F1 + number - 1
                   : 0;
    }
    return 0;
}

static int driving_input(void) {
    for (DWORD pad = 0; pad < 4; ++pad) {
        PadState state;
        if (pad_state(pad, &state) && (state.left_trigger > 40 || state.right_trigger > 40 ||
                                       state.left_x > 12000 || state.left_x < -12000))
            return 1;
    }
    static const int keys[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'W', 'A', 'S', 'D'};
    for (unsigned i = 0; i < ARRAY_COUNT(keys); ++i)
        if (focused_key(keys[i])) return 1;
    return 0;
}

static void flash(void) {
    void *manager, *context, *view;
    if (!memory_read_pointer(image + 0x4e5bb48, &manager) ||
        !(context = ((void *(*)(void *, unsigned))(image + 0x1724f70))(manager, 0)) ||
        !(view = ((void *(*)(void *, int))(image + 0x1924bc0))(context, 0)))
        return;
    ((void (*)(void *, int, float))(image + 0x1941ff0))(view, 3, 0.6f);
}

static void *camera_manager(void) {
    void *manager;
    return memory_read_pointer(image + CAMERA_MANAGER, &manager) ? manager : NULL;
}

static int camera_group(void *manager) {
    size_t count;
    int group;
    if (!manager || !memory_read((unsigned char *)manager + 0x1f0, &count, sizeof(count)) || !count)
        return -1;
    int *current = ((CameraGroupBack)(image + CAMERA_GROUP_BACK))((unsigned char *)manager + 0x1d0);
    return current && memory_read(current, &group, sizeof(group)) ? group : -1;
}

static int set_camera_group(void *manager, int group) {
    unsigned char *groups, *end;
    void *camera;
    if (!manager || group < 0 || !memory_read_pointer((unsigned char *)manager + 0x1a8, (void **)&groups) ||
        !memory_read_pointer((unsigned char *)manager + 0x1b0, (void **)&end) || end < groups ||
        (size_t)(end - groups) / 64 <= (unsigned)group ||
        !memory_read_pointer(groups + (size_t)group * 64 + 0x30, &camera) || !camera)
        return 0;
    ((SetCameraGroup)(image + CAMERA_SET_GROUP))(manager, group);
    return camera_group(manager) == group;
}

/* The CarRelativeCams camera and its shot table, if it has the expected shape. */
static unsigned char *relative_camera(void *manager, unsigned char **shots, unsigned *count) {
    unsigned char *groups, *end, *camera, *data, *last;
    void *vtable;
    unsigned mode;
    if (!manager || !memory_read_pointer((unsigned char *)manager + 0x1a8, (void **)&groups) ||
        !memory_read_pointer((unsigned char *)manager + 0x1b0, (void **)&end) ||
        (size_t)(end - groups) / 64 <= CINEMATIC_GROUP ||
        !memory_read_pointer(groups + CINEMATIC_GROUP * 64 + 0x30, (void **)&camera) ||
        !memory_read_pointer(camera, &vtable) || vtable != image + 0x42ef648 ||
        !memory_read_pointer(camera + 0x1b8, (void **)&data) || !memory_read(data, &mode, sizeof(mode)) ||
        mode != 3 || !memory_read_pointer(data + 8, (void **)shots) ||
        !memory_read_pointer(data + 0x10, (void **)&last) || last <= *shots ||
        (size_t)(last - *shots) % SHOT_SIZE || (size_t)(last - *shots) / SHOT_SIZE > 1024)
        return NULL;
    *count = (unsigned)((size_t)(last - *shots) / SHOT_SIZE);
    return camera;
}

/* Select a random shot with the given label; 0 when there is none. */
static int select_shot(unsigned char *camera, unsigned char *shots, unsigned count, int label) {
    unsigned matches = 0;
    for (unsigned i = 0; i < count; ++i) {
        unsigned char value;
        if (!memory_read(shots + (size_t)i * SHOT_SIZE + 0x20, &value, 1)) return 0;
        matches += value == label;
    }
    if (!matches) return 0;
    shot_seed = shot_seed * 1664525u + 1013904223u + (unsigned)GetTickCount64();
    unsigned pick = (shot_seed >> 8) % matches;
    for (unsigned i = 0; i < count; ++i) {
        unsigned char value;
        memory_read(shots + (size_t)i * SHOT_SIZE + 0x20, &value, 1);
        if (value != label || pick--) continue;
        float start;
        if (!memory_read(shots + (size_t)i * SHOT_SIZE + 0x28, &start, sizeof(start)) || !float_is_finite(start))
            return 0;
        *(volatile double *)(camera + 0x890) = start + 0.5;
        cinematic_label = label;
        shot_elapsed = 0.0f;
        shot_length = 0.0f;
        return 1;
    }
    return 0;
}

/* Length of the shot the anim player is playing, once it has picked it up. */
static float playing_shot_length(unsigned char *camera, unsigned char *shots, unsigned count) {
    unsigned char *config, *keys, *keys_end;
    double time;
    float first, last;
    if (!memory_read(camera + 0x890, &time, sizeof(time)) || !(time >= 0 && time < count) ||
        !memory_read_pointer(camera + 0x1c0 + 0x1a8, (void **)&config) ||
        config != shots + (size_t)time * SHOT_SIZE + 0x80 ||
        !memory_read_pointer(camera + 0x1c0 + 0x1b0, (void **)&keys) ||
        !memory_read_pointer(camera + 0x1c0 + 0x1b8, (void **)&keys_end) || keys_end <= keys ||
        (size_t)(keys_end - keys) % 64 || !memory_read(keys, &first, sizeof(first)) ||
        !memory_read(keys_end - 64, &last, sizeof(last)) || !float_is_finite(first) || !float_is_finite(last))
        return 0.0f;
    float length = last - first;
    return length < 3.0f ? 3.0f : length > 15.0f ? 15.0f : length;
}

static void cinematic_enter(void) {
    cinematic_tried = 1;
    void *manager = camera_manager();
    int previous = camera_group(manager);
    unsigned char *shots, *camera;
    unsigned count;
    /* Only from the player's driving views (chase, driver, hood, bumper). */
    if (previous < 0 || previous > 4 || !(camera = relative_camera(manager, &shots, &count)) ||
        !memory_read(camera + 0x890, &cinematic_saved_time, sizeof(cinematic_saved_time))) {
        anna_log("AUTODRIVE: cinematic camera unavailable here.");
        return;
    }
    if (!select_shot(camera, shots, count, 17) && !select_shot(camera, shots, count, 19)) {
        anna_log("AUTODRIVE: no cinematic shots in CarRelativeCams.");
        return;
    }
    if (!set_camera_group(manager, CINEMATIC_GROUP)) {
        *(volatile double *)(camera + 0x890) = cinematic_saved_time;
        anna_log("AUTODRIVE: cinematic camera unavailable here.");
        return;
    }
    cinematic_previous = previous;
    flash();
    hud_hidden(2);
    prompt_tick(2);
    anna_log("AUTODRIVE: cinematic camera on.");
}

static void cinematic_tick(float dt) {
    void *manager = camera_manager();
    unsigned char *shots, *camera;
    unsigned count;
    if (cinematic_previous < 0) return;
    if (camera_group(manager) != CINEMATIC_GROUP) {
        cinematic_previous = -1; /* the player picked another camera */
        hud_hidden(0);
        return;
    }
    if (!(camera = relative_camera(manager, &shots, &count))) return;
    if (dt > 0.0f && dt < 1.0f) shot_elapsed += dt;
    if (!shot_length) shot_length = playing_shot_length(camera, shots, count);
    float length = shot_length ? shot_length : 8.0f;
    if (shot_elapsed < length) return;
    int next = cinematic_label == 17 ? 19 : 17;
    flash();
    if (!select_shot(camera, shots, count, next)) select_shot(camera, shots, count, cinematic_label);
}

static void cinematic_leave(void) {
    void *manager = camera_manager();
    unsigned char *shots, *camera;
    unsigned count;
    if (cinematic_previous >= 0 && camera_group(manager) == CINEMATIC_GROUP) {
        flash();
        set_camera_group(manager, cinematic_previous);
        if ((camera = relative_camera(manager, &shots, &count)))
            *(volatile double *)(camera + 0x890) = cinematic_saved_time;
    }
    cinematic_previous = -1;
    hud_hidden(0);
}

static int cinematic_on(void) { return cinematic_previous >= 0; }

/* While ANNA drives her ring is lit and "Auto Drive On" sits above her (Extra Options 3+). */
static void status_shown(int shown) {
    if (host->api_version < 3 || host->size < offsetof(FH3PluginContext, set_anna_status) + sizeof(void *) ||
        !host->set_anna_status)
        return;
    if (!shown) {
        host->set_anna_status(NULL, NULL);
        return;
    }
    wchar_t label[48], value[32];
    if (MultiByteToWideChar(CP_UTF8, 0, text_get("Auto Drive"), -1, label, ARRAY_COUNT(label)) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, text_get("On"), -1, value, ARRAY_COUNT(value)) <= 0)
        return;
    host->set_anna_status(label, value);
}

static void stop(const char *reason) {
    cinematic_leave();
    prompt_tick(2);
    status_shown(0);
    if (autodrive_car && driver_mode(autodrive_car) == 0) ((SetAIDriving)(image + SET_AI_DRIVING))(autodrive_car, 0);
    autodrive_car = NULL;
    anna_log("%s", reason);
}

static void start(void) {
    void *car = player_car();
    if (!car) {
        anna_log("AUTODRIVE: no human-driven local car found.");
        return;
    }
    ((SetAIDriving)(image + SET_AI_DRIVING))(car, 1);
    if (driver_mode(car) != 0) {
        anna_log("AUTODRIVE: the AI driver did not engage.");
        return;
    }
    autodrive_car = car;
    elapsed = 0.0f;
    cinematic_tried = 0;
    cinematic_was_down = 1; /* a button already down doesn't count */
    prompt = -1;
    status_shown(1);
    /* The press that started the drive must be released before any driving input counts as a cancel. */
    input_armed = 0;
    anna_log("AUTODRIVE: AI driver engaged on the local car.");
}

void drive_toggle(void) { InterlockedExchange(&requested, 1); }
int drive_active(void) { return autodrive_car != NULL; }
void *drive_car(void) { return autodrive_car; }

void drive_frame(float dt) {
    if (!enabled) return;
    int down = focused_key(toggle_key) || toggle_chord();
    int pressed = (down && !key_was_down) || InterlockedExchange(&requested, 0);
    key_was_down = down;
    if (autodrive_car) {
        if (driver_mode(autodrive_car) != 0) {
            /* A native state (event start, reset, car change) took the car back. */
            autodrive_car = NULL;
            cinematic_leave();
            prompt_tick(2);
            status_shown(0);
            anna_log("AUTODRIVE: the game ended the AI drive.");
            return;
        }
        int input = driving_input();
        if (!input) input_armed = 1;
        if (pressed) stop("AUTODRIVE: toggle returned control to the player.");
        else if (input && input_armed) stop("AUTODRIVE: driving input returned control to the player.");
        else {
            if (dt > 0.0f && dt < 1.0f) elapsed += dt;
            int down = cinematic_down();
            if (down && !cinematic_was_down) {
                if (cinematic_on()) {
                    cinematic_leave();
                    anna_log("AUTODRIVE: cinematic camera off.");
                } else {
                    cinematic_enter();
                }
            }
            cinematic_was_down = down;
            cinematic_tick(dt);
            /* Five seconds for each prompt, none once the cinematic camera has been used. */
            prompt_tick(cinematic_on() || cinematic_tried  ? 2
                        : cinematic_button && elapsed < 5  ? 0
                        : elapsed < (cinematic_button ? 10 : 5) ? 1
                                                                : 2);
        }
        return;
    }
    if (pressed) start();
}

static void anna_autodrive(void *user, const char *key) {
    (void)user;
    (void)key;
    drive_toggle();
}

static const char *autodrive_label(void *user, const char *key) {
    (void)user;
    (void)key;
    return text_get(autodrive_car ? "Disable Auto Drive" : "Enable Auto Drive"); /* as FH2 ANNA's */
}

int drive_prepare(void) {
    if (!(enabled = anna_option(L"Autodrive", L"Enabled", 1))) return 0;
    wchar_t value[64];
    anna_option_text(L"Autodrive", L"Key", L"F2", value, ARRAY_COUNT(value));
    int key = parse_key(value);
    if (key) toggle_key = key;
    else anna_log("CONFIG: invalid [Autodrive] Key; using F2.");
    anna_option_text(L"Autodrive", L"Buttons", L"L3+R3", value, ARRAY_COUNT(value));
    WORD buttons = parse_buttons(value);
    if (buttons) toggle_buttons = buttons;
    else anna_log("CONFIG: invalid [Autodrive] Buttons; using L3+R3.");
    /* The cinematic camera's button: a single button's name, None, or (older ANNA.ini) 1 / 0. */
    static const struct { const wchar_t *name; WORD bit; const char *picture; } cinematic_buttons[] = {
        {L"LB", 0x100, "LB"}, {L"RB", 0x200, "RB"}, {L"A", 0x1000, "A"}, {L"B", 0x2000, "B"},
        {L"X", 0x4000, "X"}, {L"Y", 0x8000, "Y"}, {L"VIEW", 0x20, "View"}, {L"BACK", 0x20, "View"},
        {L"MENU", 0x10, "Menu"}, {L"START", 0x10, "Menu"}, {L"L3", 0x40, "L3"}, {L"R3", 0x80, "R3"},
        {L"UP", 0x1, "DPadUp"}, {L"LEFT", 0x4, "DPadLeft"}, {L"RIGHT", 0x8, "DPadRight"}, {L"1", 0x100, "LB"}};
    anna_option_text(L"Autodrive", L"Cinematic", L"LB", value, ARRAY_COUNT(value));
    cinematic_button = 0;
    for (unsigned i = 0; i < ARRAY_COUNT(cinematic_buttons); ++i)
        if (!lstrcmpiW(value, cinematic_buttons[i].name)) {
            cinematic_button = cinematic_buttons[i].bit;
            cinematic_name = cinematic_buttons[i].picture;
        }
    if (!cinematic_button && lstrcmpiW(value, L"None") && lstrcmpW(value, L"0"))
        anna_log("CONFIG: invalid [Autodrive] Cinematic; no cinematic camera.");
    int percent = anna_option(L"Autodrive", L"CinematicFOV", 70);
    fov_percent = percent < 20 ? 20 : percent > 200 ? 200 : percent;
    if (cinematic_button && fov_percent != 100) {
        camera_fov = (CameraFov)host->hook_pointer(0x42ef648 + 0x78, (void *)cinematic_fov);
        if (!camera_fov) anna_log("AUTODRIVE: the cinematic camera's field of view can't be changed here.");
    }
    if (anna_option(L"Autodrive", L"InAnna", 1)) {
        FH3AnnaEntry entry = {sizeof(entry)};
        entry.key = "car.autodrive";
        entry.parent = FH3ANNA_LIST_CAR;
        entry.label = "Auto Drive";
        entry.label_now = autodrive_label;
        entry.on_choose = anna_autodrive;
        menu_add_entry(&entry);
    }
    anna_log("AUTODRIVE: [Autodrive] Key or Buttons toggles the AI driver; throttle, brake or steering cancels.");
    if (cinematic_button)
        anna_log("AUTODRIVE: %s while ANNA drives switches the cinematic camera (letterbox, %d%% field of view).",
                 cinematic_name, fov_percent);
    return 1;
}
