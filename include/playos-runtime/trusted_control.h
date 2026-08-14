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

#ifdef __cplusplus
}
#endif

#endif /* PLAYOS_TRUSTED_CONTROL_H */
