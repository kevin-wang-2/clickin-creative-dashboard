#pragma once

#include "core/types/RawPayload.h"

namespace clickin {

// MVP implementation: naive in-process type erasure via shared_ptr<const void>.
// Future: replace encode/decode bodies with CBOR/JSON/msgpack serialization
// without changing the rest of the system.
template <typename Contract>
struct CapabilityCodec {
    static RawRequest encodeRequest(const typename Contract::Request& req) {
        RawRequest raw;
        raw.capability = std::string(Contract::capability);
        raw.version    = Contract::version;
        raw.payload    = RawPayload::makeInProcess(req);
        return raw;
    }

    static typename Contract::Request decodeRequest(const RawRequest& raw) {
        return raw.payload.template get<typename Contract::Request>();
    }

    static RawResult encodeResult(const typename Contract::Result& res) {
        RawResult raw;
        raw.ok         = true;
        raw.capability = std::string(Contract::capability);
        raw.version    = Contract::version;
        raw.payload    = RawPayload::makeInProcess(res);
        return raw;
    }

    static typename Contract::Result decodeResult(const RawResult& raw) {
        return raw.payload.template get<typename Contract::Result>();
    }
};

} // namespace clickin
