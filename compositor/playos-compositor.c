// PlayOS Compositor — Stage 1 skeleton (Linux only).
//
// A minimal wlroots compositor derived from the upstream TinyWL reference
// (public domain / CC0). It brings up the display, composites client surfaces
// with wlr_scene, advertises a seat so Wayland clients (the Raylib shell)
// initialize, and launches a configured shell command.
//
// Per ADR-0002 (playos-spec), the production compositor will be written in C++
// with RAII wrappers; this C skeleton exists to prove display bring-up on the
// ROG Ally first. Input routing (keyboard/pointer via the seat) is a TODO —
// the shell receives *controller* input directly through the PlayOS Platform
// API's evdev backend, which is sufficient for the Stage 1 slice.
//
// IMPORTANT: wlroots changes its API between releases. This file targets
// wlroots 0.19. If your installed version differs, adapt the calls flagged
// with "VERSION-SENSITIVE" below, using the matching upstream tinywl.c as a
// reference.

#define WLR_USE_UNSTABLE

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

struct playos_server {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;

    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_output_layout *output_layout;

    struct wlr_xdg_shell *xdg_shell;
    struct wlr_seat *seat;

    struct wl_listener new_output;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_input;

    int output_width;
    int output_height;

    const char *shell_cmd;
};

struct playos_keyboard {
    struct wlr_keyboard *wlr_keyboard;
    struct playos_server *server;
    struct wl_listener key;
    struct wl_listener destroy;
};

struct playos_output {
    struct wlr_output *wlr_output;
    struct playos_server *server;
    struct wl_listener frame;
};

static void output_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct playos_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene *scene = output->server->scene;
    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(scene, output->wlr_output);

    if (scene_output == NULL) {
        wlr_log(WLR_ERROR, "output_frame: no scene_output for output %s",
                output->wlr_output->name);
        return;
    }

    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct playos_keyboard *kb =
        wl_container_of(listener, kb, key);
    struct playos_server *server = kb->server;
    struct wlr_keyboard_key_event *event = data;
    struct wlr_seat *seat = server->seat;

    // Forward key events to the seat
    wlr_seat_set_keyboard(seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_key(seat, event->time_msec,
                                  event->keycode, event->state);
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct playos_keyboard *kb =
        wl_container_of(listener, kb, destroy);
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->destroy.link);
    free(kb);
}

static void server_new_input(struct wl_listener *listener, void *data) {
    struct playos_server *server =
        wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        struct playos_keyboard *kb = calloc(1, sizeof(*kb));
        kb->server = server;
        kb->wlr_keyboard = wlr_keyboard_from_input_device(device);
        kb->key.notify = keyboard_handle_key;
        wl_signal_add(&kb->wlr_keyboard->events.key, &kb->key);
        kb->destroy.notify = keyboard_handle_destroy;
        wl_signal_add(&device->events.destroy, &kb->destroy);

        // Set default keymap (US layout)
        struct xkb_context *xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_keymap *keymap = xkb_keymap_new_from_names(
            xkb_ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
        wlr_keyboard_set_keymap(kb->wlr_keyboard, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(xkb_ctx);

        wlr_seat_set_keyboard(server->seat, kb->wlr_keyboard);
        wlr_log(WLR_INFO, "keyboard connected");
        break;
    }
    default:
        break;
    }

    // Allow the seat to manage capabilities
    uint32_t caps = 0;
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(server->seat, caps);
}
    struct playos_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_log(WLR_INFO, "new output: %s %s", wlr_output->name,
            wlr_output->make ? wlr_output->make : "",
            wlr_output->model ? wlr_output->model : "");

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    // VERSION-SENSITIVE: wlr_output_state API (wlroots 0.18+).
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL) {
        wlr_output_state_set_mode(&state, mode);
    }
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    // Store output dimensions for toplevel configure
    server->output_width = wlr_output->width;
    server->output_height = wlr_output->height;

    struct playos_output *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    output->server = server;
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    struct wlr_output_layout_output *layout_output =
        wlr_output_layout_add_auto(server->output_layout, wlr_output);
    struct wlr_scene_output *scene_output =
        wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, layout_output,
                                       scene_output);
}

struct toplevel_commit_data {
    struct wl_listener listener;
    struct playos_server *server;
    struct wlr_xdg_toplevel *toplevel;
};

static void xdg_surface_first_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct toplevel_commit_data *td =
        wl_container_of(listener, td, listener);
    struct playos_server *server = td->server;
    struct wlr_xdg_toplevel *toplevel = td->toplevel;
    struct wlr_xdg_surface *surface = toplevel->base;

    // Only configure on the initial commit
    if (!surface->initial_commit) return;

    // Remove listener — we only care about the first commit
    wl_list_remove(&listener->link);
    free(td);

    if (surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) return;

    wlr_log(WLR_INFO, "initial commit for toplevel, configuring to %dx%d",
            server->output_width, server->output_height);

    if (server->output_width > 0 && server->output_height > 0) {
        wlr_xdg_toplevel_set_size(toplevel,
                                  server->output_width, server->output_height);
        wlr_xdg_toplevel_set_maximized(toplevel, true);
    }

    // Give keyboard focus to the toplevel's surface
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
    if (kb) {
        wlr_seat_keyboard_notify_enter(server->seat,
                                       toplevel->base->surface,
                                       kb->keycodes, kb->num_keycodes,
                                       &kb->modifiers);
    }
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct playos_server *server =
        wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;

    wlr_log(WLR_INFO, "new xdg toplevel: title='%s' app_id='%s'",
            toplevel->title ? toplevel->title : "(null)",
            toplevel->app_id ? toplevel->app_id : "(null)");

    struct wlr_scene_tree *tree =
        wlr_scene_xdg_surface_create(&server->scene->tree, toplevel->base);
    toplevel->base->data = server;
    (void)tree;

    // Wait for the initial surface commit before configuring.
    struct toplevel_commit_data *td = calloc(1, sizeof(*td));
    td->server = server;
    td->toplevel = toplevel;
    td->listener.notify = xdg_surface_first_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &td->listener);
}

static void spawn_shell(const char *cmd) {
    if (cmd == NULL) return;
    wlr_log(WLR_INFO, "spawning shell: %s", cmd);
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
        _exit(127);
    }
}

int main(int argc, char *argv[]) {
    wlr_log_init(WLR_INFO, NULL);

    struct playos_server server = {0};
    // The command to launch as the shell (e.g. the PlayOS Raylib shell).
    server.shell_cmd = (argc > 1) ? argv[1] : getenv("PLAYOS_SHELL_CMD");

    server.display = wl_display_create();
    struct wl_event_loop *loop = wl_display_get_event_loop(server.display);

    // VERSION-SENSITIVE: wlr_backend_autocreate takes an event loop in
    // wlroots 0.18+ (earlier versions took the wl_display).
    server.backend = wlr_backend_autocreate(loop, NULL);
    if (server.backend == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        return 1;
    }

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (server.renderer == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_renderer");
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.display);
        return 1;
    }
    wlr_renderer_init_wl_display(server.renderer, server.display);

    server.allocator =
        wlr_allocator_autocreate(server.backend, server.renderer);
    if (server.allocator == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.display);
        return 1;
    }

    wlr_compositor_create(server.display, 5, server.renderer);
    wlr_subcompositor_create(server.display);
    wlr_data_device_manager_create(server.display);

    // VERSION-SENSITIVE: wlr_output_layout_create takes the display in 0.19.
    server.output_layout = wlr_output_layout_create(server.display);

    server.new_input.notify = server_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);

    server.new_output.notify = server_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    server.scene = wlr_scene_create();
    server.scene_layout =
        wlr_scene_attach_output_layout(server.scene, server.output_layout);

    server.xdg_shell = wlr_xdg_shell_create(server.display, 3);
    server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel,
                  &server.new_xdg_toplevel);

    // A seat is required for Wayland clients to initialize, even though full
    // keyboard/pointer routing is a TODO (controller input comes via the
    // PlayOS Platform API evdev backend).
    server.seat = wlr_seat_create(server.display, "seat0");

    const char *socket = wl_display_add_socket_auto(server.display);
    if (socket == NULL) {
        wlr_log(WLR_ERROR, "failed to create Wayland socket");
        wlr_backend_destroy(server.backend);
        return 1;
    }

    if (!wlr_backend_start(server.backend)) {
        wlr_log(WLR_ERROR, "failed to start backend");
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.display);
        return 1;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    wlr_log(WLR_INFO, "PlayOS compositor running on WAYLAND_DISPLAY=%s", socket);

    spawn_shell(server.shell_cmd);

    wl_display_run(server.display);

    wl_display_destroy_clients(server.display);
    wl_display_destroy(server.display);
    return 0;
}
