# PlayOS Runtime

> Internal lifecycle transport, launch and control IPC, private Wayland protocols, restricted service clients, and OS integration.

**Dependency position:** `playos-runtime` is consumed by `playos-platform-api` (backend transport), `playos-compositor` (private Wayland protocol), `playos-shell`, and `playos-overlay` (control IPC client). It is **not** consumed by untrusted game code.

## What This Repository Owns

- Versioned launch and control IPC definitions
- Lifecycle-event transport (the fd-based pipe protocol)
- Private Wayland protocol XML (`protocols/playos-v1.xml`)
- Restricted client libraries used by trusted system components
- Process, session, and OS integration helpers
- The PlayOS backend transport used by `playos-platform-api`

## What It Does NOT Own

- DRM/KMS policy → `playos-compositor`
- Public application API → `playos-platform-api`
- Build system or image assembly → `playos-refdistro`

## IPC Protocol

See [`playos-spec/runtime-ipc.md`](https://github.com/your-org/playos-spec/blob/main/runtime-ipc.md) for the full protocol specification.

Current protocol version: **1**

## Wayland Protocol

Private protocol XML: `protocols/playos-v1.xml`  
See [`playos-spec/wayland-protocol.md`](https://github.com/your-org/playos-spec/blob/main/wayland-protocol.md) for specification.

Generate bindings:
```bash
wayland-scanner client-header protocols/playos-v1.xml gen/playos-v1-client.h
wayland-scanner server-header protocols/playos-v1.xml gen/playos-v1-server.h
wayland-scanner private-code  protocols/playos-v1.xml gen/playos-v1.c
```
