#include <cstdlib>
#include <map>
#include <memory>
#include <string>

#include "platform/paths.h"
#include "test.h"

using namespace sp;

namespace {

EnvLookup fakeEnv(std::map<std::string, std::string> vars) {
    auto shared = std::make_shared<std::map<std::string, std::string>>(std::move(vars));
    return [shared](const char* name) -> const char* {
        auto it = shared->find(name);
        return it == shared->end() ? nullptr : it->second.c_str();
    };
}

}  // namespace

TEST(paths_defaults_from_home) {
    auto p = resolveAppPaths("myapp", fakeEnv({{"HOME", "/home/u"}, {"XDG_CONFIG_HOME", "/nonexistent-cfg"}}));
    CHECK_EQ(p.configDir.string(), std::string("/nonexistent-cfg/myapp"));
    CHECK_EQ(p.dataDir.string(), std::string("/home/u/.local/share/myapp"));
    CHECK_EQ(p.stateDir.string(), std::string("/home/u/.local/state/myapp"));
    CHECK_EQ(p.picturesDir.string(), std::string("/home/u/Pictures/myapp"));
}

TEST(paths_xdg_overrides) {
    auto p = resolveAppPaths("app", fakeEnv({{"HOME", "/h"},
                                             {"XDG_CONFIG_HOME", "/c"},
                                             {"XDG_DATA_HOME", "/d"},
                                             {"XDG_STATE_HOME", "/s"},
                                             {"XDG_PICTURES_DIR", "/pics"}}));
    CHECK_EQ(p.configDir.string(), std::string("/c/app"));
    CHECK_EQ(p.dataDir.string(), std::string("/d/app"));
    CHECK_EQ(p.stateDir.string(), std::string("/s/app"));
    CHECK_EQ(p.picturesDir.string(), std::string("/pics/app"));
}

TEST(paths_relative_xdg_ignored) {
    auto p = resolveAppPaths("app", fakeEnv({{"HOME", "/h"}, {"XDG_DATA_HOME", "relative/dir"}}));
    CHECK_EQ(p.dataDir.string(), std::string("/h/.local/share/app"));
}

TEST(paths_user_dirs_parsing) {
    const std::string file =
        "# comment\n"
        "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
        "  XDG_PICTURES_DIR=\"$HOME/Images\"\n";
    auto r = parsePicturesDir(file, "/home/u");
    REQUIRE(r.has_value());
    CHECK_EQ(r->string(), std::string("/home/u/Images"));

    auto abs = parsePicturesDir("XDG_PICTURES_DIR=\"/mnt/pics\"\n", "/home/u");
    REQUIRE(abs.has_value());
    CHECK_EQ(abs->string(), std::string("/mnt/pics"));

    CHECK(!parsePicturesDir("XDG_MUSIC_DIR=\"$HOME/Music\"\n", "/home/u").has_value());
    CHECK(!parsePicturesDir("#XDG_PICTURES_DIR=\"$HOME/x\"\n", "/home/u").has_value());
    CHECK(!parsePicturesDir("XDG_PICTURES_DIR=\"relative\"\n", "/home/u").has_value());
}

TEST(paths_ensure_creates_dirs) {
    const char* tmp = std::getenv("TMPDIR");
    fs::path base = fs::path(tmp && *tmp ? tmp : "/tmp") / "sp_test_paths";
    fs::remove_all(base);
    AppPaths p;
    p.configDir = base / "cfg";
    p.dataDir = base / "data";
    p.stateDir = base / "state";
    std::string msg;
    CHECK(ensureAppDirs(p, "app", &msg));
    CHECK(!p.usingFallback);
    CHECK(fs::is_directory(base / "cfg"));
    CHECK(fs::is_directory(base / "state"));
    fs::remove_all(base);
}

TEST(asset_dir_candidates_order) {
    const auto c = sp::assetDirCandidates("/opt/app/usr/bin/shaderplayer", "shaderplayer", "/src/assets");
    REQUIRE(c.size() == 2u);
    CHECK_EQ(c[0], fs::path("/opt/app/usr/share/shaderplayer"));
    CHECK_EQ(c[1], fs::path("/src/assets"));
    CHECK_EQ(sp::assetDirCandidates("", "x", "").size(), size_t(0));
}
