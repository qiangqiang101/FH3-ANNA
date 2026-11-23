/* ANNA's menu (D-pad Down). HUDAnnaComponent keeps the available command names in a
 * std::vector<std::string> at +0xb8 and rebuilds its menu (+0xf8) with AddOption 0x10b0da0, which
 * labels each item with the localized "IDS_SpeechItem_<name>" via 0x10b9870. On select,
 * ANNAManualControlComponent reads the highlighted name through 0xaaa710 and hands it to the
 * voice-command dispatcher.
 *
 * Our entries are named "EO_<n>" (n: the entry's index), with a '+' that flips whenever the entry's
 * label changes: ANNA reuses an option whose name it has already labelled. The main list gets them
 * right after the native clear (0x10b2810) in both rebuilds (the list handler 0xab3fb0 and the
 * refresh 0xab7f60), so the native restore of the highlighted entry counts them like any other.
 *
 * A list opens on a borrowed native follow-up list, so ANNA keeps it active and highlighted:
 * selecting it dispatches StartMatchmakingFor, whose follow-up names land in the vector at +0xb8,
 * which is turned into menu options by the list handler or the refresh. Both are wrapped: while
 * armed, the follow-up names are replaced with the list's entries before the native code adds them.
 * The buffer is grown with the game allocator (0x1cd05f0; freed by 0x3680c0) when the list has more
 * entries than it holds. An entry chosen inside a list is an answer to the borrowed command, so ANNA
 * keeps showing the same list: it is rebuilt in place (relabel). Back (D-pad Left, ANNA's action 0xda
 * in input context 0xf, asked at 0xa01ec6 through 0x1a198f0) inside a list of a list goes up one. */
#include "anna.h"

#include <stdio.h>

enum { MAX_ENTRIES = 256, KEY_CHARS = 64, LABEL_CHARS = 128, MAX_SHOWN = 32, MAX_DEPTH = 8 };

typedef struct {
    char key[KEY_CHARS], parent[KEY_CHARS], label[LABEL_CHARS];
    FH3AnnaChooseFn on_choose;
    FH3AnnaShownFn shown;
    FH3AnnaLabelFn label_now;
    void *user;
    int stays_open;
    uint32_t label_hash; /* of the label ANNA was last given; game thread only */
    int variant;         /* the '+' */
} Entry;

typedef struct {
    char text[16];
    size_t size;
} Name;

typedef void (*AddOption)(void *, const MsvcString *);
typedef void **(*MakeLabel)(void *, void **, const MsvcString *);
typedef void **(*CreateUIString)(void **);
typedef MsvcString *(*HighlightedName)(void *, MsvcString *);
typedef unsigned char (*ActionPressed)(void *, unsigned, unsigned, unsigned char);
typedef void (*AnnaListHandler)(void *, const MsvcString *);
typedef void (*AnnaRefresh)(void *, unsigned char);
typedef void *(*Allocate)(size_t);

static Entry entries[MAX_ENTRIES];
static volatile LONG entry_count;
static CRITICAL_SECTION adding;
static int current = -1;     /* the list ANNA shows, else -1 (the main list) */
static volatile LONG pending = -1; /* the entry to run on the next frame */
static volatile LONG close_pending;  /* Backs still to answer for a choice that closes ANNA */
static ULONGLONG close_until;        /* ... within the second after it */
static ULONGLONG follow_up_until;
static void *rebuilding, *hud_component, *control_component;
static void (*clear_menu)(void *);
static MakeLabel make_label;
static HighlightedName highlighted_name;
static ActionPressed back_query;
static AnnaRefresh native_refresh;
static const char carrier[] = "StartMatchmakingFor";

/* In races the game destroys ANNAManualControlComponent (her D-pad and commands) and keeps her HUD
   component. Race mode stands in for the one that's gone: while its update hasn't run for a second
   (it asks for her activate action 0xad every frame, 0xa01bff), the D-pad drives the HUD's menu here.
   Down opens it on our main list (our entries only: her own commands don't apply in a race), Down and
   Up move the highlight (0xab10d0, 0xab1160: as hers do, with their sound), Right chooses, Left goes
   up a list or closes it. Closing is her HUD's Timeout message (0xab8160: the menu's closing event, then
   its next update empties the list); 0xaa5600 alone only clears the open flag and leaves the list drawn.
   The HUD's own rebuilds clear its menu; anna_clear_menu puts our list back. */
static ActionPressed activate_query;
static volatile ULONGLONG control_seen;
static int race_open;
static WORD race_buttons_before;
static ULONGLONG race_idle_since;
/* After the race menu closes the D-pad stays ours (not the radio's) until it's released and a moment
   has passed: the press that closed it would otherwise also change the station. */
static volatile ULONGLONG race_guard_until;

static const char *msvc_text(const MsvcString *value) {
    return value->capacity >= 16 ? value->pointer : value->text;
}

static int find(const char *key) {
    LONG count = entry_count;
    for (LONG i = 0; i < count; ++i)
        if (!lstrcmpA(entries[i].key, key)) return (int)i;
    return -1;
}

static int valid_key(const char *key) {
    size_t length = 0;
    for (; key[length]; ++length) {
        char c = key[length];
        if (length + 1 >= KEY_CHARS ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
              c == '_' || c == '-'))
            return 0;
    }
    return length != 0;
}

static void copy_text(char *out, const char *text, size_t capacity) {
    size_t i = 0;
    for (; text && text[i] && i + 1 < capacity; ++i) out[i] = text[i];
    out[i] = 0;
}

int menu_add_entry(const FH3AnnaEntry *entry) {
    if (!entry || entry->size < sizeof(FH3AnnaEntry) || !entry->key || !valid_key(entry->key) ||
        !entry->label || (entry->parent && !valid_key(entry->parent)))
        return 0;
    EnterCriticalSection(&adding);
    int added = 0;
    if (find(entry->key) < 0 && entry_count < MAX_ENTRIES) {
        Entry *own = &entries[entry_count];
        copy_text(own->key, entry->key, KEY_CHARS);
        copy_text(own->parent, entry->parent, KEY_CHARS);
        copy_text(own->label, entry->label, LABEL_CHARS);
        own->on_choose = entry->on_choose;
        own->shown = entry->shown;
        own->label_now = entry->label_now;
        own->user = entry->user;
        own->stays_open = entry->stays_open;
        own->label_hash = 0;
        own->variant = 0;
        InterlockedIncrement(&entry_count); /* published once complete */
        added = 1;
    }
    LeaveCriticalSection(&adding);
    return added;
}

static int is_list(const Entry *entry) { return !entry->on_choose; }

static int parent_is(const Entry *entry, int list) {
    return list < 0 ? !entry->parent[0] : !lstrcmpA(entry->parent, entries[list].key);
}

static int visible(int index, int depth) {
    Entry *entry = &entries[index];
    if (entry->shown && !entry->shown(entry->user, entry->key)) return 0;
    if (!is_list(entry)) return 1;
    if (depth >= MAX_DEPTH) return 0;
    LONG count = entry_count;
    for (LONG i = 0; i < count; ++i)
        if ((int)i != index && parent_is(&entries[i], index) && visible((int)i, depth + 1)) return 1;
    return 0;
}

static const char *label_of(Entry *entry) {
    const char *text = entry->label_now ? entry->label_now(entry->user, entry->key) : NULL;
    return text && text[0] ? text : entry->label;
}

static uint32_t hash_text(const char *text) {
    uint32_t hash = 2166136261u;
    for (; *text; ++text) hash = (hash ^ (unsigned char)*text) * 16777619u;
    return hash | 1; /* never 0: 0 means not labelled yet */
}

static Name name_of(int index) {
    Entry *entry = &entries[index];
    uint32_t hash = hash_text(label_of(entry));
    if (hash != entry->label_hash) {
        entry->label_hash = hash;
        entry->variant ^= 1;
    }
    Name name = {"EO_", 3};
    char digits[4];
    unsigned count = 0, value = (unsigned)index;
    do digits[count++] = (char)('0' + value % 10); while ((value /= 10) && count < sizeof(digits));
    while (count) name.text[name.size++] = digits[--count];
    if (entry->variant) name.text[name.size++] = '+';
    name.text[name.size] = 0;
    return name;
}

/* "EO_<n>" or "EO_<n>+" -> n; -1 for any other name. */
static int entry_of(const MsvcString *name) {
    const char *text = msvc_text(name);
    size_t size = name->size;
    if (size < 4 || !memory_equal(text, "EO_", 3)) return -1;
    if (text[size - 1] == '+') --size;
    int index = 0;
    for (size_t i = 3; i < size; ++i) {
        if (text[i] < '0' || text[i] > '9' || i > 6) return -1;
        index = index * 10 + (text[i] - '0');
    }
    return index < entry_count ? index : -1;
}

/* The names of `list`'s entries shown now (list -1: the main list's). */
static unsigned list_names(int list, Name *names, unsigned max) {
    unsigned count = 0;
    LONG total = entry_count;
    for (LONG i = 0; i < total && count < max; ++i)
        if (parent_is(&entries[i], list) && visible((int)i, 0)) names[count++] = name_of((int)i);
    return count;
}

/* The main list (and only it) carries these native commands. */
static int top_level(MsvcString *begin, MsvcString *end) {
    static const char *const markers[] = {"WhatShouldIDo", "RouteToNearest"};
    for (MsvcString *name = begin; name < end; ++name) {
        const char *text = msvc_text(name);
        for (unsigned i = 0; i < ARRAY_COUNT(markers); ++i) {
            size_t length = text_length(markers[i]);
            if (name->size == length && memory_equal(text, markers[i], length)) return 1;
        }
    }
    return 0;
}

static void add_option(void *menu, const Name *name) {
    MsvcString option = {{{0}}, name->size, 15};
    memory_copy(option.text, name->text, name->size + 1);
    ((AddOption)(image + 0x10b0da0))(menu, &option);
}

/* After the native clear in either rebuild: the main list gets our main-list entries first. */
static void race_restore(void *menu);

static void anna_clear_menu(void *menu) {
    clear_menu(menu);
    if (race_open) {
        race_restore(menu);
        return;
    }
    unsigned char *component = rebuilding;
    MsvcString *begin, *end;
    void *own_menu;
    if (!component || !memory_read_pointer(component + 0xf8, &own_menu) || own_menu != menu ||
        !memory_read_pointer(component + 0xb8, (void **)&begin) ||
        !memory_read_pointer(component + 0xc0, (void **)&end) || begin >= end)
        return;
    /* Any list but one of ours (EO_ names) leaves our lists. */
    if (begin->size < 3 || !memory_equal(msvc_text(begin), "EO_", 3)) current = -1;
    if (!top_level(begin, end)) return;
    static Name names[MAX_SHOWN];
    unsigned count = list_names(-1, names, MAX_SHOWN);
    for (unsigned i = 0; i < count; ++i) add_option(menu, &names[i]);
}

/* UIString text lives at +0x28 (length +0x30). Without a string table the base destructor frees it
   with the UI allocator, sized (length + 1) * 2; the UI allocator's alloc hook is 0x1edbd00. */
/* A UI string (the game's, reference counted at +0x10) holding text. */
static void ui_text(void **output, const char *utf8) {
    ((CreateUIString)(image + 0x1214480))(output);
    unsigned char *label = *output;
    if (!label) return;
    wchar_t text[2 * LABEL_CHARS];
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, text, 2 * LABEL_CHARS);
    if (length <= 0) length = 1, text[0] = 0;
    wchar_t *copy = ((Allocate)(image + 0x1edbd00))((size_t)length * sizeof(wchar_t));
    if (copy) {
        memory_copy(copy, text, (size_t)length * sizeof(wchar_t));
        *(wchar_t **)(label + 0x28) = copy;
        *(unsigned *)(label + 0x30) = (unsigned)length - 1;
    }
}

static void **anna_make_label(void *self, void **output, const MsvcString *name) {
    int index = entry_of(name);
    if (index < 0) return make_label(self, output, name);
    ui_text(output, label_of(&entries[index]));
    return output;
}

/* Rebuilds the shown list in place. AddOption keeps the highlight at the menu's restore index
   (+0x21c) while its restore flag (+0x21a) is set; each option's highlight flag is at +0x40 of the
   items in [[menu+0x210]+0x50]+0x28 -> +0x28. A new list (first) highlights its first entry. */
static void relabel(void *component, int first) {
    unsigned char *menu, *view, *collection, *list, **items;
    if (!component || !memory_read_pointer((unsigned char *)component + 0xf8, (void **)&menu) || !menu ||
        !memory_read_pointer(menu + 0x210, (void **)&view) || !view ||
        !memory_read_pointer(view + 0x50, (void **)&collection) || !collection ||
        !memory_read_pointer(collection + 0x28, (void **)&list) || !list ||
        !memory_read_pointer(list + 0x28, (void **)&items))
        return;
    static Name names[MAX_SHOWN];
    int highlighted = 0, count = (int)list_names(current, names, MAX_SHOWN);
    for (int i = 0; !first && items && i < count; ++i) {
        unsigned char *item, selected;
        if (memory_read_pointer(items + i, (void **)&item) && item && memory_read(item + 0x40, &selected, 1) &&
            selected)
            highlighted = i;
    }
    ((void (*)(void *))(image + 0x10b2810))(menu);
    *(int *)(menu + 0x21c) = highlighted;
    menu[0x21a] = 1;
    for (int k = 0; k < count; ++k) add_option(menu, &names[k]);
}

static void assign_carrier(MsvcString *output) {
    MsvcString text = {{{0}}, sizeof(carrier) - 1, sizeof(carrier) - 1};
    text.pointer = (char *)carrier;
    ((void (*)(MsvcString *, const MsvcString *, size_t, size_t))(image + 0x245070))(output, &text, 0,
                                                                                      (size_t)-1);
}

static MsvcString *anna_selected_name(void *component, MsvcString *output) {
    highlighted_name(component, output);
    control_component = component;
    int index = entry_of(output);
    if (index < 0) return output;
    Entry *entry = &entries[index];
    if (is_list(entry)) {
        if (!entry->parent[0]) {
            /* From the main list: the follow-up list ANNA opens is replaced with this list. */
            current = index;
            follow_up_until = GetTickCount64() + 2000;
        } else {
            current = index;
            relabel(component, 1);
        }
        assign_carrier(output);
    } else if (entry->stays_open) {
        entry->on_choose(entry->user, entry->key);
        relabel(component, 0);
        assign_carrier(output);
    } else {
        InterlockedExchange(&pending, index);
        /* Back inside a list goes to the main list; Back there closes her. */
        close_until = GetTickCount64() + 1000;
        InterlockedExchange(&close_pending, entry->parent[0] ? 2 : 1);
        current = -1;
        /* An empty name dispatches nothing natively; the next Back query says Back, which closes her. */
        ((char *)msvc_text(output))[0] = 0;
        output->size = 0;
    }
    return output;
}

static unsigned char anna_back_pressed(void *input, unsigned context, unsigned action, unsigned char consume) {
    unsigned char pressed = back_query(input, context, action, consume);
    /* A choice that closes ANNA: answered as Back, so she closes the way Back does (0xa01f0f: the HUD
       component's close, and the voice system told). Only asked while her menu is up. */
    if (close_pending > 0 && GetTickCount64() > close_until) InterlockedExchange(&close_pending, 0);
    if (close_pending > 0) {
        InterlockedDecrement(&close_pending);
        current = -1;
        return 1;
    }
    if (!pressed || current < 0 || !entries[current].parent[0] || !control_component) return pressed;
    int parent = find(entries[current].parent);
    if (parent < 0) return pressed;
    current = parent;
    relabel(control_component, 1);
    return 0;
}

static void free_game_string(MsvcString *value) {
    if (value->capacity > 15 && value->pointer) ((void (*)(void *))(image + 0x3680c0))(value->pointer);
}

static void replace_follow_up(void *component) {
    unsigned char *names = (unsigned char *)component + 0xb8;
    MsvcString *begin, *end, *capacity;
    if (!follow_up_until || !memory_read_pointer(names, (void **)&begin) ||
        !memory_read_pointer(names + 8, (void **)&end) || !memory_read_pointer(names + 16, (void **)&capacity) ||
        !begin || begin == end || top_level(begin, end))
        return;
    int armed = GetTickCount64() < follow_up_until;
    follow_up_until = 0;
    static Name list[MAX_SHOWN];
    unsigned count = armed && current >= 0 ? list_names(current, list, MAX_SHOWN) : 0;
    if (!count) return; /* the native follow-up stays */
    for (MsvcString *name = begin; name < end; ++name) free_game_string(name);
    if ((size_t)(capacity - begin) < count) {
        void *heap = ((void *(*)(void))(image + 0x1cd25b0))();
        MsvcString *grown = heap ? ((MsvcString * (*)(void *, size_t, unsigned, unsigned))(image + 0x1cd05f0))(
                                       heap, count * sizeof(MsvcString), 0, 0)
                                 : NULL;
        if (!grown) {
            *(MsvcString **)(names + 8) = begin; /* nothing left to show */
            return;
        }
        ((void (*)(void *))(image + 0x3680c0))(begin);
        begin = grown;
        *(MsvcString **)names = begin;
        *(MsvcString **)(names + 16) = begin + count;
    }
    for (unsigned k = 0; k < count; ++k) {
        begin[k] = (MsvcString){{{0}}, list[k].size, 15};
        memory_copy(begin[k].text, list[k].text, list[k].size + 1);
    }
    *(MsvcString **)(names + 8) = begin + count;
}

static void anna_list_message(void *component, const MsvcString *title) {
    hud_component = component;
    replace_follow_up(component);
    rebuilding = component;
    ((AnnaListHandler)(image + 0xab3fb0))(component, title);
    rebuilding = NULL;
}

static void anna_refresh(void *component, unsigned char flag) {
    hud_component = component;
    replace_follow_up(component);
    rebuilding = component;
    native_refresh(component, flag);
    rebuilding = NULL;
}

/* ---- Race mode ----------------------------------------------------------------------------------- */
static void *game_input;

static unsigned char anna_activate_query(void *input, unsigned context, unsigned action, unsigned char consume) {
    control_seen = GetTickCount64();
    game_input = input;
    return activate_query(input, context, action, consume);
}

/* Whether the game's input currently allows gameplay (scope 0), which is what its own action query
   (0x1a198f0) tests first. The pause menu pushes its own permission record to the front of the input's
   deque (at +0x38), so while it is open scope 0 is refused. The pause menu also stops the HUD
   from asking, so without this it would look like race mode. If anything can't be read, say yes. */
static int gameplay_input(void) {
    unsigned char *input = game_input;
    void *first, *queue;
    unsigned char *table;
    unsigned long long cap, start, mask_word;
    unsigned char *block;
    unsigned mask;
    if (!input || !memory_read_pointer(input + 0x38, &first) || !memory_read(input + 0x38 + 0x18, &start, 8))
        return 1;
    queue = first;
    if (queue && !memory_read_pointer(queue, &queue)) return 1;
    if (!queue || !memory_read_pointer((unsigned char *)queue + 8, (void **)&table) ||
        !memory_read((unsigned char *)queue + 0x10, &cap, 8) || !cap || !table)
        return 1;
    if (!memory_read_pointer(table + ((start >> 1) & (cap - 1)) * 8, (void **)&block) || !block) return 1;
    if (!memory_read(block + (start & 1) * 8 + 4, &mask_word, 4)) return 1;
    mask = (unsigned)(mask_word & 0xffffffffu);
    return (mask & 1) != 0;
}

static int race_mode(void) {
    return hud_component && control_seen && GetTickCount64() - control_seen > 1000 && driven_car();
}

static unsigned char *race_menu(void) {
    unsigned char *menu;
    return hud_component && memory_read_pointer((unsigned char *)hud_component + 0xf8, (void **)&menu) ? menu
                                                                                                          : NULL;
}

static void race_restore(void *menu) {
    static Name names[MAX_SHOWN];
    unsigned count = list_names(current, names, MAX_SHOWN);
    for (unsigned i = 0; i < count; ++i) add_option(menu, &names[i]);
}

/* The highlighted entry: the menu items' highlight flag (+0x40), in the order the list was added. */
static int race_highlighted(void) {
    unsigned char *menu = race_menu(), *view, *collection, *list, **items;
    if (!menu || !memory_read_pointer(menu + 0x210, (void **)&view) ||
        !memory_read_pointer(view + 0x50, (void **)&collection) ||
        !memory_read_pointer(collection + 0x28, (void **)&list) || !memory_read_pointer(list + 0x28, (void **)&items))
        return -1;
    static Name names[MAX_SHOWN];
    int count = (int)list_names(current, names, MAX_SHOWN);
    for (int i = 0; i < count; ++i) {
        unsigned char *item, selected;
        if (memory_read_pointer(items + i, (void **)&item) && memory_read(item + 0x40, &selected, 1) && selected) {
            MsvcString name = {{{0}}, names[i].size, 15};
            memory_copy(name.text, names[i].text, names[i].size + 1);
            return entry_of(&name);
        }
    }
    return -1;
}

static void race_close(void) {
    race_open = 0;
    race_guard_until = GetTickCount64() + 250;
    current = -1;
    if (hud_component) ((void (*)(void *))(image + 0xab8160))(hud_component);
}

static void race_show(int first) {
    if (!race_menu()) return;
    static Name names[MAX_SHOWN];
    if (!list_names(current, names, MAX_SHOWN)) {
        race_close();
        return;
    }
    relabel(hud_component, first);
}

int menu_race_open(void) { return race_open || GetTickCount64() < race_guard_until; }

/* Where the game has ANNA unavailable her button shows a crossed microphone; while race mode answers
   D-pad Down it shows that instead (Extra Options 3+). */
static void race_ready(int ready) {
    static int shown = -1;
    if (ready == shown || host->api_version < 3 ||
        host->size < offsetof(FH3PluginContext, set_anna_ready) + sizeof(void *) || !host->set_anna_ready)
        return;
    shown = ready;
    host->set_anna_ready(ready);
}

static void race_frame(void) {
    WORD buttons = 0;
    for (DWORD pad = 0; pad < 4; ++pad) {
        PadState state;
        if (pad_state(pad, &state)) buttons |= state.buttons;
    }
    WORD pressed = buttons & (WORD)~race_buttons_before;
    race_buttons_before = buttons;
    if (!race_open && (buttons & (PAD_DPAD_UP | PAD_DPAD_DOWN | PAD_DPAD_LEFT | PAD_DPAD_RIGHT)) &&
        GetTickCount64() < race_guard_until)
        race_guard_until = GetTickCount64() + 250;
    int racing = race_mode();
    race_ready(racing);
    if (racing && !gameplay_input()) racing = 0; /* the pause menu is open */
    if (!racing) {
        if (race_open) race_close();
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (!race_open) {
        if (!(pressed & PAD_DPAD_DOWN)) return;
        static Name names[MAX_SHOWN];
        current = -1;
        if (!list_names(-1, names, MAX_SHOWN)) return;
        race_open = 1;
        race_idle_since = now;
        race_show(1);
        anna_log("ANNA: race menu opened.");
        return;
    }
    if (pressed) race_idle_since = now;
    else if (now - race_idle_since > 10000) {
        race_close();
        return;
    }
    if (pressed & PAD_DPAD_DOWN) ((void (*)(void *))(image + 0xab10d0))(hud_component);
    if (pressed & PAD_DPAD_UP) ((void (*)(void *))(image + 0xab1160))(hud_component);
    if (pressed & PAD_DPAD_LEFT) {
        int parent = current >= 0 && entries[current].parent[0] ? find(entries[current].parent) : -1;
        if (current < 0) race_close();
        else {
            current = parent;
            race_show(1);
        }
    }
    if (pressed & PAD_DPAD_RIGHT) {
        int index = race_highlighted();
        if (index < 0) return;
        Entry *entry = &entries[index];
        if (is_list(entry)) {
            current = index;
            race_show(1);
        } else if (entry->stays_open) {
            entry->on_choose(entry->user, entry->key);
            race_show(0);
        } else {
            race_close();
            entry->on_choose(entry->user, entry->key);
        }
    }
}

int menu_is_open(void) {
    unsigned char *menu, open;
    return hud_component && memory_read_pointer((unsigned char *)hud_component + 0xf8, (void **)&menu) &&
           memory_read(menu + 0x219, &open, 1) && open;
}

void menu_relabel(void) {
    if (current >= 0 && control_component && menu_is_open()) relabel(control_component, 0);
}

void menu_frame(void) {
    race_frame();
    LONG index = InterlockedExchange(&pending, -1);
    if (index >= 0 && index < entry_count && entries[index].on_choose)
        entries[index].on_choose(entries[index].user, entries[index].key);
}

int menu_prepare(void) {
    InitializeCriticalSection(&adding);
    /* The list handler is registered by address (lea rax, [rip + handler] at 0xa9c1e7): it is pointed at
       an absolute jump to ours in the int3 padding at 0xaa0101. */
    static const unsigned char padding[12] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                                              0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
    unsigned char jump[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t handler = (uintptr_t)anna_list_message;
    memory_copy(jump + 2, &handler, sizeof(handler));
    unsigned char lea[7] = {0x48, 0x8d, 0x05}, lea_ours[7] = {0x48, 0x8d, 0x05};
    int32_t native = (int32_t)(0xab3fb0 - (0xa9c1e7 + 7)), ours = (int32_t)(0xaa0101 - (0xa9c1e7 + 7));
    memory_copy(lea + 3, &native, sizeof(native));
    memory_copy(lea_ours + 3, &ours, sizeof(ours));
    if (!memory_equal(image + 0xaa0101, padding, sizeof(padding)) || !memory_equal(image + 0xa9c1e7, lea, 7))
        return 0;
    void *first_clear = host->hook_call(0xab3ff8, (void *)anna_clear_menu);
    void *second_clear = host->hook_call(0xab7f94, (void *)anna_clear_menu);
    clear_menu = (void (*)(void *))first_clear;
    make_label = (MakeLabel)host->hook_call(0x10b0e39, (void *)anna_make_label);
    highlighted_name = (HighlightedName)host->hook_call(0xa01e2d, (void *)anna_selected_name);
    back_query = (ActionPressed)host->hook_call(0xa01ec6, (void *)anna_back_pressed);
    native_refresh = (AnnaRefresh)host->hook_call(0xab2a72, (void *)anna_refresh);
    activate_query = (ActionPressed)host->hook_call(0xa01bff, (void *)anna_activate_query);
    if (!first_clear || !second_clear || !make_label || !highlighted_name || !back_query || !native_refresh ||
        !activate_query ||
        !host->patch_bytes(0xaa0101, padding, jump, sizeof(jump)) ||
        !host->patch_bytes(0xa9c1e7, lea, lea_ours, sizeof(lea_ours)))
        return 0;
    return 1;
}
