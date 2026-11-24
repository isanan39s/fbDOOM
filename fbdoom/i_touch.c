//
// Copyright(C) 2026 wmDOOM contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//      Touchscreen input (Linux evdev) and split-screen touch UI for Walkman.
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/fb.h>

/* Undefine Linux input key macros that collide with DOOM's doomkeys.h */
#undef KEY_ENTER
#undef KEY_TAB
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#undef KEY_F12
#undef KEY_BACKSPACE
#undef KEY_PAUSE
#undef KEY_MINUS
#undef KEY_CAPSLOCK
#undef KEY_NUMLOCK
#undef KEY_HOME
#undef KEY_END

#include "doomtype.h"
#include "doomkeys.h"
#include "d_event.h"
#include "m_argv.h"
#include "i_touch.h"

/* External DOOM state for smart menu/autorun handling */
extern boolean menuactive;
extern int messageToPrint;
extern boolean messageNeedsInput;
extern int joybspeed;

/* Logical touch buttons */
enum {
    TBTN_UP = 0,
    TBTN_DOWN,
    TBTN_LEFT,
    TBTN_RIGHT,
    TBTN_SHIFT,
    TBTN_CTRL,
    TBTN_SPACE,
    TBTN_TAB,
    TBTN_ESC,
    TBTN_W1,
    TBTN_W2,
    TBTN_W3,
    TBTN_W4,
    TBTN_W5,
    TBTN_W6,
    TBTN_W7,
    TBTN_COUNT
};

/* Aligned UI geometry for 480x800 (y = 360..799) */
#define UI_TOP_Y        360

/* Row 1: SHIFT (toggle) & CTRL (fire) - y=378..465 (h=88), left/right margin=18 */
#define BTN_SHIFT_X     18
#define BTN_SHIFT_Y     378
#define BTN_SHIFT_W     190
#define BTN_SHIFT_H     88

#define BTN_CTRL_X      272
#define BTN_CTRL_Y      378
#define BTN_CTRL_W      190
#define BTN_CTRL_H      88

/* Row 2: SPACE, TAB, ESC - y=482..535 (h=54), aligned with weapon grid x=196..461 */
#define BTN_SPACE_X     196
#define BTN_SPACE_Y     482
#define BTN_SPACE_W     80
#define BTN_SPACE_H     54

#define BTN_TAB_X       289
#define BTN_TAB_Y       482
#define BTN_TAB_W       80
#define BTN_TAB_H       54

#define BTN_ESC_X       382
#define BTN_ESC_Y       482
#define BTN_ESC_W       80
#define BTN_ESC_H       54

/* Row 3 Left: Large Circular X D-Pad - center (122, 630), radius 104 (diameter 208px!) */
#define DPAD_CX         122
#define DPAD_CY         630
#define DPAD_R_DRAW     104
#define DPAD_R_HIT      116
#define DPAD_R_DEAD     14

/* Row 3 Right: Weapon buttons - top row (2,3,4), bottom row (1,5,6,7), 56x56, 14px gap */
#define WPN_SIZE        56
#define WPN_COL0_X      196
#define WPN_COL1_X      266
#define WPN_COL2_X      336
#define WPN_COL3_X      406
#define WPN_ROW_TOP_Y   652
#define WPN_ROW_BOT_Y   722

struct rect_btn {
    int id;
    int x, y, w, h;
    const char *label;
    const char *sub;
};

static const struct rect_btn rect_buttons[] = {
    { TBTN_SHIFT, BTN_SHIFT_X, BTN_SHIFT_Y, BTN_SHIFT_W, BTN_SHIFT_H, "SHIFT", "RUN" },
    { TBTN_CTRL,  BTN_CTRL_X,  BTN_CTRL_Y,  BTN_CTRL_W,  BTN_CTRL_H,  "CTRL",  "FIRE" },
    { TBTN_SPACE, BTN_SPACE_X, BTN_SPACE_Y, BTN_SPACE_W, BTN_SPACE_H, "SPACE", NULL },
    { TBTN_TAB,   BTN_TAB_X,   BTN_TAB_Y,   BTN_TAB_W,   BTN_TAB_H,   "TAB",   NULL },
    { TBTN_ESC,   BTN_ESC_X,   BTN_ESC_Y,   BTN_ESC_W,   BTN_ESC_H,   "ESC",   NULL },
    { TBTN_W2,    WPN_COL1_X,  WPN_ROW_TOP_Y, WPN_SIZE,  WPN_SIZE,    "2",     NULL },
    { TBTN_W3,    WPN_COL2_X,  WPN_ROW_TOP_Y, WPN_SIZE,  WPN_SIZE,    "3",     NULL },
    { TBTN_W4,    WPN_COL3_X,  WPN_ROW_TOP_Y, WPN_SIZE,  WPN_SIZE,    "4",     NULL },
    { TBTN_W1,    WPN_COL0_X,  WPN_ROW_BOT_Y, WPN_SIZE,  WPN_SIZE,    "1",     NULL },
    { TBTN_W5,    WPN_COL1_X,  WPN_ROW_BOT_Y, WPN_SIZE,  WPN_SIZE,    "5",     NULL },
    { TBTN_W6,    WPN_COL2_X,  WPN_ROW_BOT_Y, WPN_SIZE,  WPN_SIZE,    "6",     NULL },
    { TBTN_W7,    WPN_COL3_X,  WPN_ROW_BOT_Y, WPN_SIZE,  WPN_SIZE,    "7",     NULL }
};

#define NUM_RECT_BTNS (int)(sizeof(rect_buttons) / sizeof(rect_buttons[0]))

/* Touch device state */
static int ts_fd = -1;
static int scr_w = 480;
static int scr_h = 800;
static int raw_min_x = 0, raw_max_x = 959;
static int raw_min_y = 0, raw_max_y = 1599;

#define MAX_CONTACTS 10

/* Type B slot tracking */
static int use_type_b = 0;
static int cur_slot = 0;
struct mt_slot {
    int active;
    int x;
    int y;
};
static struct mt_slot slots[MAX_CONTACTS];

/* Type A / single-touch packet accumulator */
struct touch_pt {
    int x;
    int y;
};
static struct touch_pt pending_pts[MAX_CONTACTS];
static int pending_count = 0;
static struct touch_pt active_pts[MAX_CONTACTS];
static int active_count = 0;

static int cur_x = 0, cur_y = 0;
static int cur_has_pos = 0;
static int cur_touch_zero = 0;
static int saw_mt_report = 0;
static int saw_pressure_zero = 0;
static int finger_all_up = 0;

/* Logical button state & dash toggle */
static uint32_t prev_btn_mask = 0;
static int sent_key[TBTN_COUNT];
static int dash_toggled = 0;
static uint32_t drawn_state[4] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };

static void post_doom_key(evtype_t type, int key)
{
    event_t ev;
    if (key <= 0)
        return;
    ev.type = type;
    ev.data1 = key;
    ev.data2 = (type == ev_keydown && key < 128) ? key : 0;
    ev.data3 = 0;
    D_PostEvent(&ev);
}

static int button_to_doom_key(int btn)
{
    switch (btn) {
    case TBTN_UP:    return KEY_UPARROW;
    case TBTN_DOWN:  return KEY_DOWNARROW;
    case TBTN_LEFT:  return KEY_LEFTARROW;
    case TBTN_RIGHT: return KEY_RIGHTARROW;
    case TBTN_CTRL:
        if (messageToPrint && messageNeedsInput)
            return 'y';
        if (menuactive)
            return KEY_ENTER;
        return KEY_FIRE;
    case TBTN_SPACE:
        if (messageToPrint && messageNeedsInput)
            return 'y';
        if (menuactive)
            return KEY_ENTER;
        return KEY_USE;
    case TBTN_TAB:   return KEY_TAB;
    case TBTN_ESC:   return KEY_ESCAPE;
    case TBTN_W1:    return '1';
    case TBTN_W2:    return '2';
    case TBTN_W3:    return '3';
    case TBTN_W4:    return '4';
    case TBTN_W5:    return '5';
    case TBTN_W6:    return '6';
    case TBTN_W7:    return '7';
    default:         return 0;
    }
}

static int scale_x(int rx)
{
    int span = raw_max_x - raw_min_x + 1;
    int sx;
    if (span <= 0)
        return rx / 2;
    sx = ((rx - raw_min_x) * scr_w) / span;
    if (sx < 0) sx = 0;
    if (sx >= scr_w) sx = scr_w - 1;
    return sx;
}

static int scale_y(int ry)
{
    int span = raw_max_y - raw_min_y + 1;
    int sy;
    if (span <= 0)
        return ry / 2;
    sy = ((ry - raw_min_y) * scr_h) / span;
    if (sy < 0) sy = 0;
    if (sy >= scr_h) sy = scr_h - 1;
    return sy;
}

static uint32_t hit_test_point(int sx, int sy)
{
    uint32_t mask = 0;
    int i;
    int dx, dy, d2;

    if (sy < UI_TOP_Y)
        return 0;

    /* Check rectangular buttons first for exact hits (3px padding) */
    for (i = 0; i < NUM_RECT_BTNS; i++) {
        const struct rect_btn *b = &rect_buttons[i];
        if (sx >= b->x - 3 && sx < b->x + b->w + 3 &&
            sy >= b->y - 3 && sy < b->y + b->h + 3) {
            mask |= (1u << b->id);
            return mask;
        }
    }

    /* Check circular X D-Pad (generous outer hit radius when not on another button) */
    dx = sx - DPAD_CX;
    dy = sy - DPAD_CY;
    d2 = dx * dx + dy * dy;
    if (d2 >= DPAD_R_DEAD * DPAD_R_DEAD && d2 <= DPAD_R_HIT * DPAD_R_HIT) {
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        if (menuactive) {
            if (ady >= adx)
                mask |= (dy < 0) ? (1u << TBTN_UP) : (1u << TBTN_DOWN);
            else
                mask |= (dx < 0) ? (1u << TBTN_LEFT) : (1u << TBTN_RIGHT);
        } else {
            /* 8-way with 60-deg cardinal zones and 30-deg diagonal zones */
            if (ady * 12 >= adx * 7)
                mask |= (dy < 0) ? (1u << TBTN_UP) : (1u << TBTN_DOWN);
            if (adx * 12 >= ady * 7)
                mask |= (dx < 0) ? (1u << TBTN_LEFT) : (1u << TBTN_RIGHT);
        }
        return mask;
    }

    return 0;
}

static void apply_touch_points(void)
{
    uint32_t mask = 0;
    uint32_t changed;
    int i;

    if (use_type_b) {
        for (i = 0; i < MAX_CONTACTS; i++) {
            if (slots[i].active)
                mask |= hit_test_point(scale_x(slots[i].x), scale_y(slots[i].y));
        }
    } else {
        for (i = 0; i < active_count; i++) {
            mask |= hit_test_point(scale_x(active_pts[i].x), scale_y(active_pts[i].y));
        }
    }

    changed = mask ^ prev_btn_mask;
    if (!changed)
        return;

    /* Handle SHIFT as toggle on rising edge */
    if ((changed & (1u << TBTN_SHIFT)) && (mask & (1u << TBTN_SHIFT))) {
        dash_toggled ^= 1;
        joybspeed = dash_toggled ? 29 : 2;
    }

    /* Handle momentary buttons */
    for (i = 0; i < TBTN_COUNT; i++) {
        uint32_t bit = (1u << i);
        if (i == TBTN_SHIFT || !(changed & bit))
            continue;

        if (mask & bit) {
            int key = button_to_doom_key(i);
            sent_key[i] = key;
            post_doom_key(ev_keydown, key);
        } else {
            post_doom_key(ev_keyup, sent_key[i]);
            sent_key[i] = 0;
        }
    }

    prev_btn_mask = mask;
}

void I_InitTouch(int screen_w, int screen_h)
{
    const char *devs[] = { "/dev/input/event1", "/dev/input/event0", "/dev/input/event2", NULL };
    struct input_absinfo abs_x, abs_y;
    int p, i;

    if (screen_w > 0) scr_w = screen_w;
    if (screen_h > 0) scr_h = screen_h;

    dash_toggled = (joybspeed >= 20) ? 1 : 0;

    p = M_CheckParmWithArgs("-touch", 1);
    if (p > 0) {
        int fd = open(myargv[p + 1], O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            ts_fd = fd;
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) == 0 &&
                ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) == 0 &&
                abs_x.maximum > abs_x.minimum && abs_y.maximum > abs_y.minimum) {
                raw_min_x = abs_x.minimum;
                raw_max_x = abs_x.maximum;
                raw_min_y = abs_y.minimum;
                raw_max_y = abs_y.maximum;
            }
            printf("I_InitTouch: using %s (X: %d..%d, Y: %d..%d -> %dx%d)\n",
                   myargv[p + 1], raw_min_x, raw_max_x, raw_min_y, raw_max_y, scr_w, scr_h);
            return;
        }
        printf("I_InitTouch: could not open %s\n", myargv[p + 1]);
    }

    for (i = 0; devs[i] != NULL; i++) {
        int fd = open(devs[i], O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) == 0 &&
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) == 0 &&
            abs_x.maximum > abs_x.minimum && abs_y.maximum > abs_y.minimum) {
            ts_fd = fd;
            raw_min_x = abs_x.minimum;
            raw_max_x = abs_x.maximum;
            raw_min_y = abs_y.minimum;
            raw_max_y = abs_y.maximum;
            printf("I_InitTouch: using %s (X: %d..%d, Y: %d..%d -> %dx%d)\n",
                   devs[i], raw_min_x, raw_max_x, raw_min_y, raw_max_y, scr_w, scr_h);
            return;
        }
        if (i == 0) {
            /* Fallback to /dev/input/event1 with default 0..959, 0..1599 */
            ts_fd = fd;
            printf("I_InitTouch: using %s (default range 960x1600)\n", devs[i]);
            return;
        }
        close(fd);
    }

    printf("I_InitTouch: no touchscreen found.\n");
}

void I_GetTouchEvent(void)
{
    struct input_event ev[32];
    ssize_t n;
    int count, i;

    if (ts_fd < 0)
        return;

    while ((n = read(ts_fd, ev, sizeof(ev))) > 0) {
        count = (int)(n / (ssize_t)sizeof(struct input_event));
        for (i = 0; i < count; i++) {
            unsigned int type = ev[i].type;
            unsigned int code = ev[i].code;
            int val = ev[i].value;

            if (type == EV_ABS) {
                if (code == ABS_MT_SLOT) {
                    use_type_b = 1;
                    if (val >= 0 && val < MAX_CONTACTS)
                        cur_slot = val;
                } else if (code == ABS_MT_TRACKING_ID) {
                    if (use_type_b) {
                        slots[cur_slot].active = (val >= 0);
                    } else if (val < 0) {
                        cur_touch_zero = 1;
                    }
                } else if (code == ABS_MT_POSITION_X || code == ABS_X) {
                    if (use_type_b && code == ABS_MT_POSITION_X) {
                        slots[cur_slot].x = val;
                        slots[cur_slot].active = 1;
                    } else {
                        cur_x = val;
                        cur_has_pos = 1;
                    }
                } else if (code == ABS_MT_POSITION_Y || code == ABS_Y) {
                    if (use_type_b && code == ABS_MT_POSITION_Y) {
                        slots[cur_slot].y = val;
                        slots[cur_slot].active = 1;
                    } else {
                        cur_y = val;
                        cur_has_pos = 1;
                    }
                } else if (code == ABS_MT_TOUCH_MAJOR) {
                    if (val == 0)
                        cur_touch_zero = 1;
                } else if (code == ABS_PRESSURE) {
                    if (val == 0)
                        saw_pressure_zero = 1;
                }
            } else if (type == EV_KEY && code == BTN_TOUCH) {
                if (val == 0)
                    finger_all_up = 1;
                else
                    finger_all_up = 0;
            } else if (type == EV_SYN) {
                if (code == SYN_MT_REPORT) {
                    saw_mt_report = 1;
                    if (cur_has_pos && !cur_touch_zero && pending_count < MAX_CONTACTS) {
                        pending_pts[pending_count].x = cur_x;
                        pending_pts[pending_count].y = cur_y;
                        pending_count++;
                    }
                    cur_has_pos = 0;
                    cur_touch_zero = 0;
                } else if (code == SYN_REPORT) {
                    if (!use_type_b) {
                        if (cur_has_pos && !cur_touch_zero && pending_count < MAX_CONTACTS) {
                            pending_pts[pending_count].x = cur_x;
                            pending_pts[pending_count].y = cur_y;
                            pending_count++;
                        }
                        if (finger_all_up ||
                            (saw_mt_report && pending_count == 0) ||
                            (saw_pressure_zero && pending_count == 0)) {
                            active_count = 0;
                            finger_all_up = 0;
                        } else if (pending_count > 0) {
                            memcpy(active_pts, pending_pts, sizeof(struct touch_pt) * pending_count);
                            active_count = pending_count;
                        }
                        pending_count = 0;
                        cur_has_pos = 0;
                        cur_touch_zero = 0;
                        saw_mt_report = 0;
                        saw_pressure_zero = 0;
                    }
                    apply_touch_points();
                }
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * Clean UI Renderer (5x7 bitmap font + anti-jaggy rounded buttons + X D-Pad)
 * -------------------------------------------------------------------------- */

static const uint8_t glyph_5x7(char c, int row)
{
    /* Each glyph has 7 rows of 5 bits (MSB on left: 0x10..0x01) */
    static const uint8_t font[][7] = {
        /* 0: ' ' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        /* 1: '1' */ { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
        /* 2: '2' */ { 0x0e, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1f },
        /* 3: '3' */ { 0x1f, 0x02, 0x04, 0x06, 0x01, 0x11, 0x0e },
        /* 4: '4' */ { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
        /* 5: '5' */ { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e },
        /* 6: '6' */ { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e },
        /* 7: '7' */ { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
        /* 8: 'A' */ { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        /* 9: 'B' */ { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
        /*10: 'C' */ { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
        /*11: 'E' */ { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
        /*12: 'F' */ { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
        /*13: 'H' */ { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        /*14: 'I' */ { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
        /*15: 'L' */ { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
        /*16: 'N' */ { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
        /*17: 'O' */ { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        /*18: 'P' */ { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
        /*19: 'R' */ { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
        /*20: 'S' */ { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
        /*21: 'T' */ { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
        /*22: 'U' */ { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        /*23: ':' */ { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 }
    };
    int idx = 0;
    switch (c) {
    case '1': idx = 1; break;
    case '2': idx = 2; break;
    case '3': idx = 3; break;
    case '4': idx = 4; break;
    case '5': idx = 5; break;
    case '6': idx = 6; break;
    case '7': idx = 7; break;
    case 'A': idx = 8; break;
    case 'B': idx = 9; break;
    case 'C': idx = 10; break;
    case 'E': idx = 11; break;
    case 'F': idx = 12; break;
    case 'H': idx = 13; break;
    case 'I': idx = 14; break;
    case 'L': idx = 15; break;
    case 'N': idx = 16; break;
    case 'O': idx = 17; break;
    case 'P': idx = 18; break;
    case 'R': idx = 19; break;
    case 'S': idx = 20; break;
    case 'T': idx = 21; break;
    case 'U': idx = 22; break;
    case ':': idx = 23; break;
    default:  idx = 0; break;
    }
    return font[idx][row];
}

struct fb_ctx {
    unsigned char *base;
    int stride;
    int w;
    int h;
    int bpp_bytes;
    const struct fb_var_screeninfo *vinfo;
};

static uint32_t pack_color(const struct fb_var_screeninfo *v, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t pix = 0;
    if (v->red.length)
        pix |= ((uint32_t)(r >> (8 - v->red.length))) << v->red.offset;
    if (v->green.length)
        pix |= ((uint32_t)(g >> (8 - v->green.length))) << v->green.offset;
    if (v->blue.length)
        pix |= ((uint32_t)(b >> (8 - v->blue.length))) << v->blue.offset;
    if (v->transp.length)
        pix |= ((uint32_t)(0xffu >> (8 - v->transp.length))) << v->transp.offset;
    return pix;
}

static void put_px(const struct fb_ctx *ctx, int x, int y, uint32_t color)
{
    unsigned char *p;
    if (x < 0 || x >= ctx->w || y < 0 || y >= ctx->h)
        return;
    p = ctx->base + y * ctx->stride + x * ctx->bpp_bytes;
    if (ctx->bpp_bytes == 4) {
        *(uint32_t *)p = color;
    } else if (ctx->bpp_bytes == 2) {
        *(uint16_t *)p = (uint16_t)color;
    }
}

static void fill_rect(const struct fb_ctx *ctx, int x, int y, int w, int h, uint32_t color)
{
    int ix, iy;
    int x2 = x + w;
    int y2 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > ctx->w) x2 = ctx->w;
    if (y2 > ctx->h) y2 = ctx->h;

    for (iy = y; iy < y2; iy++) {
        unsigned char *row = ctx->base + iy * ctx->stride;
        if (ctx->bpp_bytes == 4) {
            uint32_t *p = (uint32_t *)(row + x * 4);
            for (ix = x; ix < x2; ix++)
                *p++ = color;
        } else if (ctx->bpp_bytes == 2) {
            uint16_t *p = (uint16_t *)(row + x * 2);
            for (ix = x; ix < x2; ix++)
                *p++ = (uint16_t)color;
        }
    }
}

static int inside_rrect(int dx, int dy, int w, int h, int r)
{
    int cx, cy;
    if (dx < 0 || dx >= w || dy < 0 || dy >= h)
        return 0;
    if (dx >= r && dx < w - r)
        return 1;
    if (dy >= r && dy < h - r)
        return 1;
    cx = (dx < r) ? (r - 1 - dx) : (dx - (w - r));
    cy = (dy < r) ? (r - 1 - dy) : (dy - (h - r));
    return (cx * cx + cy * cy <= r * r);
}

static void draw_rounded_button(const struct fb_ctx *ctx, int x, int y, int w, int h,
                                int radius, uint32_t fill_col, uint32_t border_col)
{
    int dx, dy;
    for (dy = 0; dy < h; dy++) {
        for (dx = 0; dx < w; dx++) {
            if (!inside_rrect(dx, dy, w, h, radius))
                continue;
            if (!inside_rrect(dx - 2, dy - 2, w - 4, h - 4, radius > 2 ? radius - 2 : 1))
                put_px(ctx, x + dx, y + dy, border_col);
            else
                put_px(ctx, x + dx, y + dy, fill_col);
        }
    }
}

static void draw_text_centered(const struct fb_ctx *ctx, int cx, int cy,
                               const char *str, int scale, uint32_t col)
{
    int len = (int)strlen(str);
    int total_w = len * (5 * scale) + (len - 1) * scale;
    int total_h = 7 * scale;
    int start_x = cx - total_w / 2;
    int start_y = cy - total_h / 2;
    int i, row, bit, sx, sy;

    for (i = 0; i < len; i++) {
        int gx = start_x + i * (6 * scale);
        for (row = 0; row < 7; row++) {
            uint8_t bits = glyph_5x7(str[i], row);
            for (bit = 0; bit < 5; bit++) {
                if (bits & (0x10 >> bit)) {
                    for (sy = 0; sy < scale; sy++)
                        for (sx = 0; sx < scale; sx++)
                            put_px(ctx, gx + bit * scale + sx, start_y + row * scale + sy, col);
                }
            }
        }
    }
}

static void draw_dpad(const struct fb_ctx *ctx, uint32_t mask,
                      uint32_t col_face, uint32_t col_border,
                      uint32_t col_active, uint32_t col_ink, uint32_t col_ink_active)
{
    int r = DPAD_R_DRAW;
    int r2 = r * r;
    int r_in2 = (r - 2) * (r - 2);
    int dx, dy;

    for (dy = -r; dy <= r; dy++) {
        for (dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            int adx = dx < 0 ? -dx : dx;
            int ady = dy < 0 ? -dy : dy;
            uint32_t c;

            if (d2 > r2)
                continue;
            if (d2 >= r_in2) {
                put_px(ctx, DPAD_CX + dx, DPAD_CY + dy, col_border);
                continue;
            }

            /* Diagonal X divider lines (thickness ~2px) */
            if (adx - ady >= -1 && adx - ady <= 1) {
                put_px(ctx, DPAD_CX + dx, DPAD_CY + dy, col_ink);
                continue;
            }

            /* Quadrant fill (highlight active directions) */
            c = col_face;
            if (dy < -adx && (mask & (1u << TBTN_UP)))
                c = col_active;
            else if (dy > adx && (mask & (1u << TBTN_DOWN)))
                c = col_active;
            else if (dx < -ady && (mask & (1u << TBTN_LEFT)))
                c = col_active;
            else if (dx > ady && (mask & (1u << TBTN_RIGHT)))
                c = col_active;

            put_px(ctx, DPAD_CX + dx, DPAD_CY + dy, c);
        }
    }

    /* Directional chevrons/triangles scaled for the 208px D-Pad */
    for (dy = 0; dy < 12; dy++) {
        for (dx = -dy; dx <= dy; dx++) {
            /* UP arrow */
            put_px(ctx, DPAD_CX + dx, DPAD_CY - 72 + dy,
                   (mask & (1u << TBTN_UP)) ? col_ink_active : col_ink);
            /* DOWN arrow */
            put_px(ctx, DPAD_CX + dx, DPAD_CY + 72 - dy,
                   (mask & (1u << TBTN_DOWN)) ? col_ink_active : col_ink);
            /* LEFT arrow */
            put_px(ctx, DPAD_CX - 72 + dy, DPAD_CY + dx,
                   (mask & (1u << TBTN_LEFT)) ? col_ink_active : col_ink);
            /* RIGHT arrow */
            put_px(ctx, DPAD_CX + 72 - dy, DPAD_CY + dx,
                   (mask & (1u << TBTN_RIGHT)) ? col_ink_active : col_ink);
        }
    }
}

void I_DrawTouchOverlay(unsigned char *screen_base, int line_length,
                        int fb_w, int fb_h, int bpp,
                        const struct fb_var_screeninfo *vinfo, int buf_idx)
{
    uint32_t cur_visual;
    struct fb_ctx ctx;
    uint32_t col_bg, col_sep, col_face, col_border, col_active, col_toggle_on;
    uint32_t col_text, col_text_inv, col_sub;
    int i;

    if (!screen_base || fb_h <= UI_TOP_Y || (bpp != 16 && bpp != 32))
        return;

    if (buf_idx < 0 || buf_idx >= 4)
        buf_idx = 0;

    cur_visual = (prev_btn_mask & 0xffffu) | ((uint32_t)(dash_toggled & 1) << 16);
    if (drawn_state[buf_idx] == cur_visual)
        return;
    drawn_state[buf_idx] = cur_visual;

    ctx.base = screen_base;
    ctx.stride = line_length;
    ctx.w = fb_w;
    ctx.h = fb_h;
    ctx.bpp_bytes = bpp / 8;
    ctx.vinfo = vinfo;

    col_bg        = pack_color(vinfo, 16, 18, 22);
    col_sep       = pack_color(vinfo, 64, 68, 78);
    col_face      = pack_color(vinfo, 232, 235, 240);
    col_border    = pack_color(vinfo, 160, 166, 178);
    col_active    = pack_color(vinfo, 224, 86, 43);   /* DOOM ember orange on press */
    col_toggle_on = pack_color(vinfo, 46, 178, 96);   /* Crisp green when RUN is toggled ON */
    col_text      = pack_color(vinfo, 20, 22, 26);
    col_text_inv  = pack_color(vinfo, 255, 255, 255);
    col_sub       = pack_color(vinfo, 90, 96, 108);

    /* Clear lower control panel & draw top divider */
    fill_rect(&ctx, 0, UI_TOP_Y, fb_w, fb_h - UI_TOP_Y, col_bg);
    fill_rect(&ctx, 0, UI_TOP_Y, fb_w, 2, col_sep);

    /* Draw rectangular buttons */
    for (i = 0; i < NUM_RECT_BTNS; i++) {
        const struct rect_btn *b = &rect_buttons[i];
        int pressed = (prev_btn_mask & (1u << b->id)) != 0;
        int is_shift_on = (b->id == TBTN_SHIFT && dash_toggled);
        uint32_t fill = pressed ? col_active : (is_shift_on ? col_toggle_on : col_face);
        uint32_t border = (pressed || is_shift_on) ? col_text_inv : col_border;
        uint32_t txt = (pressed || is_shift_on) ? col_text_inv : col_text;
        int radius = (b->h >= 80) ? 10 : 7;
        int cx = b->x + b->w / 2;
        int cy = b->y + b->h / 2;

        draw_rounded_button(&ctx, b->x, b->y, b->w, b->h, radius, fill, border);

        if (b->id == TBTN_SHIFT) {
            draw_text_centered(&ctx, cx, cy - 12, "SHIFT", 3, txt);
            draw_text_centered(&ctx, cx, cy + 18, is_shift_on ? "RUN:ON" : "RUN:OFF",
                               2, (pressed || is_shift_on) ? col_text_inv : col_sub);
        } else if (b->id == TBTN_CTRL) {
            draw_text_centered(&ctx, cx, cy - 12, "CTRL", 3, txt);
            draw_text_centered(&ctx, cx, cy + 18, "FIRE",
                               2, pressed ? col_text_inv : col_sub);
        } else if (b->h <= 60 && strlen(b->label) > 1) {
            draw_text_centered(&ctx, cx, cy, b->label, 2, txt);
        } else {
            draw_text_centered(&ctx, cx, cy, b->label, 3, txt);
        }
    }

    /* Draw circular X D-Pad */
    draw_dpad(&ctx, prev_btn_mask, col_face, col_border, col_active, col_text, col_text_inv);
}
