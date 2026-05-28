#include "core/db/CoreSchema.h"

namespace clickin {

std::vector<Migration> coreSchemaV1() {
    return {
    {
        .version     = 1,
        .description = "Core tables: asset, metadata, cache, job, plugin_registry, settings",
        .sql         = R"sql(
CREATE TABLE asset (
    id         TEXT PRIMARY KEY,
    name       TEXT NOT NULL,
    status     TEXT NOT NULL DEFAULT 'active',
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE asset_provider (
    id          TEXT PRIMARY KEY,
    asset_id    TEXT NOT NULL REFERENCES asset(id),
    provider_id TEXT NOT NULL,
    uri         TEXT NOT NULL,
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE asset_provider_capability (
    id          TEXT PRIMARY KEY,
    provider_id TEXT NOT NULL REFERENCES asset_provider(id),
    capability  TEXT NOT NULL,
    version     INTEGER NOT NULL DEFAULT 1,
    metadata    TEXT
);

CREATE TABLE asset_plugin_metadata (
    id             TEXT PRIMARY KEY,
    plugin_id      TEXT NOT NULL,
    scope_type     TEXT NOT NULL,
    scope_id       TEXT NOT NULL,
    namespace      TEXT NOT NULL,
    data           BLOB NOT NULL,
    data_format    TEXT NOT NULL DEFAULT 'raw',
    schema_version INTEGER NOT NULL DEFAULT 1,
    created_at     TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at     TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(plugin_id, scope_type, scope_id, namespace)
);

CREATE TABLE asset_plugin_cache (
    id               TEXT PRIMARY KEY,
    plugin_id        TEXT NOT NULL,
    scope_type       TEXT NOT NULL,
    scope_id         TEXT NOT NULL,
    cache_type       TEXT NOT NULL,
    cache_key        TEXT NOT NULL,
    cache_version    TEXT NOT NULL DEFAULT '',
    uri              TEXT NOT NULL,
    size_bytes       INTEGER NOT NULL DEFAULT 0,
    status           TEXT NOT NULL DEFAULT 'ready',
    created_at       TEXT NOT NULL DEFAULT (datetime('now')),
    last_accessed_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(plugin_id, scope_type, scope_id, cache_type, cache_key, cache_version)
);

CREATE TABLE job (
    id          TEXT PRIMARY KEY,
    plugin_id   TEXT NOT NULL,
    type        TEXT NOT NULL,
    status      TEXT NOT NULL DEFAULT 'pending',
    priority    INTEGER NOT NULL DEFAULT 0,
    payload     BLOB,
    result      BLOB,
    error       TEXT,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    started_at  TEXT,
    finished_at TEXT
);

CREATE TABLE plugin_registry (
    plugin_id     TEXT PRIMARY KEY,
    enabled       INTEGER NOT NULL DEFAULT 1,
    load_status   TEXT NOT NULL DEFAULT 'active',
    config        TEXT,
    registered_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE settings (
    key        TEXT PRIMARY KEY,
    value      TEXT NOT NULL,
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
)sql"
    },
    {
        .version     = 2,
        .description = "Unique index on asset_provider.uri for discovery deduplication",
        .sql         = R"sql(
CREATE UNIQUE INDEX asset_provider_uri_unique ON asset_provider(uri);
)sql"
    },
    {
        .version     = 3,
        .description = "Add kind column to asset table",
        .sql         = R"sql(
ALTER TABLE asset ADD COLUMN kind TEXT NOT NULL DEFAULT '';
)sql"
    }};
}

} // namespace clickin
