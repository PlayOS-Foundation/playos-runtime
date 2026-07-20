# PlayOS Compositor — Bring-Up Guide (ROG Ally / Linux)

Stage 1 goal: bring up the display with the wlroots compositor and launch the
Raylib shell as a Wayland client. Controller input reaches the shell through
the PlayOS Platform API's evdev backend, so full Wayland input routing is not
required for this stage.

> **Status:** The original TinyWL-derived C skeleton has been **replaced**
> with a C++17 RAII compositor per ADR-0002. The C file (`playos-compositor.c`)
> is kept for reference only — the active source is `src/main.cpp` and
> `src/compositor.cpp` with the header at `include/playos/compositor/compositor.hpp`.
>
> This compositor targets **wlroots 0.19**. If your installed wlroots differs,
> adapt the calls flagged `VERSION-SENSITIVE` in `src/compositor.cpp`, using
> the matching upstream `tinywl.c` as a reference.

## 1. Packages (Arch / CachyOS)

```sh
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf \
  wlroots wayland wayland-protocols libinput libdrm seatd \
  libxkbcommon mesa vulkan-radeon
```

Enable seat management (grants display/input access without root):

```sh
sudo systemctl enable --now seatd
sudo usermod -aG seat,video,input "$USER"   # re-login afterwards
```

## 2. Build the compositor

From `playos-runtime/`:

```sh
cmake -B build -G Ninja -DPLAYOS_BUILD_COMPOSITOR=ON
cmake --build build
```

The compositor binary is `build/compositor/playos-compositor`.

Check which wlroots was detected in the configure output:

```text
-- PlayOS compositor: using wlroots-0.19 (version 0.19.x)
```

## 3. Build the shell (Wayland client)

Build `playos-shell` on the same machine (it links the Platform API's Linux
evdev backend automatically):

```sh
cd ../playos-shell
cmake -B build -G Ninja
cmake --build build
```

## 4. Run the console loop

From a **TTY** (Ctrl+Alt+F3), not inside an existing desktop session, so the
compositor can take DRM/KMS:

```sh
cd playos-runtime
PLAYOS_SHELL_CMD="$PWD/../playos-shell/build/playos-shell" ./build/compositor/playos-compositor
```

Or pass the shell command as an argument:

```sh
./build/compositor/playos-compositor "$PWD/../playos-shell/build/playos-shell"
```

Expected: the compositor takes the display, the Raylib shell appears, and the
gamepad navigates it (via the evdev backend). Selecting the sample launches it
through the runtime; exiting returns to the shell.

### Nested (development) run

You can also run the compositor **inside** an existing Wayland/X11 session for
development; wlroots picks the appropriate backend automatically and opens a
window instead of taking the whole display.

## 5. Verify GPU acceleration

```sh
glxinfo | grep renderer     # expect: AMD Radeon 780M (not llvmpipe)
```

## Stage 1 definition of done

```text
[x] wlroots detected and compositor builds
[x] compositor takes the display on the Ally (or nests for dev)
[x] Raylib shell appears as a Wayland client
[x] keyboard navigates the shell (arrows/Enter/Esc)
[x] selecting a sample launches it and returns to the shell
[ ] gamepad navigates the shell (evdev backend) — pending ROG Ally test
```

### VMware notes

The VMware `vmwgfx` driver has broken dmabuf support. Work around it:
```sh
WLR_RENDERER=pixman ./build/compositor/playos-compositor ...
```

## Known TODOs (next stages)

- Wayland keyboard/pointer routing through the seat (Stage 2).
- Fullscreen/console layout policy for the foreground surface.
- Home-button interception in the compositor (currently the shell reads Home
  via the Platform API).
- Port from this C skeleton to the C++ RAII compositor per ADR-0002.
- Suspend/resume, overlay, brightness/volume (later stages).

See also: `playos-spec` chapters
`08-runtime-architecture/05-compositor-model.md`,
`08-runtime-architecture/03-boot-model.md`, and
`12-device-model-and-porting/12-rog-ally-reference.md`.
