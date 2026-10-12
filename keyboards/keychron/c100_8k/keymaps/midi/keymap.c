/* Keychron C100 8K -- all 100 keys as a MIDI controller, 4 layers.
 *
 * MIDI channel 1. The mechanical switches are on/off, so notes use a fixed velocity.
 * The 8x8 pad grid sits in the BOTTOM-LEFT corner (columns 1-8, bottom 8 rows).
 *
 * KEY MAP (layer 1; the picture shows the keyboard from above)
 *
 *   104 105 106 107 | 112 113 114 115 | LYR-  LYR+     layer down / layer up
 *   100 101 102 103 | 108 109 110 111 | XPS-  XPS+     transpose down / up
 *    64  65  66  67 |  96  97  98  99 | K1-   K1+      knob 1 (CC 20)
 *    60  61  62  63 |  92  93  94  95 | K2-   K2+      knob 2 (CC 21)
 *    56  57  58  59 |  88  89  90  91 | K3-   K3+      knob 3 (CC 22)
 *    52  53  54  55 |  84  85  86  87 | K4-   K4+      knob 4 (CC 23)
 *    48  49  50  51 |  80  81  82  83 | K5-   K5+      knob 5 (CC 24)
 *    44  45  46  47 |  76  77  78  79 | K6-   K6+      knob 6 (CC 25)
 *    40  41  42  43 |  72  73  74  75 | K7-   K7+      knob 7 (CC 26)
 *    36  37  38  39 |  68  69  70  71 | K8-   K8+      knob 8 (CC 27)
 *
 * LAYERS (the 8x8 grid lights up in a fixed color so you can see which one is on)
 *   1  RED     DRUM    MIDI Fighter 64 layout, notes 36-99. Each 4x4 bank is one
 *                      Ableton Drum Rack page. Does not transpose.
 *   2  GREEN   PUSH    Push "in key" isomorphic layout, C major from C1 (36).
 *                      +1 scale step per column, +3 steps (a fourth) per row.
 *                      Transposes.
 *   3  BLUE    CHROM   Chromatic isomorphic layout from C1 (36).
 *                      +1 semitone per column, +5 per row (a fourth). Transposes.
 *   4  YELLOW  LINEAR  Launchpad-style, notes 36-99. +1 per column, +8 per row.
 *                      Does not transpose. (Placeholder: change it freely.)
 *
 * In the tables below, N(36) is a fixed note and TN(36) is a note that follows the
 * transpose keys. Swap N( for TN( (or back) on any key you like.
 *
 * LAYER KEYS   Top-right pair. They wrap around (up from layer 4 goes to layer 1).
 *              The board always starts on layer 1.
 * TRANSPOSE    Second row, right pair. Moves every TN() note by 1 semitone per press,
 *              up to +/-24. Press both keys together to go back to 0.
 * KNOBS        Each horizontal pair is a knob: the left key turns it down, the right
 *              key turns it up. Hold a key and it repeats. See the settings below.
 *
 * The right-hand keys (layer, transpose and knob keys) are defined on layer 1 only.
 * On layers 2-4 they are KC_TRNS ("same as layer 1"), so they work on every layer.
 */

#include QMK_KEYBOARD_H
#include "qmk_midi.h"   /* declares midi_device */

/* ---------------- Settings you may want to change ---------------- */

#define MIDI_PAD_CHANNEL  0     /* 0 = MIDI channel 1 */
#define MIDI_PAD_VELOCITY 127

#define KNOB_COUNT     8
#define KNOB_CC_BASE   20       /* knob 1 (top) = CC 20 ... knob 8 (bottom) = CC 27 */
#define KNOB_STEP      4        /* change per press, absolute mode (0-127 range) */
#define KNOB_START     64       /* value every knob starts at after power-up */
#define KNOB_HOLD_MS   350      /* hold this long before auto-repeat starts */
#define KNOB_REPEAT_MS 50       /* time between repeats while held */

/* 0 = each knob sends its own value, 0-127 (works with every synth and DAW).
 * 1 = each press sends "+1" or "-1" as 65 / 63 (Ableton: "Relative (Bin Offset)"),
 *     so the parameter never jumps to a remembered value. */
#ifndef KNOB_RELATIVE
#    define KNOB_RELATIVE 0
#endif

#define TRANSPOSE_LIMIT 24      /* semitones up or down */

/* Fixed grid color for each layer {red, green, blue}. The Launcher brightness
 * setting still dims or brightens them. */
#ifdef RGB_MATRIX_ENABLE
static const uint8_t layer_color[][3] = {
    {255,   0,   0},   /* layer 1  red    */
    {  0, 255,   0},   /* layer 2  green  */
    {  0,  80, 255},   /* layer 3  blue   */
    {255, 170,   0},   /* layer 4  yellow */
};
#endif

/* ------------------------------------------------------------------ */

/* VIA / Keychron Launcher keeps its own copy of the keymap. It must have room
 * for all 4 layers or the extra layers would be silently cut off. */
#if defined(DYNAMIC_KEYMAP_LAYER_COUNT) && DYNAMIC_KEYMAP_LAYER_COUNT < 4
#    error "This keymap needs DYNAMIC_KEYMAP_LAYER_COUNT to be at least 4"
#endif

enum layers {
    L_DRUM,        /* layer 1 */
    L_PUSH,        /* layer 2 */
    L_CHROMATIC,   /* layer 3 */
    L_LINEAR,      /* layer 4 */
    MIDI_NUM_LAYERS
};

enum pad_keycodes {
    PAD_FIRST = SAFE_RANGE,
    PAD_LAST  = PAD_FIRST + 127,     /* N(n): fixed note n */
    TPAD_FIRST,
    TPAD_LAST = TPAD_FIRST + 127,    /* TN(n): note n, moved by the transpose keys */
    LYR_DOWN,
    LYR_UP,
    XPOSE_DOWN,
    XPOSE_UP,
    KNOB_FIRST,
    KNOB_LAST = KNOB_FIRST + 2 * KNOB_COUNT - 1,
    PAD_SAFE_RANGE
};

#define N(n)        (PAD_FIRST + (n))
#define TN(n)       (TPAD_FIRST + (n))
#define KNOB_DEC(k) (KNOB_FIRST + 2 * (k))
#define KNOB_INC(k) (KNOB_FIRST + 2 * (k) + 1)

/* ---- State ---- */

static uint8_t current_layer = L_DRUM;
static int8_t  transpose     = 0;
static bool    xpose_down_held = false;
static bool    xpose_up_held   = false;

/* Remembers which note each physical key started (note + 1; 0 = nothing). The
 * note-off is always sent for the note that key actually started, even if you
 * change layer or transpose while it is held. That prevents stuck notes. */
static uint8_t held_note[MATRIX_ROWS][MATRIX_COLS];

/* Knob state */
#if !KNOB_RELATIVE
static uint8_t  knob_value[KNOB_COUNT] = {KNOB_START, KNOB_START, KNOB_START, KNOB_START,
                                          KNOB_START, KNOB_START, KNOB_START, KNOB_START};
#endif
static bool     knob_active    = false;   /* a knob key is being held */
static bool     knob_repeating = false;
static uint8_t  knob_idx, knob_row, knob_col;
static int8_t   knob_dir;
static uint16_t knob_timer;

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    /* ---- Layer 1: DRUM (MIDI Fighter 64 / Drum Rack) + all the control keys ---- */
    [L_DRUM] = LAYOUT_tkl_ansi(
             N(104),      N(105),      N(106),      N(107),      N(112),      N(113),      N(114),      N(115),    LYR_DOWN,      LYR_UP,
             N(100),      N(101),      N(102),      N(103),      N(108),      N(109),      N(110),      N(111),  XPOSE_DOWN,    XPOSE_UP,
             N( 64),      N( 65),      N( 66),      N( 67),      N( 96),      N( 97),      N( 98),      N( 99), KNOB_DEC(0), KNOB_INC(0),
             N( 60),      N( 61),      N( 62),      N( 63),      N( 92),      N( 93),      N( 94),      N( 95), KNOB_DEC(1), KNOB_INC(1),
             N( 56),      N( 57),      N( 58),      N( 59),      N( 88),      N( 89),      N( 90),      N( 91), KNOB_DEC(2), KNOB_INC(2),
             N( 52),      N( 53),      N( 54),      N( 55),      N( 84),      N( 85),      N( 86),      N( 87), KNOB_DEC(3), KNOB_INC(3),
             N( 48),      N( 49),      N( 50),      N( 51),      N( 80),      N( 81),      N( 82),      N( 83), KNOB_DEC(4), KNOB_INC(4),
             N( 44),      N( 45),      N( 46),      N( 47),      N( 76),      N( 77),      N( 78),      N( 79), KNOB_DEC(5), KNOB_INC(5),
             N( 40),      N( 41),      N( 42),      N( 43),      N( 72),      N( 73),      N( 74),      N( 75), KNOB_DEC(6), KNOB_INC(6),
             N( 36),      N( 37),      N( 38),      N( 39),      N( 68),      N( 69),      N( 70),      N( 71), KNOB_DEC(7), KNOB_INC(7)
    ),

    /* ---- Layer 2: PUSH in-key, C major (+1 step per column, +3 steps per row) ---- */
    [L_PUSH] = LAYOUT_tkl_ansi(
            KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,
            KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,
            TN( 72),     TN( 74),     TN( 76),     TN( 77),     TN( 79),     TN( 81),     TN( 83),     TN( 84),     KC_TRNS,     KC_TRNS,
            TN( 67),     TN( 69),     TN( 71),     TN( 72),     TN( 74),     TN( 76),     TN( 77),     TN( 79),     KC_TRNS,     KC_TRNS,
            TN( 62),     TN( 64),     TN( 65),     TN( 67),     TN( 69),     TN( 71),     TN( 72),     TN( 74),     KC_TRNS,     KC_TRNS,
            TN( 57),     TN( 59),     TN( 60),     TN( 62),     TN( 64),     TN( 65),     TN( 67),     TN( 69),     KC_TRNS,     KC_TRNS,
            TN( 52),     TN( 53),     TN( 55),     TN( 57),     TN( 59),     TN( 60),     TN( 62),     TN( 64),     KC_TRNS,     KC_TRNS,
            TN( 47),     TN( 48),     TN( 50),     TN( 52),     TN( 53),     TN( 55),     TN( 57),     TN( 59),     KC_TRNS,     KC_TRNS,
            TN( 41),     TN( 43),     TN( 45),     TN( 47),     TN( 48),     TN( 50),     TN( 52),     TN( 53),     KC_TRNS,     KC_TRNS,
            TN( 36),     TN( 38),     TN( 40),     TN( 41),     TN( 43),     TN( 45),     TN( 47),     TN( 48),     KC_TRNS,     KC_TRNS
    ),

    /* ---- Layer 3: CHROMATIC isomorphic (+1 semitone per column, +5 per row) ---- */
    [L_CHROMATIC] = LAYOUT_tkl_ansi(
            KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,
            KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,
            TN( 71),     TN( 72),     TN( 73),     TN( 74),     TN( 75),     TN( 76),     TN( 77),     TN( 78),     KC_TRNS,     KC_TRNS,
            TN( 66),     TN( 67),     TN( 68),     TN( 69),     TN( 70),     TN( 71),     TN( 72),     TN( 73),     KC_TRNS,     KC_TRNS,
            TN( 61),     TN( 62),     TN( 63),     TN( 64),     TN( 65),     TN( 66),     TN( 67),     TN( 68),     KC_TRNS,     KC_TRNS,
            TN( 56),     TN( 57),     TN( 58),     TN( 59),     TN( 60),     TN( 61),     TN( 62),     TN( 63),     KC_TRNS,     KC_TRNS,
            TN( 51),     TN( 52),     TN( 53),     TN( 54),     TN( 55),     TN( 56),     TN( 57),     TN( 58),     KC_TRNS,     KC_TRNS,
            TN( 46),     TN( 47),     TN( 48),     TN( 49),     TN( 50),     TN( 51),     TN( 52),     TN( 53),     KC_TRNS,     KC_TRNS,
            TN( 41),     TN( 42),     TN( 43),     TN( 44),     TN( 45),     TN( 46),     TN( 47),     TN( 48),     KC_TRNS,     KC_TRNS,
            TN( 36),     TN( 37),     TN( 38),     TN( 39),     TN( 40),     TN( 41),     TN( 42),     TN( 43),     KC_TRNS,     KC_TRNS
    ),

    /* ---- Layer 4: LINEAR, Launchpad style (+1 per column, +8 per row) ---- */
    [L_LINEAR] = LAYOUT_tkl_ansi(
            KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,
            KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,     KC_TRNS,
             N( 92),      N( 93),      N( 94),      N( 95),      N( 96),      N( 97),      N( 98),      N( 99),     KC_TRNS,     KC_TRNS,
             N( 84),      N( 85),      N( 86),      N( 87),      N( 88),      N( 89),      N( 90),      N( 91),     KC_TRNS,     KC_TRNS,
             N( 76),      N( 77),      N( 78),      N( 79),      N( 80),      N( 81),      N( 82),      N( 83),     KC_TRNS,     KC_TRNS,
             N( 68),      N( 69),      N( 70),      N( 71),      N( 72),      N( 73),      N( 74),      N( 75),     KC_TRNS,     KC_TRNS,
             N( 60),      N( 61),      N( 62),      N( 63),      N( 64),      N( 65),      N( 66),      N( 67),     KC_TRNS,     KC_TRNS,
             N( 52),      N( 53),      N( 54),      N( 55),      N( 56),      N( 57),      N( 58),      N( 59),     KC_TRNS,     KC_TRNS,
             N( 44),      N( 45),      N( 46),      N( 47),      N( 48),      N( 49),      N( 50),      N( 51),     KC_TRNS,     KC_TRNS,
             N( 36),      N( 37),      N( 38),      N( 39),      N( 40),      N( 41),      N( 42),      N( 43),     KC_TRNS,     KC_TRNS
    ),
};

/* The position of every key in the 10x10 picture above, numbered 1-100 from the
 * top-left, row by row. Used only for the lighting: it lets the code find the
 * LEDs of the bottom-left 8x8 grid. */
#ifdef RGB_MATRIX_ENABLE
static const uint8_t key_pos[MATRIX_ROWS][MATRIX_COLS] = LAYOUT_tkl_ansi(
          1,   2,   3,   4,   5,   6,   7,   8,   9,  10,
         11,  12,  13,  14,  15,  16,  17,  18,  19,  20,
         21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
         31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
         41,  42,  43,  44,  45,  46,  47,  48,  49,  50,
         51,  52,  53,  54,  55,  56,  57,  58,  59,  60,
         61,  62,  63,  64,  65,  66,  67,  68,  69,  70,
         71,  72,  73,  74,  75,  76,  77,  78,  79,  80,
         81,  82,  83,  84,  85,  86,  87,  88,  89,  90,
         91,  92,  93,  94,  95,  96,  97,  98,  99, 100
);
#endif

/* ---- 単色リアクティブ系エフェクトをグラデーションにする ----
 *
 * リアクティブ・マルチネクサスなどは、色を1つしか持っていない。
 *   hsv.h = rgb_matrix_config.hsv.h + dy / 4;
 * QMK には時間で色相を回すビルド設定 (RGB_MATRIX_SOLID_REACTIVE_GRADIENT_MODE)
 * もあるが、それを入れると固定色に戻せなくなるうえ、速度も選べない。
 *
 * そこで色相そのものをゆっくり書き換える。エフェクトの実装には手を触れないので、
 * Keychron Launcher のエフェクト一覧もそのまま使える。
 * 対象は単色のリアクティブ系だけ。他のエフェクトは今まで通り。
 *
 * EEPROM に書くと寿命を削るので、必ず noeeprom 版を使うこと。 */
#ifdef RGB_MATRIX_ENABLE

/* 1段進むまでの時間。256段で一周するので 30ms なら約7.7秒。
 * Launcher の速度スライダーには載せない。あちらは本来
 * 「叩いた光が消えるまでの速さ」を決めるもので、
 * 色の変化と一緒に動いてしまうと片方だけ調整できなくなるため。 */
#    define HUE_STEP_MS 30

static bool hue_should_drift(void) {
    if (!rgb_matrix_is_enabled()) return false;
    switch (rgb_matrix_get_mode()) {
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
        case RGB_MATRIX_SOLID_REACTIVE_SIMPLE:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
        case RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE:
#    endif
#    ifdef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
        case RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS:
#    endif
            return true;
        default:
            return false;
    }
}

static void hue_drift_task(void) {
    static uint16_t last = 0;
    if (!hue_should_drift()) return;
    if (timer_elapsed(last) < HUE_STEP_MS) return;
    last = timer_read();
    rgb_matrix_sethsv_noeeprom(rgb_matrix_get_hue() + 1,
                               rgb_matrix_get_sat(),
                               rgb_matrix_get_val());
}

/* ---- Fixed layer color on the 8x8 grid only ----
 * Runs every lighting frame, after the normal effect, and repaints just the 64 grid
 * LEDs. All other keys keep whatever lighting you chose in the Launcher. */
bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    uint16_t v = rgb_matrix_get_val();
    uint8_t  r = (uint16_t)layer_color[current_layer][0] * v / 255;
    uint8_t  g = (uint16_t)layer_color[current_layer][1] * v / 255;
    uint8_t  b = (uint16_t)layer_color[current_layer][2] * v / 255;

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint8_t p = key_pos[row][col];
            if (p == 0) continue;
            p--;
            if (p / 10 < 2 || p % 10 > 7) continue;      /* not in the bottom-left 8x8 */
            uint8_t led = g_led_config.matrix_co[row][col];
            if (led == NO_LED || led < led_min || led >= led_max) continue;
            rgb_matrix_set_color(led, r, g, b);
        }
    }
    return true;
}

#endif /* RGB_MATRIX_ENABLE */

/* ---- Knobs ---- */

static void knob_step(uint8_t idx, int8_t dir) {
#if KNOB_RELATIVE
    midi_send_cc(&midi_device, MIDI_PAD_CHANNEL, KNOB_CC_BASE + idx, dir > 0 ? 65 : 63);
#else
    int16_t v = (int16_t)knob_value[idx] + dir * KNOB_STEP;
    if (v < 0)   v = 0;
    if (v > 127) v = 127;
    knob_value[idx] = (uint8_t)v;
    midi_send_cc(&midi_device, MIDI_PAD_CHANNEL, KNOB_CC_BASE + idx, knob_value[idx]);
#endif
}

static void knob_repeat_task(void) {
    if (!knob_active) return;
    if (timer_elapsed(knob_timer) < (knob_repeating ? KNOB_REPEAT_MS : KNOB_HOLD_MS)) return;
    knob_timer     = timer_read();
    knob_repeating = true;
    knob_step(knob_idx, knob_dir);
}

void housekeeping_task_user(void) {
#ifdef RGB_MATRIX_ENABLE
    hue_drift_task();
#endif
    knob_repeat_task();
}

/* ---- Keys ---- */

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    uint8_t row = record->event.key.row;
    uint8_t col = record->event.key.col;
    bool    pressed   = record->event.pressed;
    bool    in_matrix = (row < MATRIX_ROWS && col < MATRIX_COLS);

    if (!pressed && in_matrix) {
        /* A knob key was let go: stop its auto-repeat. */
        if (knob_active && row == knob_row && col == knob_col) {
            knob_active = false;
            return false;
        }
        /* A pad was let go: close the note this key opened, whatever its keycode is now. */
        if (held_note[row][col]) {
            midi_send_noteoff(&midi_device, MIDI_PAD_CHANNEL, held_note[row][col] - 1, 0);
            held_note[row][col] = 0;
            return false;
        }
    }

    switch (keycode) {
        case LYR_DOWN:
            if (pressed) {
                current_layer = (current_layer + MIDI_NUM_LAYERS - 1) % MIDI_NUM_LAYERS;
                layer_move(current_layer);
            }
            return false;

        case LYR_UP:
            if (pressed) {
                current_layer = (current_layer + 1) % MIDI_NUM_LAYERS;
                layer_move(current_layer);
            }
            return false;

        case XPOSE_DOWN:
            xpose_down_held = pressed;
            if (pressed) {
                if (xpose_up_held)                 transpose = 0;    /* both keys = reset */
                else if (transpose > -TRANSPOSE_LIMIT) transpose--;
            }
            return false;

        case XPOSE_UP:
            xpose_up_held = pressed;
            if (pressed) {
                if (xpose_down_held)               transpose = 0;    /* both keys = reset */
                else if (transpose < TRANSPOSE_LIMIT)  transpose++;
            }
            return false;

        case KNOB_FIRST ... KNOB_LAST:
            if (pressed) {
                knob_idx       = (keycode - KNOB_FIRST) / 2;
                knob_dir       = ((keycode - KNOB_FIRST) & 1) ? 1 : -1;
                knob_row       = row;
                knob_col       = col;
                knob_active    = true;
                knob_repeating = false;
                knob_timer     = timer_read();
                knob_step(knob_idx, knob_dir);
            }
            return false;

        case PAD_FIRST ... PAD_LAST:
        case TPAD_FIRST ... TPAD_LAST:
            if (pressed) {
                int16_t note = (keycode <= PAD_LAST) ? (int16_t)(keycode - PAD_FIRST)
                                                     : (int16_t)(keycode - TPAD_FIRST) + transpose;
                if (note >= 0 && note <= 127) {
                    if (in_matrix) held_note[row][col] = (uint8_t)(note + 1);
                    midi_send_noteon(&midi_device, MIDI_PAD_CHANNEL, (uint8_t)note, MIDI_PAD_VELOCITY);
                }
            }
            return false;   /* never emit a keyboard report for a pad */
    }
    return true;
}
