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

    const char *shell_cmd;
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

    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void server_new_output(struct wl_listener *listener, void *data) {
    struct playos_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

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

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct playos_server *server =
        wl_container_of(listener, server, new_xdg_toplevel);
    // VERSION-SENSITIVE: wlroots 0.19 emits new_toplevel with a
    // wlr_xdg_toplevel; older versions used new_surface with a
    // wlr_xdg_surface.
    struct wlr_xdg_toplevel *toplevel = data;

    // Add the toplevel to the scene graph so it is composited. The console
    // model is one fullscreen surface at a time; layout/fullscreen policy is
    // a TODO for Stage 2.
    struct wlr_scene_tree *tree =
        wlr_scene_xdg_surface_create(&server->scene->tree, toplevel->base);
    toplevel->base->data = tree;
}

static void spawn_shell(const char *cmd) {
    if (cmd == NULL) return;
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
