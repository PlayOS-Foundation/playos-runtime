/**
 * trusted_control.h — PlayOS Trusted Control IPC Client
 *
 * Wraps /run/playos/control.sock IPC for privileged operations.
 * Only linked by trusted system components (shell, overlay), never by games.
 *
 * The server uses SOCK_SEQPACKET and handles one request per connection.
 * Each function connects, sends, receives the response, and closes.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PLAYOS_TRUSTED_CONTROL_H
#define PLAYOS_TRUSTED_CONTROL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Connection management ──────────────────────────────────────── */

/**
 * Connect to the playos-init control socket.
 *
 * @return  fd on success, -1 on error.
 */
int playos_trusted_connect(void);

/**
 * Close a trusted control connection.
 */
void playos_trusted_disconnect(int fd);

/* ── Persistent shell listener (Sprint 7) ────────────────────────── */

/* Async event types delivered to a registered shell listener. Values
 * match the wire "type" strings defined in the shared IPC header. */
#define PLAYOS_TRUSTED_EVENT_GAME_STARTED              "GameStarted"
#define PLAYOS_TRUSTED_EVENT_GAME_EXITED               "GameExited"
#define PLAYOS_TRUSTED_EVENT_GAME_CRASHED              "GameCrashed"
#define PLAYOS_TRUSTED_EVENT_COMPOSITOR_STATE_CHANGED  "CompositorStateChanged"
#define PLAYOS_TRUSTED_EVENT_THERMAL_STATE_CHANGED     "ThermalStateChanged"
#define PLAYOS_TRUSTED_EVENT_PERF_PROFILE_CHANGED      "PerfProfileChanged"
#define PLAYOS_TRUSTED_EVENT_UPDATE_PROGRESS           "UpdateProgress"
#define PLAYOS_TRUSTED_EVENT_UPDATE_COMPLETE           "UpdateComplete"
/* S14.5-T4: install events relayed by init from the screen-less worker. */
#define PLAYOS_TRUSTED_EVENT_INSTALL_PROGRESS          "InstallProgress"
#define PLAYOS_TRUSTED_EVENT_INSTALL_COMPLETE          "InstallComplete"
#define PLAYOS_TRUSTED_EVENT_INSTALL_ERROR             "InstallError"
#define PLAYOS_TRUSTED_EVENT_UPDATE_ERROR              "UpdateError"

/**
 * Register this process as the persistent shell event listener.
 *
 * Connects to /run/playos/control.sock, sends ShellReady, and KEEPS the
 * connection open. playos-init promotes this fd to its shell_listener_fd
 * and streams asynchronous GameStarted/GameExited/GameCrashed events to
 * it. There is no response to ShellReady — success is a live connection.
 *
 * @return  fd on success (caller owns it, poll with
 *          playos_trusted_shell_poll(), close with
 *          playos_trusted_disconnect()), or -1 on error.
 */
int playos_trusted_register_shell(void);

/**
 * Non-blocking poll of the shell listener fd for one async event.
 *
 * @param fd          Listener fd from playos_trusted_register_shell().
 * @param type_buf    Buffer to receive the event "type" string (optional).
 * @param type_bufsz  Size of type_buf.
 * @return            1 if an event was received (type copied to type_buf),
 *                    0 if no event is pending,
 *                    -1 on error or if the server closed the connection.
 */
int playos_trusted_shell_poll(int fd, char *type_buf, size_t type_bufsz);

/* S14.5-T4: as above, but also returns the event payload (json_buf), which is
 * what carries an install step's number, name and percent. */
int playos_trusted_shell_poll_json(int fd, char *type_buf, size_t type_bufsz,
                                   char *json_buf, size_t json_bufsz);

/* ── Operations ─────────────────────────────────────────────────── */

/**
 * Request game launch via IPC.
 *
 * Sends: {"v":1,"type":"LaunchGame","game_id":"<id>"}
 * Expects response: LaunchGameAck or LaunchGameError
 *
 * @param fd       Connected socket fd.
 * @param game_id  Game identifier to launch.
 * @return         0 on success (LaunchGameAck received), -1 on error.
 */
int playos_trusted_launch_game(int fd, const char *game_id);

/**
 * Request game termination via IPC.
 *
 * Sends: {"v":1,"type":"TerminateGame"}
 * Expects response: TerminateGameAck
 *
 * @param fd  Connected socket fd.
 * @return    0 on success, -1 on error.
 */
int playos_trusted_terminate_game(int fd);

/**
 * Query system status via IPC.
 *
 * Sends: {"v":1,"type":"QueryStatus"}
 * Expects: StatusReport JSON in status_buf.
 *
 * @param fd          Connected socket fd.
 * @param status_buf  Buffer to receive status JSON.
 * @param bufsz       Size of status_buf.
 * @return            0 on success, -1 on error.
 */
int playos_trusted_query_status(int fd, char *status_buf, size_t bufsz);

/* ── Networking (Sprint 16, T6) ──────────────────────────────────────────
 * Wi-Fi requests ride the same socket: init relays each one to the trusted
 * playos-net bridge and returns the bridge's JSON reply unchanged, so the
 * shell never talks to wpa_supplicant (or the bridge) directly.
 *
 * Each writes the reply body (JSON, as documented in runtime-ipc.md) into
 * json_buf and returns its length, or -1 on failure.
 */

/** {"v":1,"type":"ScanNetworks"} → ScanResults{networks[]} */
int playos_trusted_scan_networks(int fd, char *json_buf, size_t bufsz);

/** {"v":1,"type":"ConnectNetwork","ssid":…,"psk":…,"security":…}
 *  → ConnectNetworkAck | ConnectNetworkError{reason} */
int playos_trusted_connect_network(int fd, const char *ssid, const char *psk,
                                   const char *security,
                                   char *json_buf, size_t bufsz);

/** {"v":1,"type":"DisconnectNetwork"} → DisconnectNetworkAck */
int playos_trusted_disconnect_network(int fd, char *json_buf, size_t bufsz);

/** {"v":1,"type":"NetworkStatus"} → NetworkStatusReport{state,ssid,ip,…} */
int playos_trusted_network_status(int fd, char *json_buf, size_t bufsz);

/**
 * Request system shutdown via IPC.
 *
 * Sends: {"v":1,"type":"Shutdown"}
 *
 * @param fd  Connected socket fd.
 * @return    0 on success, -1 on error.
 */
int playos_trusted_shutdown(int fd);

/**
 * Request system reboot via IPC.
 *
 * Sends: {"v":1,"type":"Reboot"}
 *
 * @param fd  Connected socket fd.
 * @return    0 on success, -1 on error.
 */
int playos_trusted_reboot(int fd);

/**
 * Request the runtime installer handoff via IPC (Sprint 13.7).
 *
 * Sends: {"v":1,"type":"StartInstaller"}
 *
 * @param fd  Connected socket fd (pass -1 to open a fresh connection).
 * @return    0 on success, -1 on error.
 */
int playos_trusted_start_installer(int fd);

/**
 * Start the runtime installer for a specific target disk (S14-T10).
 *
 * The shell's installer front-end picks the disk; passing it here lets init
 * hand it to the installer child (PLAYOS_INSTALL_TARGET) so the destructive
 * phase begins immediately instead of asking the user again.
 *
 * @param fd           Connected socket fd (pass -1 to open a fresh connection).
 * @param target_disk  Block device path such as "/dev/nvme0n1", or NULL/"" to
 *                     let the installer show its own disk picker.
 * @return             0 on success, -1 on error.
 */
int playos_trusted_start_installer_target(int fd, const char *target_disk,
                                          const char *payload_device);

/* S14.5-T2: validate an install target and release its mounts before the shell
 * commits to a progress screen. Returns 0 when the request was sent. */
int playos_trusted_prepare_install(int fd, const char *target_disk,
                                   char *err, size_t errlen);

/* S14.5-T3: report install progress and outcome from the screen-less worker.
 * init relays these to the shell listener as InstallProgress / InstallComplete /
 * InstallError. */
int playos_trusted_install_progress(int fd, int step, int percent, const char *step_name);
int playos_trusted_install_complete(int fd);
int playos_trusted_install_error(int fd, int step, const char *reason);

/**
 * Request a performance profile change via IPC (Sprint 9).
 *
 * Sends: {"v":1,"type":"SetPerfProfile","profile":"<name>"}
 * Profile is 0=balanced, 1=power_save, 2=performance.
 *
 * @param fd       Connected socket fd.
 * @param profile  Desired profile (PlayOSPerfProfile enum value).
 * @return         0 if accepted, -1 if denied or on error.
 */
int playos_trusted_set_perf_profile(int fd, int profile);

/**
 * Request system suspend (S3) via IPC (Sprint 9).
 *
 * Sends: {"v":1,"type":"Suspend"}
 * playos-init delivers SUSPEND to the active game, writes "mem" to
 * /sys/power/state, and delivers RESUME after wake.
 *
 * @param fd  Connected socket fd.
 * @return    0 on success, -1 on error.
 */
int playos_trusted_suspend(int fd);

/**
 * Show the system overlay (pause menu) over the running game (Sprint 14).
 *
 * Fire-and-forget: shell -> init -> compositor (ShowOverlay).
 *
 * @param fd  Connected socket fd (ignored; opens its own per operation).
 * @return    0 on success, -1 on error.
 */
int playos_trusted_show_overlay(int fd);

/**
 * Hide the system overlay (resume the game) (Sprint 14).
 *
 * Fire-and-forget: shell -> init -> compositor (HideOverlay).
 *
 * @param fd  Connected socket fd (ignored; opens its own per operation).
 * @return    0 on success, -1 on error.
 */
int playos_trusted_hide_overlay(int fd);

/**
 * Roll back to the other A/B system slot and reboot (Sprint 14 recovery menu).
 *
 * Fire-and-forget: shell -> init (RollbackSlot). init applies the full
 * rollback semantics to /EFI/playos/boot.json (mark the current slot bad,
 * the target slot pending, reset its boot count) and reboots; the shell must
 * never rewrite boot.json itself.
 *
 * @param fd  Connected socket fd (ignored; opens its own per operation).
 * @return    0 on success, -1 on error.
 */
int playos_trusted_rollback_slot(int fd);

/**
 * Request a factory reset via IPC (Sprint 10).
 *
 * Sends:
 *   {"v":1,"type":"FactoryReset",
 *    "erase_games":<0|1>,
 *    "erase_saves":<0|1>,
 *    "erase_cache":<0|1>,
 *    "erase_config":<0|1>,
 *    "erase_logs":<0|1>}
 *
 * playos-init refuses the request if a game is running (FactoryResetError,
 * reason "game_running"); otherwise it erases the requested trees and replies
 * FactoryResetComplete.
 *
 * @param fd           Connected socket fd.
 * @param erase_games  Erase /data/games if nonzero.
 * @param erase_saves  Erase /data/saves if nonzero.
 * @param erase_cache  Erase /data/cache if nonzero.
 * @param erase_config Erase /data/config if nonzero.
 * @param erase_logs   Erase /data/log if nonzero.
 * @return             0 on success, -1 on error (including a running game).
 */
int playos_trusted_factory_reset(int fd, int erase_games, int erase_saves,
                                 int erase_cache, int erase_config,
                                 int erase_logs);

/**
 * Request applying a system update bundle via IPC (Sprint 11).
 *
 * Sends: {"v":1,"type":"ApplyUpdate","path":"<path>"}
 * Expects response: ApplyUpdateAck {"accepted":true} on acceptance, or
 * ApplyUpdateError on rejection/error.
 *
 * @param path  Filesystem path to the .playosb update bundle.
 * @return      0 if playos-init accepted the update, -1 on error or
 *              rejection.
 */
int playos_trusted_apply_update(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* PLAYOS_TRUSTED_CONTROL_H */
