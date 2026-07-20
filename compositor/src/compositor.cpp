// PlayOS Compositor — C++ RAII implementation (Stage 1).
//
// Port of playos-compositor.c to C++17 with proper RAII ownership.
// See compositor.hpp for architecture overview.

#include "playos/compositor/compositor.hpp"

// wlr_scene.h uses C99 `static` in array parameter declarations
// (e.g. `const float color[static 4]`), which is not valid C++.
// The header has no other uses of the `static` keyword, so we
// temporarily define it away for this include only.
extern "C" {
#pragma push_macro("static")
#undef static
#define static
#include <wlr/types/wlr_scene.h>
#pragma pop_macro("static")
}

#include <cstdlib>
#include <ctime>
#include <linux/input-event-codes.h>
#include <sys/wait.h>
#include <unistd.h>

namespace PlayOS {

// ══════════════════════════════════════════════════════════════════════
//  Construction / Destruction
// ══════════════════════════════════════════════════════════════════════

Compositor::Compositor() {
    // wl_listener members are zero-initialised by the = {} defaults.
}

Compositor::~Compositor() {
    // Destroy Wayland clients first — they may hold references to
    // compositor globals that are torn down by wl_display_destroy.
    if (display_) {
        wl_display_destroy_clients(display_);
    }

    // Non-global wlroots objects are freed in reverse dependency order
    // so no object outlives something it references.
    if (allocator_)  wlr_allocator_destroy(allocator_);
    if (renderer_)   wlr_renderer_destroy(renderer_);
    if (backend_)    wlr_backend_destroy(backend_);
    if (display_)    wl_display_destroy(display_);
}

// ══════════════════════════════════════════════════════════════════════
//  Initialisation
// ══════════════════════════════════════════════════════════════════════

bool Compositor::init() {
    display_ = wl_display_create();
    if (!display_) {
        wlr_log(WLR_ERROR, "failed to create wl_display");
        return false;
    }

    wl_event_loop* loop = wl_display_get_event_loop(display_);

    // VERSION-SENSITIVE: wlr_backend_autocreate takes an event loop in
    // wlroots 0.18+ (earlier versions took the wl_display).
    backend_ = wlr_backend_autocreate(loop, nullptr);
    if (!backend_) {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        return false;
    }

    renderer_ = wlr_renderer_autocreate(backend_);
    if (!renderer_) {
        wlr_log(WLR_ERROR, "failed to create wlr_renderer");
        return false;
    }
    wlr_renderer_init_wl_display(renderer_, display_);

    allocator_ = wlr_allocator_autocreate(backend_, renderer_);
    if (!allocator_) {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        return false;
    }

    setup_scene();
    setup_protocols();

    // A seat is required for Wayland clients to initialise, even though
    // full keyboard/pointer routing is a Stage 2 TODO (controller input
    // comes via the Platform API evdev backend).
    seat_ = wlr_seat_create(display_, "seat0");
    if (!seat_) {
        wlr_log(WLR_ERROR, "failed to create wlr_seat");
        return false;
    }

    return true;
}

void Compositor::setup_scene() {
    // VERSION-SENSITIVE: wlr_output_layout_create takes the display in
    // wlroots 0.19.
    output_layout_ = wlr_output_layout_create(display_);

    scene_ = wlr_scene_create();
    scene_layout_ = wlr_scene_attach_output_layout(scene_, output_layout_);

    new_output_.notify = handle_new_output;
    wl_signal_add(&backend_->events.new_output, &new_output_);

    new_input_.notify = handle_new_input;
    wl_signal_add(&backend_->events.new_input, &new_input_);
}

void Compositor::setup_protocols() {
    // Standard Wayland protocols needed by clients.
    wlr_compositor_create(display_, 5, renderer_);
    wlr_subcompositor_create(display_);
    wlr_data_device_manager_create(display_);

    xdg_shell_ = wlr_xdg_shell_create(display_, 3);
    new_xdg_toplevel_.notify = handle_new_xdg_toplevel;
    wl_signal_add(&xdg_shell_->events.new_toplevel, &new_xdg_toplevel_);
}

// ══════════════════════════════════════════════════════════════════════
//  Run loop
// ══════════════════════════════════════════════════════════════════════

void Compositor::run(const char* shell_cmd) {
    const char* socket = wl_display_add_socket_auto(display_);
    if (!socket) {
        wlr_log(WLR_ERROR, "failed to create Wayland socket");
        return;
    }

    if (!wlr_backend_start(backend_)) {
        wlr_log(WLR_ERROR, "failed to start backend");
        return;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    wlr_log(WLR_INFO, "PlayOS compositor running on WAYLAND_DISPLAY=%s",
            socket);

    spawn_shell(shell_cmd);
    wl_display_run(display_);
}

void Compositor::spawn_shell(const char* cmd) {
    if (!cmd) return;

    wlr_log(WLR_INFO, "spawning shell: %s", cmd);

    pid_t pid = fork();
    if (pid < 0) {
        wlr_log(WLR_ERROR, "fork failed for shell command");
        return;
    }
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "/bin/sh", "-c", cmd, static_cast<void*>(nullptr));
        _exit(127);
    }
    // Parent continues; the compositor does not track the shell process
    // in Stage 1 — runtime IPC (Stage 2) will formalise this.
}

void Compositor::handle_home_button() {
    if (!game_toplevel_) {
        wlr_log(WLR_INFO, "Home pressed — no game active, ignoring");
        return;
    }

    wlr_log(WLR_INFO, "Home pressed — returning to shell");
    wlr_xdg_toplevel_send_close(game_toplevel_);
    // game_toplevel_ is cleared in handle_toplevel_destroy when the
    // client disconnects; the shell (still alive in the background)
    // regains keyboard focus on its next surface commit.
}

// ══════════════════════════════════════════════════════════════════════
//  Output handlers
// ══════════════════════════════════════════════════════════════════════

void Compositor::handle_new_output(wl_listener* listener, void* data) {
    Compositor* self = wl_container_of(listener, self, new_output_);
    auto* wlr_output = static_cast<struct wlr_output*>(data);

    wlr_log(WLR_INFO, "new output: %s %s %s",
            wlr_output->name,
            wlr_output->make ? wlr_output->make : "",
            wlr_output->model ? wlr_output->model : "");

    wlr_output_init_render(wlr_output, self->allocator_, self->renderer_);

    // VERSION-SENSITIVE: wlr_output_state API (wlroots 0.18+).
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    // Cache dimensions for toplevel configure.
    self->output_width_  = wlr_output->width;
    self->output_height_ = wlr_output->height;

    // Per-output bookkeeping.
    auto* output = new Output{wlr_output, self, {}};
    output->frame.notify = handle_output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    wlr_output_layout_output* layout_output =
        wlr_output_layout_add_auto(self->output_layout_, wlr_output);
    wlr_scene_output* scene_output =
        wlr_scene_output_create(self->scene_, wlr_output);
    wlr_scene_output_layout_add_output(self->scene_layout_, layout_output,
                                       scene_output);
}

void Compositor::handle_output_frame(wl_listener* listener, void* /*data*/) {
    Output* output = wl_container_of(listener, output, frame);
    Compositor* self = output->compositor;

    wlr_scene_output* scene_output =
        wlr_scene_get_scene_output(self->scene_, output->output);
    if (!scene_output) {
        wlr_log(WLR_ERROR,
                "output_frame: no scene_output for output %s",
                output->output->name);
        return;
    }

    wlr_scene_output_commit(scene_output, nullptr);

    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

// ══════════════════════════════════════════════════════════════════════
//  Input handlers
// ══════════════════════════════════════════════════════════════════════

void Compositor::handle_new_input(wl_listener* listener, void* data) {
    Compositor* self = wl_container_of(listener, self, new_input_);
    wlr_input_device* device = static_cast<wlr_input_device*>(data);

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        auto* kb = new Keyboard{};
        kb->device = wlr_keyboard_from_input_device(device);
        kb->compositor = self;

        kb->key.notify = handle_keyboard_key;
        wl_signal_add(&kb->device->events.key, &kb->key);

        kb->modifiers.notify = handle_keyboard_modifiers;
        wl_signal_add(&kb->device->events.modifiers, &kb->modifiers);

        kb->destroy.notify = handle_keyboard_destroy;
        wl_signal_add(&device->events.destroy, &kb->destroy);

        // Default US keymap.
        xkb_context* xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        xkb_keymap* keymap = xkb_keymap_new_from_names(
            xkb_ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
        wlr_keyboard_set_keymap(kb->device, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(xkb_ctx);

        wlr_seat_set_keyboard(self->seat_, kb->device);
        wlr_log(WLR_INFO, "keyboard connected");
        break;
    }
    default:
        // Pointer and touch are Stage 2.
        break;
    }

    // Advertise available seat capabilities.
    uint32_t caps = 0;
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    // Stage 2: |= WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_TOUCH
    wlr_seat_set_capabilities(self->seat_, caps);
}

void Compositor::handle_keyboard_key(wl_listener* listener, void* data) {
    Keyboard* kb = wl_container_of(listener, kb, key);
    Compositor* self = kb->compositor;
    wlr_keyboard_key_event* event = static_cast<wlr_keyboard_key_event*>(data);

    // Intercept the Home button globally so the player can always
    // return to the shell, even if a game is frozen.
    //
    // Match the same keycode set used by the Platform API's evdev
    // backend (linux_input_backend.cpp + input_mapping.cpp):
    //   BTN_MODE (316)          — Xbox guide button (ROG Ally default)
    //   KEY_PROG1 (148)         — ASUS Armoury button
    //   BTN_TRIGGER_HAPPY1–4    — generic vendor buttons (Steam Deck, etc.)
    //
    // See also: playos-reference-devices/rog-ally/device-profile.toml
    //           playos-platform-api/src/backends/linux/input_mapping.cpp
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED &&
        (event->keycode == KEY_PROG1 ||
         event->keycode == BTN_MODE ||
         event->keycode == BTN_TRIGGER_HAPPY1 ||
         event->keycode == BTN_TRIGGER_HAPPY2 ||
         event->keycode == BTN_TRIGGER_HAPPY3 ||
         event->keycode == BTN_TRIGGER_HAPPY4)) {
        self->handle_home_button();
        return;
    }

    // Stage 3+: also intercept gamepad button events (BTN_SOUTH, etc.)
    // for devices that expose Home as a gamepad button rather than a
    // keyboard key.

    wlr_seat_set_keyboard(self->seat_, kb->device);
    wlr_seat_keyboard_notify_key(self->seat_, event->time_msec,
                                  event->keycode, event->state);
}

void Compositor::handle_keyboard_modifiers(wl_listener* listener,
                                           void* /*data*/) {
    Keyboard* kb = wl_container_of(listener, kb, modifiers);
    Compositor* self = kb->compositor;

    wlr_seat_set_keyboard(self->seat_, kb->device);
    wlr_seat_keyboard_notify_modifiers(self->seat_,
        &kb->device->modifiers);
}

void Compositor::handle_keyboard_destroy(wl_listener* listener,
                                         void* /*data*/) {
    Keyboard* kb = wl_container_of(listener, kb, destroy);
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->modifiers.link);
    wl_list_remove(&kb->destroy.link);
    delete kb;
}

// ══════════════════════════════════════════════════════════════════════
//  XDG toplevel handlers
// ══════════════════════════════════════════════════════════════════════

void Compositor::handle_new_xdg_toplevel(wl_listener* listener, void* data) {
    Compositor* self = wl_container_of(listener, self, new_xdg_toplevel_);
    wlr_xdg_toplevel* toplevel = static_cast<wlr_xdg_toplevel*>(data);

    wlr_log(WLR_INFO, "new xdg toplevel: title='%s' app_id='%s'",
            toplevel->title ? toplevel->title : "(null)",
            toplevel->app_id ? toplevel->app_id : "(null)");

    // The first client is the shell; subsequent clients are games.
    // Stage 2+: replace with explicit surface roles (playos_shell_v1, etc.).
    if (!self->shell_toplevel_) {
        self->shell_toplevel_ = toplevel;
    } else {
        self->game_toplevel_ = toplevel;
    }

    // Place the surface in the scene tree.
    wlr_scene_xdg_surface_create(&self->scene_->tree, toplevel->base);
    toplevel->base->data = self;

    // Defer configuration until the first surface commit so the client
    // has a chance to set its initial state.
    auto* td = new ToplevelCommit{};
    td->compositor = self;
    td->toplevel   = toplevel;

    td->listener.notify = handle_toplevel_first_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &td->listener);

    td->destroy.notify = handle_toplevel_destroy;
    wl_signal_add(&toplevel->events.destroy, &td->destroy);
}

void Compositor::handle_toplevel_first_commit(wl_listener* listener,
                                              void* /*data*/) {
    ToplevelCommit* td = wl_container_of(listener, td, listener);
    Compositor* self = td->compositor;
    wlr_xdg_toplevel* toplevel = td->toplevel;
    wlr_xdg_surface* surface = toplevel->base;

    if (surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) return;

    // Reconfigure on every commit so the shell gets proper size after
    // returning from a fullscreen sample. Only reconfigure if the size
    // differs to avoid an infinite configure→commit loop.
    if (self->output_width_ > 0 && self->output_height_ > 0 &&
        (surface->geometry.width  != self->output_width_ ||
         surface->geometry.height != self->output_height_)) {
        wlr_xdg_toplevel_set_size(toplevel,
                                  self->output_width_, self->output_height_);
        wlr_xdg_toplevel_set_maximized(toplevel, true);
    }

    // Give keyboard focus.
    wlr_keyboard* kb = wlr_seat_get_keyboard(self->seat_);
    if (kb) {
        wlr_seat_keyboard_notify_enter(self->seat_,
                                       toplevel->base->surface,
                                       kb->keycodes, kb->num_keycodes,
                                       &kb->modifiers);
    }
}

void Compositor::handle_toplevel_destroy(wl_listener* listener,
                                         void* /*data*/) {
    ToplevelCommit* td = wl_container_of(listener, td, destroy);
    Compositor* self = td->compositor;

    // Clear shell/game tracking.
    if (self->shell_toplevel_ == td->toplevel) {
        self->shell_toplevel_ = nullptr;
    }
    if (self->game_toplevel_ == td->toplevel) {
        wlr_log(WLR_INFO, "game toplevel destroyed — shell regains foreground");
        self->game_toplevel_ = nullptr;
    }

    wl_list_remove(&td->listener.link);
    wl_list_remove(&td->destroy.link);
    delete td;
}

} // namespace PlayOS
