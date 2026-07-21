// PlayOS Compositor — C++ RAII compositor on wlroots (Stage 1).
//
// Replaces the TinyWL-derived C skeleton (playos-compositor.c) with a
// modular C++17 compositor per ADR-0002. Matches Stage 1 behaviour exactly:
// display bring-up, fullscreen shell, keyboard input, game launch/return.
//
// Targets wlroots 0.19. Calls flagged VERSION-SENSITIVE may need adjustment
// for other wlroots versions (consult upstream tinywl.c for the matching
// release).

#ifndef PLAYOS_COMPOSITOR_COMPOSITOR_HPP
#define PLAYOS_COMPOSITOR_COMPOSITOR_HPP

// WLR_USE_UNSTABLE is set via CMake (target_compile_definitions).
// Do not redefine it here.

// wlroots is a C library; wrap includes so the linker sees C linkage.
extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
}

// wlr_scene.h uses C99 `static` in array parameter declarations (not
// valid C++), so it is included only in the .cpp file with a workaround.
// Forward-declare the types we reference here.
struct wlr_scene;
struct wlr_scene_tree;
struct wlr_scene_output_layout;
struct wlr_scene_output;

#include <string>

namespace PlayOS {

/// A console-focused Wayland compositor built on wlroots.
///
/// Owns the display, backend, renderer, allocator, scene graph, seat,
/// and protocol handlers. The destructor tears down resources in reverse
/// dependency order so RAII is the single cleanup path.
///
/// Stage 1 capabilities:
///   - DRM/KMS or nested backend
///   - Fullscreen shell as a Wayland client
///   - Keyboard input routed through wlr_seat
///   - One launched game; returns to shell on exit
///
/// Stage 2+ extension points are called out in comments.
class Compositor {
public:
    Compositor();
    ~Compositor();

    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    /// Create the backend, renderer, allocator, scene graph, protocols,
    /// and seat. Returns true on success; logs errors on failure.
    bool init();

    /// Start the backend, spawn the shell, and enter the Wayland event
    /// loop. Blocks until the display shuts down.
    /// @param shell_cmd  Shell command to launch (may be nullptr).
    void run(const char* shell_cmd);

private:
    // ── wlroots / Wayland state ──────────────────────────────────────
    wl_display*      display_        = nullptr;
    wlr_backend*     backend_        = nullptr;
    wlr_renderer*    renderer_       = nullptr;
    wlr_allocator*   allocator_      = nullptr;
    wlr_scene*       scene_          = nullptr;
    wlr_output_layout* output_layout_ = nullptr;
    wlr_scene_output_layout* scene_layout_ = nullptr;
    wlr_xdg_shell*   xdg_shell_      = nullptr;
    wlr_seat*        seat_           = nullptr;

    /// Cached output dimensions for toplevel configure.
    int output_width_  = 0;
    int output_height_ = 0;

    /// Track which toplevel is the shell vs the active game.
    /// Assigned via playos_shell_v1 / playos_game_v1 protocol
    /// requests rather than the old first-client-is-shell heuristic.
    wlr_xdg_toplevel* shell_toplevel_ = nullptr;
    wlr_xdg_toplevel* game_toplevel_  = nullptr;

    /// Pending surface role assignments. When a client calls
    /// set_shell_surface / set_game_surface before the xdg_toplevel
    /// exists, the surface pointer is stored here.  Cleared once the
    /// matching xdg_toplevel appears in handle_new_xdg_toplevel.
    wlr_surface* pending_shell_surface_ = nullptr;
    wlr_surface* pending_game_surface_  = nullptr;

    /// Custom protocol globals — created in setup_protocols(),
    /// destroyed by wl_display_destroy.
    wl_global* playos_shell_global_   = nullptr;
    wl_global* playos_game_global_    = nullptr;
    wl_global* playos_control_global_ = nullptr;

    /// The runtime's control resource (at most one client).
    /// Used to send game_closed / shell_closed events.
    wl_resource* control_resource_ = nullptr;

    /// Surface currently holding keyboard + pointer focus.
    wlr_surface* focused_surface_ = nullptr;

    /// Cursor position in layout coordinates, updated on pointer motion.
    double cursor_x_ = 0.0;
    double cursor_y_ = 0.0;

    /// Layer sub-trees (bottom-to-top z-order per compositor model §4).
    /// Surfaces are placed into the appropriate layer based on their role.
    wlr_scene_tree* background_layer_ = nullptr;  // PlayOS Shell
    wlr_scene_tree* game_layer_       = nullptr;  // active game
    wlr_scene_tree* overlay_layer_    = nullptr;  // quick menu, HUD, OSD, VK
    wlr_scene_tree* system_layer_     = nullptr;  // crash / emergency UI

    /// Shell process tracking for crash recovery.
    /// When the shell exits unexpectedly, the compositor respawns it.
    pid_t        shell_pid_   = -1;
    std::string  shell_cmd_;
    wl_event_source* sigchld_source_ = nullptr;

    // ── Per-device state (owned via wl_listener destroy handlers) ────

    struct Output {
        wlr_output*  output;
        Compositor*  compositor;
        wl_listener  frame;
    };

    struct Keyboard {
        wlr_keyboard* device;
        Compositor*   compositor;
        wl_listener   key;
        wl_listener   modifiers;
        wl_listener   destroy;
    };

    struct Pointer {
        wlr_pointer* device;
        Compositor*  compositor;
        wl_listener  motion;
        wl_listener  motion_absolute;
        wl_listener  button;
        wl_listener  axis;
        wl_listener  frame;
        wl_listener  destroy;
    };

    struct Touch {
        wlr_touch*  device;
        Compositor* compositor;
        wl_listener down;
        wl_listener up;
        wl_listener motion;
        wl_listener cancel;
        wl_listener destroy;
    };

    /// Per-toplevel state used to defer configuration until the first
    /// surface commit. Freed in the toplevel destroy handler.
    struct ToplevelCommit {
        wl_listener       listener;
        wl_listener       destroy;
        Compositor*       compositor;
        wlr_xdg_toplevel* toplevel;
    };

    // ── wlroots event listeners ──────────────────────────────────────

    wl_listener new_output_;
    wl_listener new_xdg_toplevel_;
    wl_listener new_input_;

    // ── Event handlers (static, use wl_container_of) ──────────────────

    static void handle_new_output(wl_listener* listener, void* data);
    static void handle_new_xdg_toplevel(wl_listener* listener, void* data);
    static void handle_new_input(wl_listener* listener, void* data);

    // Device-level handlers
    static void handle_output_frame(wl_listener* listener, void* data);
    static void handle_keyboard_key(wl_listener* listener, void* data);
    static void handle_keyboard_modifiers(wl_listener* listener, void* data);
    static void handle_keyboard_destroy(wl_listener* listener, void* data);

    static void handle_pointer_motion(wl_listener* listener, void* data);
    static void handle_pointer_motion_absolute(wl_listener* listener, void* data);
    static void handle_pointer_button(wl_listener* listener, void* data);
    static void handle_pointer_axis(wl_listener* listener, void* data);
    static void handle_pointer_frame(wl_listener* listener, void* data);
    static void handle_pointer_destroy(wl_listener* listener, void* data);

    static void handle_touch_down(wl_listener* listener, void* data);
    static void handle_touch_up(wl_listener* listener, void* data);
    static void handle_touch_motion(wl_listener* listener, void* data);
    static void handle_touch_cancel(wl_listener* listener, void* data);
    static void handle_touch_destroy(wl_listener* listener, void* data);

    // Toplevel commit handlers
    static void handle_toplevel_first_commit(wl_listener* listener, void* data);
    static void handle_toplevel_destroy(wl_listener* listener, void* data);

    // Crash recovery
    static int handle_sigchld(int signal_number, void* data);

    // ── Internal helpers ─────────────────────────────────────────────

    void setup_scene();
    void setup_protocols();
    void spawn_shell(const char* cmd);

    /// Create the playos_shell_v1 and playos_game_v1 globals.
    void setup_custom_protocols();

    /// Intercept the Home (Armoury) button globally: if a game is
    /// active, close it so the shell regains the foreground.
    /// The game toplevel pointer is cleared by the destroy handler.
    void handle_home_button();

    /// Re-launch the shell after it has exited or crashed.
    void respawn_shell();

public:
    // ── Custom protocol handlers (static, use wl_resource_get_user_data) ──

    // playos_shell_v1
    static void shell_bind(wl_client* client, void* data,
                           uint32_t version, uint32_t id);
    static void shell_set_shell_surface(wl_client* client,
                                        wl_resource* resource,
                                        wl_resource* surface_resource);

    // playos_game_v1
    static void game_bind(wl_client* client, void* data,
                          uint32_t version, uint32_t id);
    static void game_set_game_surface(wl_client* client,
                                      wl_resource* resource,
                                      wl_resource* surface_resource);

    // playos_compositor_control_v1
    static void control_bind(wl_client* client, void* data,
                             uint32_t version, uint32_t id);
    static void control_activate_shell(wl_client* client,
                                        wl_resource* resource);
    static void control_resource_destroy(wl_resource* resource);
};

} // namespace PlayOS

#endif // PLAYOS_COMPOSITOR_COMPOSITOR_HPP
