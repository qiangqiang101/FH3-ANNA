/* FH3 ANNA plugin internals: the menu (menu.c), Auto Drive (drive.c), the car's lights and parts
   (car.c), translations (text.c) and the plugin entry (anna.c). */
#ifndef ANNA_INTERNAL_H
#define ANNA_INTERNAL_H
#include "support.h"
#include "sdk/fh3_plugin.h"
#include "include/fh3anna.h"

extern const FH3PluginContext *host;
extern unsigned char *image;

/* anna.c */
void anna_log(const char *format, ...);
int anna_option(const wchar_t *section, const wchar_t *key, int fallback);
void anna_option_text(const wchar_t *section, const wchar_t *key, const wchar_t *fallback,
                      wchar_t *out, unsigned capacity);
void *player_car(void); /* the human-driven local car, or NULL */
void *driven_car(void); /* the player's car, also while Auto Drive drives it */

/* Input (anna.c): XInput pads and keys, only while the game window has the focus. */
typedef struct {
    DWORD packet;
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT left_x, left_y, right_x, right_y;
} PadState;
enum { PAD_DPAD_UP = 0x1, PAD_DPAD_DOWN = 0x2, PAD_DPAD_LEFT = 0x4, PAD_DPAD_RIGHT = 0x8,
       PAD_LEFT_THUMB = 0x40, PAD_RIGHT_THUMB = 0x80 };
int pad_state(DWORD pad, PadState *state); /* 1 when that pad is connected */
int focused_key(int key);

/* The game's std::string (MSVC layout). */
typedef struct {
    union {
        char text[16];
        char *pointer;
    };
    size_t size, capacity;
} MsvcString;

/* menu.c */
int menu_prepare(void);          /* hooks ANNA's HUD menu; 0 when the game code differs */
void menu_frame(void);           /* runs the choice ANNA closed for */
int menu_add_entry(const FH3AnnaEntry *entry);
int menu_is_open(void);
int menu_race_open(void);       /* ANNA's race menu has the D-pad (open, or just closed and still held) */
void menu_relabel(void);         /* the list shown again, with its labels asked again */

/* drive.c */
int drive_prepare(void);         /* reads [Autodrive]; 1 when Auto Drive is on */
void drive_frame(float dt);
void drive_toggle(void);         /* from ANNA: on the next frame */
int drive_active(void);
void *drive_car(void);

/* car.c */
int car_prepare(int menu, int dpad);
void car_frame(void);
void car_menu_entries(void);     /* Car > Lights and Car > Doors */

/* text.c */
const char *text_get(const char *english); /* the game's language, or english itself */
#endif
