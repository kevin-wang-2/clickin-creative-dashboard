#pragma once
#include "sdk/contracts/builtin/AssetRef.h"
#include <functional>
#include <string_view>

class QWidget;  // forward-declare only — SDK has no Qt::Widgets dependency

namespace clickin {

// A plugin registers a handler for this contract to contribute preview widgets.
//
// Two modes:
//   Embedded  — widget lives inside PreviewHost (docked in the main window).
//   Window    — widget lives in a floating top-level window.
//
// Rules:
//   • If supportsEmbedded is true, supportsWindow MUST also be true.
//   • The reverse is not required (a plugin can be window-only).
//   • The plugin may reuse the same factory for both modes or specialise each.
//
struct AssetPreviewWidgetContract {
    static constexpr std::string_view capability = "builtin.ui.preview_widget";
    static constexpr int version = 1;

    using Request = AssetRef;

    struct Result {
        bool supportsEmbedded = false;
        bool supportsWindow   = false;

        // Valid iff supportsEmbedded. Creates a widget to be parented into the
        // PreviewHost docked area.
        std::function<QWidget*(QWidget* parent)> embeddedFactory;

        // Valid iff supportsWindow (guaranteed non-null when supportsEmbedded).
        // Creates a widget to be parented into a floating top-level window.
        std::function<QWidget*(QWidget* parent)> windowFactory;
    };
};

} // namespace clickin
