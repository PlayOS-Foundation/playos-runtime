/*
 * playos-runtime/tests/test_trusted_control.c — S12-T6 control socket
 *
 * Verifies the trusted control-socket policy implemented in ipc_server.c:
 *
 *   - playos_ipc_server_check_peer() accepts only a peer whose primary
 *     gid is 1000 (playos-trusted) or whose uid is 0 (root). Everything
 *     else — including the game identity (uid/gid 1001) — is rejected.
 *   - playos_ipc_server_create() creates the socket root:playos-trusted
 *     mode 0660, and cleans the socket file up on failure.
 *
 * The peer-credential check is exercised with a socketpair, where the
 * peer is this process itself: the expected verdict is derived from our
 * own uid/gid and must match the documented policy exactly.
 */
#define _GNU_SOURCE
#include "ipc.h"

#include <assert.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) {                                                        \
            printf("PASS: %s\n", name);                                    \
        } else {                                                           \
            printf("FAIL: %s\n", name);                                    \
            failures++;                                                    \
        }                                                                  \
    } while (0)

int
main(void)
{
    const char *sock_path = "/tmp/playos-test-control.sock";

    printf("== playos-runtime Sprint 12 control-socket tests ==\n");

    /* ── Peer policy ─────────────────────────────────────────────── */
    {
        int fds[2];
        int expected_ok;
        int rc;

        if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) != 0) {
            perror("socketpair");
            return 1;
        }

        /* The peer of a socketpair is this process. The documented policy
         * accepts gid 1000 or uid 0; anything else must be rejected. */
        expected_ok = (getgid() == 1000 || getuid() == 0);

        rc = playos_ipc_server_check_peer(fds[0], "playos-trusted");
        CHECK(rc == 0, "peer check returns a definitive verdict");
        CHECK((rc == 0) == (expected_ok != 0),
              "peer check matches documented policy (gid 1000 or uid 0)");

        close(fds[0]);
        close(fds[1]);
    }

    /* ── Socket creation / ownership ─────────────────────────────── */
    {
        int srv;

        unlink(sock_path);
        srv = playos_ipc_server_create(sock_path, "playos-trusted");

        if (getuid() == 0) {
            /* Root: socket must exist as root:1000 mode 0660. */
            struct stat st;

            CHECK(srv >= 0, "server socket created (root)");
            if (srv >= 0) {
                if (stat(sock_path, &st) == 0) {
                    CHECK((st.st_mode & 0777) == 0660,
                          "control socket mode is 0660");
                    CHECK(st.st_uid == 0 && st.st_gid == 1000,
                          "control socket owned by root:playos-trusted");
                } else {
                    CHECK(0, "control socket stat");
                }
                close(srv);
            }
        } else {
            /* Non-root: chown to root:1000 fails, the function must fail
             * and must not leave a socket file behind. */
            CHECK(srv < 0, "server socket creation fails without privilege");
            CHECK(access(sock_path, F_OK) != 0,
                  "failed creation leaves no socket file behind");
        }

        unlink(sock_path);
    }

    if (failures) {
        printf("RESULT: %d failure(s)\n", failures);
        return 1;
    }
    printf("RESULT: all control-socket tests passed\n");
    return 0;
}
