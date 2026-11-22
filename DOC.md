# Writing mods for ANNA

ANNA is Forza Horizon 3's in-car assistant: D-pad Down while driving opens her list. FH3 ANNA (this repo)
turns that list into a menu other mods can add to. Your mod gets its own entries, lists inside lists, and
labels that change, all opened with the D-pad like the game's own commands. It also works in races, where
the game normally switches ANNA off.

This page covers everything a mod needs. For a quick start, copy one of the two examples:

| Your mod is | Start from |
|---|---|
| An [Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions) plugin (C or C++) | [`examples/extra-options-plugin`](examples/extra-options-plugin/anna_example.c) |
| An [FH3 Mod Platform](https://github.com/Levilente/FH3_ModPlatform) mod (C++) | [`examples/mod-platform`](examples/mod-platform/anna_example_mod.cpp) |
| Any other DLL in the game | Either one: only `fh3anna.h` matters |

[Groove Music Radio](https://github.com/qiangqiang101/FH3-GrooveMusic) is a complete, real mod built on the same API.

## Contents

- [How it fits together](#how-it-fits-together)
- [Getting ANNA's API](#getting-annas-api)
- [Adding entries](#adding-entries)
- [Lists](#lists)
- [Callbacks](#callbacks)
- [Threads and lifetime](#threads-and-lifetime)
- [Labels and languages](#labels-and-languages)
- [In races and during Auto Drive](#in-races-and-during-auto-drive)
- [Limits](#limits)
- [The examples](#the-examples)
- [Troubleshooting](#troubleshooting)
- [API reference](#api-reference)

## How it fits together

- ANNA is an Extra Options plugin: `mods\Forza Horizon 3\ANNA\plugins\ANNA.dll`, next to `azimuth.exe`.
- `ANNA.dll` exports one function, `FH3ANNA_GetApi`. [`include/fh3anna.h`](include/fh3anna.h) finds it for you.
  The header is plain C and works from C and C++, so copy it into your mod.
- Your mod doesn't link against ANNA. If ANNA isn't installed, the API is simply `NULL`, and your mod carries on
  without her.

## Getting ANNA's API

```c
#include "fh3anna.h"

const FH3AnnaApi* anna = fh3anna_get(1);   /* 1: the API version your mod needs */
if (anna) {
    /* add your entries */
}
```

`fh3anna_get` returns `NULL` until `ANNA.dll` has loaded, so what you call depends on when your mod starts:

| Your mod | When it starts | Call |
|---|---|---|
| Extra Options plugin in a mod folder named after `ANNA` (e.g. `ANNAExample`) | after ANNA | `fh3anna_get(1)` in `FH3PluginInitialize` |
| Extra Options plugin that may load first (named before `ANNA`, or ordered first in `loadorder.txt`) | maybe before ANNA | `fh3anna_get(1)`, and if that's `NULL`, `fh3anna_wait` on a thread |
| FH3 Mod Platform mod (`fh3-mods`) | before Extra Options plugins | `fh3anna_wait(1, 120000)` on a thread |

`fh3anna_wait(version, timeout_ms)` retries every 250 ms until ANNA is there or the timeout passes. It **blocks**,
so call it from a thread of your own, never from `FH3PluginInitialize`, `fh3mod_init` or `DllMain`:

```c
static DWORD WINAPI wait_for_anna(LPVOID unused) {
    const FH3AnnaApi* anna = fh3anna_wait(1, 60000);
    if (anna) add_my_entries(anna);
    return 0;
}

/* in your start-up function: */
const FH3AnnaApi* anna = fh3anna_get(1);
if (anna) add_my_entries(anna);
else CloseHandle(CreateThread(NULL, 0, wait_for_anna, NULL, 0, NULL));
```

The Extra Options example does exactly this, so it works in either load order.

## Adding entries

```c
static void honk(void* user, const char* key) { /* runs on the game's thread */ }

FH3AnnaEntry e = {sizeof e};   /* C++: FH3AnnaEntry e{}; e.size = sizeof e; */
e.key       = "mymod.honk";    /* unique across all mods */
e.parent    = "mymod";         /* a list's key, or NULL for ANNA's main list */
e.label     = "Honk";
e.on_choose = honk;
if (!anna->add_entry(&e)) { /* key taken, or entry incomplete */ }
```

- `size` must be `sizeof(FH3AnnaEntry)`.
- `key` must be unique across **every** mod. Start it with your mod's name (`mymod.honk`). Letters, digits, `.`,
  `_` and `-` only, at most 63 characters.
- `label` is required, even when you also give `label_now` (it's the fallback).
- `add_entry` copies the entry and its strings, so your `FH3AnnaEntry` can be a local variable.
- Add entries any time, from any thread. They appear the next time their list opens.
- Entries can't be removed. To take one away, hide it with `shown` (see [Callbacks](#callbacks)).

## Lists

An entry **without** `on_choose` is a list. Its entries are the ones whose `parent` is its key:

```c
FH3AnnaEntry list = {sizeof list};
list.key   = "mymod";
list.label = "My Mod";          /* appears in ANNA's main list */
anna->add_entry(&list);

FH3AnnaEntry item = {sizeof item};
item.key       = "mymod.honk";
item.parent    = "mymod";       /* inside "My Mod" */
item.label     = "Honk";
item.on_choose = honk;
anna->add_entry(&item);
```

- Choosing a list (D-pad Right) opens it inside ANNA. D-pad Left goes back up one list.
- A list only appears while at least one of its entries does, so an empty list never shows.
- Lists can nest up to 8 deep.
- You can add to ANNA's own lists as well:

| Key | Constant | ANNA's list |
|---|---|---|
| `car` | `FH3ANNA_LIST_CAR` | Car (Auto Drive, Lights, Doors) |
| `car.lights` | `FH3ANNA_LIST_CAR_LIGHTS` | Car > Lights |
| `car.doors` | `FH3ANNA_LIST_CAR_DOORS` | Car > Doors |

On her main list, ANNA's entries and other mods' come first, then the game's own commands (What Should I Do?,
Set Route To Nearest...).

## Callbacks

All three get back the entry's `user` pointer and its `key`, so one function can serve several entries.

| Field | Signature | Called |
|---|---|---|
| `on_choose` | `void (*)(void* user, const char* key)` | when the player chooses the entry |
| `shown` | `int (*)(void* user, const char* key)` | each time its list opens; return 0 to hide the entry this time |
| `label_now` | `const char* (*)(void* user, const char* key)` | each time its list opens; return the label to show |

**`on_choose` and `stays_open`:**

- `stays_open = 0` (the default): ANNA closes, and `on_choose` runs on the **next frame**. Use this for actions.
- `stays_open = 1`: `on_choose` runs **at once**, and ANNA stays open on the same list, asking every label again.
  Use this for toggles and counters the player may press several times.

**`label_now`** lets a label say what choosing it will do: "Open Roof" / "Close Roof", "Party Mode On" / "Party
Mode Off". ANNA copies the text immediately, so returning a `static` buffer is fine. Return `NULL` or `""` to use
`label`.

**`shown`** hides an entry until it applies. Groove Music Radio's list only shows while the radio is on its
station, and the examples hide an entry unless Auto Drive is driving.

```c
static int party;
static void party_toggle(void* user, const char* key) { party = !party; }
static const char* party_label(void* user, const char* key) { return party ? "Party Mode Off" : "Party Mode On"; }

FH3AnnaEntry e = {sizeof e};
e.key        = "mymod.party";
e.parent     = "mymod";
e.label      = "Party Mode";
e.label_now  = party_label;
e.on_choose  = party_toggle;
e.stays_open = 1;
anna->add_entry(&e);
```

## Threads and lifetime

- **Every callback runs on the game's thread**, the one that updates the HUD and the cars. Keep callbacks short.
  Don't wait, sleep or do file or network work in them; hand slow work to a thread of your own.
- Calling game code from callbacks is safe in the same way as from `FH3PluginUpdate` (Extra Options) or a hook
  (FH3 Mod Platform).
- `add_entry`, `is_open` and `is_autodriving` can be called from any thread.
- **ANNA keeps your function pointers for the whole session.** Your DLL must stay loaded until the game exits:
  - Extra Options never unloads plugins.
  - FH3 Mod Platform can hot-reload mods that export `fh3mod_shutdown`. **A mod that adds entries to ANNA must not
    export `fh3mod_shutdown`**: after a reload, ANNA would call into the unloaded DLL, and the new build's
    `add_entry` would fail anyway (the keys are taken).

## Labels and languages

- Labels are UTF-8, at most 127 bytes.
- ANNA's font covers Latin, Cyrillic and Greek. Other scripts (Chinese, Japanese, Korean...) may show as boxes.
- ANNA translates her own labels into the game's language. Translate yours yourself; `label_now` is a good place
  to pick the language each time the list opens.
- Use the game's capitalisation style, Title Case ("Set Route To Nearest"), so your entries match.

## In races and during Auto Drive

- **Races.** The game switches ANNA off in races. FH3 ANNA replaces her there with a menu of its own on the same
  HUD: D-pad Down opens it, Up/Down move, Right chooses, Left goes back. It shows ANNA's entries and other mods',
  not the game's own commands. Your entries work there as usual, with the same callbacks and the same thread.
- **Auto Drive.** `anna->is_autodriving()` is 1 while ANNA's Auto Drive drives the player's car. Use it in `shown`
  for entries that only make sense then, or to leave the car's controls alone while ANNA drives.
- `anna->is_open()` is 1 while ANNA's menu is on screen. Use it to keep your mod's own D-pad handling out of her way.

## Limits

| What | Limit |
|---|---|
| Entries, all mods together (ANNA's own included) | 256 |
| Entries shown in one list | 32 |
| Key | 63 characters: `a-z A-Z 0-9 . _ -` |
| Label | 127 bytes of UTF-8 |
| Lists inside lists | 8 deep |

`add_entry` returns 0 when the key is taken, the key or parent is invalid, `label` is missing, `size` is wrong, or
the 256 entries are used up.

## The examples

Both examples add the same kinds of entries: a list of their own ("ANNA Example"), an entry in ANNA's Car list,
an on/off entry whose label changes, a counter that keeps ANNA open, and an entry shown only during Auto Drive.

### Extra Options plugin ([`examples/extra-options-plugin`](examples/extra-options-plugin))

```bash
cd examples/extra-options-plugin
sh build.sh                     # llvm-mingw; makes ANNAExample.dll
```

Install it as `<Azimuth folder>\mods\Forza Horizon 3\ANNAExample\plugins\ANNAExample.dll`. Its log lines go to
`FH3_ExtraOptions.log`, marked `PLUGIN:`. It uses [`sdk/fh3_plugin.h`](sdk/fh3_plugin.h), Extra Options' plugin
header, for `FH3PluginInitialize`, `FH3PluginUpdate` and the log.

### FH3 Mod Platform mod ([`examples/mod-platform`](examples/mod-platform))

```bash
cd examples/mod-platform
git clone https://github.com/Levilente/FH3_ModPlatform platform
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64-x86_64.cmake
cmake --build build             # makes build/ANNAExampleMod.dll
```

Install it in `fh3-mods` next to `azimuth.exe`. Its log lines go to `fh3-mods\platform.log`. It also adds a MODS
page row ("Show in ANNA") that hides its entries through `shown`. It waits for ANNA on its own thread, because
platform mods start before Extra Options plugins, and it doesn't export `fh3mod_shutdown` (see
[Threads and lifetime](#threads-and-lifetime)).

## Troubleshooting

| Problem | Check |
|---|---|
| `fh3anna_get` returns `NULL` | ANNA is installed (`mods\Forza Horizon 3\ANNA\plugins\ANNA.dll`) and enabled (no `[Mods] ANNA=0` in `FH3_ExtraOptions.ini`); your mod may start before ANNA, so use `fh3anna_wait` on a thread |
| `fh3anna_wait` returns `NULL` after the timeout | as above; `FH3_ExtraOptions.log` should say `ANNA 1.0.0: menu ready` |
| `add_entry` returns 0 | the key is taken (by you on a second call, or by another mod), has a character outside `a-z A-Z 0-9 . _ -`, or `label`/`size` is missing |
| The entry doesn't show | its `parent` is misspelt; `shown` returns 0; its list has no visible entries; the list already has 32 entries |
| A list doesn't show | none of its entries is visible right now (a list hides when empty) |
| The label doesn't change | a label only updates when its list opens, or after an entry with `stays_open` is chosen |
| The game crashes after a hot reload | the mod exports `fh3mod_shutdown`; see [Threads and lifetime](#threads-and-lifetime) |
| Boxes instead of letters | the script isn't in ANNA's font (Latin, Cyrillic and Greek are) |

## API reference

Everything is in [`include/fh3anna.h`](include/fh3anna.h).

```c
#define FH3ANNA_EXPORT "FH3ANNA_GetApi"
#define FH3ANNA_DLL L"ANNA.dll"
#define FH3ANNA_LIST_CAR "car"
#define FH3ANNA_LIST_CAR_LIGHTS "car.lights"
#define FH3ANNA_LIST_CAR_DOORS "car.doors"

typedef void (*FH3AnnaChooseFn)(void* user, const char* key);
typedef int (*FH3AnnaShownFn)(void* user, const char* key);          /* 0: hidden this time */
typedef const char* (*FH3AnnaLabelFn)(void* user, const char* key);  /* copied at once */

typedef struct FH3AnnaEntry {
    uint32_t size;             /* sizeof(FH3AnnaEntry) */
    const char* key;           /* unique: up to 63 of a-z A-Z 0-9 . _ - */
    const char* parent;        /* NULL: the main list; else a list's key */
    const char* label;         /* UTF-8; required */
    FH3AnnaChooseFn on_choose; /* NULL: the entry is a list */
    FH3AnnaShownFn shown;      /* NULL: always shown */
    FH3AnnaLabelFn label_now;  /* NULL: label */
    void* user;                /* passed back to the callbacks */
    int stays_open;            /* 1: on_choose at once, ANNA stays open */
} FH3AnnaEntry;

typedef struct FH3AnnaApi {
    uint32_t size;             /* sizeof(FH3AnnaApi) of the ANNA that gave it */
    uint32_t version;          /* 1 */
    int (*add_entry)(const FH3AnnaEntry* entry);  /* 1 added; 0 refused */
    int (*is_open)(void);                         /* ANNA's menu on screen */
    int (*is_autodriving)(void);                  /* ANNA's Auto Drive driving */
} FH3AnnaApi;

const FH3AnnaApi* fh3anna_get(uint32_t min_version);                        /* NULL: no ANNA (yet) */
const FH3AnnaApi* fh3anna_wait(uint32_t min_version, uint32_t timeout_ms);  /* blocks: own thread */
```

**Versions.** Ask for the version whose features you use: `fh3anna_get(1)`. A newer ANNA keeps every older call
working. Later versions only add fields at the end of `FH3AnnaApi`, so check `anna->version` (and `anna->size`)
before using a field newer than 1.
