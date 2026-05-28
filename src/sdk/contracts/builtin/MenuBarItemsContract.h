#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace clickin {

struct MenuBarItem {
    std::string menuName;  // menu to insert into, e.g. "Plugins"; created if absent
    std::string actionId;  // unique stable id, e.g. "myplugin.scan"
    std::string label;
    std::string shortcut;  // optional, e.g. "Ctrl+Shift+S"
    std::function<void()> action;  // called on trigger (runs on UI thread)
};

struct MenuBarItemsContract {
    static constexpr std::string_view capability = "builtin.ui.menubar.items";
    static constexpr int version = 1;
    struct Request {};
    struct Result { std::vector<MenuBarItem> items; };
};

} // namespace clickin
