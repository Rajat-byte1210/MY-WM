/* mywm compile-time configuration.
 *
 * This header is included by mywm.c after the core types are declared.
 * Edit, run `make`, then restart the WM with Super+Shift+r.
 */
#ifndef MYWM_CONFIG_H
#define MYWM_CONFIG_H

#define MODKEY XCB_MOD_MASK_4       /* Super */
#define TERMINAL "xterm"
#define EDITOR "xterm -e nvim"
#define BROWSER "firefox"
#define LAUNCHER "rofi -show drun"

#define BORDER_WIDTH 2
#define GAPS 8
#define MASTER_FACTOR 0.60f
#define MASTER_COUNT 1
#define FOCUS_FOLLOWS_MOUSE true
#define SWALLOW true

#define IPC_ENABLE true
#define IPC_ALLOW_SPAWN true

#define AUTOSTART_RELATIVE ".config/mywm/autostart"

#define COLOR_FOCUSED "#89b4fa"
#define COLOR_UNFOCUSED "#45475a"

#define TAG_COUNT 9
static const char *const tag_names[TAG_COUNT] = {
    "code", "web", "notes", "chat", "media", "misc", "7", "8", "9",
};

/* Implemented keymap (M0 + M1). */
static const Key keys[] = {
    /* Applications */
    { MODKEY, XK_Return, spawn, { .v = TERMINAL } },
    { MODKEY, XK_e, spawn, { .v = EDITOR } },
    { MODKEY, XK_b, spawn, { .v = BROWSER } },
    { MODKEY, XK_d, spawn, { .v = LAUNCHER } },

    /* Client lifecycle */
    { MODKEY, XK_q, close_focused, { 0 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_q, quit, { 0 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_r, restart, { 0 } },

    /* Tags */
    { MODKEY, XK_Tab, last_tag, { 0 } },
    { MODKEY, XK_1, view_tag, { .ui = 1 } },
    { MODKEY, XK_2, view_tag, { .ui = 2 } },
    { MODKEY, XK_3, view_tag, { .ui = 3 } },
    { MODKEY, XK_4, view_tag, { .ui = 4 } },
    { MODKEY, XK_5, view_tag, { .ui = 5 } },
    { MODKEY, XK_6, view_tag, { .ui = 6 } },
    { MODKEY, XK_7, view_tag, { .ui = 7 } },
    { MODKEY, XK_8, view_tag, { .ui = 8 } },
    { MODKEY, XK_9, view_tag, { .ui = 9 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_1, tag_focused, { .ui = 1 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_2, tag_focused, { .ui = 2 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_3, tag_focused, { .ui = 3 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_4, tag_focused, { .ui = 4 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_5, tag_focused, { .ui = 5 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_6, tag_focused, { .ui = 6 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_7, tag_focused, { .ui = 7 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_8, tag_focused, { .ui = 8 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_9, tag_focused, { .ui = 9 } },
    { MODKEY | XCB_MOD_MASK_CONTROL, XK_1, toggle_view_tag, { .ui = 1 } },
    { MODKEY | XCB_MOD_MASK_CONTROL, XK_2, toggle_view_tag, { .ui = 2 } },
    { MODKEY | XCB_MOD_MASK_CONTROL, XK_3, toggle_view_tag, { .ui = 3 } },
    { MODKEY | XCB_MOD_MASK_CONTROL, XK_4, toggle_view_tag, { .ui = 4 } },
    { MODKEY | XCB_MOD_MASK_CONTROL, XK_5, toggle_view_tag, { .ui = 5 } },
    { MODKEY | XCB_MOD_MASK_CONTROL, XK_6, toggle_view_tag, { .ui = 6 } },
    { MODKEY | XCB_MOD_MASK_CONTROL, XK_7, toggle_view_tag, { .ui = 7 } },
    { MODKEY | XCB_MOD_MASK_CONTROL, XK_8, toggle_view_tag, { .ui = 8 } },
    { MODKEY | XCB_MOD_MASK_CONTROL, XK_9, toggle_view_tag, { .ui = 9 } },

    /* Focus and layout */
    { MODKEY, XK_h, focus_dir, { .i = DIR_LEFT } },
    { MODKEY, XK_j, focus_dir, { .i = DIR_DOWN } },
    { MODKEY, XK_k, focus_dir, { .i = DIR_UP } },
    { MODKEY, XK_l, focus_dir, { .i = DIR_RIGHT } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_h, swap_dir, { .i = DIR_LEFT } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_j, swap_dir, { .i = DIR_DOWN } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_k, swap_dir, { .i = DIR_UP } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_l, swap_dir, { .i = DIR_RIGHT } },
    { MODKEY | XCB_MOD_MASK_CONTROL, XK_h, resize_master, { .f = -0.05f } },
    { MODKEY | XCB_MOD_MASK_CONTROL, XK_l, resize_master, { .f = 0.05f } },
    { MODKEY, XK_space, cycle_layout, { 0 } },
    { MODKEY | XCB_MOD_MASK_SHIFT, XK_space, toggle_floating, { 0 } },
};

/* Window rules are intentionally not active yet. Planned shape:
 *
 * static const Rule rules[] = {
 *     { .class_name = "Code", .tags = 1u << 0 },
 *     { .class_name = "firefox", .tags = 1u << 1 },
 *     { .type = TYPE_DIALOG, .floating = true, .center = true },
 * };
 */

#endif /* MYWM_CONFIG_H */
