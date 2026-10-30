#pragma once
// Minimal ImGui file picker for .json files (no native dialog dependency).

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace sp {

class FileBrowser {
public:
    void open(const std::filesystem::path& startDir);
    bool isOpen() const { return open_; }
    // Returns the chosen files once (one, ticked ones, or the whole folder), then closes.
    std::vector<std::filesystem::path> draw();

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
    std::set<std::string> ticked_;
    std::string error_;
};

}  // namespace sp
