# Contributing to Clickin Creative Dashboard

## Project overview

Clickin Creative Dashboard is a modular asset browser built with C++20 and Qt6.  
The architecture is plugin-driven: Core provides a capability bus and storage services; all domain logic (audio, file, AI) lives in plugins.  
See [`main-prd.md`](main-prd.md) for the full architecture specification.

---

## Repository layout

```
clickin-creative-dashboard/
├── CMakeLists.txt                  # Root build file
├── cmake/
│   ├── Modules/                    # Custom Find*.cmake helpers
│   └── Toolchains/                 # Platform-specific toolchain files
│
├── src/
│   ├── core/
│   │   ├── types/                  # Fundamental value types (AssetId, RawPayload, …)
│   │   ├── capability/             # CapabilityRegistry, CapabilityBroker, CapabilityFuture
│   │   ├── services/               # AssetService, MetadataService, CacheService, JobService, SettingsService
│   │   ├── db/                     # SQLite wrapper + schema migration engine
│   │   └── app/                    # Application startup/shutdown sequence, CoreContext
│   │
│   ├── sdk/                        # Public headers that plugins depend on
│   │   ├── IPlugin.h
│   │   ├── IRawCapabilityHandler.h
│   │   ├── TypedCapabilityHandler.h
│   │   ├── CapabilityCodec.h
│   │   └── contracts/              # Versioned capability contracts (builtin + domain)
│   │       ├── builtin/            # builtin.asset.*, builtin.asset.discovery, …
│   │       └── media/              # media.audio.waveform, media.audio.preview, …
│   │
│   ├── providers/                  # Built-in plugin implementations
│   │   ├── core_asset/             # builtin.core_asset — fallback presentation
│   │   ├── local_file/             # builtin.local_file — folder discovery, file locator
│   │   └── local_audio/            # builtin.local_audio — metadata, waveform, preview
│   │
│   └── ui/
│       ├── shell/                  # Main window, tab bar, startup/teardown
│       ├── asset_list/             # Asset list view (Core UI — builtin capabilities only)
│       ├── plugin_mgmt/            # Plugin management view
│       ├── inspector/              # Asset inspector / details panel
│       ├── job_status/             # Background job / status bar
│       └── audio_preview/          # Audio domain UI — waveform widget + playback controls
│                                   # (depends on media.audio.* capabilities, not Core UI)
│
├── tests/
│   ├── core/                       # Unit tests for capability system, DB layer, services
│   └── providers/                  # Integration tests for built-in plugins
│
├── docs/                           # Architecture notes, decision records
├── main-prd.md
├── CONTRIBUTING.md
└── README.md
```

### Key rules about the directory structure

- **`src/core/`** must not include any domain headers (audio, video, file paths). It only knows about its own types and the SDK interfaces.
- **`src/sdk/`** is the only thing external/future plugins would depend on. Keep it stable and minimal.
- **`src/ui/shell/`, `asset_list/`, `plugin_mgmt/`, `inspector/`** are Core UI — they may only use `builtin.*` capabilities.
- **`src/ui/audio_preview/`** is a domain UI module — it is allowed to depend on `media.audio.*` capabilities.

---

## Platform support

| Platform | Status | Notes |
|----------|--------|-------|
| macOS (arm64 + x86_64) | Primary | Main development target |
| Windows 10/11 (x64) | Required | Must compile and run; CI covers this |
| Linux | Stretch goal | Not required for MVP |

### Platform-specific guidelines

**Paths and file system**
- Never hardcode `/` or `\` separators. Use `QDir`, `QFileInfo`, and `QUrl` for all path construction.
- Prefer `std::filesystem::path` for non-Qt code; convert to `QString` only at Qt boundaries.
- On Windows, watch for long-path issues (`\\?\` prefix) and drive-letter roots.

**"Reveal in file manager" action**
- macOS: `QDesktopServices::openUrl(QUrl("file:///path/to/folder"))` or `open -R <file>` via `QProcess`
- Windows: `explorer /select,"<file>"` via `QProcess`
- Implement as a platform-abstracted function in `providers/local_file/`, not inline in UI code.

**Audio backend**
- Use **Qt Multimedia** for MVP — it works on both platforms without extra dependencies.
- If Qt Multimedia proves insufficient, miniaudio is the fallback (header-only, cross-platform).
- Do not use platform-native audio APIs (CoreAudio, WASAPI) directly in MVP.

**SQLite**
- Use the bundled SQLite (via CMake FetchContent or vcpkg). Do not rely on a system-installed version for portability.

**Build system**
- CMake ≥ 3.25 required (for `cmake_path`, presets v3).
- Use `CMakePresets.json` for per-platform build configs (debug/release, macOS/Windows).
- Do not use `find_package(Qt6)` with hardcoded paths; rely on `CMAKE_PREFIX_PATH` or Qt's toolchain file.
- Windows builds must use MSVC (Qt6 official Windows binaries are MSVC-linked). MinGW is not supported.

---

## C++ style

- **Standard:** C++20. Use concepts, ranges, and `std::span` where they improve clarity.
- **Naming:** `PascalCase` for types and classes, `camelCase` for variables and functions, `UPPER_SNAKE_CASE` for compile-time constants.
- **Headers:** use `#pragma once`. Keep `src/sdk/` headers free of implementation details.
- **Comments:** only where the *why* is non-obvious — hidden constraints, workarounds, subtle invariants. Do not describe what the code does.
- **No exceptions across plugin boundaries.** Within a plugin, exceptions are fine internally; never let them propagate into `invokeRaw` — catch and convert to `RawResult` with an error code.
- **Thread safety:** `CapabilityBroker::invokeRaw` must be callable from worker threads. Qt UI calls must stay on the main thread.

---

## Branching and commits

- `main` — always releasable; direct pushes blocked.
- Feature branches: `feature/<issue-number>-short-description`
- Bugfix branches: `fix/<issue-number>-short-description`

Commit message format:

```
<type>(<scope>): <short summary>

<optional body>
```

Types: `feat`, `fix`, `refactor`, `test`, `docs`, `build`, `chore`  
Scope examples: `core/capability`, `providers/local_audio`, `ui/asset_list`

Example:

```
feat(providers/local_audio): implement waveform peak cache generation

Cache key: waveform.peaks / default / waveform-peaks-v1
Falls back to full decode on cache miss; cache miss is treated as normal.
```

---

## Pull requests

- One PR per issue (or per logical unit of work).
- Link the issue in the PR description (`Closes #N`).
- All CI checks must pass before merge.
- PRs touching `src/sdk/` need an extra review pass — SDK changes are hard to reverse once plugins depend on them.

---

## Issue tracking

Issues are organised under the **MVP Construction** milestone.  
Use the following labels:

| Label | Meaning |
|-------|---------|
| `enhancement` | New feature or implementation phase |
| `documentation` | Docs, CONTRIBUTING, README |
| `bug` | Something broken |
| `architecture` | Design decisions that affect multiple modules |

---

## Getting started (dev setup)

### macOS

```bash
# Install Qt6 modules (Homebrew splits them across formulae)
brew install qtbase qtmultimedia ninja

# Configure — pass both qtbase and qtmultimedia prefix paths,
# plus the Multimedia cmake dir explicitly (Homebrew installs it separately)
cmake -B build/macos-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase);$(brew --prefix qtmultimedia)" \
  -DQt6Multimedia_DIR=$(brew --prefix qtmultimedia)/lib/cmake/Qt6Multimedia

# Build
cmake --build build/macos-debug

# Test
./build/macos-debug/tests/core/test_core
./build/macos-debug/tests/providers/test_providers
```

### Windows

```powershell
# Install Qt6 via Qt online installer, then:
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

> The first time you set up a new platform, add a note here if the steps differ.
