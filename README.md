# Clickin Creative Dashboard

A modular asset browser built with C++20 and Qt6.

The first use case is a local sound asset library, but the architecture is domain-agnostic: Core provides a capability bus and storage services; all domain logic (audio, file scanning, AI, remote sync) lives in plugins.

---

## Architecture in one picture

```
┌─────────────────────────────────────────────────────┐
│  UI Layer           asset list · preview · inspector │
├───────────────────────┬─────────────────────────────┤
│  Core UI              │  Domain UI (audio preview)   │
│  (builtin.* only)     │  (media.audio.* capabilities)│
├───────────────────────┴─────────────────────────────┤
│  Built-in Plugins                                    │
│  builtin.core_asset · builtin.local_file             │
│  builtin.local_audio                                 │
├─────────────────────────────────────────────────────┤
│  Capability Broker / Registry   ← all routing here  │
├─────────────────────────────────────────────────────┤
│  Core Services   Asset · Metadata · Cache · Job     │
├─────────────────────────────────────────────────────┤
│  Database (SQLite)                                   │
└─────────────────────────────────────────────────────┘
```

**Core** knows nothing about audio, files, or AI. It only knows how to:
- register and route capability handlers
- store plugin-private metadata
- manage evictable plugin cache
- run jobs/futures

**Plugins** implement everything domain-specific by registering typed capability handlers. A waveform UI asks `media.audio.waveform:v1` — it doesn't care whether the asset is a local WAV, a video track, or a cloud file.

See [`main-prd.md`](main-prd.md) for the full architecture specification.

---

## Current state

| Phase | Status | Description |
|-------|--------|-------------|
| 0 — Skeleton | ✅ Done | CMake/Qt6, directory structure, capability system interfaces, stub plugins, GoogleTest |
| 1 — Capability system | 🔲 Planned | Full `CapabilityBroker` + async `CapabilityFuture`, contract enforcement |
| 2 — Database layer | 🔲 Planned | SQLite schema, migrations, core services |
| 2b — Plugin lifecycle | 🔲 Planned | `IPlugin` startup/shutdown, `plugin_registry` table |
| 3 — Built-in plugins | 🔲 Planned | `local_file` discovery, `local_audio` metadata/waveform/preview |
| 4 — Qt UI | 🔲 Planned | Asset list, plugin management, inspector, waveform + playback |

Work is tracked under the [MVP Construction milestone](https://github.com/kevin-wang-2/clickin-creative-dashboard/milestone/1).

---

## Building

### Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | ≥ 3.25 | |
| Ninja | any | `brew install ninja` |
| Qt6 | 6.8+ | qtbase + qtmultimedia |
| Compiler | AppleClang 15+ (macOS) / MSVC 2022 (Windows) | |

### macOS (Homebrew Qt)

```bash
brew install qtbase qtmultimedia ninja

# Copy the preset template and fill in your Qt paths
cp CMakeUserPresets.json.template CMakeUserPresets.json
# Edit CMakeUserPresets.json — update paths to match your installed Qt version

# Configure + build
cmake --preset macos-debug
cmake --build build/macos-debug

# Run tests
./build/macos-debug/tests/core/test_core
./build/macos-debug/tests/providers/test_providers
```

### Windows (MSVC + Qt online installer)

```powershell
# Install Qt 6.8+ via https://www.qt.io/download — include QtMultimedia module
# Then copy and edit CMakeUserPresets.json.template

cmake --preset windows-debug
cmake --build build/windows-debug --config Debug
```

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for detailed platform setup, VS Code integration, and branching conventions.

---

## Repository layout

```
src/
  core/          Core types, capability system, services, DB, app startup
  sdk/           Public headers plugins depend on (IPlugin, IRawCapabilityHandler, …)
  providers/     Built-in plugin implementations
  ui/            Qt UI modules (shell, asset list, preview, inspector, …)
tests/
  core/          Unit tests — capability broker, etc.
  providers/     Integration tests — plugin manifests, etc.
third_party/
  sqlite/        Vendored SQLite 3.46 amalgamation
main-prd.md      Full architecture specification
```

---

## CI

[![CI](https://github.com/kevin-wang-2/clickin-creative-dashboard/actions/workflows/ci.yml/badge.svg)](https://github.com/kevin-wang-2/clickin-creative-dashboard/actions/workflows/ci.yml)

Builds and runs tests on macOS and Windows on every push and pull request.
