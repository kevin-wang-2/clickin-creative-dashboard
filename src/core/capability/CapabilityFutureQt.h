#pragma once
// UI-affine extension to CapabilityFuture<T>.
// Include in Qt/UI code only — not in core, SDK, or tests.
#include "core/capability/CapabilityFuture.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>

namespace clickin {

// Dispatches fn(result) on the thread that owns `affinity` (the UI thread for
// QWidget subclasses) via Qt::QueuedConnection.
//
// Guarantees:
//   • Always posted — never called inline, even if the future is already resolved.
//   • Safe if affinity is destroyed first — the posted callback is a no-op.
//   • Called exactly once.
//
// Usage:
//   thenOnUi(broker->invoke<Foo>(ref, req), this,
//            [this](const Foo::Result& r) { /* runs on UI thread */ });
template<typename T, typename Fn>
void thenOnUi(CapabilityFuture<T> future, QObject* affinity, Fn&& fn) {
    QPointer<QObject> guard(affinity);
    auto fn_cp = std::forward<Fn>(fn);
    future.onReady([guard, fn_cp = std::move(fn_cp)](const T& v) mutable {
        if (guard.isNull()) return;
        T v_copy = v;
        QMetaObject::invokeMethod(guard.data(),
            [fn_cp = std::move(fn_cp), v_copy = std::move(v_copy), guard]() mutable {
                if (!guard.isNull()) fn_cp(std::move(v_copy));
            },
            Qt::QueuedConnection);
    });
}

} // namespace clickin
