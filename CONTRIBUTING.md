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
│   │   └── contracts/
│   │       ├── builtin/            # Built-in contracts (see "Capability contract scopes" below)
│   │       └── ui/                 # UI contracts (AssetPreviewWidgetContract, …)
│   │
│   ├── providers/                  # Built-in plugin implementations + domain contract libraries
│   │   ├── audio/                  # Audio domain — INTERFACE library (headers only)
│   │   │   └── contracts/          # AudioMetadataContract, AudioWaveformContract, AudioPreviewContract
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
│       └── preview_host/           # Generic preview host — hosts plugin-contributed widgets
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
- **`src/ui/preview_host/`** is Core UI that hosts plugin-contributed widgets; it uses `AssetPreviewWidgetContract` (a UI contract in `sdk/contracts/ui/`) and has no domain knowledge.

---

## Capability contract scopes

Capability contracts (`struct FooContract { ... }`) fall into three scopes that determine where their headers live and which CMake target owns them.

### 1. Built-in — `sdk/contracts/builtin/` and `sdk/contracts/ui/`

Contracts defined and owned by Core. Every plugin and every UI module may depend on them freely. Examples: `AssetDiscoveryContract`, `AssetLocatorContract`, `AssetOpenActionsContract`, `AssetPreviewWidgetContract`.

These headers are part of `clickin_core`'s public interface. No extra CMake dependency is needed.

### 2. Domain-specific — `providers/<domain>/contracts/`

Contracts that define the shared interface for a capability domain that **multiple independent plugins** implement or consume. The domain module is a **header-only CMake `INTERFACE` library**. Plugins depend on the domain library, not on any specific implementation plugin.

```
providers/
  audio/                  ← CMake INTERFACE target: audio_domain
    contracts/
      AudioMetadataContract.h
      AudioWaveformContract.h
      AudioPreviewContract.h
  local_audio/            ← links PUBLIC audio_domain (implements the contracts)
  portaudio_preview/      ← links PUBLIC audio_domain (future; also implements the contracts)
```

**Rules:**
- The domain directory contains **only headers** — no `.cpp`, no implementation.
- Any plugin that implements or consumes domain contracts declares `target_link_libraries(my_plugin PUBLIC <domain>_domain)`.
- Plugins never depend on each other to get at domain contracts. `portaudio_preview` must NOT link `local_audio` just to get audio headers.

**Adding a new domain:**
1. Create `providers/<domain>/CMakeLists.txt` with a single `INTERFACE` target.
2. Add `add_subdirectory(<domain>)` to `providers/CMakeLists.txt` **before** any implementing plugin.
3. Document the new domain in this section.

**Current domains:**

| Domain | CMake target | Contracts |
|--------|-------------|-----------|
| Audio  | `audio_domain` | `AudioMetadataContract`, `AudioWaveformContract`, `AudioPreviewContract` |

### 3. Plugin-private — `providers/<plugin>/contracts/` (or inline in the plugin's headers)

Contracts used exclusively within a single plugin's ecosystem — only that plugin implements them, and no other plugin is expected to implement or directly consume them. Exposed as `PUBLIC` headers of the plugin's own CMake target. Consumers depend on the plugin target directly.

> **When in doubt:** if you expect only one implementation to ever exist and the contract is tightly coupled to a specific plugin, keep it plugin-private. If you can imagine a second independent implementation, it belongs in a domain library.

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
- Use the bundled SQLite in `third_party/sqlite/`. Do not rely on a system-installed version for portability.

---

## Database schema migrations

Schema changes are managed by `MigrationRunner` (`src/core/db/Migration.h`). Each migration is a versioned SQL string executed once on startup and recorded in the `schema_migration` table. **Never modify a migration that has already been merged to `main`** — it will not re-run on existing databases.

### Version number convention

| Range | Owner |
|-------|-------|
| 1 – 99 | Core (`src/core/db/CoreSchema.cpp`) |
| 100 × N — 100 × N + 99 | Plugin N (claim a block in the plugin's header) |

Example: `builtin.local_audio` claims version 200–299.

### Adding a new migration

Add a new entry to `coreSchemaV1()` (or your plugin's migration list) with the next available version:

```cpp
// src/core/db/CoreSchema.cpp
{
    .version     = 2,
    .description = "asset: add duration_ms column",
    .sql         = "ALTER TABLE asset ADD COLUMN duration_ms INTEGER;"
}
```

The runner applies it automatically on the next startup, then never again.

### SQLite DDL limitations

SQLite's `ALTER TABLE` supports only **add column** and **rename column / table**. For anything else (drop column, change column type, reorder columns, add a NOT NULL column to an existing table), use the standard multi-step procedure inside the migration SQL:

```sql
-- 1. Create new table with the desired schema
CREATE TABLE asset_new (...);
-- 2. Copy data
INSERT INTO asset_new SELECT ... FROM asset;
-- 3. Drop old table
DROP TABLE asset;
-- 4. Rename
ALTER TABLE asset_new RENAME TO asset;
```

Put all four steps in a single `.sql` string — the migration runs inside a transaction, so a failure at any step rolls back cleanly and the version is not recorded.

### No automatic rollback

If a migration fails, the transaction is rolled back and the version is **not** written to `schema_migration`. The runner will retry it on the next startup. There is no "downgrade" path — if a broken migration reaches `main`, the fix must come as a new, higher-versioned migration (not by editing the old one).

---

## Asset hierarchy

### Overview

The asset hierarchy is a **read-through cache** maintained by Core. Plugins own their graph data; Core caches it in `hierarchy_nodes / hierarchy_edges` for fast UI queries. The two sides communicate through three built-in contracts:

| Contract | Capability string | Direction |
|---|---|---|
| `AssetHierarchyRootsContract` | `builtin.asset.hierarchy.roots` | Core → Plugin: "give me your root nodes" |
| `AssetHierarchyChildrenContract` | `builtin.asset.hierarchy.children` | Core → Plugin: "give me children of node X" |
| `AssetHierarchyVersionContract` | `builtin.asset.hierarchy.version` | Core → Plugin: "has your graph changed?" |

A plugin is the sole owner of its graph. Core never writes to the plugin's data — it only queries the three contracts above and writes to its own cache tables.

### Plugin contract

```
hierarchy_nodes        ← Core-managed cache
hierarchy_edges        ← Core-managed cache
     ↑ written by traverse()
CapabilityBroker
     ↑ queried by traverse()
[Your Plugin]
  HierarchyRootsHandler   → returns []HierarchyNode{nodeId, assetId, hasChildren}
  HierarchyChildrenHandler → given nodeId, returns []HierarchyNode
  HierarchyVersionHandler  → returns an opaque version token (or "" = always dirty)
```

**`HierarchyNode`** has three fields:

```cpp
struct HierarchyNode {
    std::string nodeId;          // plugin-defined stable identifier
    std::string assetId;         // core asset table id
    bool        hasChildren = false;
};
```

- `nodeId` is **plugin-defined and opaque to Core**. It is the key the plugin will receive in `AssetHierarchyChildrenContract::Request::nodeId`. It must be stable across re-traversals so that Core can preserve node UUIDs (see `upsertNode`).
- `assetId` must match a row in the `asset` table. Create/find the asset before returning the node.
- `hasChildren` is a hint — set it to `true` if the node has children; Core will call `AssetHierarchyChildrenContract` for it. Setting it conservatively (always `true`) is safe but triggers an extra round-trip.

### What a plugin stores internally — no prescription

Core does not care how a plugin stores its graph. The three handlers are the only interface. Examples:

- **`builtin.local_file`** — writes a newline-delimited adjacency list into `MetadataService` during directory scan (roots, per-parent child lists, version UUID). The handlers read back from MetadataService.
- A **network/cloud plugin** might keep no local state at all; it queries a remote API in `RootsHandler` / `ChildrenHandler` directly and returns `""` as the version so Core always re-traverses.
- An **AI tagging plugin** might derive the graph from embedding clusters recomputed periodically, storing results in its own plugin DB table.

The contract is the interface; the storage is the plugin's concern.

### Core-side traverse flow

`HierarchyService::traverse(pluginId, broker)` runs a BFS:

1. Calls `AssetHierarchyRootsContract` → gets root `HierarchyNode` list.
2. Calls `clearEdgesForPlugin(pluginId)` to reset this plugin's edges.
3. For each root, calls `upsertNode(pluginId, node.nodeId, node.assetId)` which **preserves the stable Core UUID** if the same `pluginNodeId` was seen before.
4. For every node with `hasChildren = true`, calls `AssetHierarchyChildrenContract` and recurses (BFS).
5. Calls `pruneNodes(pluginId, visitedNodeIds)` to delete any of this plugin's nodes not reached in this traversal.

`HierarchyService::checkAndTraverse(pluginId, broker, settings)` wraps the above with a version check:

- Calls `AssetHierarchyVersionContract` to get a token.
- Compares it against the last-stored token in `SettingsService` (key: `"hierarchy.version.<pluginId>"`).
- Skips traversal if tokens match; runs `traverse` and updates the stored token if different (or if the plugin returns `""`).

`HierarchyService::traverseAll(broker, settings)` calls `checkAndTraverse` for every plugin that has registered a `AssetHierarchyRootsContract` handler.

### When to trigger traversal

| Trigger | Recommended call |
|---|---|
| App startup, after all plugins are loaded | `traverseAll(broker, settings)` |
| After a plugin's scan/sync operation completes | `traverse(pluginId, broker)` directly (bypasses version check, always refreshes) |
| Periodic background poll | `checkAndTraverse(pluginId, broker, settings)` |

### Node UUID stability

Core generates a UUID per `(pluginId, pluginNodeId)` pair on first traversal and **reuses it on all subsequent traversals**. This means UI nodeIds remain stable across re-scans, and any state tied to a nodeId (selection, expansion) survives a re-traverse.

The `pluginNodeId` you choose should therefore be stable for the lifetime of a logical node:
- For a file plugin: the file URI is a good choice.
- For a cloud plugin: the remote item's permanent ID.
- Avoid using transient values (sequence numbers, pointer addresses).

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
