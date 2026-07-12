# CLAUDE.md

This file provides guidance to LLM agents when working with code in this repository.

## What this repo is

A personal fork of SuperCollider (audio synthesis platform: `scsynth`/`supernova` servers, `sclang` language, `scide` editor) whose sole purpose is adding a **Vim mode to the SC IDE editor**. It is not intended to be merged upstream. Fork work happens on the `supercollider-vim-mode` branch; `develop` tracks upstream and is the base branch for PRs within this fork.

## Build

The active build tree is `build-make/` (Unix Makefiles, RelWithDebInfo, Homebrew Qt 6 at `/opt/homebrew/opt/qt`). The `build/` directory is a stale Xcode-generator configure — ignore it.

```sh
cmake --build build-make                    # incremental build
./launch-supercollider.sh                   # build + install + launch the IDE (macOS)
```

The launch script installs the app bundle to `build-make/Install/SuperCollider/` and writes the IDE log to `/tmp/supercollider-ide.log`.

To reconfigure from scratch:

```sh
cmake -B build-make -G "Unix Makefiles" -D CMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -D CMAKE_BUILD_TYPE=RelWithDebInfo .
```

After pulling upstream changes, sync vendored deps: `git submodule update --init --recursive`.

## Tests

C++ tests run through CTest from the build tree:

```sh
cd build-make && ctest              # all C++ tests
ctest -R ide__vim_mode              # only the vim mode test
```

The vim mode test (`testsuite/ide/vim_mode_test.cpp`) is a Boost.Test binary that links `libscide` and needs an offscreen Qt platform. To rebuild it and run a single test case:

```sh
cmake --build build-make --target vim_mode_test
QT_QPA_PLATFORM=offscreen ./build-make/testsuite/ide/vim_mode_test --run_test=vim_mode/mode_transitions
```

sclang class-library tests (`testsuite/classlibrary/Test*.sc`) are `UnitTest` subclasses run from inside a running sclang session (e.g. `TestArray.run` or `UnitTest.gui`), not via ctest.

## Formatting

C++ is formatted with clang-format — **version 14.x specifically**; the helper script (`tools/clang-format.py`) rejects other versions and needs `clang-format-diff.py` on PATH. Config is `.clang-format` (WebKit-based, 120 columns, 4-space indent). Build-tree targets:

```sh
cmake --build build-make --target format    # format files changed vs HEAD
cmake --build build-make --target lint      # check only, nonzero exit on violations
cmake --build build-make --target formatall # whole tree (lintall to check)
```

## Architecture

SuperCollider is three separate programs plus shared support code. The language and the audio servers are **separate processes communicating over OSC** (UDP/TCP) — that boundary shapes everything.

- `server/scsynth` — the real-time audio server; `server/supernova` — alternative server with multicore-parallel DSP. Both load UGen plugins built from `server/plugins`. The C++ plugin API lives in `include/plugin_interface/`.
- `lang/` — the sclang interpreter. `LangSource/` is the VM (lexer/parser/compiler, PyrObject object model, garbage collector); `LangPrimSource/` implements the built-in primitives that `.sc` code calls down into.
- `SCClassLibrary/` — the sclang class library, compiled by sclang at startup. Most user-facing behavior is defined here in sclang code, backed by primitives.
- `QtCollider/` — Qt GUI classes exposed to sclang (when `SC_QT=ON`).
- `editors/sc-ide/` — the Qt IDE. It builds as a static library `libscide` plus a thin `SuperCollider` executable; this split is what lets the testsuite link IDE code. The IDE runs sclang as a child `QProcess` (`core/sc_process.cpp`) and implements introspection/help by evaluating sclang code.
- `common/` and `include/common/` — code shared across the above. `external_libraries/` — vendored deps, many as git submodules. `HelpSource/` — `.schelp` documentation sources rendered by `SCDoc/`; the vim keybindings are documented in `HelpSource/Reference/KeyboardShortcuts.schelp`.

## Vim mode (this fork's feature)

- Implementation: `editors/sc-ide/widgets/code_editor/vim_mode.{hpp,cpp}` — `VimModeController`, a modal key handler (Normal/Insert/Visual/VisualLine) plus operator/motion/text-object state and dot-repeat recording. It deliberately operates on a plain `QPlainTextEdit*`, not `ScCodeEditor`, so tests can drive it on a bare widget.
- Integration: `ScCodeEditor` owns the controller (`widgets/code_editor/sc_editor.cpp`) and routes `keyPressEvent` and `ShortcutOverride` events through it before normal handling; the status-bar mode label is `MainWindow::updateVimStatus`. Enabled by the `vimMode` settings key (Preferences → Editor; default off, set in `core/settings/manager.cpp`).
- The unnamed register and last search pattern are static members of `VimModeController`, shared across all open editors.
- Tests build synthetic `QKeyEvent`s via helpers (`press`, `typeText`, `EditorFixture`) and assert on the widget's text and cursor position.
