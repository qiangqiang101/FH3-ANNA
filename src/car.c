/* The car's lights and animated parts, and their ANNA lists (Car > Lights, Car > Doors).
 *
 * Car parts: ForzaCar's animator (+0x73a8) points to the car's animation graph at +0x1e0 and takes
 * named events through 0x1889b40(car, name), the path the game uses for "doorLF:close". The graph's
 * events are a map at graph+0x268 (end node at +0x270), searched with 0x1d08e60. Cars define
 * "<part>:open" and "<part>:close" (sometimes "<part>:toggle") for the parts they animate (the same
 * clips ForzaVista plays). In free roam the graph runs its race LOD, whose LODSelect branch has no
 * part animations; ForzaVista runs LOD 0 (Car_Autovista). The graph input GameInputs:AnimationLOD
 * picks the branch: looked up with 0x21f7490(graph, &name), set with 0x21f83f0(input, value).
 *
 * Lights: headlights (ForzaCar, read each frame into the light material packet by 0x1889140) come
 * from +0x7852 while the manual flag +0x7851 is set, else from +0x7868. The car update rewrites them
 * every frame, so the override is applied just before the packet is built: its one call (0x18b8e22)
 * is wrapped. The packet's hazard and indicator inputs (+0x786f..+0x7871) drive the native blink; car
 * models draw it once their indicator materials are repaired (indicator_material_load). */
#include "anna.h"

enum { PART_ROOF, PART_HOOD, PART_TRUNK, PART_DOOR_LF, PART_DOOR_RF, PART_DOOR_LR, PART_DOOR_RR,
       PART_COUNT, PART_ALL = PART_COUNT, LIGHT_HEAD, LIGHT_HAZARD, LIGHT_LEFT, LIGHT_RIGHT, ITEMS };
static const char *const part_names[PART_COUNT] = {"roof", "hood", "trunk", "doorLF",
                                                   "doorRF", "doorLR", "doorRR"};
enum { CAR_LIGHTS_MANUAL = 0x7851, CAR_LIGHTS_ON = 0x7852, CAR_LIGHTS_AUTO = 0x7868 };
enum { CAR_HAZARDS = 0x786f, CAR_SIGNAL_LEFT = 0x7870, CAR_SIGNAL_RIGHT = 0x7871 };
enum { SIGNAL_OFF, SIGNAL_HAZARD, SIGNAL_LEFT, SIGNAL_RIGHT };

typedef void (*CarEvent)(void *, const char *);
typedef void *(*EventFind)(void *, void **, const MsvcString *);

static void *parts_car;
static unsigned parts_open;
static void *headlight_car;
static int headlight_override = -1; /* -1 native, else 0 off / 1 on */
/* signal_car blinks signal_active; signal_cleared is a car whose signals still have to be switched
   off (compared only, never dereferenced here). */
static void *signal_car, *signal_cleared;
static int signal_active, signal_turned;
static void *lod_graph, *lod_input;
static ULONGLONG lod_restore_at;
static void (*light_packet)(void *, float);

static int car_has_event(void *car, const char *part, const char *action) {
    unsigned char *animator, *graph;
    void *end, *found = NULL;
    MsvcString key = {{{0}}, 0, 15};
    size_t part_length = text_length(part), action_length = text_length(action);
    if (!car || part_length + 1 + action_length > 15 ||
        !memory_read_pointer((unsigned char *)car + 0x73a8, (void **)&animator) || !animator ||
        !memory_read_pointer(animator + 0x1e0, (void **)&graph) || !graph ||
        !memory_read_pointer(graph + 0x270, &end) || !end)
        return 0;
    memory_copy(key.text, part, part_length);
    key.text[part_length] = ':';
    memory_copy(key.text + part_length + 1, action, action_length);
    key.size = part_length + 1 + action_length;
    ((EventFind)(image + 0x1d08e60))(graph + 0x268, &found, &key);
    return found && found != end;
}

static int is_light(int part) { return part >= LIGHT_HEAD && part <= LIGHT_RIGHT; }
static int signal_of(int light) { return light - LIGHT_HAZARD + SIGNAL_HAZARD; }

static int headlights_on(void *car) {
    if (car == headlight_car && headlight_override >= 0) return headlight_override;
    unsigned char manual, value;
    if (!memory_read((unsigned char *)car + CAR_LIGHTS_MANUAL, &manual, 1) ||
        !memory_read((unsigned char *)car + (manual ? CAR_LIGHTS_ON : CAR_LIGHTS_AUTO), &value, 1))
        return 0;
    return value != 0;
}

static int single_part_supported(void *car, int part) {
    return car_has_event(car, part_names[part], "toggle") ||
           (car_has_event(car, part_names[part], "open") && car_has_event(car, part_names[part], "close"));
}

static int part_supported(void *car, int part) {
    if (!car) return 0;
    if (is_light(part)) return 1;
    if (part < PART_ALL) return single_part_supported(car, part);
    int count = 0;
    for (int i = 0; i < PART_COUNT; ++i) count += single_part_supported(car, i);
    return count >= 2;
}

static int part_is_open(void *car, int part) {
    if (part == LIGHT_HEAD) return car && headlights_on(car);
    if (is_light(part)) return car && car == signal_car && signal_active == signal_of(part);
    if (part > PART_ALL || car != parts_car) return 0;
    return part == PART_ALL ? parts_open != 0 : (int)(parts_open >> part & 1);
}

/* ForzaCar keeps its world velocity at +0x7600; squared speed in (m/s)^2. */
static float car_speed2(void *car) {
    float velocity[3];
    if (!memory_read((unsigned char *)car + 0x7600, velocity, sizeof(velocity))) return -1.0f;
    for (int i = 0; i < 3; ++i)
        if (!float_is_finite(velocity[i])) return -1.0f;
    return velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2];
}

static int car_stationary(void *car) {
    float speed2 = car_speed2(car);
    return speed2 >= 0.0f && speed2 < 0.25f;
}

/* Above about 1 m/s (3.6 km/h). */
static int car_moving(void *car) { return car_speed2(car) > 1.0f; }

/* Replaces the call to the light packet builder 0x1889140(car, dt). */
static void car_light_packet(void *car, float dt) {
    unsigned char *bytes = car;
    if (car && car == headlight_car && headlight_override >= 0) {
        bytes[CAR_LIGHTS_MANUAL] = 1;
        bytes[CAR_LIGHTS_ON] = (unsigned char)headlight_override;
    }
    if (car && (car == signal_car || car == signal_cleared)) {
        int signal = car == signal_car ? signal_active : SIGNAL_OFF;
        bytes[CAR_HAZARDS] = signal == SIGNAL_HAZARD;
        bytes[CAR_SIGNAL_LEFT] = signal == SIGNAL_LEFT;
        bytes[CAR_SIGNAL_RIGHT] = signal == SIGNAL_RIGHT;
        if (car == signal_cleared) signal_cleared = NULL;
    }
    light_packet(car, dt);
}

static void use_autovista_lod(void *car) {
    unsigned char *animator;
    void *graph;
    if (!memory_read_pointer((unsigned char *)car + 0x73a8, (void **)&animator) || !animator ||
        !memory_read_pointer(animator + 0x1e0, &graph) || !graph)
        return;
    if (graph != lod_graph) {
        static const char name[] = "GameInputs:AnimationLOD";
        MsvcString key = {{{0}}, sizeof(name) - 1, sizeof(name) - 1};
        key.pointer = (char *)name;
        lod_input = ((void *(*)(void *, const MsvcString *))(image + 0x21f7490))(graph, &key);
        lod_graph = graph;
    }
    if (lod_input) ((void (*)(void *, float))(image + 0x21f83f0))(lod_input, 0.0f);
}

static void send_part_event(void *car, int part, int open) {
    int currently = parts_open >> part & 1;
    if (currently == open) return;
    const char *action = car_has_event(car, part_names[part], "toggle") ? "toggle"
                         : open                                       ? "open"
                                                                      : "close";
    char event[16];
    size_t length = text_length(part_names[part]);
    memory_copy(event, part_names[part], length);
    event[length] = ':';
    memory_copy(event + length + 1, action, text_length(action) + 1);
    use_autovista_lod(car);
    ((CarEvent)(image + 0x1889b40))(car, event);
    anna_log("CAR PARTS: sent %s.", event);
    parts_open ^= 1u << part;
    lod_restore_at = parts_open ? 0 : GetTickCount64() + 3000; /* let the close play out */
}

static void close_all_parts(void *car) {
    for (int i = 0; i < PART_COUNT; ++i)
        if (single_part_supported(car, i)) send_part_event(car, i, 0);
}

/* Tapping the throttle, or the car moving at all (rolling downhill, pushed, driven by the AI),
   closes every open part; the race LOD (Car_Race_Focus) returns once every part has closed. */
static void parts_tick(void) {
    if (parts_open && parts_car) {
        void *car = player_car();
        int close = car != parts_car || car_moving(car) || focused_key('W') || focused_key(VK_UP);
        for (DWORD pad = 0; !close && pad < 4; ++pad) {
            PadState state;
            close = pad_state(pad, &state) && state.right_trigger > 40;
        }
        if (close) {
            if (car == parts_car) close_all_parts(car);
            else parts_open = 0; /* the car changed; its parts reset with it */
        }
    }
    if (!lod_restore_at || GetTickCount64() < lod_restore_at) return;
    lod_restore_at = 0;
    void *car = player_car();
    unsigned char *animator;
    void *graph;
    if (lod_input && car && memory_read_pointer((unsigned char *)car + 0x73a8, (void **)&animator) &&
        animator && memory_read_pointer(animator + 0x1e0, &graph) && graph == lod_graph)
        ((void (*)(void *, float))(image + 0x21f83f0))(lod_input, 1.0f);
}

/* Indicators turn themselves off like a real stalk: once the steering has turned (|steering| >= 0.3)
   and come back to the centre (<= 0.1). The steering is the driver's input: the left stick of any
   pad, or a steering key (full lock). */
static float steering_input(void) {
    static const int keys[] = {'A', 'D', VK_LEFT, VK_RIGHT};
    for (unsigned i = 0; i < ARRAY_COUNT(keys); ++i)
        if (focused_key(keys[i])) return 1.0f;
    float strongest = 0.0f;
    for (DWORD pad = 0; pad < 4; ++pad) {
        PadState state;
        if (!pad_state(pad, &state)) continue;
        float x = state.left_x < 0 ? -(float)state.left_x / 32768.0f : (float)state.left_x / 32767.0f;
        if (x > strongest) strongest = x;
    }
    return strongest;
}

static void set_signal(void *car, int signal) {
    static const char *const messages[] = {"LIGHTS: indicators off.", "LIGHTS: hazard lights on.",
                                           "LIGHTS: left indicator on.", "LIGHTS: right indicator on."};
    if (signal_car && signal_car != car) signal_cleared = signal_car;
    signal_car = signal ? car : NULL;
    if (!signal) signal_cleared = car;
    signal_active = signal;
    signal_turned = steering_input() >= 0.3f;
    anna_log("%s", messages[signal]);
}

/* D-pad Left/Right (the RadioLeft/RadioRight actions 0x1d/0x1e) both change the radio station and,
   held for a second, toggle that indicator. The radio consumers (the driving input handler 0xe04xxx,
   in input contexts 9 and 0xe, and the radio page update 0x14705e0) ask 0x1a198f0(input, context,
   action, 1) whether the action was just pressed; those six calls are wrapped. A press the game
   reports while that D-pad direction is down on a pad starts a hold and is held back: released within
   the second, it is reported on the next query (a tap changes the station); held, it toggles the
   indicator instead. The query only answers inside the radio's input context, so menus keep the
   D-pad. Keyboard radio keys pass straight through. */
enum { RADIO_LEFT = 0x1d, RADIO_RIGHT = 0x1e, INDICATOR_HOLD_MS = 1000, RADIO_TAP_MS = 250 };
typedef unsigned char (*ActionPressed)(void *, unsigned, unsigned, unsigned char);
static ActionPressed radio_query;
static int hold_side, hold_consumed, tap_side;
static ULONGLONG hold_since, tap_deadline;

static WORD dpad_down(void) {
    WORD buttons = 0;
    for (DWORD pad = 0; pad < 4; ++pad) {
        PadState state;
        if (pad_state(pad, &state)) buttons |= state.buttons;
    }
    return buttons & (PAD_DPAD_LEFT | PAD_DPAD_RIGHT);
}

static WORD side_button(int side) { return side == SIGNAL_LEFT ? PAD_DPAD_LEFT : PAD_DPAD_RIGHT; }

/* Finish holds and cancel indicators after a turn. */
static void signals_tick(void) {
    if (!signal_car && !hold_side) return;
    void *car = driven_car();
    if (signal_car && car != signal_car) {
        signal_cleared = signal_car; /* the car changed or the drive ended */
        signal_car = NULL;
        signal_active = SIGNAL_OFF;
    }
    if (hold_side) {
        ULONGLONG now = GetTickCount64();
        if (!(dpad_down() & side_button(hold_side))) {
            if (!hold_consumed) {
                tap_side = hold_side;
                tap_deadline = now + RADIO_TAP_MS;
            }
            hold_side = 0;
        } else if (!hold_consumed && now - hold_since >= INDICATOR_HOLD_MS) {
            hold_consumed = 1;
            if (car)
                set_signal(car, car == signal_car && signal_active == hold_side ? SIGNAL_OFF : hold_side);
        }
    }
    if (car && car == signal_car && (signal_active == SIGNAL_LEFT || signal_active == SIGNAL_RIGHT)) {
        float steering = steering_input();
        if (steering >= 0.3f) signal_turned = 1;
        else if (signal_turned && steering <= 0.1f) set_signal(car, SIGNAL_OFF);
    }
}

static unsigned char radio_action_pressed(void *input, unsigned context, unsigned action,
                                          unsigned char consume) {
    unsigned char pressed = radio_query(input, context, action, consume);
    int side = action == RADIO_LEFT ? SIGNAL_LEFT : action == RADIO_RIGHT ? SIGNAL_RIGHT : 0;
    if (!side) return pressed;
    if (menu_race_open()) return 0; /* the D-pad is ANNA's race menu's */
    signals_tick();
    if (pressed && (dpad_down() & side_button(side))) {
        if (!hold_side) {
            hold_side = side;
            hold_consumed = 0;
            hold_since = GetTickCount64();
        }
        return 0;
    }
    if (!pressed && tap_side == side) {
        tap_side = 0;
        return GetTickCount64() <= tap_deadline;
    }
    return pressed;
}

static void toggle_light(void *car, int light) {
    if (light != LIGHT_HEAD) {
        set_signal(car, part_is_open(car, light) ? SIGNAL_OFF : signal_of(light));
        return;
    }
    int on = !headlights_on(car);
    headlight_car = car;
    headlight_override = on;
    anna_log(on ? "LIGHTS: headlights on." : "LIGHTS: headlights off.");
}

/* Parts open only while the car is stationary; closing always works. */
static void toggle_part(int part) {
    void *car = player_car();
    if (!car || !part_supported(car, part)) return;
    if (is_light(part)) {
        toggle_light(car, part);
        return;
    }
    if (car != parts_car) {
        parts_car = car; /* a new car starts with everything closed */
        parts_open = 0;
    }
    int open = !part_is_open(car, part);
    if (open && !car_stationary(car)) {
        anna_log("CAR PARTS: the car must be stationary to open parts.");
    } else if (part == PART_ALL) {
        if (open) {
            for (int i = 0; i < PART_COUNT; ++i)
                if (single_part_supported(car, i)) send_part_event(car, i, 1);
        } else {
            close_all_parts(car);
        }
    } else {
        send_part_event(car, part, open);
    }
}

/* ---- Indicator lamps ------------------------------------------------------------------------------
   Car materials are GrubReader ("burG") chunks, loaded by 0x1ebf820(self, owner, view, context) from
   its one call at 0x1eb7c3f. Many cars' turn-signal materials leave their LightType property at 70
   (none), so the hazard and indicator inputs never reach them. For a material named like a turn signal
   (turn, indicator, blinker) whose side is known (from its name, or from a model entirely on one side
   of the car), a private copy of the chunk gets LightType 21 (left) or 20 (right) and is loaded in its
   place. The game's buffers and files are never changed; materials without that property, or on both
   sides, are left alone. */
typedef struct {
    uint32_t tag;
    uint16_t version, metadata_count;
    uint32_t metadata, offset, size, unpacked;
} GrubChunk;
typedef struct {
    void *table, *allocation, *stream;
    const unsigned char *header;
    const GrubChunk *chunks;
    const unsigned char *data;
    uint32_t count, padding;
} GrubReader;
typedef struct {
    const GrubReader *reader;
    const GrubChunk *chunk;
} GrubView;
_Static_assert(sizeof(GrubChunk) == 24 && sizeof(GrubReader) == 0x38 && sizeof(GrubView) == 16,
               "GrubReader layout");
typedef void (*MaterialLoad)(void *, void *, const GrubView *, void *);
enum { GRUB_MATERIAL = 0x4d617449, GRUB_MODEL = 0x4d6f646c, GRUB_NAME = 0x4e616d65,
       GRUB_BOUNDS = 0x42426f78, GRUB_PROPERTIES = 0x4d545052 };
enum { LIGHT_TYPE_NONE = 70, LIGHT_TYPE_RIGHT = 20, LIGHT_TYPE_LEFT = 21 };
static MaterialLoad material_load;
static volatile LONG material_messages;

/* The reader's data offset and total size, checked against its chunk count. */
static int grub_header(const GrubReader *reader, uint32_t *data_offset, uint32_t *total) {
    unsigned char head[20];
    uint32_t count;
    if (!memory_read(reader->header, head, sizeof(head)) || !memory_equal(head, "burG\1\1\0\0", 8))
        return 0;
    memory_copy(data_offset, head + 8, 4);
    memory_copy(total, head + 12, 4);
    memory_copy(&count, head + 16, 4);
    return reader->count && reader->count <= 8192 && reader->count == count &&
           *data_offset >= 20 + reader->count * sizeof(GrubChunk) && *data_offset <= 16u << 20 &&
           *total > *data_offset && *total <= 64u << 20;
}

/* Copies a chunk's metadata entry `tag` (at most `capacity` bytes); its size, 0 when missing or
   malformed. */
static size_t grub_metadata(const GrubReader *reader, const GrubChunk *chunk, uint32_t tag,
                            void *output, size_t capacity) {
    uint32_t offset, total;
    if (!grub_header(reader, &offset, &total) || !chunk->metadata_count ||
        chunk->metadata_count > 64 || chunk->metadata > offset ||
        (size_t)chunk->metadata_count * 8 > offset - chunk->metadata)
        return 0;
    for (unsigned i = 0; i < chunk->metadata_count; ++i) {
        size_t at = chunk->metadata + i * 8;
        unsigned char entry[8];
        uint32_t entry_tag;
        uint16_t packed, relative;
        if (!memory_read(reader->header + at, entry, sizeof(entry))) return 0;
        memory_copy(&entry_tag, entry, 4);
        if (entry_tag != tag) continue;
        memory_copy(&packed, entry + 4, 2);
        memory_copy(&relative, entry + 6, 2);
        size_t size = packed >> 4;
        if (relative > offset - at || size > offset - at - relative || size > capacity) return 0;
        return memory_read(reader->header + at + relative, output, size) ? size : 0;
    }
    return 0;
}

/* The chunk's name in lower case; 0 when missing or not printable. */
static size_t grub_name(const GrubReader *reader, const GrubChunk *chunk, char name[513]) {
    size_t length = grub_metadata(reader, chunk, GRUB_NAME, name, 512);
    if (length && !name[length - 1]) --length;
    for (size_t i = 0; i < length; ++i) {
        if (name[i] < ' ' || name[i] > '~') return 0;
        if (name[i] >= 'A' && name[i] <= 'Z') name[i] += 32;
    }
    name[length] = 0;
    return length;
}

static int contains_any(const char *text, size_t length, const char *const *needles, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        if (text_find(text, length, needles[i])) return 1;
    return 0;
}

static unsigned material_side(const GrubReader *reader, const char *name, size_t length) {
    static const char *const left[] = {"lightl", "lensl", "signall", "indicatorl", "mirrorl", "left",
                                       "_lf_",   "_lr_",  "_fl_",    "_rl_",       "_l_"};
    static const char *const right[] = {"lightr", "lensr", "signalr", "indicatorr", "mirrorr",
                                        "right",  "_rf_",  "_rr_",    "_fr_",       "_r_"};
    int is_left = contains_any(name, length, left, ARRAY_COUNT(left));
    int is_right = contains_any(name, length, right, ARRAY_COUNT(right));
    if (is_left != is_right) return is_left ? LIGHT_TYPE_LEFT : LIGHT_TYPE_RIGHT;
    /* The first model's bounds (min xyz, max xyz): one side of the car only. */
    for (uint32_t i = 0; i < reader->count; ++i) {
        GrubChunk chunk;
        float bounds[6];
        if (!memory_read(reader->chunks + i, &chunk, sizeof(chunk))) return 0;
        if (chunk.tag != GRUB_MODEL) continue;
        if (grub_metadata(reader, &chunk, GRUB_BOUNDS, bounds, sizeof(bounds)) != sizeof(bounds))
            return 0;
        for (int j = 0; j < 6; ++j)
            if (!float_is_finite(bounds[j])) return 0;
        for (int j = 0; j < 3; ++j)
            if (bounds[j] > bounds[j + 3]) return 0;
        return bounds[3] < -0.05f ? LIGHT_TYPE_LEFT : bounds[0] > 0.05f ? LIGHT_TYPE_RIGHT : 0;
    }
    return 0;
}

/* A heap copy of the material chunk with its unset LightType filled in, or NULL. */
static unsigned char *repair_material(const GrubView *view, GrubReader *reader, GrubChunk *chunk,
                                      char name[513], unsigned *side) {
    GrubView shown;
    uint32_t offset, total;
    if (!memory_read(view, &shown, sizeof(shown)) || !memory_read(shown.reader, reader, sizeof(*reader)) ||
        !memory_read(shown.chunk, chunk, sizeof(*chunk)) || chunk->tag != GRUB_MATERIAL ||
        !grub_header(reader, &offset, &total) || !reader->data || chunk->size != chunk->unpacked ||
        chunk->offset < offset || chunk->offset > total || chunk->size > total - chunk->offset ||
        chunk->size < 20 || chunk->size > 1u << 20)
        return NULL;
    size_t name_length = grub_name(reader, chunk, name);
    static const char *const signals[] = {"turn", "indicator", "blinker"};
    if (!contains_any(name, name_length, signals, ARRAY_COUNT(signals)) ||
        !(*side = material_side(reader, name, name_length)))
        return NULL;
    size_t length = chunk->size;
    unsigned char *copy = HeapAlloc(GetProcessHeap(), 0, length);
    if (!copy) return NULL;
    uint32_t count, start, size;
    int changed = 0;
    if (!memory_read(reader->data + (chunk->offset - offset), copy, length)) goto failed;
    memory_copy(&start, copy + 8, 4);
    memory_copy(&size, copy + 12, 4);
    memory_copy(&count, copy + 16, 4);
    if (!memory_equal(copy, "burG\1\1\0\0", 8) || !count || count > 64 ||
        start < 20 + count * sizeof(GrubChunk) || start > length || size != length)
        goto failed;
    static const unsigned char light_type[] = {3, 0, 0xa0, 0x73, 0x30, 0x49, 4};
    for (uint32_t i = 0; i < count; ++i) {
        GrubChunk part;
        memory_copy(&part, copy + 20 + i * sizeof(GrubChunk), sizeof(part));
        if (part.offset < start || part.offset > length || part.size > length - part.offset ||
            part.size != part.unpacked)
            goto failed;
        if (part.tag != GRUB_PROPERTIES) continue;
        unsigned char *properties = copy + part.offset;
        for (size_t j = 0; j + 27 <= part.size; ++j) {
            uint32_t value;
            if (!memory_equal(properties + j, light_type, sizeof(light_type))) continue;
            memory_copy(&value, properties + j + 23, 4);
            if (value != LIGHT_TYPE_NONE) continue;
            memory_copy(properties + j + 23, side, 4);
            changed = 1;
        }
    }
    if (changed) {
        reader->data = copy; /* the copy is the whole chunk: it starts at the data offset */
        chunk->offset = offset;
        return copy;
    }
failed:
    HeapFree(GetProcessHeap(), 0, copy);
    return NULL;
}

static void indicator_material_load(void *self, void *owner, const GrubView *view, void *context) {
    GrubReader reader;
    GrubChunk chunk;
    char name[513];
    unsigned side = 0;
    unsigned char *copy = view ? repair_material(view, &reader, &chunk, name, &side) : NULL;
    if (!copy) {
        material_load(self, owner, view, context);
        return;
    }
    GrubView replacement = {&reader, &chunk};
    material_load(self, owner, &replacement, context);
    HeapFree(GetProcessHeap(), 0, copy);
    if (InterlockedIncrement(&material_messages) <= 32)
        anna_log("LIGHTS: indicator material %s lights as the %s indicator (in memory).", name,
                 side == LIGHT_TYPE_LEFT ? "left" : "right");
}

/* ---- ANNA entries ------------------------------------------------------------------------------- */
static const struct {
    const char *key, *parent, *label, *close_label;
    int part;
} items[] = {
    {"car.lights", FH3ANNA_LIST_CAR, "Lights", NULL, -1},
    {"car.lights.headlights", FH3ANNA_LIST_CAR_LIGHTS, "Headlights On", "Headlights Off", LIGHT_HEAD},
    {"car.lights.hazards", FH3ANNA_LIST_CAR_LIGHTS, "Hazard Lights On", "Hazard Lights Off", LIGHT_HAZARD},
    {"car.lights.left", FH3ANNA_LIST_CAR_LIGHTS, "Left Indicator On", "Left Indicator Off", LIGHT_LEFT},
    {"car.lights.right", FH3ANNA_LIST_CAR_LIGHTS, "Right Indicator On", "Right Indicator Off", LIGHT_RIGHT},
    {"car.doors", FH3ANNA_LIST_CAR, "Doors", NULL, -1},
    {"car.doors.all", FH3ANNA_LIST_CAR_DOORS, "Open All", "Close All", PART_ALL},
    {"car.doors.roof", FH3ANNA_LIST_CAR_DOORS, "Open Roof", "Close Roof", PART_ROOF},
    {"car.doors.hood", FH3ANNA_LIST_CAR_DOORS, "Open Hood", "Close Hood", PART_HOOD},
    {"car.doors.trunk", FH3ANNA_LIST_CAR_DOORS, "Open Trunk", "Close Trunk", PART_TRUNK},
    {"car.doors.lf", FH3ANNA_LIST_CAR_DOORS, "Open Front Left Door", "Close Front Left Door", PART_DOOR_LF},
    {"car.doors.rf", FH3ANNA_LIST_CAR_DOORS, "Open Front Right Door", "Close Front Right Door", PART_DOOR_RF},
    {"car.doors.lr", FH3ANNA_LIST_CAR_DOORS, "Open Rear Left Door", "Close Rear Left Door", PART_DOOR_LR},
    {"car.doors.rr", FH3ANNA_LIST_CAR_DOORS, "Open Rear Right Door", "Close Rear Right Door", PART_DOOR_RR},
};

static int item_shown(void *user, const char *key) {
    (void)key;
    return part_supported(player_car(), (int)(intptr_t)user);
}

static const char *item_label(void *user, const char *key) {
    (void)user;
    for (unsigned i = 0; i < ARRAY_COUNT(items); ++i)
        if (!lstrcmpA(items[i].key, key)) {
            const char *english = items[i].close_label && part_is_open(player_car(), items[i].part)
                                      ? items[i].close_label
                                      : items[i].label;
            return text_get(english);
        }
    return key;
}

static void item_choose(void *user, const char *key) {
    (void)key;
    toggle_part((int)(intptr_t)user);
}

void car_menu_entries(void) {
    for (unsigned i = 0; i < ARRAY_COUNT(items); ++i) {
        FH3AnnaEntry entry = {sizeof(entry)};
        entry.key = items[i].key;
        entry.parent = items[i].parent;
        entry.label = items[i].label;
        entry.label_now = item_label;
        if (items[i].part >= 0) {
            entry.on_choose = item_choose;
            entry.shown = item_shown;
            entry.user = (void *)(intptr_t)items[i].part;
            entry.stays_open = 1;
        }
        menu_add_entry(&entry);
    }
}

int car_prepare(int menu, int dpad) {
    if (!menu && !dpad) return 0;
    if (!(light_packet = (void (*)(void *, float))host->hook_call(0x18b8e22, (void *)car_light_packet)) ||
        !(material_load = (MaterialLoad)host->hook_call(0x1eb7c3f, (void *)indicator_material_load))) {
        anna_log("LIGHTS: the game's light code differs; car lights unavailable.");
        return 0;
    }
    if (dpad) {
        static const unsigned radio[] = {0xe040a4, 0xe040d7, 0xe0420c, 0xe0423f, 0x147062f, 0x147067b};
        for (unsigned i = 0; i < ARRAY_COUNT(radio); ++i) {
            ActionPressed original = (ActionPressed)host->hook_call(radio[i], (void *)radio_action_pressed);
            if (original) radio_query = original;
        }
        if (radio_query)
            anna_log("LIGHTS: hold D-pad Left/Right for 1 s for the indicators; a tap changes the radio station.");
    }
    if (menu) {
        car_menu_entries();
        anna_log("CAR PARTS: ANNA Car list with Lights (headlights, hazards, indicators) and Doors "
                 "(roof, hood, trunk, doors).");
    }
    anna_log("LIGHTS: hazard and indicator overrides; indicator materials repaired in memory.");
    return 1;
}

void car_frame(void) {
    parts_tick();
    signals_tick();
}
