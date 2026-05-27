#pragma once
#include "sdk/contracts/builtin/AssetRef.h"
#include <functional>
#include <string_view>

class QWidget;  // forward-declare only — SDK has no Qt::Widgets dependency

namespace clickin {

// A plugin registers a handler for this contract to contribute a preview widget
// for a given asset. The result carries a factory function; the caller (PreviewHost)
// invokes the factory to create and parent the widget.
struct AssetPreviewWidgetContract {
    static constexpr std::string_view capability = "builtin.ui.preview_widget";
    static constexpr int version = 1;

    using Request = AssetRef;

    struct Result {
        bool hasPreview = false;
        // Factory: create and return a new QWidget parented to the given parent.
        // Caller takes ownership (the widget is reparented via Qt's parent chain).
        std::function<QWidget*(QWidget* parent)> factory;
    };
};

} // namespace clickin
