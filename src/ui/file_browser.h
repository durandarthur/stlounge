#pragma once
// Minimal ImGui file picker for .json files (no native dialog dependency).

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sp {

class FileBrowser {
public:
    void open(const std::filesystem::path& startDir);
    bool isOpen() const { return open_; }
    // Returns the chosen file once, then closes.
    std::optional<std::filesystem::path> draw();

private:
    void list();

    struct Item {
        std::string name;
        bool isDir;
    };
    bool open_ = false;
    bool showHidden_ = false;
    std::filesystem::path dir_;
    std::string pathInput_;
    std::vector<Item> items_;
    std::string error_;
};

}  // namespace sp
