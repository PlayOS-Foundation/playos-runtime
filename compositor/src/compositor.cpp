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
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// Generated protocol headers (produced by wayland-scanner from XML).
extern "C" {
#include "playos-shell-v1-protocol.h"
#include "playos-game-v1-protocol.h"
#include "playos-compositor-control-v1-protocol.h"
}

namespace PlayOS {

// ══════════════════════════════════════════════════════════════════════
//  Custom protocol resource types
// ══════════════════════════════════════════════════════════════════════

struct PlayosShellResource {
    wl_resource* resource;
    Compositor*  compositor;
    wlr_surface* surface = nullptr;  // set by set_shell_surface
};

struct PlayosGameResource {
    wl_resource* resource;
    Compositor*  compositor;
    wlr_surface* surface = nullptr;  // set by set_game_surface
};

// ── playos_shell_v1 implementation ───────────────────────────────────

static void shell_resource_destroy(wl_resource* resource) {
    auto* r = static_cast<PlayosShellResource*>(wl_resource_get_user_data(resource));
    delete r;
}

void Compositor::shell_set_shell_surface(wl_client* /*client*/,
                                          wl_resource* resource,
                                          wl_resource* surface_resource) {
    auto* r = static_cast<PlayosShellResource*>(wl_resource_get_user_data(resource));
    Compositor* self = r->compositor;

    wlr_surface* surface = wlr_surface_from_resource(surface_resource);
    r->surface = surface;

    wlr_xdg_surface* xdg =
        wlr_xdg_surface_try_from_wlr_surface(surface);
    if (xdg && xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        if (self->shell_toplevel_ && self->shell_toplevel_ != xdg->toplevel) {
            wlr_xdg_toplevel_send_close(self->shell_toplevel_);
        }
        self->shell_toplevel_ = xdg->toplevel;
        self->pending_shell_surface_ = nullptr;
        wlr_log(WLR_INFO, "shell surface role assigned (immediate)");
    } else {
        self->pending_shell_surface_ = surface;
        wlr_log(WLR_INFO, "shell surface role pending (xdg_toplevel not yet created)");
    }
}

static const struct playos_shell_v1_interface shell_impl = {
    .set_shell_surface = Compositor::shell_set_shell_surface,
};

void Compositor::shell_bind(wl_client* client, void* data,
                             uint32_t version, uint32_t id) {
    auto* self = static_cast<Compositor*>(data);
    auto* r = new PlayosShellResource{};
    r->compositor = self;
    r->resource = wl_resource_create(
        client, &playos_shell_v1_interface, version, id);
    if (!r->resource) {
        delete r;
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(
        r->resource, &shell_impl, r, shell_resource_destroy);
    wlr_log(WLR_INFO, "playos_shell_v1 bound");
}

// ── playos_game_v1 implementation ────────────────────────────────────

static void game_resource_destroy(wl_resource* resource) {
    auto* r = static_cast<PlayosGameResource*>(wl_resource_get_user_data(resource));
    delete r;
}

void Compositor::game_set_game_surface(wl_client* /*client*/,
                                        wl_resource* resource,
                                        wl_resource* surface_resource) {
    auto* r = static_cast<PlayosGameResource*>(wl_resource_get_user_data(resource));
    Compositor* self = r->compositor;

    wlr_surface* surface = wlr_surface_from_resource(surface_resource);
    r->surface = surface;

    wlr_xdg_surface* xdg =
        wlr_xdg_surface_try_from_wlr_surface(surface);
    if (xdg && xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        if (self->game_toplevel_ && self->game_toplevel_ != xdg->toplevel) {
            wlr_xdg_toplevel_send_close(self->game_toplevel_);
        }
        self->game_toplevel_ = xdg->toplevel;
        self->pending_game_surface_ = nullptr;
        wlr_log(WLR_INFO, "game surface role assigned (immediate)");
    } else {
        self->pending_game_surface_ = surface;
        wlr_log(WLR_INFO, "game surface role pending (xdg_toplevel not yet created)");
    }
}

static const struct playos_game_v1_interface game_impl = {
    .set_game_surface = Compositor::game_set_game_surface,
};

void Compositor::game_bind(wl_client* client, void* data,
                            uint32_t version, uint32_t id) {
    auto* self = static_cast<Compositor*>(data);
    auto* r = new PlayosGameResource{};
    r->compositor = self;
    r->resource = wl_resource_create(
        client, &playos_game_v1_interface, version, id);
    if (!r->resource) {
        delete r;
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(
        r->resource, &game_impl, r, game_resource_destroy);
    wlr_log(WLR_INFO, "playos_game_v1 bound");
}

// ══════════════════════════════════════════════════════════════════════
//  playos_compositor_control_v1 implementation
// ══════════════════════════════════════════════════════════════════════

struct ControlResource {
    wl_resource* resource;
    Compositor*  compositor;
};

void Compositor::control_activate_shell(wl_client* /*client*/,
                                         wl_resource* resource) {
    auto* r = static_cast<ControlResource*>(wl_resource_get_user_data(resource));
    // Reuse the existing Home-button logic: close active game → shell
    // regains foreground.  The game_closed event will be sent by
    // handle_toplevel_destroy when the game toplevel is destroyed.
    r->compositor->handle_home_button();
    wlr_log(WLR_INFO, "control: activate_shell requested");
}

static const struct playos_compositor_control_v1_interface control_impl = {
    .activate_shell = Compositor::control_activate_shell,
};

void Compositor::control_resource_destroy(wl_resource* resource) {
    auto* r = static_cast<ControlResource*>(wl_resource_get_user_data(resource));
    if (r->compositor->control_resource_ == resource) {
        r->compositor->control_resource_ = nullptr;
        wlr_log(WLR_INFO, "compositor control client disconnected");
    }
    delete r;
}

void Compositor::control_bind(wl_client* client, void* data,
                               uint32_t version, uint32_t id) {
    auto* self = static_cast<Compositor*>(data);

    // Enforce single-control-client policy.
    if (self->control_resource_) {
        wlr_log(WLR_ERROR, "compositor control: only one control client allowed");
        wl_client_post_implementation_error(client,
            "playos_compositor_control_v1 already bound by another client");
        return;
    }

    auto* r = new ControlResource{};
    r->compositor = self;
    r->resource = wl_resource_create(
        client, &playos_compositor_control_v1_interface, version, id);
    if (!r->resource) {
        delete r;
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(
        r->resource, &control_impl, r, control_resource_destroy);
    self->control_resource_ = r->resource;
    wlr_log(WLR_INFO, "playos_compositor_control_v1 bound (runtime connected)");
}

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

    // Build the 4-layer z-ordered scene tree per compositor model §4.
    // Children are rendered first→last, so bottom layer first, top last.
    background_layer_ = wlr_scene_tree_create(&scene_->tree);
    game_layer_       = wlr_scene_tree_create(&scene_->tree);
    overlay_layer_    = wlr_scene_tree_create(&scene_->tree);
    system_layer_     = wlr_scene_tree_create(&scene_->tree);

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

    setup_custom_protocols();
}

void Compositor::setup_custom_protocols() {
    // ── playos_shell_v1 ──────────────────────────────────────────
    playos_shell_global_ = wl_global_create(
        display_, &playos_shell_v1_interface, 1, this, shell_bind);

    // ── playos_game_v1 ───────────────────────────────────────────
    playos_game_global_ = wl_global_create(
        display_, &playos_game_v1_interface, 1, this, game_bind);

    // ── playos_compositor_control_v1 ─────────────────────────────
    playos_control_global_ = wl_global_create(
        display_, &playos_compositor_control_v1_interface, 1, this,
        control_bind);

    wlr_log(WLR_INFO, "custom protocols registered: playos_shell_v1, "
            "playos_game_v1, playos_compositor_control_v1");
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

    // Register a SIGCHLD handler on the Wayland event loop so we can
    // detect when the shell (or other child processes) exits.
    wl_event_loop* loop = wl_display_get_event_loop(display_);
    sigchld_source_ = wl_event_loop_add_signal(
        loop, SIGCHLD, handle_sigchld, this);

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

    // Track the shell process so the SIGCHLD handler can detect when
    // it exits or crashes and respawn it.
    shell_pid_ = pid;
    shell_cmd_ = cmd;
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

void Compositor::respawn_shell() {
    if (shell_cmd_.empty()) return;
    wlr_log(WLR_INFO, "respawning shell: %s", shell_cmd_.c_str());
    spawn_shell(shell_cmd_.c_str());
}

int Compositor::handle_sigchld(int /*signal_number*/, void* data) {
    Compositor* self = static_cast<Compositor*>(data);

    // Reap all dead children so we don't leave zombies.
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (pid == self->shell_pid_) {
            if (WIFEXITED(status)) {
                wlr_log(WLR_INFO,
                        "shell process %d exited with status %d",
                        pid, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                wlr_log(WLR_INFO,
                        "shell process %d killed by signal %d (%s)",
                        pid, WTERMSIG(status), strsignal(WTERMSIG(status)));
            }
            self->shell_pid_ = -1;

            // Respawn the shell. The previous shell's Wayland resources
            // are cleaned up by handle_toplevel_destroy when the client
            // disconnects. The new shell instance will bind playos_shell_v1
            // and create a fresh xdg_toplevel.
            self->respawn_shell();
        } else {
            wlr_log(WLR_INFO, "child process %d reaped (status %d)",
                    pid, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        }
    }
    return 0;
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
    case WLR_INPUT_DEVICE_POINTER: {
        auto* ptr = new Pointer{};
        ptr->device = wlr_pointer_from_input_device(device);
        ptr->compositor = self;

        ptr->motion.notify = handle_pointer_motion;
        wl_signal_add(&ptr->device->events.motion, &ptr->motion);

        ptr->motion_absolute.notify = handle_pointer_motion_absolute;
        wl_signal_add(&ptr->device->events.motion_absolute,
                      &ptr->motion_absolute);

        ptr->button.notify = handle_pointer_button;
        wl_signal_add(&ptr->device->events.button, &ptr->button);

        ptr->axis.notify = handle_pointer_axis;
        wl_signal_add(&ptr->device->events.axis, &ptr->axis);

        ptr->frame.notify = handle_pointer_frame;
        wl_signal_add(&ptr->device->events.frame, &ptr->frame);

        ptr->destroy.notify = handle_pointer_destroy;
        wl_signal_add(&device->events.destroy, &ptr->destroy);

        wlr_log(WLR_INFO, "pointer connected");
        break;
    }
    case WLR_INPUT_DEVICE_TOUCH: {
        auto* tch = new Touch{};
        tch->device = wlr_touch_from_input_device(device);
        tch->compositor = self;

        tch->down.notify = handle_touch_down;
        wl_signal_add(&tch->device->events.down, &tch->down);

        tch->up.notify = handle_touch_up;
        wl_signal_add(&tch->device->events.up, &tch->up);

        tch->motion.notify = handle_touch_motion;
        wl_signal_add(&tch->device->events.motion, &tch->motion);

        tch->cancel.notify = handle_touch_cancel;
        wl_signal_add(&tch->device->events.cancel, &tch->cancel);

        tch->destroy.notify = handle_touch_destroy;
        wl_signal_add(&device->events.destroy, &tch->destroy);

        wlr_log(WLR_INFO, "touch connected");
        break;
    }
    default:
        break;
    }

    // Advertise available seat capabilities.
    uint32_t caps = WL_SEAT_CAPABILITY_KEYBOARD;
    caps |= WL_SEAT_CAPABILITY_POINTER;
    caps |= WL_SEAT_CAPABILITY_TOUCH;
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
//  Pointer handlers
// ══════════════════════════════════════════════════════════════════════

void Compositor::handle_pointer_motion(wl_listener* listener, void* data) {
    Pointer* ptr = wl_container_of(listener, ptr, motion);
    Compositor* self = ptr->compositor;
    auto* event = static_cast<wlr_pointer_motion_event*>(data);

    // Update stored cursor position.
    self->cursor_x_ += event->delta_x;
    self->cursor_y_ += event->delta_y;

    // Clamp to output bounds.
    if (self->cursor_x_ < 0) self->cursor_x_ = 0;
    if (self->cursor_y_ < 0) self->cursor_y_ = 0;
    if (self->output_width_ > 0 && self->cursor_x_ >= self->output_width_)
        self->cursor_x_ = self->output_width_ - 1;
    if (self->output_height_ > 0 && self->cursor_y_ >= self->output_height_)
        self->cursor_y_ = self->output_height_ - 1;

    // Scene-surface lookup: find the surface under the cursor.
    double sx = 0, sy = 0;
    wlr_surface* surface = nullptr;
    if (self->scene_) {
        wlr_scene_node* node = wlr_scene_node_at(
            &self->scene_->tree.node, self->cursor_x_, self->cursor_y_, &sx, &sy);
        if (node && node->type == WLR_SCENE_NODE_BUFFER) {
            wlr_scene_buffer* buf = wlr_scene_buffer_from_node(node);
            wlr_scene_surface* ss = wlr_scene_surface_try_from_buffer(buf);
            if (ss) surface = ss->surface;
        }
    }

    // Update pointer focus if the surface under the cursor changed.
    if (surface != self->focused_surface_) {
        wlr_seat_pointer_notify_clear_focus(self->seat_);
        if (surface) {
            wlr_seat_pointer_notify_enter(self->seat_, surface, sx, sy);
        }
        self->focused_surface_ = surface;
    }

    wlr_seat_pointer_notify_motion(
        self->seat_, event->time_msec, event->delta_x, event->delta_y);
}

void Compositor::handle_pointer_motion_absolute(wl_listener* listener,
                                                 void* data) {
    Pointer* ptr = wl_container_of(listener, ptr, motion_absolute);
    Compositor* self = ptr->compositor;
    auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);

    // Map absolute coordinates (0..1) to output dimensions.
    self->cursor_x_ = event->x * self->output_width_;
    self->cursor_y_ = event->y * self->output_height_;

    // Scene-surface lookup.
    double sx = 0, sy = 0;
    wlr_surface* surface = nullptr;
    if (self->scene_) {
        wlr_scene_node* node = wlr_scene_node_at(
            &self->scene_->tree.node, self->cursor_x_, self->cursor_y_, &sx, &sy);
        if (node && node->type == WLR_SCENE_NODE_BUFFER) {
            wlr_scene_buffer* buf = wlr_scene_buffer_from_node(node);
            wlr_scene_surface* ss = wlr_scene_surface_try_from_buffer(buf);
            if (ss) surface = ss->surface;
        }
    }

    // Update pointer focus if the surface under the cursor changed.
    if (surface != self->focused_surface_) {
        wlr_seat_pointer_notify_clear_focus(self->seat_);
        if (surface) {
            wlr_seat_pointer_notify_enter(self->seat_, surface, sx, sy);
        }
        self->focused_surface_ = surface;
    }

    wlr_seat_pointer_notify_motion(
        self->seat_, event->time_msec, self->cursor_x_, self->cursor_y_);
}

void Compositor::handle_pointer_button(wl_listener* listener, void* data) {
    Pointer* ptr = wl_container_of(listener, ptr, button);
    Compositor* self = ptr->compositor;
    auto* event = static_cast<wlr_pointer_button_event*>(data);

    wlr_seat_pointer_notify_button(
        self->seat_, event->time_msec, event->button, event->state);
}

void Compositor::handle_pointer_axis(wl_listener* listener, void* data) {
    Pointer* ptr = wl_container_of(listener, ptr, axis);
    Compositor* self = ptr->compositor;
    auto* event = static_cast<wlr_pointer_axis_event*>(data);

    wlr_seat_pointer_notify_axis(self->seat_, event->time_msec,
        event->orientation, event->delta, event->delta_discrete,
        event->source, event->relative_direction);
}

void Compositor::handle_pointer_frame(wl_listener* listener, void* /*data*/) {
    Pointer* ptr = wl_container_of(listener, ptr, frame);
    Compositor* self = ptr->compositor;

    wlr_seat_pointer_notify_frame(self->seat_);
}

void Compositor::handle_pointer_destroy(wl_listener* listener,
                                         void* /*data*/) {
    Pointer* ptr = wl_container_of(listener, ptr, destroy);
    wl_list_remove(&ptr->motion.link);
    wl_list_remove(&ptr->motion_absolute.link);
    wl_list_remove(&ptr->button.link);
    wl_list_remove(&ptr->axis.link);
    wl_list_remove(&ptr->frame.link);
    wl_list_remove(&ptr->destroy.link);
    delete ptr;
}

// ══════════════════════════════════════════════════════════════════════
//  Touch handlers
// ══════════════════════════════════════════════════════════════════════

void Compositor::handle_touch_down(wl_listener* listener, void* data) {
    Touch* tch = wl_container_of(listener, tch, down);
    Compositor* self = tch->compositor;
    auto* event = static_cast<wlr_touch_down_event*>(data);

    // Route touch to the currently focused surface. On a fullscreen
    // console compositor this is the right target; a future stage may
    // add per-coordinate surface lookup via wlr_scene.
    double sx = event->x;
    double sy = event->y;
    if (self->focused_surface_) {
        wlr_seat_touch_notify_down(self->seat_, self->focused_surface_,
                                    event->time_msec, event->touch_id,
                                    sx, sy);
    }
}

void Compositor::handle_touch_up(wl_listener* listener, void* data) {
    Touch* tch = wl_container_of(listener, tch, up);
    Compositor* self = tch->compositor;
    auto* event = static_cast<wlr_touch_up_event*>(data);

    wlr_seat_touch_notify_up(self->seat_, event->time_msec,
                              event->touch_id);
}

void Compositor::handle_touch_motion(wl_listener* listener, void* data) {
    Touch* tch = wl_container_of(listener, tch, motion);
    Compositor* self = tch->compositor;
    auto* event = static_cast<wlr_touch_motion_event*>(data);

    double sx = event->x;
    double sy = event->y;
    wlr_seat_touch_notify_motion(self->seat_, event->time_msec,
                                  event->touch_id, sx, sy);
}

void Compositor::handle_touch_cancel(wl_listener* listener, void* data) {
    Touch* tch = wl_container_of(listener, tch, cancel);
    Compositor* self = tch->compositor;

    wlr_seat_client* seat_client = nullptr;
    if (self->focused_surface_) {
        wl_client* wl = wl_resource_get_client(self->focused_surface_->resource);
        seat_client = wlr_seat_client_for_wl_client(self->seat_, wl);
    }
    wlr_seat_touch_notify_cancel(self->seat_, seat_client);
}

void Compositor::handle_touch_destroy(wl_listener* listener,
                                       void* /*data*/) {
    Touch* tch = wl_container_of(listener, tch, destroy);
    wl_list_remove(&tch->down.link);
    wl_list_remove(&tch->up.link);
    wl_list_remove(&tch->motion.link);
    wl_list_remove(&tch->cancel.link);
    wl_list_remove(&tch->destroy.link);
    delete tch;
}

// ══════════════════════════════════════════════════════════════════════
//  XDG toplevel handlers
// ══════════════════════════════════════════════════════════════════════

void Compositor::handle_new_xdg_toplevel(wl_listener* listener, void* data) {
    Compositor* self = wl_container_of(listener, self, new_xdg_toplevel_);
    wlr_xdg_toplevel* toplevel = static_cast<wlr_xdg_toplevel*>(data);
    wlr_surface* surface = toplevel->base->surface;

    wlr_log(WLR_INFO, "new xdg toplevel: title='%s' app_id='%s'",
            toplevel->title ? toplevel->title : "(null)",
            toplevel->app_id ? toplevel->app_id : "(null)");

    // Resolve pending surface role assignments from custom protocols.
    // A client may call set_shell_surface / set_game_surface before its
    // xdg_toplevel exists; we catch the match here.
    if (self->pending_shell_surface_ == surface) {
        if (self->shell_toplevel_ && self->shell_toplevel_ != toplevel) {
            wlr_xdg_toplevel_send_close(self->shell_toplevel_);
        }
        self->shell_toplevel_ = toplevel;
        self->pending_shell_surface_ = nullptr;
        wlr_log(WLR_INFO, "shell surface role resolved (pending → toplevel)");
    } else if (self->pending_game_surface_ == surface) {
        if (self->game_toplevel_ && self->game_toplevel_ != toplevel) {
            wlr_xdg_toplevel_send_close(self->game_toplevel_);
        }
        self->game_toplevel_ = toplevel;
        self->pending_game_surface_ = nullptr;
        wlr_log(WLR_INFO, "game surface role resolved (pending → toplevel)");
    } else {
        wlr_log(WLR_INFO, "xdg toplevel without explicit role — "
                "client must bind playos_shell_v1 or playos_game_v1");
    }

    // Place the surface in the appropriate scene layer.
    wlr_scene_tree* target_layer = self->background_layer_;  // default
    if (self->game_toplevel_ == toplevel) {
        target_layer = self->game_layer_;
    }
    wlr_scene_xdg_surface_create(target_layer, toplevel->base);
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

    // Set pointer focus on the same surface so the client receives
    // pointer motion, button, and axis events.
    self->focused_surface_ = toplevel->base->surface;
    wlr_seat_pointer_notify_enter(self->seat_, self->focused_surface_, 0, 0);
    wlr_seat_pointer_notify_frame(self->seat_);
}

void Compositor::handle_toplevel_destroy(wl_listener* listener,
                                         void* /*data*/) {
    ToplevelCommit* td = wl_container_of(listener, td, destroy);
    Compositor* self = td->compositor;
    wlr_surface* surface = td->toplevel->base->surface;

    // Clear shell/game tracking.
    if (self->shell_toplevel_ == td->toplevel) {
        self->shell_toplevel_ = nullptr;
        if (self->control_resource_) {
            playos_compositor_control_v1_send_shell_closed(
                self->control_resource_);
            wlr_log(WLR_INFO, "→ control: shell_closed event sent");
        }
    }
    if (self->game_toplevel_ == td->toplevel) {
        wlr_log(WLR_INFO, "game toplevel destroyed — shell regains foreground");
        self->game_toplevel_ = nullptr;
        if (self->control_resource_) {
            playos_compositor_control_v1_send_game_closed(
                self->control_resource_);
            wlr_log(WLR_INFO, "→ control: game_closed event sent");
        }
    }

    // Clear pending surface role assignments.
    if (self->pending_shell_surface_ == surface) {
        self->pending_shell_surface_ = nullptr;
    }
    if (self->pending_game_surface_ == surface) {
        self->pending_game_surface_ = nullptr;
    }

    // Clear pointer/keyboard focus if this was the focused surface.
    if (self->focused_surface_ == surface) {
        self->focused_surface_ = nullptr;
        wlr_seat_pointer_notify_clear_focus(self->seat_);
        wlr_seat_keyboard_notify_clear_focus(self->seat_);
    }

    wl_list_remove(&td->listener.link);
    wl_list_remove(&td->destroy.link);
    delete td;
}

} // namespace PlayOS
