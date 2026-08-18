/**
 * trusted_control.c — PlayOS Trusted Control IPC Client
 *
 * Sends commands over /run/playos/control.sock to playos-init.
 * Each operation is a separate connect→send→recv→close cycle.
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

/* IPC framing — bundled from playos-refdistro/src/playos-init/ipc/ */
#include "ipc.h"

#include "playos-runtime/trusted_control.h"

/* ── Internal helpers ────────────────────────────────────────────── */

/** Default control socket path. */
#define CONTROL_SOCK_PATH "/run/playos/control.sock"

/** Max frame buffer: header + 64 KiB body. */
#define FRAME_BUF_SIZE (sizeof(struct playos_ipc_frame) + PLAYOS_IPC_MAX_BODY)

/**
 * Send a message and wait for a response.
 *
 * Opens a fresh connection, sends `msg`, receives response into `buf`.
 * Server closes after one request — we close our end too.
 *
 * @param msg   Message to send (caller must free after).
 * @param buf   Buffer for the response body (at least bufsz bytes).
 * @param bufsz Size of buf.
 * @return      0 on success, -1 on error.
 */
static int
send_and_recv(struct playos_ipc_message *msg, char *buf, size_t bufsz)
{
    int ret = -1;
    int fd = playos_ipc_client_connect(CONTROL_SOCK_PATH);
    if (fd < 0) {
        fprintf(stderr, "[E] trusted_control: connect to %s failed: %s\n",
                CONTROL_SOCK_PATH, strerror(errno));
        goto done;
    }

    /* Send */
    if (playos_ipc_client_send(fd, msg) != 0) {
        fprintf(stderr, "[E] trusted_control: send failed\n");
        goto close_fd;
    }

    /* Receive response */
    struct playos_ipc_message resp;
    memset(&resp, 0, sizeof(resp));
    int n = playos_ipc_client_recv(fd, &resp, FRAME_BUF_SIZE);
    if (n <= 0) {
        fprintf(stderr, "[E] trusted_control: recv failed (n=%d)\n", n);
        goto close_fd;
    }

    /* Copy response body to caller buffer */
    if (buf && bufsz > 0) {
        size_t copy_len = resp.json_len < bufsz - 1 ? resp.json_len : bufsz - 1;
        memcpy(buf, resp.json_raw, copy_len);
        buf[copy_len] = '\0';
    }

    /* Check for error response */
    if (resp.type && strcmp(resp.type, PLAYOS_IPC_TYPE_ERROR) == 0) {
        fprintf(stderr, "[E] trusted_control: server returned Error\n");
        playos_ipc_message_free(&resp);
        goto close_fd;
    }
    if (resp.type && strcmp(resp.type, PLAYOS_IPC_TYPE_LAUNCH_GAME_ERROR) == 0) {
        fprintf(stderr, "[E] trusted_control: LaunchGameError: %s\n",
                buf ? buf : "(no details)");
        playos_ipc_message_free(&resp);
        goto close_fd;
    }
    if (resp.type && strcmp(resp.type, "ApplyUpdateError") == 0) {
        fprintf(stderr, "[E] trusted_control: ApplyUpdateError: %s\n",
                buf ? buf : "(no details)");
        playos_ipc_message_free(&resp);
        goto close_fd;
    }

    playos_ipc_message_free(&resp);
    ret = 0;

close_fd:
    close(fd);
done:
    return ret;
}

/**
 * Send a message and close the connection without waiting for a response.
 *
 * For fire-and-forget requests (e.g. Suspend) where playos-init performs the
 * action synchronously and sends no reply.
 *
 * @param msg   Message to send (caller must free after).
 * @return      0 if the message was sent, -1 on error.
 */
static int
send_only(struct playos_ipc_message *msg)
{
    int ret = -1;
    int fd = playos_ipc_client_connect(CONTROL_SOCK_PATH);
    if (fd < 0) {
        fprintf(stderr, "[E] trusted_control: connect to %s failed: %s\n",
                CONTROL_SOCK_PATH, strerror(errno));
        return -1;
    }

    if (playos_ipc_client_send(fd, msg) != 0)
        fprintf(stderr, "[E] trusted_control: send failed\n");
    else
        ret = 0;

    close(fd);
    return ret;
}

/* ── Connection management ──────────────────────────────────────── */

int
playos_trusted_connect(void)
{
    return playos_ipc_client_connect(CONTROL_SOCK_PATH);
}

void
playos_trusted_disconnect(int fd)
{
    if (fd >= 0)
        close(fd);
}

int
playos_trusted_register_shell(void)
{
    int fd = playos_ipc_client_connect(CONTROL_SOCK_PATH);
    if (fd < 0) {
        fprintf(stderr, "[E] trusted_control: shell register connect to %s failed: %s\n",
                CONTROL_SOCK_PATH, strerror(errno));
        return -1;
    }

    struct playos_ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    if (playos_ipc_message_from_type(PLAYOS_IPC_PROTOCOL_VERSION,
                                     PLAYOS_IPC_TYPE_SHELL_READY,
                                     NULL, &msg) != 0) {
        close(fd);
        return -1;
    }

    if (playos_ipc_client_send(fd, &msg) != 0) {
        fprintf(stderr, "[E] trusted_control: ShellReady send failed: %s\n",
                strerror(errno));
        playos_ipc_message_free(&msg);
        close(fd);
        return -1;
    }
    playos_ipc_message_free(&msg);

    /* Keep this connection open: playos-init promotes it to the persistent
     * shell listener and streams GameStarted/GameExited/GameCrashed here.
     * The caller owns the fd and must poll it via
     * playos_trusted_shell_poll() and close it with
     * playos_trusted_disconnect(). */
    return fd;
}

int
playos_trusted_shell_poll(int fd, char *type_buf, size_t type_bufsz)
{
    if (fd < 0)
        return 0;

    /* Polled from the shell's render loop — make the fd non-blocking so a
     * slow/missing event never stalls a frame. */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct playos_ipc_message msg;
    memset(&msg, 0, sizeof(msg));

    int n = playos_ipc_client_recv(fd, &msg, FRAME_BUF_SIZE);
    if (n == 0) {
        /* Server closed the listener connection. */
        return -1;
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;   /* no event pending */
        return -1;
    }

    if (type_buf && type_bufsz > 0 && msg.type)
        snprintf(type_buf, type_bufsz, "%s", msg.type);

    playos_ipc_message_free(&msg);
    return 1;
}

/* ── Operations ─────────────────────────────────────────────────── */

int
playos_trusted_launch_game(int fd, const char *game_id)
{
    (void)fd; /* We open our own connection per operation */

    char extra[256];
    int n = snprintf(extra, sizeof(extra),
                     "\"game_id\":\"%s\",\"manifest_path\":\"\"",
                     game_id);
    if (n < 0 || (size_t)n >= sizeof(extra))
        return -1;

    struct playos_ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    if (playos_ipc_message_from_type(PLAYOS_IPC_PROTOCOL_VERSION,
                                     PLAYOS_IPC_TYPE_LAUNCH_GAME,
                                     extra, &msg) != 0)
        return -1;

    char buf[512] = {0};
    int ret = send_and_recv(&msg, buf, sizeof(buf));
    playos_ipc_message_free(&msg);

    if (ret == 0) {
        fprintf(stderr, "[I] trusted_control: LaunchGame '%s' → %s\n",
                game_id, buf);
    }
    return ret;
}

int
playos_trusted_terminate_game(int fd)
{
    (void)fd;

    struct playos_ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    if (playos_ipc_message_from_type(PLAYOS_IPC_PROTOCOL_VERSION,
                                     PLAYOS_IPC_TYPE_TERMINATE_GAME,
                                     NULL, &msg) != 0)
        return -1;

    char buf[256] = {0};
    int ret = send_and_recv(&msg, buf, sizeof(buf));
    playos_ipc_message_free(&msg);
    return ret;
}

int
playos_trusted_query_status(int fd, char *status_buf, size_t bufsz)
{
    (void)fd;

    struct playos_ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    if (playos_ipc_message_from_type(PLAYOS_IPC_PROTOCOL_VERSION,
                                     PLAYOS_IPC_TYPE_QUERY_STATUS,
                                     NULL, &msg) != 0)
        return -1;

    int ret = send_and_recv(&msg, status_buf, bufsz);
    playos_ipc_message_free(&msg);
    return ret;
}

int
playos_trusted_shutdown(int fd)
{
    (void)fd;

    struct playos_ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    if (playos_ipc_message_from_type(PLAYOS_IPC_PROTOCOL_VERSION,
                                     PLAYOS_IPC_TYPE_SHUTDOWN,
                                     NULL, &msg) != 0)
        return -1;

    char buf[128] = {0};
    int ret = send_and_recv(&msg, buf, sizeof(buf));
    playos_ipc_message_free(&msg);
    return ret;
}

int
playos_trusted_reboot(int fd)
{
    (void)fd;

    struct playos_ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    if (playos_ipc_message_from_type(PLAYOS_IPC_PROTOCOL_VERSION,
                                     PLAYOS_IPC_TYPE_REBOOT,
                                     NULL, &msg) != 0)
        return -1;

    char buf[128] = {0};
    int ret = send_and_recv(&msg, buf, sizeof(buf));
    playos_ipc_message_free(&msg);
    return ret;
}

static const char *
perf_profile_wire_name(int profile)
{
    switch (profile) {
    case 1:  return "power_save";
    case 2:  return "performance";
    case 0:
    default: return "balanced";
    }
}

int
playos_trusted_set_perf_profile(int fd, int profile)
{
    (void)fd;

    if (profile < 0 || profile > 2)
        return -1;

    char extra[128];
    int n = snprintf(extra, sizeof(extra), "\"profile\":\"%s\"",
                     perf_profile_wire_name(profile));
    if (n < 0 || (size_t)n >= sizeof(extra))
        return -1;

    struct playos_ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    if (playos_ipc_message_from_type(PLAYOS_IPC_PROTOCOL_VERSION,
                                     PLAYOS_IPC_TYPE_SET_PERF_PROFILE,
                                     extra, &msg) != 0)
        return -1;

    char buf[256] = {0};
    int ret = send_and_recv(&msg, buf, sizeof(buf));
    playos_ipc_message_free(&msg);

    if (ret == 0 && strstr(buf, "\"accepted\":false") != NULL)
        ret = -1;
    return ret;
}

int
playos_trusted_suspend(int fd)
{
    (void)fd;

    struct playos_ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    if (playos_ipc_message_from_type(PLAYOS_IPC_PROTOCOL_VERSION,
                                     PLAYOS_IPC_TYPE_SUSPEND,
                                     NULL, &msg) != 0)
        return -1;

    /* Suspend is fire-and-forget: playos-init sends no response (it blocks
     * writing to /sys/power/state). Send and close without waiting. */
    int ret = send_only(&msg);
    playos_ipc_message_free(&msg);
    return ret;
}

int
playos_trusted_factory_reset(int fd, int erase_games, int erase_saves,
                             int erase_cache, int erase_config, int erase_logs)
{
    (void)fd;

    char extra[256];
    int n = snprintf(extra, sizeof(extra),
                     "\"erase_games\":%d,\"erase_saves\":%d,"
                     "\"erase_cache\":%d,\"erase_config\":%d,"
                     "\"erase_logs\":%d",
                     erase_games ? 1 : 0,
                     erase_saves ? 1 : 0,
                     erase_cache ? 1 : 0,
                     erase_config ? 1 : 0,
                     erase_logs ? 1 : 0);
    if (n < 0 || (size_t)n >= sizeof(extra))
        return -1;

    struct playos_ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    if (playos_ipc_message_from_type(PLAYOS_IPC_PROTOCOL_VERSION,
                                     PLAYOS_IPC_TYPE_FACTORY_RESET,
                                     extra, &msg) != 0)
        return -1;

    char buf[512] = {0};
    int ret = send_and_recv(&msg, buf, sizeof(buf));
    playos_ipc_message_free(&msg);

    if (ret != 0)
        return -1;

    /* send_and_recv already rejects generic Error; FactoryResetError can
     * also arrive as a non-Error type with a "reason" (game_running). */
    if (strstr(buf, "FactoryResetError") != NULL ||
        strstr(buf, "\"reason\":\"game_running\"") != NULL) {
        fprintf(stderr, "[E] trusted_control: FactoryReset denied: %s\n", buf);
        return -1;
    }

    return 0;
}

int
playos_trusted_apply_update(const char *path)
{
    if (path == NULL)
        return -1;

    char extra[512];
    int n = snprintf(extra, sizeof(extra), "\"path\":\"%s\"", path);
    if (n < 0 || (size_t)n >= sizeof(extra))
        return -1;

    struct playos_ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    if (playos_ipc_message_from_type(PLAYOS_IPC_PROTOCOL_VERSION,
                                     "ApplyUpdate",
                                     extra, &msg) != 0)
        return -1;

    char buf[512] = {0};
    int ret = send_and_recv(&msg, buf, sizeof(buf));
    playos_ipc_message_free(&msg);

    if (ret != 0)
        return -1;

    /* send_and_recv rejects generic Error and ApplyUpdateError; a valid
     * acceptance must be ApplyUpdateAck with accepted:true. */
    if (strstr(buf, "\"type\":\"ApplyUpdateAck\"") == NULL ||
        strstr(buf, "\"accepted\":true") == NULL) {
        fprintf(stderr, "[E] trusted_control: ApplyUpdate not accepted: %s\n", buf);
        return -1;
    }

    return 0;
}
