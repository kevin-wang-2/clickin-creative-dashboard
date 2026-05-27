# Asset Browser Plugin/Core Architecture Draft

## 0. Design Goal

This application should not be implemented as a fixed `sound browser`.

The intended architecture is:

> A modular asset browser whose first implemented provider set is a local sound asset module.

Core should not directly understand audio/video/image/network/AI-specific logic. Core should provide a small set of stable mechanisms:

1. Plugin/provider registration
2. Capability discovery and invocation
3. Private plugin metadata storage
4. Evictable plugin cache storage
5. Job/future execution infrastructure

Plugins/providers implement actual domain behavior.

Current MVP scope:

- C++/Qt application
- Built-in C++ providers only
- No external dynamic plugin ABI yet
- No Python plugin runtime yet
- No IPC yet
- Capability call path still keeps a raw encoded layer shape for future extensibility
- MVP encode/decode may use naive in-process type erasure instead of real serialization

Future extension path:

- Python can be hidden behind a built-in C++ provider, e.g. `PythonBinderProvider`
- External native plugins may later use C ABI + SDK wrapper
- Raw encoded payloads may later become JSON/CBOR/MessagePack/protobuf
- Current typed C++ capability API should not block those future options

---

# 1. Plugin Discovery / Registration Flow

## 1.1 MVP Plugin Model

MVP only supports built-in C++ plugins/providers.

A plugin is a module that registers one or more capability handlers into Core.

Example built-in modules:

```text
LocalFileProvider
LocalSoundProvider
AudioWaveformProvider
AudioPreviewProvider
BasicAssetActionProvider
```

A plugin may internally contain multiple capability handlers.

Example:

```text
LocalSoundPlugin
  - Asset descriptor handler
  - Local file locator handler
  - Audio metadata handler
  - Waveform handler
  - Preview action handler
```

In MVP, these are statically linked and registered during application startup.

No plugin folder scan, no external dylib loading, no Python worker loading in MVP.

---

## 1.2 Core Registry Responsibility

Core owns the runtime registry.

Core stores:

```text
providerId
capability name
capability version
handler pointer / handler backend
enabled / disabled state
priority
availability status
execution hints
```

Core does not understand domain-specific request/result types.

Core does not parse plugin private metadata.

Core only indexes and routes capability handlers.

---

## 1.3 Built-in Registration Flow

Application startup:

```text
Application starts
  ↓
Core creates PluginRegistry / CapabilityBroker
  ↓
Built-in modules register their handlers
  ↓
Registry indexes handlers by capability + version
  ↓
UI / services can query capabilities through broker
```

Example conceptual code:

```cpp
void registerBuiltInProviders(CapabilityRegistry& registry) {
    registry.registerHandler(std::make_unique<LocalFileDescriptorHandler>());
    registry.registerHandler(std::make_unique<LocalFileLocatorHandler>());
    registry.registerHandler(std::make_unique<LocalSoundMetadataHandler>());
    registry.registerHandler(std::make_unique<AudioWaveformHandler>());
    registry.registerHandler(std::make_unique<AudioPreviewHandler>());
}
```

---

## 1.4 Capability Declaration

Each handler declares:

```text
capability name
version
provider id
describe(query)
invokeRaw(rawRequest)
```

Example:

```text
capability: media.audio.waveform
version: 1
providerId: builtin.local_sound.waveform
```

The pair:

```text
capability + version
```

defines the typed contract.

Example:

```text
media.audio.waveform:v1
  Request = AssetSegment
  Result  = StandardWaveform
```

Another example:

```text
media.audio.multitrack_waveform:v1
  Request = AssetSegment
  Result  = MultitrackWaveform
```

A future version may use a different result type:

```text
media.audio.waveform:v2
  Request = AssetSegment
  Result  = MipmapWaveform
```

---

## 1.5 Capability Semantics

Capability names should be based on what the consumer wants, not how the provider implements it.

Example:

```text
media.audio.waveform
```

means:

> For a given asset/scope that has an audio-bearing interpretation, provide an audio waveform representation.

It does not mean:

> The asset is necessarily a local audio file.

Therefore the following can all provide the same capability:

```text
.wav file provider
.mp3 file provider
video provider extracting audio track waveform
remote/cloud audio provider
MCP cue render provider
virtual audio render provider
```

A waveform UI only needs to ask:

```text
Can this asset provide media.audio.waveform?
```

It should not need to know whether the asset is local audio, video, remote, or virtual.

---

## 1.6 Query Flow

Capability query is two-stage conceptually.

### Stage 1: Registry Coarse Query

Core uses its registry index:

```text
capability name + version
  → candidate handlers
```

This is cheap and does not require domain-specific decoding.

### Stage 2: Handler Runtime Description

Core asks each candidate handler:

```cpp
CapabilityDescriptor describe(const CapabilityQuery& query);
```

This checks whether the handler can actually provide the capability for the given asset/scope/context.

Example checks:

```text
Does the asset have readable content?
Can this provider interpret the asset as audio-bearing?
Is a cache already available?
Will this require a job?
Is provider currently available?
```

Result may include:

```text
available / unavailable
priority
longRunning
cancellable
execution kind
reason if unavailable
```

---

## 1.7 CapabilityRef / Handle

Consumers should not receive raw provider object pointers.

Core should return a lightweight capability reference/handle.

Example:

```cpp
struct CapabilityRef {
    ProviderId providerId;
    std::string capability;
    int version;
};
```

Consumer uses broker to invoke it.

Preferred shape:

```cpp
auto ref = broker.findBest<MediaAudioWaveformV1>(query);
auto future = broker.invoke<MediaAudioWaveformV1>(ref, request);
```

Or:

```cpp
auto handle = broker.findBestHandle<MediaAudioWaveformV1>(query);
auto future = handle.invoke(request);
```

This keeps provider lifetime, dispatch, cancellation, worker policy, and future backend changes inside Core.

---

# 2. Capability Invocation Flow

## 2.1 Key Principle

Capability invocation is typed at consumer/provider edges, but raw/encoded inside Core.

The flow is:

```text
typed request
  ↓ encode
raw request
  ↓ core dispatch
raw request
  ↓ provider decode
typed request
  ↓ provider implementation
typed result
  ↓ provider encode
raw result
  ↓ core future path
raw result
  ↓ consumer decode
typed result
```

This allows MVP to use C++ strong typing while preserving a future path toward IPC/C ABI/Python.

---

## 2.2 Contract Definition

A versioned capability contract defines:

```text
capability name
version
Request type
Result type
```

Example:

```cpp
struct MediaAudioWaveformV1 {
    static constexpr std::string_view capability = "media.audio.waveform";
    static constexpr int version = 1;

    using Request = AssetSegment;
    using Result  = StandardWaveform;
};
```

Example request/result:

```cpp
struct AssetSegment {
    AssetId assetId;
    std::optional<ContentVersionId> versionId;

    double startSeconds = 0.0;
    std::optional<double> durationSeconds;

    // Optional future extension:
    // audioTrackIndex, region, quality, etc.
};

struct StandardWaveform {
    double durationSeconds = 0.0;
    int channelCount = 0;
    int framesPerPeak = 0;

    std::vector<float> minValues;
    std::vector<float> maxValues;
};
```

---

## 2.3 Codec Role

Each contract has a codec.

Conceptually:

```cpp
template <typename Contract>
struct CapabilityCodec {
    static RawRequest encodeRequest(const typename Contract::Request& request);
    static typename Contract::Request decodeRequest(const RawRequest& raw);

    static RawResult encodeResult(const typename Contract::Result& result);
    static typename Contract::Result decodeResult(const RawResult& raw);
};
```

In MVP, this codec may use naive in-process type erasure.

Future versions can replace the codec implementation with binary serialization.

The rest of the system should not care.

---

## 2.4 MVP Raw Request / Result

MVP may use in-process erased objects instead of serialization.

Example conceptual shape:

```cpp
struct RawPayload {
    enum class Kind {
        InProcessObject,
        SerializedBytes
    };

    Kind kind = Kind::InProcessObject;

    std::type_index type = typeid(void);
    std::shared_ptr<const void> object;

    std::string format;
    std::vector<std::byte> bytes;
};

struct RawRequest {
    std::string capability;
    int version;
    ProviderId providerId;
    RequestId requestId;

    RawPayload payload;
};

struct RawResult {
    bool ok = true;

    std::string capability;
    int version;
    RequestId requestId;

    RawPayload payload;

    std::string errorCode;
    std::string errorMessage;
};
```

For MVP:

```text
RawPayload.kind = InProcessObject
RawPayload.object = shared_ptr<const Request/Result>
RawPayload.type = typeid(Request/Result)
```

Future:

```text
RawPayload.kind = SerializedBytes
RawPayload.format = cbor/json/msgpack/protobuf
RawPayload.bytes = encoded payload
```

---

## 2.5 Consumer-Side Invocation Flow

Consumer code:

```cpp
AssetSegment segment;
segment.assetId = assetId;

auto ref = broker.findBest<MediaAudioWaveformV1>(query);
auto future = broker.invoke<MediaAudioWaveformV1>(ref, segment);
```

Header-only typed invoke:

```cpp
template <typename Contract>
CapabilityFuture<typename Contract::Result>
CapabilityBroker::invoke(
    const CapabilityRef& ref,
    const typename Contract::Request& request
) {
    RawRequest raw = CapabilityCodec<Contract>::encodeRequest(request);

    raw.capability = std::string(Contract::capability);
    raw.version = Contract::version;
    raw.providerId = ref.providerId;

    CapabilityFuture<RawResult> rawFuture = invokeRaw(ref, std::move(raw));

    return rawFuture.then([](const RawResult& rawResult) {
        return CapabilityCodec<Contract>::decodeResult(rawResult);
    });
}
```

Important:

```text
invoke<Contract>() can be header-only.
It should stay thin.
It should only encode request, call invokeRaw, and decode result.
All dispatch/worker/logging/cancellation logic belongs in invokeRaw implementation.
```

---

## 2.6 Core Raw Invocation

Core implementation:

```cpp
CapabilityFuture<RawResult>
CapabilityBroker::invokeRaw(const CapabilityRef& ref, RawRequest request);
```

This function is not templated and can live in `.cpp`.

It is responsible for:

```text
handler lookup
provider enabled/disabled checks
availability checks
priority/fallback policy
worker/thread dispatch
future creation
cancellation
timeout
logging
error propagation
job integration if needed
```

MVP may implement it simply:

```text
lookup handler by CapabilityRef
call handler.invokeRaw(request, context)
return Future<RawResult>
```

Future implementations may route to:

```text
in-process C++ handler
Python binder
C ABI dynamic plugin
process worker
remote provider
```

The typed API should not change.

---

## 2.7 Provider-Side Raw-to-Typed Adapter

Provider authors should implement typed logic, not raw parsing.

Core SDK provides:

```cpp
class IRawCapabilityHandler {
public:
    virtual ~IRawCapabilityHandler() = default;

    virtual std::string_view capabilityName() const = 0;
    virtual int capabilityVersion() const = 0;
    virtual std::string_view providerId() const = 0;

    virtual CapabilityDescriptor describe(const CapabilityQuery& query) = 0;

    virtual CapabilityFuture<RawResult>
    invokeRaw(const RawRequest& request, CapabilityContext& ctx) = 0;
};
```

Core SDK also provides typed adapter:

```cpp
template <typename Contract>
class TypedCapabilityHandler : public IRawCapabilityHandler {
public:
    std::string_view capabilityName() const final {
        return Contract::capability;
    }

    int capabilityVersion() const final {
        return Contract::version;
    }

    CapabilityFuture<RawResult>
    invokeRaw(const RawRequest& raw, CapabilityContext& ctx) final {
        typename Contract::Request typedRequest =
            CapabilityCodec<Contract>::decodeRequest(raw);

        CapabilityFuture<typename Contract::Result> typedFuture =
            invokeTyped(typedRequest, ctx);

        return typedFuture.then([](const typename Contract::Result& result) {
            return CapabilityCodec<Contract>::encodeResult(result);
        });
    }

protected:
    virtual CapabilityFuture<typename Contract::Result>
    invokeTyped(const typename Contract::Request& request,
                CapabilityContext& ctx) = 0;
};
```

Provider implementation:

```cpp
class LocalSoundWaveformHandler
    : public TypedCapabilityHandler<MediaAudioWaveformV1> {
public:
    std::string_view providerId() const override {
        return "builtin.local_sound.waveform";
    }

    CapabilityDescriptor describe(const CapabilityQuery& query) override {
        // Check whether the asset/scope can provide audio waveform.
        // Return available/priority/execution info.
    }

protected:
    CapabilityFuture<StandardWaveform>
    invokeTyped(const AssetSegment& segment,
                CapabilityContext& ctx) override {
        // Actual implementation:
        // - check waveform cache
        // - decode/generate if needed
        // - return StandardWaveform
    }
};
```

---

## 2.8 Provider Decode/Encode Location

Decode happens inside the provider-side adapter.

Core does not decode typed request.

Provider chain:

```text
provider.invokeRaw(rawRequest)
  ↓
CapabilityCodec<Contract>::decodeRequest(rawRequest)
  ↓
provider.invokeTyped(typedRequest)
  ↓
CapabilityCodec<Contract>::encodeResult(typedResult)
  ↓
return RawResult
```

Consumer chain:

```text
broker.invoke<Contract>(typedRequest)
  ↓
CapabilityCodec<Contract>::encodeRequest(typedRequest)
  ↓
broker.invokeRaw(rawRequest)
  ↓
raw future resolves
  ↓
CapabilityCodec<Contract>::decodeResult(rawResult)
  ↓
typed future resolves
```

---

## 2.9 Error Types

Recommended error categories:

```text
no_handler
handler_unavailable
capability_version_mismatch
invalid_request
invalid_result
provider_error
timeout
cancelled
internal_error
```

Provider-side decode failure:

```text
invalid_request
```

Consumer-side decode failure:

```text
invalid_result
```

---

## 2.10 Future and Job Distinction

Capability invocation returns a future.

```text
CapabilityFuture<T>
```

This represents one invocation.

Long-running operations may return a job reference as their result.

Example:

```text
media.audio.waveform:v1
  may return StandardWaveform directly
  or may return a result indicating jobId/cache pending
```

Recommended distinction:

```text
Future = invocation completion channel
Job = persistent long-running task tracked by Core
```

MVP may keep this simple and return futures only.

---

# 3. Core Services Provided to Plugins

Core provides two storage mechanisms to plugins:

```text
metadata
cache
```

And one interaction mechanism:

```text
capability
```

Principle:

```text
metadata = plugin private long-term memory
cache     = plugin large/derived/evictable material
capability = public interaction interface
```

---

## 3.1 Plugin Metadata

Metadata is plugin-private persistent state.

Core does not interpret metadata.

Other plugins must not read another plugin's metadata.

Cross-plugin information must be exposed through capabilities.

Metadata characteristics:

```text
plugin-owned
persistent
small / size-limited
opaque to Core
not evictable
not a cross-plugin API
```

Suitable for:

```text
provider private state
scan state
remote asset id
small analysis result
AI caption summary
model version
plugin schema version
asset-specific plugin config
```

Not suitable for:

```text
waveform peaks
thumbnail image
proxy media file
large transcript
large embedding index
temporary downloaded file
```

---

## 3.2 Metadata Storage Shape

Core may store metadata as opaque blobs.

Do not force JSON at the core level.

MVP built-in plugins may use JSON for convenience, but Core should not depend on it.

Conceptual table:

```sql
asset_plugin_metadata (
  scope TEXT NOT NULL,          -- asset / content_version / provider
  scope_id TEXT NOT NULL,

  plugin_id TEXT NOT NULL,
  namespace TEXT NOT NULL,

  data_blob BLOB NOT NULL,
  data_format TEXT NOT NULL,    -- json / cbor / msgpack / custom
  schema_version INTEGER NOT NULL DEFAULT 1,
  size_bytes INTEGER NOT NULL,

  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,

  PRIMARY KEY (scope, scope_id, plugin_id, namespace)
);
```

Scope examples:

```text
asset
content_version
provider_binding
```

Example metadata:

```text
scope = content_version
scope_id = version_001
plugin_id = builtin.local_sound
namespace = audio_metadata
data_format = json
data_blob = {
  "durationSeconds": 2.18,
  "sampleRate": 48000,
  "channels": 2,
  "codec": "pcm_s24le"
}
```

---

## 3.3 Metadata API

Plugins should only access their own metadata through a scoped PluginStorage API.

Example:

```cpp
class PluginMetadataStore {
public:
    std::optional<PluginMetadataRecord> read(
        ScopeRef scope,
        std::string_view namespaceId
    );

    void write(
        ScopeRef scope,
        std::string_view namespaceId,
        std::span<const std::byte> data,
        std::string_view dataFormat,
        int schemaVersion
    );

    void remove(
        ScopeRef scope,
        std::string_view namespaceId
    );
};
```

Core enforces:

```text
plugin can only read/write its own plugin_id
metadata size limit
metadata lifetime follows scope lifetime
```

Core does not expose:

```text
readMetadataOfOtherPlugin()
parsePluginMetadata()
queryByPluginMetadataField()
```

---

## 3.4 Metadata Size Limit

Metadata should be bounded.

Example policy:

```text
single metadata record max: 64 KB or 256 KB
per asset/plugin metadata max: 1 MB
```

If a plugin needs larger storage, it should use cache or external storage.

---

## 3.5 Plugin Cache

Cache is plugin-owned derived material that Core may evict.

Cache characteristics:

```text
plugin-owned
large-data friendly
evictable
not guaranteed persistent
usually file/URI based
may become stale
must be regeneratable or recoverable
```

Suitable for:

```text
waveform peaks
video thumbnails
proxy audio/video
spectrogram image
downloaded remote file
materialized cloud asset
AI intermediate file
embedding index file
```

Plugin must treat cache miss as normal.

Correct pattern:

```text
find cache
  if valid:
    use it
  else:
    regenerate / schedule job / fallback
```

---

## 3.6 Cache Storage Shape

Conceptual table:

```sql
asset_plugin_cache (
  id TEXT PRIMARY KEY,

  scope TEXT NOT NULL,          -- asset / content_version / provider
  scope_id TEXT NOT NULL,

  plugin_id TEXT NOT NULL,
  cache_type TEXT NOT NULL,
  cache_key TEXT,

  uri TEXT NOT NULL,
  size_bytes INTEGER NOT NULL DEFAULT 0,

  data_format TEXT,
  cache_version TEXT NOT NULL DEFAULT '1',
  status TEXT NOT NULL DEFAULT 'ready',

  evictable INTEGER NOT NULL DEFAULT 1,
  pinned INTEGER NOT NULL DEFAULT 0,

  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  last_accessed_at TEXT,

  UNIQUE(scope, scope_id, plugin_id, cache_type, cache_key, cache_version)
);
```

Example:

```text
scope = content_version
scope_id = version_001
plugin_id = builtin.local_sound
cache_type = waveform.peaks
cache_key = default
uri = file:///.../cache/waveforms/version_001.peaks
size_bytes = 18432
cache_version = waveform-peaks-v1
evictable = true
```

---

## 3.7 Cache API

Core provides a cache service.

Example:

```cpp
class PluginCacheStore {
public:
    std::optional<CacheEntry> find(
        ScopeRef scope,
        std::string_view cacheType,
        std::string_view cacheKey,
        std::string_view cacheVersion
    );

    CacheEntry registerCache(
        ScopeRef scope,
        std::string_view cacheType,
        std::string_view cacheKey,
        std::string_view cacheVersion,
        Uri uri,
        int64_t sizeBytes,
        CacheOptions options
    );

    void markAccessed(CacheId cacheId);

    void remove(CacheId cacheId);
};
```

Core is responsible for:

```text
cache size accounting
eviction policy
last accessed timestamp
asset deletion cascade
cache directory management
pinned/non-evictable handling
```

Plugin is responsible for:

```text
knowing how to regenerate cache
validating whether cache content is useful
defining cache_type/cache_key/cache_version
```

---

## 3.8 Cache Eviction

Core may evict cache entries based on:

```text
global cache size limit
per-plugin cache size limit
least recently used
cache type policy
manual cleanup
asset deletion
stale content version
```

Plugin must not assume cache persistence.

Cache is not source of truth.

---

## 3.9 Metadata vs Cache Boundary

Use metadata when data is:

```text
small
persistent
private plugin state
needed for plugin correctness
not safely evictable
```

Use cache when data is:

```text
large
derived
rebuildable
temporary or performance-oriented
safe to evict
```

Examples:

```text
duration/sampleRate/channels → metadata
waveform peak file → cache

AI caption summary → metadata
full transcript for long media → cache or document ref

remote asset id → metadata
downloaded remote media file → cache

embedding model name → metadata
large vector index → cache/external index
```

---

## 3.10 Capability Access to Other Plugins

Plugins must not communicate by reading each other's metadata.

Wrong:

```text
AudioPreviewProvider reads LocalFileProvider metadata to get path
```

Correct:

```text
AudioPreviewProvider asks broker for a capability:
  builtin.provide_locator
  or builtin.read_stream
  or media.audio.waveform
```

Rule:

```text
Plugin metadata is private storage.
Capabilities are the only public interface.
```

---

# 4. Capability Categories

Capabilities are divided into two categories:

```text
1. Builtin capabilities
2. Plugin-defined capabilities
```

The distinction is important:

```text
builtin capability
  = capability required by Core or Core UI

plugin-defined capability
  = capability defined by a plugin/domain module and consumed by other plugins or UI modules
```

Core should only depend on builtin capabilities.

Domain-specific abilities such as waveform, audio preview, AI captioning, remote sync, etc. should be plugin-defined capabilities unless Core itself directly requires them.

---

## 4.1 Builtin Capabilities

Builtin capabilities are capabilities that Core needs in order to operate the asset browser.

They are part of the Core SDK.

Builtin capability contracts are defined by Core and are always authoritative.

If a plugin claims to provide a builtin capability, it must use the exact Core-defined contract for that capability/version.

If it does not, registration fails.

Builtin capabilities are divided by scope.

---

## 4.2 Asset-Scoped Builtin Capabilities

Asset-scoped builtin capabilities operate on a specific asset or asset-like scope.

These are used by Core UI to display and operate assets without knowing the asset's domain type.

Examples:

```text
builtin.asset.name
builtin.asset.kind
builtin.asset.thumbnail
builtin.asset.open_actions
```

These capabilities do not mean Core understands audio/video/image/etc.

They mean Core can ask plugins:

```text
How should this asset be displayed?
What kind/category should be shown to the user?
What thumbnail/visual should be shown?
What actions can the user perform on this asset?
```

---

### 4.2.1 Asset Name

Core owns the user-facing asset name.

```text
asset.name
  = user/team-facing canonical name
  = editable
  = stored in Core asset table
```

However, plugins may provide name candidates/descriptors.

Builtin capability:

```text
builtin.asset.name:v1
  Request = AssetRef
  Result  = AssetNameDescriptor
```

Example:

```cpp
struct AssetNameDescriptor {
    std::string text;
    std::string source;      // file_name, remote_title, ai_summary, cue_label
    bool canSuggestName = true;
    int priority = 0;
};
```

Rules:

```text
Core asset.name is authoritative for the main display name.
Plugin-provided names are descriptors/candidates.
Plugin-provided names may be used as default suggestions.
Plugin-provided names should not overwrite user-edited asset.name automatically.
```

---

### 4.2.2 Asset Kind / Type

Core should not hardcode domain behavior based on asset type.

However, Core UI often needs a user-facing kind/category label.

Builtin capability:

```text
builtin.asset.kind:v1
  Request = AssetRef
  Result  = AssetKindDescriptor
```

Example:

```cpp
struct AssetKindDescriptor {
    std::string kind;        // audio, video, image, document, cue, virtual, unknown
    std::string label;       // user-facing label
    std::string mimeHint;    // optional
    double confidence = 1.0;
    int priority = 0;
};
```

Rules:

```text
Asset kind is presentation/classification information.
It should not be used by Core to directly execute domain logic.
Domain logic should still go through capabilities.
```

Example:

```text
A video asset may provide:
  builtin.asset.kind = video

The same asset may also provide:
  media.audio.waveform
  media.audio.preview
```

This allows an audio preview UI to consume the video asset as an audio-bearing asset without Core needing to know video internals.

---

### 4.2.3 Asset Thumbnail / Visual

Core UI needs a way to show a visual representation for an asset.

This should be builtin because the asset browser UI needs it.

Builtin capability:

```text
builtin.asset.thumbnail:v1
  Request = AssetRef
  Result  = AssetThumbnailDescriptor
```

The result should be general enough to represent icons, image thumbnails, waveform previews, text badges, or other simple visuals.

Example:

```cpp
struct AssetThumbnailDescriptor {
    enum class Kind {
        Icon,
        Image,
        CacheRef,
        TextBadge,
        ColorBlock,
        None
    };

    Kind kind = Kind::None;

    std::string iconKey;
    std::string cacheId;
    std::string uri;
    std::string text;

    int priority = 0;
};
```

Rules:

```text
Core asks for a thumbnail/visual.
Plugins decide how to represent the asset.
Core does not assume the thumbnail is a bitmap mipmap.
```

Examples:

```text
local audio plugin:
  may return waveform-like cache thumbnail

video plugin:
  may return poster frame cache

remote plugin:
  may return cloud icon or cached remote thumbnail

AI plugin:
  may return text badge or semantic tag
```

---

### 4.2.4 Asset Open Actions

Core UI needs to know what the user can do when opening or interacting with an asset.

Builtin capability:

```text
builtin.asset.open_actions:v1
  Request = AssetRef
  Result  = AssetOpenActions
```

Example:

```cpp
struct AssetOpenAction {
    std::string actionId;
    std::string label;
    std::string providerId;

    bool defaultCandidate = false;
    int priority = 0;

    // optional UI hints
    std::string iconKey;
};

struct AssetOpenActions {
    std::vector<AssetOpenAction> actions;
};
```

Action execution may use a paired builtin capability:

```text
builtin.asset.execute_action:v1
  Request = AssetActionRequest
  Result  = AssetActionResult
```

Example:

```cpp
struct AssetActionRequest {
    AssetId assetId;
    std::string actionId;
    std::map<std::string, std::string> options;
};

struct AssetActionResult {
    bool accepted = true;
    std::string message;
};
```

Rules:

```text
Core collects open/action candidates.
Core chooses default action by priority/user preference.
User may choose "Open With" if multiple actions exist.
Actual behavior remains plugin-owned.
```

Examples:

```text
local sound plugin:
  Preview
  Reveal in Finder
  Regenerate waveform

ClickIn plugin:
  Open in ClickIn
  Sync to production

MCP plugin:
  Send to cue
  Open in MCP project
```

---

## 4.3 Global-Scoped Builtin Capabilities

Global-scoped builtin capabilities are not attached to a specific asset.

They are used by Core to discover or create asset records.

The most important MVP example is asset discovery.

---

### 4.3.1 Asset Discovery

Asset discovery is builtin because Core needs a standard way to ask providers to discover assets.

Builtin capability:

```text
builtin.asset.discovery:v1
  Request = AssetDiscoveryRequest
  Result  = AssetDiscoveryResult
```

Example:

```cpp
struct AssetDiscoveryRequest {
    std::string sourceType;   // local.folder, clickin.production, r2.bucket, etc.
    std::string uri;
    std::map<std::string, std::string> options;
};

struct DiscoveredAsset {
    std::string suggestedName;
    std::string providerId;

    // Optional identity hints.
    std::string fingerprint;
    int64_t sizeBytes = -1;

    // Provider-specific data should not be interpreted by Core.
    // It may be stored as provider metadata.
};

struct AssetDiscoveryResult {
    std::vector<DiscoveredAsset> assets;
};
```

Rules:

```text
Core does not scan assets directly.
Core asks discovery providers to discover assets.
Discovery providers return discovered asset candidates.
Core creates or updates asset records.
Provider-specific state is stored as plugin metadata/provider metadata.
```

Example discovery providers:

```text
Local folder discovery
ClickIn production discovery
R2 bucket discovery
MCP project discovery
Future P2P/team library discovery
```

MVP may only implement local folder discovery.

---

## 4.4 Plugin-Defined Capabilities

Plugin-defined capabilities are not required by Core.

They are defined by plugins or domain modules.

Examples:

```text
media.audio.waveform
media.audio.preview
media.audio.metadata
media.video.thumbnail
ai.audio.caption
ai.audio.recommend
remote.sync
clickin.mount_asset
mcp.send_to_cue
```

Core does not interpret the semantics of plugin-defined capabilities.

Core only stores and routes:

```text
capability name
version
provider id
handler reference/backend
availability
priority
```

Consumers that understand a plugin-defined capability may query and invoke it.

---

## 4.5 Plugin-Defined Capability Declaration

Plugin-defined capabilities are declared in the plugin manifest or built-in plugin registration metadata.

Example:

```text
capability: media.audio.waveform
version: 1
contract:
  Request = AssetSegment
  Result  = StandardWaveform
```

For MVP built-in C++ providers, the manifest can be represented by static registration code rather than an external JSON manifest.

Future external plugins should declare plugin-defined capabilities in their manifest.

Example future manifest shape:

```json
{
  "id": "com.example.video",
  "capabilities": [
    {
      "capability": "media.audio.waveform",
      "version": 1,
      "contract": "media.audio.waveform:v1"
    }
  ]
}
```

---

## 4.6 Capability Interface Authority

For plugin-defined capabilities, the first authoritative definition should preferably come from a built-in plugin or built-in domain module.

Example:

```text
builtin media/audio module defines:
  media.audio.waveform:v1
    Request = AssetSegment
    Result  = StandardWaveform
```

Other plugins may implement the same capability/version only if they use the same contract.

Example valid providers:

```text
local audio provider implements media.audio.waveform:v1
video provider implements media.audio.waveform:v1
remote audio provider implements media.audio.waveform:v1
MCP cue render provider implements media.audio.waveform:v1
```

All of them must accept:

```text
AssetSegment
```

and return:

```text
StandardWaveform
```

for version 1.

---

## 4.7 Registration Rule for Same Capability

If an external plugin registers a capability/version that already exists, it must use the same contract as the registered standard.

Example existing standard:

```text
media.audio.waveform:v1
  Request = AssetSegment
  Result  = StandardWaveform
```

Valid external registration:

```text
media.audio.waveform:v1
  Request = AssetSegment
  Result  = StandardWaveform
```

Invalid external registration:

```text
media.audio.waveform:v1
  Request = VideoAssetSegment
  Result  = CustomWaveformFormat
```

Result:

```text
registration fails
```

or:

```text
that capability entry is disabled
```

The plugin may still register a different capability name if it wants a different interface.

Example:

```text
com.vendor.custom_waveform:v1
  Request = VendorWaveformRequest
  Result  = VendorWaveformResult
```

But it should not claim to be:

```text
media.audio.waveform:v1
```

unless it uses the standard contract.

---

## 4.8 Hard vs Soft Enforcement

Builtin capabilities:

```text
hard enforced
```

If a plugin provides a builtin capability with the wrong interface, registration fails.

Plugin-defined capabilities with established built-in/domain standard:

```text
hard enforced for same capability + version
```

If a plugin registers the same capability/version with a different interface, registration fails.

Plugin-defined capabilities without existing standard:

```text
allowed as new capability
```

The defining plugin becomes the initial authority for that capability/version.

However, other plugins should not consume that capability unless they depend on that defining interface.

---

## 4.9 Capability Versioning Rule

A capability version defines the fixed request/result contract.

Example:

```text
media.audio.waveform:v1
  Request = AssetSegment
  Result  = StandardWaveform

media.audio.waveform:v2
  Request = AssetSegment
  Result  = MipmapWaveform

media.audio.multitrack_waveform:v1
  Request = AssetSegment
  Result  = MultitrackWaveform
```

Rules:

```text
same capability + same version = same contract
same capability + different version = contract may change
different capability = different semantic ability
```

---

## 4.10 Reusable Payload Types

Some result/request payload types may be reused by multiple capabilities.

Example:

```text
StandardWaveform
```

may be used by:

```text
media.audio.waveform:v1
media.cue.rendered_waveform:v1
future preview-related waveform capabilities
```

However, reusable payload types are not themselves capabilities.

A capability version still defines a full contract:

```text
RequestType -> ResultType
```

Example:

```text
media.audio.waveform:v1
  AssetSegment -> StandardWaveform
```

The reusable part is:

```text
StandardWaveform
```

not the whole capability.

---

## 4.11 Minimal MVP Capability Examples

Builtin asset-scoped capabilities:

```text
builtin.asset.name:v1
builtin.asset.kind:v1
builtin.asset.thumbnail:v1
builtin.asset.open_actions:v1
builtin.asset.execute_action:v1
```

Builtin global-scoped capabilities:

```text
builtin.asset.discovery:v1
```

Plugin-defined capabilities:

```text
media.audio.metadata:v1
media.audio.waveform:v1
media.audio.preview:v1
```

MVP local sound workflow may use:

```text
builtin.asset.discovery:v1
builtin.asset.name:v1
builtin.asset.kind:v1
builtin.asset.thumbnail:v1
builtin.asset.open_actions:v1
media.audio.metadata:v1
media.audio.waveform:v1
media.audio.preview:v1
```

---

## 4.12 Core Dependency Rule

Core may directly depend on builtin capabilities.

Core should not directly depend on plugin-defined capabilities.

Allowed:

```text
Core asset browser UI asks:
  builtin.asset.name
  builtin.asset.kind
  builtin.asset.thumbnail
  builtin.asset.open_actions
```

Not allowed:

```text
Core hardcodes:
  if audio then call media.audio.waveform
```

Instead, domain UI modules may depend on plugin-defined capabilities.

Example:

```text
AudioPreviewPanel depends on:
  media.audio.waveform
  media.audio.preview
```

Core can host the panel, but the dependency belongs to the audio/domain UI module, not Core itself.

---

# 5. Current MVP Simplifications

MVP intentionally does not implement:

```text
external plugin folder scan
dynamic dylib loading
C ABI plugin boundary
Python plugin runtime
IPC
schema validation
JSON/CBOR/protobuf serialization
third-party plugin capability standardization
complex version negotiation
```

MVP does implement:

```text
built-in C++ provider registry
capability query
typed invoke API
raw encoded invoke path
naive in-process type erasure
plugin metadata storage
plugin cache storage
local sound workflow
```

The goal is:

> Architecture-level plugin boundaries without building a full plugin ecosystem too early.

Future extension path remains possible because the core invoke path already has:

```text
typed request
  → encode
  → raw request
  → raw dispatch
  → provider decode
  → typed implementation
  → provider encode
  → raw result
  → consumer decode
  → typed result
```

Only the encode/decode implementation needs to evolve from naive type erasure to real serialization when required.

# 6. Basic Core Startup Flow

## 6.1 Core Startup Goal

Core startup should establish the minimum runtime environment required for:

```text
asset database
plugin registry
capability broker
metadata/cache services
job/future system
UI shell
```

Core startup should not hardcode domain behavior such as audio/video/AI processing.

Domain behavior should be activated by plugins/providers.

---

## 6.2 MVP Startup Sequence

MVP startup sequence:

```text
Application starts
  ↓
Initialize Core Services
  ↓
Open database
  ↓
Run database migrations
  ↓
Initialize metadata/cache services
  ↓
Initialize job/future runtime
  ↓
Initialize capability registry/broker
  ↓
Discover plugins
  ↓
Update plugin registry
  ↓
Activate enabled plugins
  ↓
Register plugin capabilities
  ↓
Start UI shell
  ↓
Load asset list
```

---

## 6.3 Initialize Core Services

Core creates the basic services:

```text
DatabaseService
MetadataService
CacheService
JobService
PluginRegistry
CapabilityBroker
AssetService
SettingsService
```

Core services should be available through a controlled context object.

Example:

```cpp
struct CoreContext {
    DatabaseService& database;
    AssetService& assets;
    MetadataService& metadata;
    CacheService& cache;
    JobService& jobs;
    CapabilityBroker& capabilities;
    SettingsService& settings;
};
```

Plugins should not access raw database connections directly.

Plugins should access Core functionality through scoped services.

---

## 6.4 Database Startup

Core opens the application database.

Startup steps:

```text
open database
check schema version
run pending core migrations
run pending built-in plugin migrations if needed
validate required tables
```

Minimum MVP tables:

```text
asset
asset_provider
asset_provider_capability
asset_plugin_metadata
asset_plugin_cache
job
plugin_registry
schema_migration
```

MVP can keep this simple. The important part is that Core owns schema migration and lifecycle.

---

## 6.5 Cache Startup

Core initializes cache directories and cache accounting.

Startup steps:

```text
ensure cache root exists
load cache entries from database
verify basic cache directory access
optionally mark missing cache files as stale/missing
calculate approximate cache size
apply eviction policy if over limit
```

MVP does not need aggressive validation of every cache file on startup.

A lazy validation strategy is acceptable:

```text
cache entry is checked when plugin tries to use it
```

---

## 6.6 Plugin Discovery

Plugin discovery finds available plugins/providers.

MVP only supports built-in plugins.

Therefore MVP discovery is static:

```text
register built-in plugin descriptors
register built-in plugin factory functions
```

Future external plugin discovery may include:

```text
scan plugin folders
read plugin manifest
validate plugin compatibility
update plugin_registry table
load enabled plugins
```

MVP should still model built-in plugins as discovered plugins, so the future external plugin path does not require a redesign.

---

## 6.7 Update Plugin Registry

Core maintains a registry of known plugins.

For each discovered plugin:

```text
plugin_id
name
version
runtime
builtin/external
enabled
load_status
last_error
declared capabilities
```

MVP `plugin_registry` may contain built-in plugins too.

Example conceptual table:

```sql
plugin_registry (
  plugin_id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  version TEXT NOT NULL,
  runtime TEXT NOT NULL,        -- builtin, future: python-worker, native-worker
  builtin INTEGER NOT NULL DEFAULT 0,
  enabled INTEGER NOT NULL DEFAULT 1,

  load_status TEXT NOT NULL,    -- discovered, active, disabled, failed
  last_error TEXT,

  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
```

For MVP:

```text
builtin plugins are always discoverable
some critical builtin plugins may not be disableable
plugin management UI may still show them
```

---

## 6.8 Plugin Activation

After discovery and registry update, Core activates enabled plugins.

Activation flow:

```text
for each enabled plugin:
  create plugin instance
  provide PluginContext/CoreContext
  call plugin.initialize()
  plugin registers capability handlers
  Core indexes capability handlers
  update plugin load_status
```

Example:

```cpp
class IPlugin {
public:
    virtual PluginManifest manifest() const = 0;

    virtual void initialize(PluginContext& context) = 0;
    virtual void shutdown() = 0;

    virtual std::vector<std::unique_ptr<IRawCapabilityHandler>>
    createCapabilityHandlers() = 0;
};
```

MVP may simplify this further:

```cpp
void LocalSoundPlugin::registerCapabilities(CapabilityRegistry& registry);
```

But conceptually, plugin activation should have a lifecycle.

---

## 6.9 Capability Registration During Activation

A plugin registers each capability handler.

For each handler, Core records:

```text
providerId
pluginId
capability
version
priority
scope support
handler/backend
```

Example:

```text
plugin: builtin.local_audio
handler: LocalAudioDiscoveryHandler
capability: builtin.asset.discovery
version: 1

plugin: builtin.local_audio
handler: LocalAudioWaveformHandler
capability: media.audio.waveform
version: 1

plugin: builtin.local_audio
handler: LocalAudioPreviewHandler
capability: media.audio.preview
version: 1
```

Core validates:

```text
builtin capabilities use Core-defined contracts
existing plugin-defined capability/version uses matching contract
provider id is unique
capability/version registration is valid
```

---

## 6.10 Startup Error Handling

Plugin activation failure should not crash the whole app unless the failed plugin is critical.

Recommended behavior:

```text
non-critical plugin fails:
  mark plugin load_status = failed
  store last_error
  continue startup

critical plugin fails:
  show startup error
  optionally abort startup
```

Critical MVP plugins may include:

```text
core asset database
basic UI shell
```

Local audio plugin failure should degrade functionality but should not necessarily prevent the app from opening.

---

## 6.11 Asset List Loading

After plugin activation, Core loads basic asset records.

Startup UI does not need all plugin metadata immediately.

Initial asset list can load:

```text
asset id
asset.name
basic status
available builtin descriptors if cached
```

Plugin-provided presentation can be lazy:

```text
visible rows request builtin.asset.name/kind/thumbnail
inspector requests details for selected asset
preview panel requests media.audio.waveform/media.audio.preview when needed
```

This avoids doing expensive plugin calls for every asset on startup.

---

## 6.12 Shutdown Flow

Shutdown flow:

```text
stop accepting new capability invocations
cancel or finish active jobs according to policy
call plugin.shutdown()
flush metadata/cache registry writes
close database
release cache locks/resources
```

Plugins should not rely on destructors alone for important cleanup.

---

# 7. Basic Built-in Plugins

## 7.1 MVP Built-in Plugin Set

MVP should include a small set of built-in plugins/providers:

```text
Core Builtin Asset Plugin
Local File Discovery Plugin
Local Audio Metadata Plugin
Local Audio Waveform Plugin
Local Audio Preview Plugin
Basic Asset Action Plugin
```

These may be implemented as separate classes or grouped inside one built-in module initially.

Recommended grouping for MVP:

```text
builtin.core_asset
builtin.local_file
builtin.local_audio
```

Where:

```text
builtin.core_asset
  - asset name/kind/thumbnail/open action fallback

builtin.local_file
  - local folder discovery
  - file locator
  - reveal in Finder/File Explorer

builtin.local_audio
  - audio metadata
  - waveform generation
  - audio preview/playback
```

---

## 7.2 Core Builtin Asset Plugin

Purpose:

```text
provide fallback asset presentation/actions
```

Capabilities:

```text
builtin.asset.name:v1
builtin.asset.kind:v1
builtin.asset.thumbnail:v1
builtin.asset.open_actions:v1
builtin.asset.execute_action:v1
```

Fallback behavior:

```text
name:
  use asset.name from Core asset table

kind:
  unknown / generic asset

thumbnail:
  generic icon

open_actions:
  no-op or generic inspect action
```

This ensures every asset can be displayed even when no domain plugin can handle it.

---

## 7.3 Local File Discovery Plugin

Purpose:

```text
scan local folders and create asset records
```

Capabilities:

```text
builtin.asset.discovery:v1
builtin.asset.name:v1
builtin.asset.kind:v1
builtin.provide_locator:v1
builtin.asset.open_actions:v1
builtin.asset.execute_action:v1
```

Responsibilities:

```text
scan configured local folders
detect supported files
create discovered asset candidates
store provider-specific local file state in plugin metadata
provide local file locator
provide file-name-based descriptor/name candidate
provide reveal/open-in-folder action
```

Local file provider metadata may include:

```text
path
observed size
observed modified time
weak fingerprint
last seen time
```

This metadata is private to the local file plugin.

Other plugins should not read it directly.

Other plugins should request locator/read capabilities instead.

---

## 7.4 Local File Discovery Flow

User adds a local folder source:

```text
User selects folder
  ↓
Core creates asset source entry
  ↓
Core invokes builtin.asset.discovery:v1 on local file plugin
  ↓
Local file plugin scans folder
  ↓
Plugin returns discovered asset candidates
  ↓
Core creates/updates asset records
  ↓
Core creates/updates asset_provider entries
  ↓
Local file plugin stores provider metadata
```

MVP can simplify by having the local file plugin directly call AssetService through a controlled API, but conceptually discovery returns candidates and Core owns asset creation.

---

## 7.5 Local File Locator

The local file plugin provides a locator for local assets.

Capability:

```text
builtin.provide_locator:v1
  Request = AssetRef
  Result  = AssetLocator
```

Example result:

```cpp
struct AssetLocator {
    std::string scheme;   // file
    std::string uri;      // file:///...
    bool local = true;
    bool seekable = true;
};
```

Future versions may split this into:

```text
provide locator
resolve locator
read stream
materialize local
```

MVP can keep it simple.

---

## 7.6 Local Audio Metadata Plugin

Purpose:

```text
extract basic audio metadata
```

Capabilities:

```text
media.audio.metadata:v1
```

Possible contract:

```text
media.audio.metadata:v1
  Request = AssetRef or AssetSegment
  Result  = AudioMetadata
```

Example result:

```cpp
struct AudioMetadata {
    double durationSeconds = 0.0;
    int sampleRate = 0;
    int channelCount = 0;

    std::string codec;
    std::string container;
    int64_t bitRate = 0;
};
```

Responsibilities:

```text
obtain local file/stream through capability
decode or inspect audio header
store metadata in plugin metadata
return typed metadata result
```

Possible implementation options:

```text
libsndfile
FFmpeg/libavformat
Qt Multimedia metadata
custom lightweight wav/aiff parser for MVP
```

MVP can begin with a narrow supported format set, such as wav/aiff/mp3 via FFmpeg or Qt.

---

## 7.7 Local Audio Waveform Plugin

Purpose:

```text
generate and provide standard audio waveform data
```

Capability:

```text
media.audio.waveform:v1
  Request = AssetSegment
  Result  = StandardWaveform
```

Responsibilities:

```text
check cache for waveform
if cache exists and valid:
  load or reference cache
else:
  decode audio and generate waveform
  register waveform cache
  return StandardWaveform or cache-backed waveform
```

Cache type:

```text
waveform.peaks
```

Suggested cache key:

```text
default
```

Suggested cache version:

```text
waveform-peaks-v1
```

Waveform should be considered cache, not metadata.

Reason:

```text
waveform data can be large
waveform data is derived
waveform data can be regenerated
Core may evict it
```

---

## 7.8 Standard Waveform

MVP standard waveform:

```cpp
struct StandardWaveform {
    double durationSeconds = 0.0;

    int channelCount = 0;
    int framesPerPeak = 0;

    std::vector<float> minValues;
    std::vector<float> maxValues;
};
```

Notes:

```text
This is a simple v1 representation.
It does not need to support every future UI need.
Future media.audio.waveform:v2 may return MipmapWaveform.
Future media.audio.multitrack_waveform:v1 may return MultitrackWaveform.
```

---

## 7.9 Local Audio Preview Plugin

Purpose:

```text
play/preview audio-bearing assets
```

Capability:

```text
media.audio.preview:v1
  Request = AssetSegment
  Result  = PreviewSessionRef
```

Possible result:

```cpp
struct PreviewSessionRef {
    std::string sessionId;
    bool supportsSeek = true;
    bool supportsPause = true;
    bool supportsLoop = false;
};
```

Responsibilities:

```text
obtain local file/stream through locator/read capability
create playback session
support play/pause/stop/seek
notify UI of playback state
```

MVP implementation can use:

```text
Qt Multimedia
miniaudio
custom C++ audio preview backend
existing MCP/audio backend if convenient
```

Preview is plugin-owned behavior, but Core UI can use the capability through a preview panel.

---

## 7.10 Audio Preview and Waveform Relationship

Audio preview UI may consume both:

```text
media.audio.waveform:v1
media.audio.preview:v1
```

The UI should not require the asset to be a local audio file.

It should only require:

```text
asset can provide media.audio.waveform
asset can provide media.audio.preview
```

This allows future providers such as:

```text
video audio track provider
remote audio provider
MCP cue render provider
virtual audio provider
```

to work with the same UI.

---

## 7.11 Basic Asset Action Plugin

Purpose:

```text
provide basic user actions for assets
```

Capabilities:

```text
builtin.asset.open_actions:v1
builtin.asset.execute_action:v1
```

Basic actions:

```text
Preview
Reveal in Finder/File Explorer
Regenerate waveform
Inspect metadata
```

Open action selection:

```text
Core collects action candidates
Core chooses default by priority/user preference
User can choose "Open With" if multiple actions exist
```

---

# 8. Basic UI Modules

## 8.1 UI Goal

The MVP UI should expose the asset browser without depending on specific domain logic inside Core.

The UI should be divided into:

```text
Core UI
Domain/plugin UI
```

Core UI is always present.

Plugin UI appears when relevant plugins are active.

---

## 8.2 MVP Required UI Modules

MVP should at least include:

```text
Plugin Management View
Asset List View
Asset Inspector / Details Panel
Preview Panel
Job/Status Area
```

Minimum required by user request:

```text
Plugin Management View
Asset List View
```

Recommended practical MVP also includes:

```text
Asset Inspector
Preview Panel
```

because audio browsing is hard to use without preview.

---

## 8.3 Plugin Management View

Purpose:

```text
show discovered/registered plugins
show enabled/disabled status
show load errors
show provided capabilities
allow enabling/disabling non-critical plugins
```

MVP plugin management fields:

```text
plugin name
plugin id
version
runtime
builtin/external
enabled
load status
last error
capabilities
```

Example UI table:

```text
Name              Runtime   Status    Enabled   Capabilities
Local File        builtin   active    yes       discovery, locator
Local Audio       builtin   active    yes       metadata, waveform, preview
Core Asset        builtin   active    yes       name, kind, thumbnail, actions
```

MVP actions:

```text
enable plugin
disable plugin
view capabilities
view last error
reload/reinitialize plugin if safe
```

For critical built-in plugins:

```text
disable button should be unavailable
```

Future plugin management may add:

```text
install external plugin
remove plugin
scan plugin folder
trust/untrust plugin
plugin settings
dependency status
```

---

## 8.4 Asset List View

Purpose:

```text
show asset catalog
allow selection
allow basic open/preview action
```

Minimum columns:

```text
name
kind
thumbnail/icon
availability/status
source/provider
modified/imported time
```

MVP list behavior:

```text
load asset records from Core asset table
request builtin.asset.name/kind/thumbnail lazily for visible rows
show fallback data if plugin presentation is unavailable
select asset to show details
double-click invokes default open action
right-click shows open/action menu
```

Asset list should not hardcode:

```text
audio duration
sample rate
waveform
video thumbnail
AI tags
```

Those should appear through plugin-provided columns/details later.

---

## 8.5 Asset Name Display

Display name source priority:

```text
1. asset.name from Core asset table
2. plugin-provided name descriptor if asset.name is empty/default
3. fallback asset id
```

User-edited asset name should not be overwritten automatically by plugin descriptors.

---

## 8.6 Asset Thumbnail Display

Asset list asks:

```text
builtin.asset.thumbnail:v1
```

If no plugin thumbnail is available:

```text
show generic icon
```

For local audio MVP:

```text
audio plugin may return waveform thumbnail/cache
or simple audio icon
```

Waveform panel itself should use:

```text
media.audio.waveform:v1
```

not the generic thumbnail capability.

---

## 8.7 Asset Inspector / Details Panel

Purpose:

```text
show details for selected asset
```

MVP sections:

```text
Basic Info
Providers
Plugin Metadata Summary
Cache Entries
Available Actions
```

Important rule:

```text
Inspector may show plugin metadata only through plugin-provided presentation/summary capability, or as debug/private view.
Core should not parse arbitrary plugin metadata as product logic.
```

For MVP internal/debug mode, it is acceptable to show raw plugin metadata blob for development.

---

## 8.8 Preview Panel

Purpose:

```text
preview selected asset if any plugin provides preview capability
```

For audio MVP:

```text
query media.audio.waveform:v1
query media.audio.preview:v1
```

If available:

```text
show waveform
show play/pause/stop
show position/time
support seek if preview session supports it
```

If unavailable:

```text
show "No preview available"
```

Preview panel should not care whether the asset is:

```text
local audio
video with audio track
remote audio
virtual audio
```

It only cares about capabilities.

---

## 8.9 Job / Status Area

Purpose:

```text
show background operations
```

Examples:

```text
scanning folder
extracting metadata
generating waveform
building preview cache
```

MVP may show:

```text
current job label
progress
error if failed
cancel button if supported
```

Even if MVP uses simple futures, having a job/status area will make future long-running task support easier.

---

## 8.10 Future Search UI

Search is not required for the first MVP, but the UI should leave room for it.

Future search may include:

```text
name search
tag search
plugin-provided metadata search
AI semantic search
filter by provider/source
filter by availability
```

Search should not be built around hardcoded audio fields in Core.

Audio-specific filters should belong to audio plugin/domain UI.

---

## 8.11 Core UI vs Plugin UI Rule

Core UI may depend on builtin capabilities.

Core UI should not directly depend on plugin-defined capabilities.

Example allowed:

```text
Asset List uses:
  builtin.asset.name
  builtin.asset.kind
  builtin.asset.thumbnail
  builtin.asset.open_actions
```

Example domain/plugin UI:

```text
Audio Preview Panel uses:
  media.audio.waveform
  media.audio.preview
```

This means the audio preview panel belongs to the audio/domain UI module, not pure Core UI.

Core can host this panel, but the dependency is modular.

---

## 8.12 MVP UI Layout Suggestion

Simple layout:

```text
Left / top:
  source or plugin navigation

Main:
  asset list

Right:
  inspector / preview panel

Bottom:
  job/status area
```

Plugin Management can be:

```text
settings page
modal dialog
separate tab
```

MVP can begin with:

```text
Tabs:
  Assets
  Plugins
```

Assets tab:

```text
Asset list
Inspector
Preview panel
```

Plugins tab:

```text
Plugin table
Capability details
Load status/errors
```