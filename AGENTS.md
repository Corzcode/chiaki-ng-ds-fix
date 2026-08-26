# AGENTS.md

## Project Overview

`chiaki-ng` is a cross-platform PlayStation 4/5 Remote Play client (Linux, Windows, macOS, Android, Switch) and the next-generation continuation of the Chiaki project. Code is split into:

- **`lib/`** — core Remote Play protocol library in C (session, discovery, stream/control, crypto, FEC, codec wrappers, networking). Most feature work starts here.
- **`gui/`** — Qt6/QML desktop app wrapping `chiaki-lib` through C++ bridge objects.
- **`cli/`** — small CLI wrapper over `chiaki-lib` (`discover`, `wakeup`), also used by GUI command mode.
- **`android/`**, **`switch/`** — platform frontends (Switch uses Borealis).
- **`steamdeck_native/`**, **`setsu/`** — optional Steam Deck gyro/haptics and touchpad modules.
- **`test/`** — unit tests (munit framework).
- **`third-party/`** — vendored dependencies (git submodules).
- **`scripts/`**, **`.github/workflows/`** — build, packaging, CI automation.
- **`docs/`** — user documentation site (MkDocs Material).

## Getting Started

- README: `README.md`
- Contributor orientation / mental model: `CONTRIBUTOR_GUIDE.md`, `docs/`

## Build

Initialize submodules first: `git submodule update --init --recursive`. Typical desktop build (CMake + Ninja):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target chiaki
```

The GUI binary is `build/gui/chiaki`. Platform builds (AppImage, Flatpak, MSYS2, macOS, Android, Switch) are driven by `scripts/` and `.github/workflows/` (see `build-pr.yaml`).

### Task Completion Checklist

After any code task, do a full Release build and deploy. Close the running chiaki first — the deployed `chiaki-ng-Win/chiaki.exe` copy is locked while the app runs.

```
cmake --build build --config Release --target chiaki
Copy-Item build/gui/chiaki.exe chiaki-ng-Win/ -Force
```

## Tests

Unit tests live in `test/` (vendored munit) and build into the `chiaki-unit` target:

```
cmake -S . -B build -DCHIAKI_ENABLE_TESTS=ON
cmake --build build --target chiaki-unit
build/test/chiaki-unit        # or: ctest --test-dir build --output-on-failure
```

New suites are registered as `MunitTest` arrays exported as `tests_*` symbols in `test/main.c`.

## Code Conventions

- **C code (`lib/`)**: preserve `*_init` / `*_fini` pairing; propagate `ChiakiErrorCode`; no exceptions or `goto`-based error flows.
- **C++ standard**: C++11 (root `CMakeLists.txt`).
- **Licensing**: every source file starts with `// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL`.
- **Indentation**: tabs.
- **Feature gates**: respect `CHIAKI_ENABLE_*` / `CHIAKI_GUI_ENABLE_*` / `CHIAKI_LIB_ENABLE_*`. Many are tri-state (`AUTO|ON|OFF`), not booleans — check `CMakeLists.txt`.
- **Platform guards**: keep platform logic behind existing guards (`Q_OS_*`, `WIN32`, `CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE`, `CHIAKI_GUI_ENABLE_SETSU`, ...).
- **GUI behavior**: settings-driven via `Settings` / `QmlSettings`; avoid hardcoding defaults in session code.
- **Protobuf**: generated at build time from `lib/protobuf/takion.proto`. Never commit generated `takion.pb.c/.h`.
- **i18n**: user-facing strings go through Qt translation. Add to `.ts` files in `gui/translations/` (`chiaki_en.ts`, `chiaki_zh_CN.ts`); helpers `scripts/i18n_extract.py` / `i18n_fill.py`; new `.ts` files must be wired into `qt_add_lrelease` in `gui/CMakeLists.txt`.

## Key Source Locations

- Protocol/session: `lib/src/ctrl.c`, `takion.c`, `session.c`
- Media receive: `lib/src/videoreceiver.c`, `audioreceiver.c`, `reorderqueue.c`
- Codec wrappers: `lib/src/ffmpegdecoder.c`, `opusdecoder.c`, `opusencoder.c`
- Input/feedback: `lib/src/controller*.c`, `feedbacksender.c`, `audiosender.c`
- GUI bridge: `gui/src/qmlbackend.cpp` (lifecycle), `streamsession.cpp` (streaming/audio/controller)
- Renderer/window: `gui/src/qmlmainwindow.cpp` (libplacebo, swapchain, HDR)
- QML views: `gui/src/qml/`
- Remote connect (RUDP/holepunch): `lib/src/remote/`, `lib/src/session.c`

## Debugging

This fork = `chiaki-ng-gyro-fix`, based on official `1.10.0`. The rules below are the hard-won, reusable lessons — follow this order, not the crash dump.

### 1. GUI/QML startup crash → read the QML log FIRST, never the dump

A "crash before the window appears" — usually surfacing later as a libplacebo/Vulkan reset (`pl_tex_destroy`, `custom_mpv`) — is **almost always a downstream symptom of a QML load/compile error or a bad settings path**. libplacebo just crashes in cleanup after the real failure. The `QML_IMPORT_TRACE` points at the true cause; chasing the dump wastes hours.

Capture the log (chiaki is a GUI app; stdout is empty, everything goes to stderr):

```powershell
$env:QML_IMPORT_TRACE="1"; $env:QT_LOGGING_TO_CONSOLE="1"; $env:QT_DEBUG_PLUGINS="1"
Start-Process -FilePath ".\chiaki-ng-Win\chiaki.exe" -WorkingDirectory ".\chiaki-ng-Win" `
  -RedirectStandardError "err.txt" -RedirectStandardOutput "out.txt" -Wait
```

Then `Grep` `err.txt` for the lines just before the crash and for `unavailable`, `Cannot`, `error`, `Warning`, `Failed`, `not found`. Example smoking gun:

```
qrc:/Main.qml:619:9: Type GyroSteerSettingsDialog unavailable
qrc:/GyroSteerSettingsDialog.qml:15:5: Cannot assign object of type "Timer"
    to property of type "QQuickItem*" ...
```

### 2. Validate QML statically before building

`qmlformat -i file.qml` (bracket/structure) and `qmllint file.qml` flag `Cannot assign X to Y` / `unqualified` / `unavailable` early — cheaper than another deploy-test cycle.

### 3. Local logs & crash dumps

- **Runtime logs**: `%APPDATA%\Chiaki\Chiaki\log`.
- **Crash dumps**: `gui/src/main.cpp` `InstallCrashHandlers()` writes `MiniDumpNormal` (use `Normal`, not `WithFullMemory`) to `%LOCALAPPDATA%\CrashDumps\chiaki_crash_<tick>.dmp`.
- Only parse the dump for a genuine protocol/lower-layer crash (QML log was clean):
  - MSYS2 **system libplacebo is shipped WITH DWARF** — `addr2line -e libplacebo-*.dll -f -C <VA>` resolves offset → function+source line.
  - chiaki.exe (Release) also carries `.debug_info`; `addr2line` with full VA (`0x140000000 + RVA`) gives `settings.cpp`, `mocs_compilation.cpp`, etc.
  - MSYS2 gdb/MinGW **cannot read a Windows minidump** (`"not a core dump"`); use the python minidump parser (read streams 3/4/5/6 for exception address + module map) or skip — the QML log of step 1 is the better tool.
  - **Caution:** VA/RVA-mixed stack-scan "frames" (pointers into `settings.cpp`/`custom_mpv.c`, kernel noise) are often **data, not real frames**; validate against PE `.pdata`/`.rdata`. Only the exact `RIP` from the exception stream is trustworthy.

### 4. Bisect commits instead of re-fitting the layer

If the QML trace is clean, bisect the last N commits by file lists (`git diff --stat base..head`) and keep **reverting whole chunk files** (`git checkout <commit> -- path/to/file`) to isolate the culprit while sparing long GUI relinks.

## Common Pitfalls (reusable across the fork)

- **`DialogView` `default property` trap (causes the startup UAF above).** `DialogView.qml` declares `default property Item mainItem`, so a dialog's **first top-level child** is implicitly assigned to a `QQuickItem*`. Placing a non-item first child (e.g. a bare `Timer {}`) makes the dialog unavailable at load: `Cannot assign object of type "Timer" to property of type "QQuickItem*"`; a new dialog referenced from a `Component` in `Main.qml` then fails the whole startup. **Fix:** wrap the body in `Item { anchors.fill: parent }` as the first child; put `Timer`/`Flickable` inside it. Any new `DialogView` subclass: pay attention.
- **libplacebo: link the system package, not `libplacebo/`.** The vendored tree is reference-only — look up enum/signature names there (e.g. `libplacebo/src/include/libplacebo/colorspace.h`), **don't guess**. Fix libplacebo bugs on the chiaki-ng side.
- **Vulkan threading.** Intermittent libplacebo assertion `pool->sync[cmd->qindex].value == cmd->sync.value`: `deferred_swap_thread` races `render_thread`. Keep all `pl_gpu` recording under `QMutex placebo_gpu_mutex` (`render()` and `processDeferredSwapTask`). Do **not** patch libplacebo.
- **Build gotchas (MSYS2/Windows).** Prepend `E:/test/msys64/mingw64/bin` to PATH or `rcc.exe` fails (`0xc0000135`). After editing `.qml`, the `qrc_qml.cpp` autogen may not regenerate — `touch gui/src/qml/qml.qrc` (build log must show "Automatic RCC"). QML is **zstd-compressed** in `qt_resource_data` of `build/gui/chiaki_autogen/VLDSMZLXNG/qrc_qml.cpp`; verify via zstd magic `28 b5 2f fd`, don't grep the exe. Repo is LF (`core.eol=lf`, no `.gitattributes`): a CRLF rewrite shows ~10k fake diff lines — check `git diff --stat HEAD | tail`, diff with `--ignore-space-at-eol`.
- **Git & versioning.** **User pushes themselves**; agent only edits + commits locally. `origin` = `github.com/Corzcode/chiaki-ng-gyro-fix`; on HTTP 401 use `git -c credential.helper=wincred push origin main`. Version carries `-gyrofix` via `CHIAKI_VERSION_SUFFIX` in root `CMakeLists.txt`; keep `MAJOR/MINOR/PATCH` plain numbers (`chiaking.rc.in`/CPACK depend on them).

## Fork-specific features (context when touching these)

- **HDR output** is judged via the swapchain `sw_frame.color_space` (reflects the Windows HDR toggle), **never** the source `hint`; test with `pl_color_space_is_hdr()`. Color transfer enum is `PL_COLOR_TRC_*` (`PL_COLOR_TRC_PQ`/`HLG`), not `PL_TRANSFER_*`.
- **DS5 haptics (`gui/src/streamsession.cpp`).** USB opens the DS5 4ch haptics device (`haptics_output>0`) → linear motors. Bluetooth (Windows) won't open (`haptics_output==0`); with "Rumble Haptics" on, `PushHapticsFrame` converts haptic-audio mean-abs to motor rumble (GT7 rumble root cause), suppressed by a noise gate (`rumble_haptics_baseline`, decay 0.8). Diagnose "no haptics": log `Haptics Audio Device '...' opened` (real) vs `could not find the DualSense audio device` (fallback).
- **Stats overlay & Ctrl+O menu.** Stats overlay is an in-window QML layer (`statsOverlay` in `StreamView.qml`) refreshed by a 250 ms Timer (a C++ `Qt::Tool` at 60 Hz caused render-path heap corruption). `StatsOverlayWidget` is dead code (null-guarded). **Ctrl+O has TWO menus by `runtimeRendererBackend`:** Vulkan → `StreamMenuWindow.qml`; OpenGL → inlined `menuContentComponent` in `StreamView.qml` — edit **both**. **i18n trap:** QML `qsTr()` context = the **QML file name** — a `StreamView.qml` string lives under the **StreamView** context (not `StreamMenuWindow`). Verify with `lrelease` "0 unfinished"; don't grep the (encrypted) `.qm`.
- **Performance note.** On high-refresh monitors (e.g. 160 Hz) the Qt Quick Scene Graph re-composites at native vsync even for a 60 fps stream, inflating GPU cost and `pl_queue_update` mutex hold (`[latency] placebo_mutex_hold_us=`) past the frame budget → `pending_overflow_evict` drops/stutter during streaming. Fix via present-path levers, not by swapping GUI frameworks.

## Editing Boundaries

- Prefer first-party code under `lib/`, `gui/`, `cli/`, `test/`, `cmake/`, `scripts/`.
- Avoid broad edits in vendored trees unless required: `third-party/`, `sdl2-compat-*`, `SDL3-*`, `switch/borealis/`, `libplacebo/`.
- If build options/deps change, update matching files in the same change: `CMakeLists.txt`, `scripts/`, workflow files.

## Commit Conventions

Conventional Commits with scope, in English, concise, describing user-visible or protocol impact: `feat(gui): ...`, `fix(gui): ...`, `ci(gui): ...`, `feat(lib): ...`.