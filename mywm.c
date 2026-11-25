/*
 * mywm — minimal student coding window manager
 *
 * Milestone: M0 + M1
 *   - WM role, EWMH identity, client lifecycle, EWMH client lists
 *   - borders, focus history, sloppy/keyboard focus, safe close
 *   - Super+drag move and Super+right-drag resize
 *   - master-stack and monocle layouts with gaps and per-tag layout memory
 *   - nine tags, view/move/toggle/last-tag, hidden clients, focus recovery
 *
 * Still deferred: floating rules, dialogs, fullscreen, RandR monitors, IPC,
 * scratchpad, PiP, and restart persistence. See TODO.md.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#define WM_NAME "mywm"
#define WM_VERSION "0.1.0-m1"
#define MAX_TAGS 16

#define WM_STATE_NORMAL 1
#define WM_STATE_ICONIC 3

typedef union {
    int i;
    unsigned int ui;
    float f;
    const void *v;
} Arg;

typedef enum {
    DIR_LEFT = 0,
    DIR_DOWN,
    DIR_UP,
    DIR_RIGHT
} Direction;

typedef enum {
    LAYOUT_TILE = 0,
    LAYOUT_MONOCLE = 1
} Layout;

typedef struct {
    unsigned int mod;
    xcb_keysym_t keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Key;

typedef struct {
    const char *class_name;
    const char *instance;
    const char *title;
    unsigned int tags;
    int monitor;
    bool floating;
    int width;
    int height;
    bool center;
    bool pip;
    bool dock;
} Rule;

typedef struct Monitor Monitor;

typedef struct Client {
    xcb_window_t win;
    struct Client *next;
    struct Client *prev;
    struct Client *focus_next;
    Monitor *mon;

    int x, y, w, h;
    int fx, fy, fw, fh;
    unsigned int tags;
    uint16_t border_width;
    uint32_t border_pixel;

    int ignore_unmap;
    int close_requests;
    bool floating;
    bool mapped;
    bool has_take_focus;
    bool has_delete_window;
} Client;

struct Monitor {
    int x, y, w, h;
    int wx, wy, ww, wh;
    Client *clients;
    Client *sel;
    unsigned int tagset;
    unsigned int prev_tagset;
    Layout layout[MAX_TAGS];
    int nmaster;
    float mfact;
};

/* Actions referenced by config.h. */
static void spawn(const Arg *arg);
static void quit(const Arg *arg);
static void restart(const Arg *arg);
static void close_focused(const Arg *arg);
static void view_tag(const Arg *arg);
static void tag_focused(const Arg *arg);
static void toggle_view_tag(const Arg *arg);
static void last_tag(const Arg *arg);
static void focus_dir(const Arg *arg);
static void swap_dir(const Arg *arg);
static void resize_master(const Arg *arg);
static void cycle_layout(const Arg *arg);
static void toggle_floating(const Arg *arg);

/* Internal helpers. */
static void logmsg(const char *fmt, ...);
static void fatal(const char *fmt, ...);
static void setup_signals(void);
static void connect_x(void);
static void intern_atoms(void);
static void setup_colors(void);
static void setup_wm(void);
static uint32_t parse_color(const char *hex, uint32_t fallback);
static unsigned int lowest_tag(unsigned int mask);
static bool window_has_protocol(xcb_window_t win, xcb_atom_t protocol);
static int get_wm_state(xcb_window_t win);
static void send_client_message(xcb_window_t win, xcb_atom_t type,
                                uint32_t d0, uint32_t d1, uint32_t d2,
                                uint32_t d3, uint32_t d4);
static void set_wm_state(Client *c, bool iconic);
static void set_net_desktop(Client *c);
static void update_client_lists(void);
static void update_desktop_state(void);
static Client *client_by_window(xcb_window_t win);
static bool client_is_visible(const Client *c);
static void set_border(Client *c, bool focused);
static void apply_borders(void);
static void focus_stack_remove(Client *c);
static void focus_stack_push(Client *c);
static void focus_client(Client *c);
static void focus_by_window(xcb_window_t win);
static void focus_last_visible(void);
static void send_configure_notify(Client *c);
static void configure_client(Client *c, int x, int y, int w, int h,
                             bool notify);
static void place_client(Client *c, int x, int y, int w, int h);
static Layout current_layout(void);
static void arrange(void);
static void set_floating(Client *c, bool floating);
static void apply_visibility(void);
static void apply_tag_change(unsigned int next, bool save_prev);
static Client *neighbor_in_direction(Client *cur, Direction dir);
static void swap_client_positions(Client *a, Client *b);
static Client *manage_client(xcb_window_t win, bool from_map_request);
static void unmanage_client(Client *c, bool destroyed);
static void scan_windows(void);
static void close_window(Client *c);
static void grab_keys(void);
static void grab_buttons(Client *c);
static void start_drag(Client *c, uint8_t button);
static void handle_drag_motion(xcb_motion_notify_event_t *ev);
static void end_drag(void);
static void handle_configure_request(xcb_configure_request_event_t *ev);
static void handle_key_press(xcb_key_press_event_t *ev);
static void handle_event(xcb_generic_event_t *event);
static void event_loop(void);
static void cleanup(void);
static void run_autostart(void);

#include "config.h"

_Static_assert(TAG_COUNT <= MAX_TAGS, "TAG_COUNT exceeds MAX_TAGS");

#define MIN_CLIENT_SIZE (2 * BORDER_WIDTH + 1)

enum {
    A_WM_PROTOCOLS,
    A_WM_DELETE_WINDOW,
    A_WM_TAKE_FOCUS,
    A_WM_STATE,
    A_NET_SUPPORTED,
    A_NET_SUPPORTING_WM_CHECK,
    A_NET_WM_NAME,
    A_NET_ACTIVE_WINDOW,
    A_NET_CLOSE_WINDOW,
    A_NET_CLIENT_LIST,
    A_NET_CLIENT_LIST_STACKING,
    A_NET_WM_DESKTOP,
    A_NET_WORKAREA,
    A_NET_NUMBER_OF_DESKTOPS,
    A_NET_CURRENT_DESKTOP,
    A_NET_DESKTOP_NAMES,
    A_UTF8_STRING,
    ATOM_COUNT
};

static const char *const atom_names[ATOM_COUNT] = {
    "WM_PROTOCOLS",
    "WM_DELETE_WINDOW",
    "WM_TAKE_FOCUS",
    "WM_STATE",
    "_NET_SUPPORTED",
    "_NET_SUPPORTING_WM_CHECK",
    "_NET_WM_NAME",
    "_NET_ACTIVE_WINDOW",
    "_NET_CLOSE_WINDOW",
    "_NET_CLIENT_LIST",
    "_NET_CLIENT_LIST_STACKING",
    "_NET_WM_DESKTOP",
    "_NET_WORKAREA",
    "_NET_NUMBER_OF_DESKTOPS",
    "_NET_CURRENT_DESKTOP",
    "_NET_DESKTOP_NAMES",
    "UTF8_STRING",
};

_Static_assert(LENGTH(atom_names) == ATOM_COUNT, "atom name table mismatch");

static xcb_connection_t *conn;
static xcb_screen_t *screen;
static xcb_window_t root;
static xcb_window_t wmcheck = XCB_NONE;
static xcb_key_symbols_t *keysyms;
static xcb_atom_t atoms[ATOM_COUNT];
static uint32_t color_focused_pixel;
static uint32_t color_unfocused_pixel;

static Monitor mon;
static Client *focus_stack;

static Client *drag_client;
static uint8_t drag_button;
static int drag_last_x;
static int drag_last_y;

static bool running = true;
static bool restart_requested = false;
static volatile sig_atomic_t signal_seen = 0;
static int sig_fd[2] = { -1, -1 };
static char **saved_argv;

static void logmsg(const char *fmt, ...)
{
    va_list ap;

    fputs("mywm: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void fatal(const char *fmt, ...)
{
    va_list ap;

    fputs("mywm: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static void sig_handler(int signo)
{
    int saved_errno = errno;
    const char byte = (char)signo;
    ssize_t ignored = write(sig_fd[1], &byte, 1);

    (void)ignored;
    signal_seen = 1;
    errno = saved_errno;
}

static void setup_signals(void)
{
    struct sigaction sa;

    if (pipe(sig_fd) < 0)
        fatal("pipe: %s", strerror(errno));

    for (int i = 0; i < 2; i++) {
        int flags = fcntl(sig_fd[i], F_GETFL, 0);
        if (flags < 0 || fcntl(sig_fd[i], F_SETFL, flags | O_NONBLOCK) < 0)
            fatal("fcntl: %s", strerror(errno));
        if (fcntl(sig_fd[i], F_SETFD, FD_CLOEXEC) < 0)
            fatal("fcntl: %s", strerror(errno));
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0)
        fatal("sigaction: %s", strerror(errno));

    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
}

static void connect_x(void)
{
    int screen_num = 0;
    xcb_screen_iterator_t iter;

    conn = xcb_connect(NULL, &screen_num);
    if (!conn || xcb_connection_has_error(conn))
        fatal("cannot connect to X display");

    iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
    for (int i = 0; i < screen_num; i++)
        xcb_screen_next(&iter);
    if (!iter.data)
        fatal("no X screen available");

    screen = iter.data;
    root = screen->root;

    int fd = xcb_get_file_descriptor(conn);
    if (fd >= 0)
        fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
}

static void intern_atoms(void)
{
    for (int i = 0; i < ATOM_COUNT; i++) {
        xcb_intern_atom_cookie_t cookie =
            xcb_intern_atom(conn, 0, (uint16_t)strlen(atom_names[i]),
                            atom_names[i]);
        xcb_intern_atom_reply_t *reply =
            xcb_intern_atom_reply(conn, cookie, NULL);

        if (!reply)
            fatal("cannot intern atom %s", atom_names[i]);
        atoms[i] = reply->atom;
        free(reply);
    }
}

static uint32_t parse_color(const char *hex, uint32_t fallback)
{
    unsigned int r, g, b;
    xcb_alloc_color_cookie_t cookie;
    xcb_alloc_color_reply_t *reply;
    uint32_t pixel;

    if (!hex || hex[0] != '#' || strlen(hex) != 7)
        return fallback;
    if (sscanf(hex + 1, "%2x%2x%2x", &r, &g, &b) != 3)
        return fallback;

    cookie = xcb_alloc_color(conn, screen->default_colormap,
                             (uint16_t)(r * 257), (uint16_t)(g * 257),
                             (uint16_t)(b * 257));
    reply = xcb_alloc_color_reply(conn, cookie, NULL);
    if (!reply)
        return fallback;

    pixel = reply->pixel;
    free(reply);
    return pixel;
}

static void setup_colors(void)
{
    color_focused_pixel = parse_color(COLOR_FOCUSED, screen->white_pixel);
    color_unfocused_pixel = parse_color(COLOR_UNFOCUSED, screen->black_pixel);
}

static unsigned int lowest_tag(unsigned int mask)
{
    for (unsigned int i = 0; i < TAG_COUNT; i++) {
        if (mask & (1u << i))
            return i;
    }
    return 0;
}

static bool window_has_protocol(xcb_window_t win, xcb_atom_t protocol)
{
    xcb_get_property_cookie_t cookie;
    xcb_get_property_reply_t *reply;
    xcb_atom_t *values;
    bool found = false;
    int count;

    if (win == XCB_NONE)
        return false;

    cookie = xcb_get_property(conn, 0, win, atoms[A_WM_PROTOCOLS],
                              XCB_ATOM_ATOM, 0, 32);
    reply = xcb_get_property_reply(conn, cookie, NULL);
    if (!reply)
        return false;

    values = xcb_get_property_value(reply);
    count = xcb_get_property_value_length(reply) / (int)sizeof(xcb_atom_t);
    for (int i = 0; i < count; i++) {
        if (values[i] == protocol) {
            found = true;
            break;
        }
    }

    free(reply);
    return found;
}

static int get_wm_state(xcb_window_t win)
{
    xcb_get_property_cookie_t cookie;
    xcb_get_property_reply_t *reply;
    uint32_t *values;
    int state = 0;

    cookie = xcb_get_property(conn, 0, win, atoms[A_WM_STATE],
                              atoms[A_WM_STATE], 0, 2);
    reply = xcb_get_property_reply(conn, cookie, NULL);
    if (!reply)
        return 0;

    if (reply->format == 32 && reply->value_len >= 1) {
        values = xcb_get_property_value(reply);
        state = (int)values[0];
    }

    free(reply);
    return state;
}

static void send_client_message(xcb_window_t win, xcb_atom_t type,
                                uint32_t d0, uint32_t d1, uint32_t d2,
                                uint32_t d3, uint32_t d4)
{
    xcb_client_message_event_t event;

    memset(&event, 0, sizeof(event));
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = win;
    event.type = type;
    event.data.data32[0] = d0;
    event.data.data32[1] = d1;
    event.data.data32[2] = d2;
    event.data.data32[3] = d3;
    event.data.data32[4] = d4;

    xcb_send_event(conn, 0, win, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
}

static void set_wm_state(Client *c, bool iconic)
{
    uint32_t state[2];

    state[0] = iconic ? WM_STATE_ICONIC : WM_STATE_NORMAL;
    state[1] = XCB_NONE;

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, c->win,
                        atoms[A_WM_STATE], atoms[A_WM_STATE], 32, 2, state);
}

static void set_net_desktop(Client *c)
{
    uint32_t desktop = lowest_tag(c->tags);

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, c->win,
                        atoms[A_NET_WM_DESKTOP], XCB_ATOM_CARDINAL, 32, 1,
                        &desktop);
}

static Client *client_by_window(xcb_window_t win)
{
    for (Client *c = mon.clients; c; c = c->next) {
        if (c->win == win)
            return c;
    }
    return NULL;
}

static bool client_is_visible(const Client *c)
{
    return c && ((c->tags & mon.tagset) != 0);
}

static void update_client_lists(void)
{
    size_t count = 0;
    xcb_window_t *wins;
    size_t i = 0;

    for (Client *c = mon.clients; c; c = c->next)
        count++;

    wins = calloc(count ? count : 1, sizeof(*wins));
    if (!wins)
        fatal("calloc: %s", strerror(errno));

    for (Client *c = mon.clients; c; c = c->next)
        wins[i++] = c->win;

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        atoms[A_NET_CLIENT_LIST], XCB_ATOM_WINDOW, 32,
                        (uint32_t)count, wins);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        atoms[A_NET_CLIENT_LIST_STACKING], XCB_ATOM_WINDOW, 32,
                        (uint32_t)count, wins);
    free(wins);
}

static void update_desktop_state(void)
{
    uint32_t count = TAG_COUNT;
    uint32_t current = lowest_tag(mon.tagset);
    uint32_t workarea[4 * MAX_TAGS];
    size_t names_len = 0;
    char *names;
    size_t offset = 0;

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        atoms[A_NET_NUMBER_OF_DESKTOPS], XCB_ATOM_CARDINAL,
                        32, 1, &count);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        atoms[A_NET_CURRENT_DESKTOP], XCB_ATOM_CARDINAL,
                        32, 1, &current);

    for (unsigned int i = 0; i < TAG_COUNT; i++) {
        workarea[i * 4 + 0] = (uint32_t)mon.x;
        workarea[i * 4 + 1] = (uint32_t)mon.y;
        workarea[i * 4 + 2] = (uint32_t)mon.w;
        workarea[i * 4 + 3] = (uint32_t)mon.h;
    }
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        atoms[A_NET_WORKAREA], XCB_ATOM_CARDINAL, 32,
                        (uint32_t)(4 * TAG_COUNT), workarea);

    for (int i = 0; i < TAG_COUNT; i++)
        names_len += strlen(tag_names[i]) + 1;

    names = calloc(1, names_len);
    if (!names)
        fatal("calloc: %s", strerror(errno));

    for (int i = 0; i < TAG_COUNT; i++) {
        size_t len = strlen(tag_names[i]) + 1;
        memcpy(names + offset, tag_names[i], len);
        offset += len;
    }

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        atoms[A_NET_DESKTOP_NAMES], atoms[A_UTF8_STRING],
                        8, (uint32_t)names_len, names);
    free(names);
}

static void set_border(Client *c, bool focused)
{
    uint32_t pixel = focused ? color_focused_pixel : color_unfocused_pixel;

    if (c->border_pixel == pixel)
        return;

    c->border_pixel = pixel;
    xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXEL, &pixel);
}

static void apply_borders(void)
{
    for (Client *c = mon.clients; c; c = c->next)
        set_border(c, c == mon.sel);
}

static void focus_stack_remove(Client *c)
{
    Client **link = &focus_stack;

    while (*link && *link != c)
        link = &(*link)->focus_next;

    if (*link) {
        *link = c->focus_next;
        c->focus_next = NULL;
    }
}

static void focus_stack_push(Client *c)
{
    if (!c)
        return;

    focus_stack_remove(c);
    c->focus_next = focus_stack;
    focus_stack = c;
}

static void focus_client(Client *c)
{
    xcb_window_t active;

    if (c && !client_is_visible(c))
        return;

    focus_stack_push(c);
    mon.sel = c;
    apply_borders();

    if (!c) {
        xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, root,
                            XCB_CURRENT_TIME);
        active = XCB_NONE;
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                            atoms[A_NET_ACTIVE_WINDOW], XCB_ATOM_WINDOW, 32,
                            1, &active);
        return;
    }

    if (c->has_take_focus) {
        send_client_message(c->win, atoms[A_WM_PROTOCOLS],
                            atoms[A_WM_TAKE_FOCUS], XCB_CURRENT_TIME,
                            0, 0, 0);
    }

    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, c->win,
                        XCB_CURRENT_TIME);
    active = c->win;
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        atoms[A_NET_ACTIVE_WINDOW], XCB_ATOM_WINDOW, 32, 1,
                        &active);

    {
        uint32_t values[] = { XCB_STACK_MODE_ABOVE };
        xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_STACK_MODE,
                             values);
    }
    update_client_lists();
}

static void focus_by_window(xcb_window_t win)
{
    Client *c = client_by_window(win);

    if (c && client_is_visible(c))
        focus_client(c);
}

static void focus_last_visible(void)
{
    for (Client *c = focus_stack; c; c = c->focus_next) {
        if (c->mon == &mon && c->mapped && client_is_visible(c)) {
            focus_client(c);
            return;
        }
    }
    focus_client(NULL);
}

static void send_configure_notify(Client *c)
{
    xcb_configure_notify_event_t event;

    memset(&event, 0, sizeof(event));
    event.response_type = XCB_CONFIGURE_NOTIFY;
    event.event = c->win;
    event.window = c->win;
    event.above_sibling = XCB_NONE;
    event.x = (int16_t)c->x;
    event.y = (int16_t)c->y;
    event.width = (uint16_t)c->w;
    event.height = (uint16_t)c->h;
    event.border_width = BORDER_WIDTH;
    event.override_redirect = 0;

    xcb_send_event(conn, 0, c->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                   (const char *)&event);
}

static void configure_client(Client *c, int x, int y, int w, int h,
                             bool notify)
{
    uint32_t values[5];

    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;

    if (c->floating) {
        c->fx = x;
        c->fy = y;
        c->fw = w;
        c->fh = h;
    }

    values[0] = (uint32_t)x;
    values[1] = (uint32_t)y;
    values[2] = (uint32_t)w;
    values[3] = (uint32_t)h;
    values[4] = BORDER_WIDTH;

    xcb_configure_window(conn, c->win,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                         XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                         XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);

    if (notify)
        send_configure_notify(c);
}

static void place_client(Client *c, int x, int y, int w, int h)
{
    int iw = w - 2 * BORDER_WIDTH;
    int ih = h - 2 * BORDER_WIDTH;

    if (iw < MIN_CLIENT_SIZE)
        iw = MIN_CLIENT_SIZE;
    if (ih < MIN_CLIENT_SIZE)
        ih = MIN_CLIENT_SIZE;

    configure_client(c, x + BORDER_WIDTH, y + BORDER_WIDTH, iw, ih, true);
}

static Layout current_layout(void)
{
    return mon.layout[lowest_tag(mon.tagset)];
}

static void arrange(void)
{
    Client *tiled[MAX_TAGS];
    size_t n = 0;
    int ax, ay, aw, ah;

    for (Client *c = mon.clients; c; c = c->next) {
        if (c->mapped && client_is_visible(c) && !c->floating &&
            n < LENGTH(tiled)) {
            tiled[n++] = c;
        }
    }

    if (n == 0)
        return;

    if (n == 1) {
        place_client(tiled[0], 0, 0, mon.w, mon.h);
        apply_borders();
        return;
    }

    ax = GAPS;
    ay = GAPS;
    aw = mon.w - 2 * GAPS;
    ah = mon.h - 2 * GAPS;
    if (aw < MIN_CLIENT_SIZE)
        aw = MIN_CLIENT_SIZE;
    if (ah < MIN_CLIENT_SIZE)
        ah = MIN_CLIENT_SIZE;

    if (current_layout() == LAYOUT_MONOCLE) {
        for (size_t i = 0; i < n; i++)
            place_client(tiled[i], ax, ay, aw, ah);
        if (mon.sel) {
            uint32_t values[] = { XCB_STACK_MODE_ABOVE };
            xcb_configure_window(conn, mon.sel->win,
                                 XCB_CONFIG_WINDOW_STACK_MODE, values);
        }
        apply_borders();
        return;
    }

    {
        int nmaster = mon.nmaster;
        int master_w;
        int master_h;
        int stack_w;
        int stack_h;
        size_t nstack;

        if (nmaster < 1)
            nmaster = 1;
        if (nmaster > (int)n)
            nmaster = (int)n;

        master_w = (int)(aw * mon.mfact);
        if (master_w < MIN_CLIENT_SIZE)
            master_w = MIN_CLIENT_SIZE;
        if (master_w > aw - MIN_CLIENT_SIZE)
            master_w = aw - MIN_CLIENT_SIZE;
        if (nmaster == (int)n)
            master_w = aw;

        master_h = (ah - (nmaster - 1) * GAPS) / nmaster;
        if (master_h < MIN_CLIENT_SIZE)
            master_h = MIN_CLIENT_SIZE;

        for (int i = 0; i < nmaster; i++) {
            int y = ay + i * (master_h + GAPS);
            place_client(tiled[i], ax, y, master_w, master_h);
        }

        nstack = n - (size_t)nmaster;
        if (nstack == 0) {
            apply_borders();
            return;
        }

        stack_w = aw - master_w - GAPS;
        if (stack_w < MIN_CLIENT_SIZE)
            stack_w = MIN_CLIENT_SIZE;
        stack_h = (ah - (int)(nstack - 1) * GAPS) / (int)nstack;
        if (stack_h < MIN_CLIENT_SIZE)
            stack_h = MIN_CLIENT_SIZE;

        for (size_t i = 0; i < nstack; i++) {
            int x = ax + master_w + GAPS;
            int y = ay + (int)i * (stack_h + GAPS);
            place_client(tiled[nmaster + i], x, y, stack_w, stack_h);
        }
    }

    apply_borders();
}

static void set_floating(Client *c, bool floating)
{
    if (!c || c->floating == floating)
        return;

    if (floating) {
        c->fx = c->x;
        c->fy = c->y;
        c->fw = c->w;
        c->fh = c->h;
        c->floating = true;
        configure_client(c, c->fx, c->fy, c->fw, c->fh, false);
    } else {
        c->floating = false;
    }

    arrange();
    focus_client(c);
}

static void apply_visibility(void)
{
    for (Client *c = mon.clients; c; c = c->next) {
        bool visible = client_is_visible(c);

        if (visible && !c->mapped) {
            c->mapped = true;
            xcb_map_window(conn, c->win);
            set_wm_state(c, false);
        } else if (!visible && c->mapped) {
            c->mapped = false;
            c->ignore_unmap++;
            xcb_unmap_window(conn, c->win);
            set_wm_state(c, true);
        }
    }
}

static void apply_tag_change(unsigned int next, bool save_prev)
{
    if (next == 0 || next == mon.tagset)
        return;

    if (save_prev)
        mon.prev_tagset = mon.tagset;
    mon.tagset = next;

    apply_visibility();
    update_desktop_state();
    arrange();

    if (mon.sel && !client_is_visible(mon.sel))
        focus_last_visible();
    else
        focus_client(mon.sel);
}

static void view_tag(const Arg *arg)
{
    unsigned int n = arg->ui;

    if (n < 1 || n > TAG_COUNT)
        return;
    apply_tag_change(1u << (n - 1), true);
}

static void tag_focused(const Arg *arg)
{
    unsigned int n = arg->ui;
    Client *c = mon.sel;

    if (!c || n < 1 || n > TAG_COUNT)
        return;

    c->tags = 1u << (n - 1);
    set_net_desktop(c);
    apply_visibility();
    arrange();

    if (client_is_visible(c))
        focus_client(c);
    else
        focus_last_visible();
}

static void toggle_view_tag(const Arg *arg)
{
    unsigned int n = arg->ui;
    unsigned int next;

    if (n < 1 || n > TAG_COUNT)
        return;

    next = mon.tagset ^ (1u << (n - 1));
    if (next == 0)
        next = mon.tagset;
    apply_tag_change(next, true);
}

static void last_tag(const Arg *arg)
{
    unsigned int next = mon.prev_tagset;

    (void)arg;
    if (next == 0 || next == mon.tagset)
        return;

    mon.prev_tagset = mon.tagset;
    mon.tagset = next;

    apply_visibility();
    update_desktop_state();
    arrange();

    if (mon.sel && !client_is_visible(mon.sel))
        focus_last_visible();
    else
        focus_client(mon.sel);
}

static void cycle_layout(const Arg *arg)
{
    (void)arg;
    mon.layout[lowest_tag(mon.tagset)] =
        (mon.layout[lowest_tag(mon.tagset)] == LAYOUT_TILE)
            ? LAYOUT_MONOCLE
            : LAYOUT_TILE;
    arrange();
}

static void resize_master(const Arg *arg)
{
    mon.mfact += arg->f;
    if (mon.mfact < 0.10f)
        mon.mfact = 0.10f;
    if (mon.mfact > 0.90f)
        mon.mfact = 0.90f;
    arrange();
}

static Client *neighbor_in_direction(Client *cur, Direction dir)
{
    int cx, cy;
    Client *best = NULL;
    unsigned long best_score = ULONG_MAX;

    if (!cur)
        return NULL;

    cx = cur->x + cur->w / 2;
    cy = cur->y + cur->h / 2;

    for (Client *c = mon.clients; c; c = c->next) {
        int dx, dy;
        unsigned long score;

        if (c == cur || !c->mapped || !client_is_visible(c))
            continue;

        dx = (c->x + c->w / 2) - cx;
        dy = (c->y + c->h / 2) - cy;

        switch (dir) {
        case DIR_LEFT:
            if (dx >= 0)
                continue;
            score = (unsigned long)(-dx) * 1000UL +
                    (unsigned long)abs(dy);
            break;
        case DIR_RIGHT:
            if (dx <= 0)
                continue;
            score = (unsigned long)dx * 1000UL + (unsigned long)abs(dy);
            break;
        case DIR_UP:
            if (dy >= 0)
                continue;
            score = (unsigned long)(-dy) * 1000UL +
                    (unsigned long)abs(dx);
            break;
        case DIR_DOWN:
        default:
            if (dy <= 0)
                continue;
            score = (unsigned long)dy * 1000UL + (unsigned long)abs(dx);
            break;
        }

        if (score < best_score) {
            best_score = score;
            best = c;
        }
    }

    return best;
}

static void focus_dir(const Arg *arg)
{
    Client *next = neighbor_in_direction(mon.sel, (Direction)arg->i);

    if (next)
        focus_client(next);
}

static void swap_client_positions(Client *a, Client *b)
{
    Client **list;
    size_t count = 0;
    size_t i = 0;

    if (!a || !b || a == b)
        return;

    for (Client *c = mon.clients; c; c = c->next)
        count++;
    if (count < 2)
        return;

    list = calloc(count, sizeof(*list));
    if (!list)
        fatal("calloc: %s", strerror(errno));

    for (Client *c = mon.clients; c; c = c->next)
        list[i++] = c;

    for (i = 0; i < count; i++) {
        if (list[i] == a)
            list[i] = b;
        else if (list[i] == b)
            list[i] = a;
    }

    for (i = 0; i < count; i++) {
        list[i]->prev = i ? list[i - 1] : NULL;
        list[i]->next = (i + 1 < count) ? list[i + 1] : NULL;
    }
    mon.clients = list[0];
    free(list);

    arrange();
    focus_client(a);
}

static void swap_dir(const Arg *arg)
{
    Client *target = neighbor_in_direction(mon.sel, (Direction)arg->i);

    if (target)
        swap_client_positions(mon.sel, target);
}

static void toggle_floating(const Arg *arg)
{
    (void)arg;
    if (mon.sel)
        set_floating(mon.sel, !mon.sel->floating);
}

static void grab_keys(void)
{
    const uint16_t extra_mods[] = {
        0,
        XCB_MOD_MASK_LOCK,
        XCB_MOD_MASK_2,
        XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2,
    };

    for (size_t i = 0; i < LENGTH(keys); i++) {
        xcb_keycode_t *codes =
            xcb_key_symbols_get_keycode(keysyms, keys[i].keysym);

        if (!codes) {
            logmsg("no keycode for keysym 0x%x", keys[i].keysym);
            continue;
        }

        for (xcb_keycode_t *code = codes; *code != XCB_NO_SYMBOL; code++) {
            for (size_t m = 0; m < LENGTH(extra_mods); m++) {
                uint16_t modifiers = (uint16_t)(keys[i].mod | extra_mods[m]);
                xcb_void_cookie_t cookie = xcb_grab_key_checked(
                    conn, 1, root, modifiers, *code, XCB_GRAB_MODE_ASYNC,
                    XCB_GRAB_MODE_ASYNC);
                xcb_generic_error_t *error = xcb_request_check(conn, cookie);

                if (error) {
                    logmsg("cannot grab key 0x%x: X error %u",
                           keys[i].keysym, error->error_code);
                    free(error);
                }
            }
        }

        free(codes);
    }
}

static void grab_buttons(Client *c)
{
    const uint16_t extra_mods[] = {
        0,
        XCB_MOD_MASK_LOCK,
        XCB_MOD_MASK_2,
        XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2,
    };
    const uint8_t buttons[] = { 1, 3 };

    for (size_t b = 0; b < LENGTH(buttons); b++) {
        for (size_t m = 0; m < LENGTH(extra_mods); m++) {
            uint16_t modifiers = (uint16_t)(MODKEY | extra_mods[m]);
            xcb_void_cookie_t cookie = xcb_grab_button_checked(
                conn, 0, c->win, XCB_EVENT_MASK_BUTTON_PRESS,
                XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE,
                buttons[b], modifiers);
            xcb_generic_error_t *error = xcb_request_check(conn, cookie);

            if (error) {
                logmsg("cannot grab button %u: X error %u",
                       buttons[b], error->error_code);
                free(error);
            }
        }
    }
}

static Client *manage_client(xcb_window_t win, bool from_map_request)
{
    xcb_get_window_attributes_cookie_t attr_cookie;
    xcb_get_window_attributes_reply_t *attr;
    xcb_get_geometry_cookie_t geom_cookie;
    xcb_get_geometry_reply_t *geom;
    Client *c;
    uint32_t values[2];
    uint32_t border_width = BORDER_WIDTH;

    if (win == XCB_NONE || win == root || win == wmcheck ||
        client_by_window(win)) {
        return NULL;
    }

    attr_cookie = xcb_get_window_attributes(conn, win);
    attr = xcb_get_window_attributes_reply(conn, attr_cookie, NULL);
    if (!attr)
        return NULL;

    if (attr->override_redirect) {
        free(attr);
        return NULL;
    }

    if (!from_map_request && attr->map_state == XCB_MAP_STATE_UNMAPPED &&
        get_wm_state(win) != WM_STATE_ICONIC) {
        free(attr);
        return NULL;
    }

    c = calloc(1, sizeof(*c));
    if (!c)
        fatal("calloc: %s", strerror(errno));

    c->win = win;
    c->mon = &mon;
    c->border_pixel = color_unfocused_pixel;
    free(attr);

    geom_cookie = xcb_get_geometry(conn, win);
    geom = xcb_get_geometry_reply(conn, geom_cookie, NULL);
    if (!geom) {
        free(c);
        return NULL;
    }

    c->border_width = geom->border_width;
    c->x = geom->x;
    c->y = geom->y;
    c->w = geom->width;
    c->h = geom->height;
    c->fx = c->x;
    c->fy = c->y;
    c->fw = c->w;
    c->fh = c->h;
    free(geom);

    c->tags = mon.tagset ? mon.tagset : 1u;
    c->has_take_focus = window_has_protocol(win, atoms[A_WM_TAKE_FOCUS]);
    c->has_delete_window = window_has_protocol(win, atoms[A_WM_DELETE_WINDOW]);

    values[0] = color_unfocused_pixel;
    values[1] = XCB_EVENT_MASK_PROPERTY_CHANGE |
                XCB_EVENT_MASK_ENTER_WINDOW |
                XCB_EVENT_MASK_BUTTON_PRESS |
                XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_change_window_attributes(conn, win,
                                 XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK,
                                 values);
    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         &border_width);

    grab_buttons(c);

    c->next = mon.clients;
    if (mon.clients)
        mon.clients->prev = c;
    mon.clients = c;

    set_net_desktop(c);
    set_wm_state(c, false);
    update_client_lists();

    if (client_is_visible(c)) {
        c->mapped = true;
        xcb_map_window(conn, win);
        focus_client(c);
    }

    arrange();
    return c;
}

static void unmanage_client(Client *c, bool destroyed)
{
    bool was_focused = (mon.sel == c);
    Client *prev = c->prev;
    Client *next = c->next;

    if (!c)
        return;

    focus_stack_remove(c);

    if (prev)
        prev->next = next;
    else
        mon.clients = next;
    if (next)
        next->prev = prev;

    if (was_focused)
        mon.sel = NULL;

    if (c->mapped && !destroyed) {
        uint32_t values[1];
        uint32_t pixel = color_unfocused_pixel;

        values[0] = c->border_width;
        xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                             values);
        xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXEL,
                                     &pixel);
    }

    free(c);
    update_client_lists();

    if (was_focused)
        focus_last_visible();
    else
        apply_borders();
    arrange();
}

static void scan_windows(void)
{
    xcb_query_tree_cookie_t cookie = xcb_query_tree(conn, root);
    xcb_query_tree_reply_t *reply = xcb_query_tree_reply(conn, cookie, NULL);

    if (!reply)
        return;

    for (int i = 0; i < xcb_query_tree_children_length(reply); i++) {
        xcb_window_t win = xcb_query_tree_children(reply)[i];
        manage_client(win, false);
    }

    free(reply);
    update_client_lists();
    arrange();
    focus_last_visible();
}

static void close_window(Client *c)
{
    if (!c)
        return;

    if (c->has_delete_window && c->close_requests == 0) {
        c->close_requests = 1;
        send_client_message(c->win, atoms[A_WM_PROTOCOLS],
                            atoms[A_WM_DELETE_WINDOW], XCB_CURRENT_TIME,
                            0, 0, 0);
        return;
    }

    /* Second request, or a client that never advertised WM_DELETE_WINDOW. */
    xcb_kill_client(conn, c->win);
}

static void spawn(const Arg *arg)
{
    pid_t pid;

    if (!arg->v)
        return;

    pid = fork();
    if (pid < 0) {
        logmsg("fork: %s", strerror(errno));
        return;
    }

    if (pid == 0) {
        if (conn)
            close(xcb_get_file_descriptor(conn));
        if (sig_fd[0] >= 0)
            close(sig_fd[0]);
        if (sig_fd[1] >= 0)
            close(sig_fd[1]);

        setsid();
        execl("/bin/sh", "sh", "-c", (const char *)arg->v, (char *)NULL);
        fprintf(stderr, "mywm: exec /bin/sh: %s\n", strerror(errno));
        _exit(127);
    }
}

static void quit(const Arg *arg)
{
    (void)arg;
    running = false;
}

static void restart(const Arg *arg)
{
    (void)arg;
    restart_requested = true;
    running = false;
}

static void close_focused(const Arg *arg)
{
    (void)arg;
    close_window(mon.sel);
}

static void start_drag(Client *c, uint8_t button)
{
    xcb_query_pointer_cookie_t cookie = xcb_query_pointer(conn, root);
    xcb_query_pointer_reply_t *reply =
        xcb_query_pointer_reply(conn, cookie, NULL);
    xcb_grab_pointer_cookie_t grab;
    xcb_grab_pointer_reply_t *status;

    if (!reply)
        return;

    drag_client = c;
    drag_button = button;
    drag_last_x = reply->root_x;
    drag_last_y = reply->root_y;
    free(reply);

    grab = xcb_grab_pointer(
        conn, 0, root,
        XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE,
        XCB_CURRENT_TIME);
    status = xcb_grab_pointer_reply(conn, grab, NULL);
    if (!status || status->status != XCB_GRAB_STATUS_SUCCESS) {
        logmsg("cannot grab pointer: status %u",
               status ? status->status : 0);
        free(status);
        drag_client = NULL;
    } else {
        free(status);
    }
}

static void handle_drag_motion(xcb_motion_notify_event_t *ev)
{
    int dx, dy;
    Client *c = drag_client;

    if (!c)
        return;

    dx = ev->root_x - drag_last_x;
    dy = ev->root_y - drag_last_y;
    if (dx == 0 && dy == 0)
        return;

    drag_last_x = ev->root_x;
    drag_last_y = ev->root_y;

    if (drag_button == 1) {
        int nx = c->fx + dx;
        int ny = c->fy + dy;

        if (nx < 0)
            nx = 0;
        if (ny < 0)
            ny = 0;
        if (nx > mon.w - c->fw - 2 * BORDER_WIDTH)
            nx = mon.w - c->fw - 2 * BORDER_WIDTH;
        if (ny > mon.h - c->fh - 2 * BORDER_WIDTH)
            ny = mon.h - c->fh - 2 * BORDER_WIDTH;
        configure_client(c, nx, ny, c->fw, c->fh, false);
    } else {
        int nw = c->fw + dx;
        int nh = c->fh + dy;
        int max_w = mon.w - c->fx - 2 * BORDER_WIDTH;
        int max_h = mon.h - c->fy - 2 * BORDER_WIDTH;

        if (nw < MIN_CLIENT_SIZE)
            nw = MIN_CLIENT_SIZE;
        if (nh < MIN_CLIENT_SIZE)
            nh = MIN_CLIENT_SIZE;
        if (nw > max_w)
            nw = max_w;
        if (nh > max_h)
            nh = max_h;
        configure_client(c, c->fx, c->fy, nw, nh, false);
    }

    xcb_flush(conn);
}

static void end_drag(void)
{
    if (!drag_client)
        return;

    xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
    drag_client = NULL;
    drag_button = 0;
}

static void handle_configure_request(xcb_configure_request_event_t *ev)
{
    Client *c = client_by_window(ev->window);
    uint32_t values[7];
    uint16_t mask;
    unsigned int n = 0;
    int x, y, w, h;

    mask = ev->value_mask &
           (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
            XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
            XCB_CONFIG_WINDOW_BORDER_WIDTH | XCB_CONFIG_WINDOW_SIBLING |
            XCB_CONFIG_WINDOW_STACK_MODE);

    if (c && client_is_visible(c) && !c->floating) {
        /* Tiled clients keep the geometry chosen by the layout. */
        send_configure_notify(c);
        return;
    }

    if (c && c->floating) {
        x = c->fx;
        y = c->fy;
        w = c->fw;
        h = c->fh;

        if (mask & XCB_CONFIG_WINDOW_X)
            x = ev->x;
        if (mask & XCB_CONFIG_WINDOW_Y)
            y = ev->y;
        if (mask & XCB_CONFIG_WINDOW_WIDTH)
            w = ev->width;
        if (mask & XCB_CONFIG_WINDOW_HEIGHT)
            h = ev->height;

        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        if (x > mon.w - MIN_CLIENT_SIZE)
            x = mon.w - MIN_CLIENT_SIZE;
        if (y > mon.h - MIN_CLIENT_SIZE)
            y = mon.h - MIN_CLIENT_SIZE;
        if (w < MIN_CLIENT_SIZE)
            w = MIN_CLIENT_SIZE;
        if (h < MIN_CLIENT_SIZE)
            h = MIN_CLIENT_SIZE;
        if (w > mon.w - x - 2 * BORDER_WIDTH)
            w = mon.w - x - 2 * BORDER_WIDTH;
        if (h > mon.h - y - 2 * BORDER_WIDTH)
            h = mon.h - y - 2 * BORDER_WIDTH;

        if (mask & XCB_CONFIG_WINDOW_X)
            values[n++] = (uint32_t)x;
        if (mask & XCB_CONFIG_WINDOW_Y)
            values[n++] = (uint32_t)y;
        if (mask & XCB_CONFIG_WINDOW_WIDTH)
            values[n++] = (uint32_t)w;
        if (mask & XCB_CONFIG_WINDOW_HEIGHT)
            values[n++] = (uint32_t)h;
        if (mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
            values[n++] = ev->border_width;
        if (mask & XCB_CONFIG_WINDOW_SIBLING)
            values[n++] = ev->sibling;
        if (mask & XCB_CONFIG_WINDOW_STACK_MODE)
            values[n++] = ev->stack_mode;

        if (mask)
            xcb_configure_window(conn, c->win, mask, values);
        c->fx = x;
        c->fy = y;
        c->fw = w;
        c->fh = h;
        return;
    }

    /* Unmanaged and override-redirect windows pass through unchanged. */
    if (mask & XCB_CONFIG_WINDOW_X)
        values[n++] = (uint32_t)ev->x;
    if (mask & XCB_CONFIG_WINDOW_Y)
        values[n++] = (uint32_t)ev->y;
    if (mask & XCB_CONFIG_WINDOW_WIDTH)
        values[n++] = ev->width;
    if (mask & XCB_CONFIG_WINDOW_HEIGHT)
        values[n++] = ev->height;
    if (mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
        values[n++] = ev->border_width;
    if (mask & XCB_CONFIG_WINDOW_SIBLING)
        values[n++] = ev->sibling;
    if (mask & XCB_CONFIG_WINDOW_STACK_MODE)
        values[n++] = ev->stack_mode;

    if (mask)
        xcb_configure_window(conn, ev->window, mask, values);
}

static void handle_key_press(xcb_key_press_event_t *ev)
{
    /* Column 0 ignores Shift, so Super+Shift+1 still matches XK_1. */
    xcb_keysym_t sym = xcb_key_symbols_get_keysym(keysyms, ev->detail, 0);
    unsigned int state = ev->state &
                         ~(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2);

    for (size_t i = 0; i < LENGTH(keys); i++) {
        if (sym == keys[i].keysym && state == keys[i].mod) {
            keys[i].func(&keys[i].arg);
            return;
        }
    }
}

static void handle_event(xcb_generic_event_t *event)
{
    if (event->response_type == 0) {
        xcb_generic_error_t *error = (xcb_generic_error_t *)event;
        logmsg("X error: code=%u major=%u minor=%u resource=0x%x",
               error->error_code, error->major_code, error->minor_code,
               error->resource_id);
        return;
    }

    switch (event->response_type & ~0x80) {
    case XCB_MAP_REQUEST: {
        xcb_map_request_event_t *ev = (xcb_map_request_event_t *)event;
        Client *c = client_by_window(ev->window);

        if (!c)
            c = manage_client(ev->window, true);
        else if (client_is_visible(c) && !c->mapped) {
            c->mapped = true;
            xcb_map_window(conn, c->win);
            set_wm_state(c, false);
            arrange();
            focus_client(c);
        }
        break;
    }
    case XCB_ENTER_NOTIFY: {
        xcb_enter_notify_event_t *ev = (xcb_enter_notify_event_t *)event;
        Client *c;

        if (!FOCUS_FOLLOWS_MOUSE)
            break;
        if (ev->mode != XCB_NOTIFY_MODE_NORMAL ||
            ev->detail == XCB_NOTIFY_DETAIL_INFERIOR)
            break;

        c = client_by_window(ev->event);
        if (c && client_is_visible(c) && c != mon.sel)
            focus_client(c);
        break;
    }
    case XCB_BUTTON_PRESS: {
        xcb_button_press_event_t *ev = (xcb_button_press_event_t *)event;
        Client *c = client_by_window(ev->event);

        if (!c || !client_is_visible(c))
            break;
        if (!(ev->state & MODKEY))
            break;
        if (ev->detail != 1 && ev->detail != 3)
            break;

        focus_client(c);
        if (!c->floating)
            set_floating(c, true);
        start_drag(c, ev->detail);
        break;
    }
    case XCB_MOTION_NOTIFY:
        handle_drag_motion((xcb_motion_notify_event_t *)event);
        break;
    case XCB_BUTTON_RELEASE: {
        xcb_button_release_event_t *ev = (xcb_button_release_event_t *)event;
        if (drag_client && ev->detail == drag_button)
            end_drag();
        break;
    }
    case XCB_CONFIGURE_REQUEST:
        handle_configure_request((xcb_configure_request_event_t *)event);
        break;
    case XCB_KEY_PRESS:
        handle_key_press((xcb_key_press_event_t *)event);
        break;
    case XCB_UNMAP_NOTIFY: {
        xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t *)event;
        Client *c;

        /* Synthetic events carry the high bit; root substructure events are
         * duplicates of the client's StructureNotify event. */
        if ((event->response_type & 0x80) || ev->event == root)
            break;

        c = client_by_window(ev->window);
        if (!c)
            break;
        if (c->ignore_unmap > 0) {
            c->ignore_unmap--;
            break;
        }
        unmanage_client(c, false);
        break;
    }
    case XCB_DESTROY_NOTIFY: {
        xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t *)event;
        Client *c = client_by_window(ev->window);

        if (c)
            unmanage_client(c, true);
        break;
    }
    case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t *ev = (xcb_client_message_event_t *)event;

        if (ev->type == atoms[A_NET_CLOSE_WINDOW]) {
            close_window(client_by_window(ev->window));
        } else if (ev->type == atoms[A_NET_ACTIVE_WINDOW]) {
            focus_by_window(ev->window);
        }
        break;
    }
    case XCB_PROPERTY_NOTIFY: {
        xcb_property_notify_event_t *ev = (xcb_property_notify_event_t *)event;
        Client *c = client_by_window(ev->window);

        if (c && ev->atom == atoms[A_WM_PROTOCOLS]) {
            c->has_take_focus =
                window_has_protocol(c->win, atoms[A_WM_TAKE_FOCUS]);
            c->has_delete_window =
                window_has_protocol(c->win, atoms[A_WM_DELETE_WINDOW]);
        }
        break;
    }
    case XCB_MAPPING_NOTIFY:
        (void)xcb_refresh_keyboard_mapping(
            keysyms, (xcb_mapping_notify_event_t *)event);
        grab_keys();
        break;
    default:
        break;
    }
}

static void event_loop(void)
{
    struct pollfd fds[2];

    fds[0].fd = xcb_get_file_descriptor(conn);
    fds[0].events = POLLIN;
    fds[1].fd = sig_fd[0];
    fds[1].events = POLLIN;

    while (running) {
        xcb_generic_event_t *event;

        xcb_flush(conn);

        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            logmsg("poll: %s", strerror(errno));
            break;
        }

        if (fds[1].revents & POLLIN) {
            char buffer[64];
            while (read(sig_fd[0], buffer, sizeof(buffer)) > 0)
                ;
        }

        if (signal_seen)
            running = false;

        while ((event = xcb_poll_for_event(conn))) {
            handle_event(event);
            free(event);
        }

        if (xcb_connection_has_error(conn)) {
            logmsg("X connection lost");
            break;
        }
    }
}

static void setup_wm(void)
{
    uint32_t mask;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;
    xcb_window_t none = XCB_NONE;
    xcb_atom_t supported[] = {
        atoms[A_NET_SUPPORTED],
        atoms[A_NET_SUPPORTING_WM_CHECK],
        atoms[A_NET_WM_NAME],
        atoms[A_NET_ACTIVE_WINDOW],
        atoms[A_NET_CLOSE_WINDOW],
        atoms[A_NET_CLIENT_LIST],
        atoms[A_NET_CLIENT_LIST_STACKING],
        atoms[A_NET_WM_DESKTOP],
        atoms[A_NET_WORKAREA],
        atoms[A_NET_NUMBER_OF_DESKTOPS],
        atoms[A_NET_CURRENT_DESKTOP],
        atoms[A_NET_DESKTOP_NAMES],
    };

    mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
           XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
           XCB_EVENT_MASK_PROPERTY_CHANGE |
           XCB_EVENT_MASK_STRUCTURE_NOTIFY |
           XCB_EVENT_MASK_BUTTON_PRESS;

    cookie = xcb_change_window_attributes_checked(
        conn, root, XCB_CW_EVENT_MASK, &mask);
    error = xcb_request_check(conn, cookie);
    if (error) {
        if (error->error_code == XCB_ACCESS)
            fatal("another window manager is already running");
        fatal("cannot select root events: X error %u", error->error_code);
    }

    keysyms = xcb_key_symbols_alloc(conn);
    if (!keysyms)
        fatal("cannot allocate key symbols");

    intern_atoms();
    setup_colors();

    mon.x = 0;
    mon.y = 0;
    mon.w = screen->width_in_pixels;
    mon.h = screen->height_in_pixels;
    mon.wx = mon.x;
    mon.wy = mon.y;
    mon.ww = mon.w;
    mon.wh = mon.h;
    mon.tagset = 1u;
    mon.prev_tagset = 1u;
    mon.nmaster = MASTER_COUNT;
    mon.mfact = MASTER_FACTOR;
    for (unsigned int i = 0; i < TAG_COUNT; i++)
        mon.layout[i] = LAYOUT_TILE;

    wmcheck = xcb_generate_id(conn);
    {
        uint32_t values[] = { 1 };
        xcb_create_window(conn, XCB_COPY_FROM_PARENT, wmcheck, root,
                          0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                          XCB_COPY_FROM_PARENT, XCB_CW_OVERRIDE_REDIRECT,
                          values);
    }

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        atoms[A_NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW,
                        32, 1, &wmcheck);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, wmcheck,
                        atoms[A_NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW,
                        32, 1, &wmcheck);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, wmcheck,
                        atoms[A_NET_WM_NAME], atoms[A_UTF8_STRING],
                        8, (uint32_t)strlen(WM_NAME), WM_NAME);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        atoms[A_NET_SUPPORTED], XCB_ATOM_ATOM, 32,
                        (uint32_t)LENGTH(supported), supported);

    update_desktop_state();
    grab_keys();

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        atoms[A_NET_ACTIVE_WINDOW], XCB_ATOM_WINDOW, 32, 1,
                        &none);
    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, root,
                        XCB_CURRENT_TIME);
    xcb_flush(conn);
}

static void run_autostart(void)
{
    const char *home = getenv("HOME");
    char path[PATH_MAX];
    Arg arg;

    if (!home)
        return;

    if (snprintf(path, sizeof(path), "%s/%s", home, AUTOSTART_RELATIVE) >=
        (int)sizeof(path)) {
        logmsg("autostart path too long");
        return;
    }

    if (access(path, R_OK) != 0)
        return;

    logmsg("running autostart: %s", path);
    arg.v = path;
    spawn(&arg);
}

static void cleanup(void)
{
    xcb_window_t none = XCB_NONE;

    if (!conn)
        return;

    end_drag();

    for (Client *c = mon.clients; c; c = c->next) {
        uint32_t values[1];
        uint32_t pixel = color_unfocused_pixel;

        values[0] = c->border_width;
        xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                             values);
        xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXEL,
                                     &pixel);
    }

    while (mon.clients) {
        Client *next = mon.clients->next;
        free(mon.clients);
        mon.clients = next;
    }
    focus_stack = NULL;
    mon.sel = NULL;

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        atoms[A_NET_ACTIVE_WINDOW], XCB_ATOM_WINDOW, 32, 1,
                        &none);
    xcb_delete_property(conn, root, atoms[A_NET_SUPPORTING_WM_CHECK]);
    xcb_delete_property(conn, root, atoms[A_NET_SUPPORTED]);
    xcb_delete_property(conn, root, atoms[A_NET_CLIENT_LIST]);
    xcb_delete_property(conn, root, atoms[A_NET_CLIENT_LIST_STACKING]);
    xcb_delete_property(conn, root, atoms[A_NET_WORKAREA]);
    xcb_delete_property(conn, root, atoms[A_NET_NUMBER_OF_DESKTOPS]);
    xcb_delete_property(conn, root, atoms[A_NET_CURRENT_DESKTOP]);
    xcb_delete_property(conn, root, atoms[A_NET_DESKTOP_NAMES]);

    if (wmcheck != XCB_NONE) {
        xcb_destroy_window(conn, wmcheck);
        wmcheck = XCB_NONE;
    }

    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, root,
                        XCB_CURRENT_TIME);
    xcb_flush(conn);

    if (keysyms) {
        xcb_key_symbols_free(keysyms);
        keysyms = NULL;
    }

    xcb_disconnect(conn);
    conn = NULL;
}

int main(int argc, char **argv)
{
    saved_argv = argv;

    if (argc > 1) {
        if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
            printf("%s %s\n", WM_NAME, WM_VERSION);
            return EXIT_SUCCESS;
        }
        fprintf(stderr, "usage: %s [-v]\n", argv[0]);
        return 2;
    }

    setup_signals();
    connect_x();
    setup_wm();
    scan_windows();
    run_autostart();

    logmsg("%s %s running on %s", WM_NAME, WM_VERSION,
           getenv("DISPLAY") ? getenv("DISPLAY") : "(unknown display)");

    event_loop();

    if (restart_requested) {
        cleanup();
        execvp(saved_argv[0], saved_argv);
        logmsg("restart failed: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    cleanup();
    return EXIT_SUCCESS;
}
