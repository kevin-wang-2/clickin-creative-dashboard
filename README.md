# Clickin Creative Dashboard

A modular asset browser built with C++20 and Qt6.

The first use case is a local sound asset library, but the architecture is domain-agnostic: Core provides a capability bus and storage services; all domain logic (audio, file scanning, AI, remote sync) lives in plugins.

---

## Architecture in one picture

```
┌──────────────────────────────────────────────────────────────┐
│  UI Layer                                                     │
│  asset list · breadcrumb navigation · preview host           │
│  inspector · plugin management · job status bar              │
├──────────────────────────────────────────────────────────────┤
│  Core UI (builtin.* contracts only)                          │
│  + Preview Host (hosts plugin-contributed QWidget factories) │
├──────────────────────────────────────────────────────────────┤
│  Built-in Plugins                                            │
│  builtin.core_asset  builtin.local_file  builtin.local_audio │
├──────────────────────────────────────────────────────────────┤
│  Capability Broker / Registry        ← all routing here      │
├──────────────────────────────────────────────────────────────┤
│  Core Services   Asset · Metadata · Cache · Job · Settings   │
├──────────────────────────────────────────────────────────────┤
│  Database (SQLite, file-backed)                              │
└──────────────────────────────────────────────────────────────┘
```

**Core** knows nothing about audio, files, or AI. It only knows how to route capability handlers, store plugin-private metadata, manage evictable cache, and run jobs.

**Plugins** implement everything domain-specific by registering typed capability handlers. A waveform UI asks `media.audio.waveform:v1` — it doesn't care whether the asset is a local WAV, a video track, or a cloud file.

**Contract scopes** (see [`CONTRIBUTING.md`](CONTRIBUTING.md) for the full convention):

| Scope | Location | Example |
|---|---|---|
| Built-in | `sdk/contracts/builtin/`, `sdk/contracts/ui/` | `AssetLocatorContract` |
| Domain | `providers/<domain>/contracts/` | `AudioWaveformContract` in `providers/audio/` |
| Plugin-private | `providers/<plugin>/contracts/` | plugin-specific extensions |

See [`main-prd.md`](main-prd.md) for the full architecture specification.

---

## Status

### MVP Construction — ✅ Complete

| Phase | Description |
|-------|-------------|
| 0 | CMake/Qt6 skeleton, directory structure, capability interfaces, GoogleTest |
| 1 | `CapabilityRegistry` + `CapabilityBroker` + typed dispatch |
| 2 | SQLite wrapper, idempotent migrations, `DatabaseService`, core services |
| 3 | `PluginManager` activation/shutdown, `plugin_registry` table, error isolation |
| 4 | `local_file`, `local_audio`, `core_asset` built-in plugins |
| 5 | Qt UI shell — asset list, folder scan, plugin management, inspector, audio waveform preview |

### Upcoming milestones

| Milestone | Key features |
|-----------|-------------|
| [Threading, Multiprocess and Worker](https://github.com/kevin-wang-2/clickin-creative-dashboard/milestone/2) | Async `CapabilityFuture`, worker pool, execution policy, subprocess isolation |
| [More Plugin Features](https://github.com/kevin-wang-2/clickin-creative-dashboard/milestone/3) | Context menu actions, asset search, plugin windows, dependency resolution |
| [Asset Hierarchy](https://github.com/kevin-wang-2/clickin-creative-dashboard/milestone/4) | Plugin-attributed parent-child graph, breadcrumb navigation, folder scan hierarchy |
| [More Asset Metadata](https://github.com/kevin-wang-2/clickin-creative-dashboard/milestone/5) | Asset kind column, thumbnails, discovery deduplication |
| [Asset Multi-binding](https://github.com/kevin-wang-2/clickin-creative-dashboard/milestone/6) | Multiple locators per asset, unbind UI, binding questioner SDK — prerequisite for network plugins |

Documentation is maintained in [issue #3](https://github.com/kevin-wang-2/clickin-creative-dashboard/issues/3).

---

## Building and running

### Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | ≥ 3.25 | |
| Ninja | any | `brew install ninja` |
| Qt6 | 6.8+ | qtbase + qtmultimedia |
| Compiler | AppleClang 15+ (macOS) / MSVC 2022 (Windows) | |

### macOS

```bash
brew install qtbase qtmultimedia ninja

# Copy the preset template and fill in your Qt paths
cp CMakeUserPresets.json.template CMakeUserPresets.json
# Edit CMakeUserPresets.json — update CMAKE_PREFIX_PATH to your Qt installation

# Configure + build
cmake --preset macos-debug
cmake --build build/macos-debug

# Run the app
open build/macos-debug/src/ClickinDashboard.app

# Run tests
./build/macos-debug/tests/core/test_core
./build/macos-debug/tests/providers/test_providers
```

### Windows

```powershell
# Install Qt 6.8+ via https://www.qt.io/download — include QtMultimedia
# Copy and edit CMakeUserPresets.json.template

cmake --preset windows-debug
cmake --build build/windows-debug --config Debug

# Run the app
build\windows-debug\src\Debug\ClickinDashboard.exe
```

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for detailed platform setup and branching conventions.

---

## Repository layout

```
src/
  core/          Core types, capability system, services, DB, app startup
  sdk/           Public headers plugins depend on (IPlugin, contracts/builtin/, …)
  providers/     Built-in plugins + domain contract libraries
    audio/         Audio domain — INTERFACE library (headers only)
    core_asset/    builtin.core_asset
    local_file/    builtin.local_file
    local_audio/   builtin.local_audio
  ui/            Qt UI modules (shell, asset list, preview host, inspector, …)
tests/
  core/          Unit tests — capability broker, DB layer, services
  providers/     Integration tests — plugin manifests, audio waveform, etc.
third_party/
  sqlite/        Vendored SQLite amalgamation
main-prd.md      Full architecture specification
CONTRIBUTING.md  Development conventions, contract scopes, migration guide
```

---

## CI

[![CI](https://github.com/kevin-wang-2/clickin-creative-dashboard/actions/workflows/ci.yml/badge.svg)](https://github.com/kevin-wang-2/clickin-creative-dashboard/actions/workflows/ci.yml)

Builds and runs tests on macOS and Windows on every push and pull request.
