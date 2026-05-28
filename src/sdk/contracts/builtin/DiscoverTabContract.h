#pragma once
#include <functional>
#include <string>
#include <string_view>

class QWidget;

namespace clickin {

struct DiscoverTabContract {
    static constexpr std::string_view capability = "builtin.ui.discover.tab";
    static constexpr int version = 1;
    struct Request {};
    struct Result {
        std::string tabId;    // unique stable id, e.g. "myplugin.browse"
        std::string label;    // tab text shown to the user
        int         priority = 50;  // lower = further left; built-in Assets tab is 0
        // Called once on first tab activation; returned widget is owned by the tab.
        std::function<QWidget*(QWidget* parent)> widgetFactory;
    };
};

} // namespace clickin
