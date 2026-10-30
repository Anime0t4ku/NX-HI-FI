#pragma once
#include "core.hpp"
namespace nxh {
// Local Settings navigation; field indices still refer to the shared remote schema.
struct SettingsNavigation {
    struct Category {
        std::string name;
        std::vector<int> fields;
        int selected = 0, scroll = 0;
    };
    std::vector<Category> categories;
    int category = 0;
    bool entries = false;
    SettingsNavigation() {
        const auto fields = settingFields();
        for (int i = 0; i < int(fields.size()); ++i) {
            const auto name = fields[i]["group"].get<std::string>();
            auto found = std::find_if(categories.begin(), categories.end(),
                                      [&](const auto &c) { return c.name == name; });
            if (found == categories.end()) {
                categories.push_back({name, {}});
                found = categories.end() - 1;
            }
            found->fields.push_back(i);
        }
    }
    const Category &current() const { return categories.at(category); }
    int fieldIndex() const { return current().fields.at(current().selected); }
    void move(int delta, int rows) {
        if (!entries) {
            const int count = int(categories.size());
            category = (category + delta % count + count) % count;
            return;
        }
        auto &c = categories[category];
        const int count = int(c.fields.size());
        c.selected = (c.selected + delta % count + count) % count;
        c.scroll = std::clamp(c.scroll, std::max(0, c.selected - rows + 1), c.selected);
    }
};
}
