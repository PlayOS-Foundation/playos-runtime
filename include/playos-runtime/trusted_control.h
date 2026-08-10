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
