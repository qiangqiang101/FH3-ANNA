# FH3 ANNA

ANNA's menu for Forza Horizon 3 on Azimuth: press **D-pad Down** while driving. ANNA is an
[Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions) plugin (an FH3 build newer than 0.6.0: older ones have ANNA built in, and then this plugin stays
out of the way), and any other mod can add its own entries to her menu.

## For players

With Extra Options for FH3 installed, put `ANNA.dll` in `mods\Forza Horizon 3\ANNA\plugins\` next to
`azimuth.exe`. ANNA adds:

- **Car** (in ANNA's main list):
  - **Auto Drive**: the game's AI drives your car along the route set on the map or by ANNA.
    A key (F2) or controller chord (L3+R3) toggles it too; throttle, brake or steering hands the car
    back. While ANNA drives, her ring around the minimap is lit with "Auto Drive On" above her, and LB
    switches to slow orbiting cinematic shots behind black bars, and back.
  - **Lights**: headlights, hazard lights and left/right indicators.
  - **Doors**: open and close the roof, hood, trunk and doors the car animates, or all at once. Parts
    open only while stationary; the throttle closes them.
- **ANNA in races**: the game turns ANNA off in races; D-pad Down still opens her list there.
- **Indicators on the D-pad**: hold D-pad Left/Right for a second to switch that indicator (a tap still
  changes the radio station). Indicators turn off once a turn is made and the steering is centred.

Settings are in `mods\Forza Horizon 3\ANNA\ANNA.ini`. ANNA writes it on her first start, with the
values `FH3_ExtraOptions.ini`'s `[Vehicle]` section had (those settings were part of Extra Options
before ANNA became a plugin). Her log lines are in `FH3_ExtraOptions.log`, marked `PLUGIN:`.

## For mod developers

**[DOC.md](DOC.md)** has the full guide, and two examples to start from: an Extra Options plugin
([`examples/extra-options-plugin`](examples/extra-options-plugin)) and an FH3 Mod Platform mod
([`examples/mod-platform`](examples/mod-platform)). In short:

Copy [`include/fh3anna.h`](include/fh3anna.h) into your mod. It is plain C and works from C and C++,
whatever loads your mod: an Extra Options plugin, an
[FH3 Mod Platform](https://github.com/Levilente/FH3_ModPlatform) mod in `fh3-mods`, or a DLL of
your own. ANNA.dll exports `FH3ANNA_GetApi`; the header's `fh3anna_get` finds it.

```c
#include "fh3anna.h"

static void honk(void* user, const char* key) { /* on the game's thread */ }

static void add_my_entries(const FH3AnnaApi* anna) {
    FH3AnnaEntry list = {sizeof list};
    list.key   = "mymod";              /* a list of your own in ANNA's main list */
    list.label = "My Mod";
    anna->add_entry(&list);

    FH3AnnaEntry e = {sizeof e};
    e.key        = "mymod.honk";       /* unique: start it with your mod's name */
    e.parent     = "mymod";            /* or FH3ANNA_LIST_CAR for her Car list */
    e.label      = "Honk";
    e.on_choose  = honk;
    e.stays_open = 1;                  /* ANNA stays open; leave 0 to close her first */
    anna->add_entry(&e);
}
```

- **An Extra Options plugin** whose name comes after `ANNA` (plugins load in name order) calls
  `fh3anna_get(1)` from `FH3PluginInitialize`.
- **Anything that may start earlier** (FH3 Mod Platform mods, a plugin a `loadorder.txt` puts first)
  calls `fh3anna_wait(1, 60000)` from a thread of its own; it returns once ANNA has loaded, or NULL
  after the timeout when she isn't installed.

`NULL` from either means ANNA isn't installed (or is too old): carry on without her.

Entries without `on_choose` are lists: they show only while one of their entries shows, open inside
ANNA, and D-pad Left goes back up. `shown` hides an entry until it applies (Groove Music Radio's list
shows only while the radio plays the Groove station); `label_now` gives a label that changes
("Open Roof" / "Close Roof"). Labels are UTF-8; translate yours into the game's language yourself.
Every callback runs on the game's thread, so keep them short. Groove Music Radio
([FH3-GrooveMusic](https://github.com/qiangqiang101/FH3-GrooveMusic)) is a complete example.

ANNA's own lists: `car` (Car), `car.lights` (Car > Lights), `car.doors` (Car > Doors).

## Building

`sh build.sh [out.dll]` with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw).
`sdk/fh3_plugin.h` is a copy of Extra Options' plugin SDK (API version 2 or newer: Auto Drive's
cinematic camera hides the HUD with `set_hud_hidden`; with an older Extra Options the HUD stays).
