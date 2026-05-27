#pragma once

#include <memory>
#include <string>
#include <typeindex>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <stdexcept>

namespace clickin {

using AssetId          = std::string;
using ProviderId       = std::string;
using RequestId        = std::string;
using ContentVersionId = std::string;
using CacheId          = std::string;

// MVP: in-process type erasure. Future: SerializedBytes with format tag.
struct RawPayload {
    enum class Kind { InProcessObject, SerializedBytes };

    Kind kind = Kind::InProcessObject;

    // InProcessObject fields
    std::type_index type   = typeid(void);
    std::shared_ptr<const void> object;

    // SerializedBytes fields (future)
    std::string format;
    std::vector<std::byte> bytes;

    template <typename T>
    static RawPayload makeInProcess(const T& value) {
        RawPayload p;
        p.kind   = Kind::InProcessObject;
        p.type   = std::type_index(typeid(T));
        p.object = std::make_shared<const T>(value);
        return p;
    }

    template <typename T>
    const T& get() const {
        if (kind != Kind::InProcessObject)
            throw std::logic_error("RawPayload is not an in-process object");
        if (type != std::type_index(typeid(T)))
            throw std::logic_error("RawPayload type mismatch");
        return *static_cast<const T*>(object.get());
    }
};

struct RawRequest {
    std::string capability;
    int version = 0;
    ProviderId  providerId;
    RequestId   requestId;
    RawPayload  payload;
};

struct RawResult {
    bool ok = true;

    std::string capability;
    int version = 0;
    RequestId   requestId;

    RawPayload payload;

    std::string errorCode;
    std::string errorMessage;
};

} // namespace clickin
