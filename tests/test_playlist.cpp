// Settings, playlists, playlist order, per-shader metadata, image files.

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "config/settings.h"
#include "library/crash_guard.h"
#include "library/library.h"
#include "library/playlist_player.h"
#include "library/playlists.h"
#include "platform/files.h"
#include "test.h"
#include "textures/image_io.h"

using namespace sp;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path tempDir(const char* name) {
    const char* tmp = std::getenv("TMPDIR");
    fs::path d = fs::path(tmp && *tmp ? tmp : "/tmp") / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void writeText(const fs::path& p, const std::string& s) {
    std::ofstream(p, std::ios::binary) << s;
}

std::vector<std::string> ids(int n) {
    std::vector<std::string> v;
    for (int i = 0; i < n; ++i) v.push_back("s" + std::to_string(i));
    return v;
}

}  // namespace

// ------------------------------------------------------------------ settings

TEST(settings_defaults_and_round_trip) {
    const Settings d;
    CHECK(!d.crossfade);  // owner's choice: off by default
    CHECK(d.fpsCap == 0 && d.renderScale == 1.0f && d.compileTimeCapMs == 0);
    CHECK_EQ(d.frameTimeGuardMs, 1000);
    std::vector<std::string> warn;
    CHECK_EQ(settingsFromJson(json::parse(R"({"frame_time_guard_ms": 50})"), &warn).frameTimeGuardMs, 1000);
    CHECK_EQ(warn.size(), size_t(1));

    Settings s;
    s.fpsCap = 30;
    s.renderScale = 0.5f;
    s.autoAdvance = false;
    s.autoAdvanceSeconds = 45;
    s.shuffle = s.locked = s.crossfade = true;
    s.crossfadeSeconds = 3.5;
    s.audioSource = "alsa_output.foo.monitor";
    s.textureOverrides["abstract1"] = "/home/me/a.png";
    s.compileTimeCapMs = 2000;
    s.frameTimeGuardMs = 0;
    std::vector<std::string> w;
    const Settings back = settingsFromJson(json::parse(settingsToJson(s).dump()), &w);
    CHECK(w.empty());
    CHECK(back == s);

    const fs::path dir = tempDir("sp_test_settings");
    std::string err;
    REQUIRE(saveSettings(dir / "settings.json", s, &err));
    Settings loaded;
    CHECK(loadSettings(dir / "settings.json", &loaded, &w));
    CHECK(loaded == s);
}

TEST(settings_missing_file_gives_defaults) {
    Settings s;
    s.fpsCap = 99;
    std::vector<std::string> w;
    CHECK(loadSettings(tempDir("sp_test_settings_missing") / "nope.json", &s, &w));
    CHECK(s == Settings{});
    CHECK(w.empty());
}

TEST(settings_bad_values_fall_back_per_key) {
    std::vector<std::string> w;
    const json j = json::parse(R"({"fps_cap": 5, "render_scale": "big", "auto_advance_seconds": 30,
        "crossfade": 1, "compile_time_cap_ms": 0, "shuffle": true, "unknown_key": [1,2],
        "texture_overrides": {"abstract1": "/x.png", "bad": 3}})");
    const Settings s = settingsFromJson(j, &w);
    CHECK_EQ(s.fpsCap, 0);             // 5 is below the minimum
    CHECK_EQ(s.renderScale, 1.0f);     // wrong type
    CHECK_EQ(s.autoAdvanceSeconds, 30.0);
    CHECK(!s.crossfade);               // 1 is not a boolean
    CHECK_EQ(s.compileTimeCapMs, 0);
    CHECK(s.shuffle);
    CHECK_EQ(s.textureOverrides.size(), size_t(1));
    CHECK_EQ(w.size(), size_t(3));

    const fs::path dir = tempDir("sp_test_settings_bad");
    writeText(dir / "settings.json", "{ not json");
    Settings loaded;
    w.clear();
    CHECK(!loadSettings(dir / "settings.json", &loaded, &w));
    CHECK(loaded == Settings{});
    CHECK_EQ(w.size(), size_t(1));
}

TEST(settings_advance_seconds_override) {
    Settings s;
    s.autoAdvanceSeconds = 100;
    CHECK_EQ(advanceSecondsFor(s, 0), 100.0);
    CHECK_EQ(advanceSecondsFor(s, 15), 15.0);
}

TEST(session_round_trip_and_garbage) {
    const fs::path dir = tempDir("sp_test_session");
    std::string err;
    REQUIRE(saveSession(dir / "session.json", {"XsX3Rr", "Chill"}, &err));
    Session s = loadSession(dir / "session.json");
    CHECK_EQ(s.lastShader, std::string("XsX3Rr"));
    CHECK_EQ(s.playlist, std::string("Chill"));
    writeText(dir / "session.json", "[1, 2]");
    s = loadSession(dir / "session.json");
    CHECK(s.lastShader.empty() && s.playlist.empty());
}

// ------------------------------------------------------------------ files

TEST(atomic_write_replaces_and_leaves_no_tmp) {
    const fs::path dir = tempDir("sp_test_files");
    std::string err, text;
    REQUIRE(writeFileAtomically(dir / "sub" / "a.txt", "one", &err));
    REQUIRE(writeFileAtomically(dir / "sub" / "a.txt", "two", &err));
    REQUIRE(readTextFile(dir / "sub" / "a.txt", &text, &err));
    CHECK_EQ(text, std::string("two"));
    CHECK(!fs::exists(dir / "sub" / "a.txt.tmp"));
    CHECK(!readTextFile(dir / "sub" / "a.txt", &text, &err, 2));  // over the size limit
}

// ------------------------------------------------------------------ playlist store

TEST(playlists_create_rename_delete) {
    const fs::path dir = tempDir("sp_test_playlists");
    PlaylistStore st(dir);
    st.scan();
    std::string err;
    CHECK(st.create("Chill", &err));
    CHECK(st.create("Loud stuff", &err));
    CHECK(!st.create("chill", &err));        // case-insensitive duplicate
    CHECK(!st.create("Favorites", &err));    // reserved
    CHECK(!st.create("*x", &err));
    CHECK(!st.create("", &err));
    CHECK(!st.create("Loud_stuff", &err));   // same file name as "Loud stuff"
    CHECK_EQ(st.playlists().size(), size_t(2));

    CHECK(st.addShader("Chill", "abc123", &err));
    CHECK(st.addShader("Chill", "local-1", &err));
    CHECK(st.addShader("Chill", "abc123", &err));  // no duplicate
    CHECK(!st.addShader("Chill", "../evil", &err));
    REQUIRE(st.find("Chill") != nullptr);
    CHECK_EQ(st.find("Chill")->ids.size(), size_t(2));

    CHECK(st.rename("Chill", "Calm", &err));
    CHECK(st.find("Chill") == nullptr);
    REQUIRE(st.find("Calm") != nullptr);
    CHECK_EQ(st.find("Calm")->ids.size(), size_t(2));
    CHECK(!fs::exists(dir / "chill.json"));

    PlaylistStore again(dir);  // persisted
    again.scan();
    REQUIRE(again.find("Calm") != nullptr);
    CHECK_EQ(again.find("Calm")->ids[1], std::string("local-1"));

    CHECK(st.remove("Calm", &err));
    CHECK(st.find("Calm") == nullptr);
    CHECK_EQ(st.playlists().size(), size_t(1));
}

TEST(playlists_move_remove_and_forget) {
    PlaylistStore st(tempDir("sp_test_playlists_move"));
    st.scan();
    std::string err;
    REQUIRE(st.create("P", &err));
    REQUIRE(st.create("Q", &err));
    for (const char* id : {"a", "b", "c"}) st.addShader("P", id, &err);
    st.addShader("Q", "b", &err);
    CHECK(st.move("P", 0, 1, &err));   // b a c
    CHECK(st.move("P", 2, -5, &err));  // c b a
    CHECK((st.find("P")->ids == std::vector<std::string>{"c", "b", "a"}));
    CHECK(st.removeShader("P", "c", &err));
    st.forgetShader("b");
    CHECK((st.find("P")->ids == std::vector<std::string>{"a"}));
    CHECK(st.find("Q")->ids.empty());
}

TEST(playlists_ignore_broken_files) {
    const fs::path dir = tempDir("sp_test_playlists_broken");
    writeText(dir / "a.json", "garbage");
    writeText(dir / "b.json", R"({"name": "Good", "shaders": ["x", 5, "x", "../bad", "y"]})");
    writeText(dir / "c.json", R"({"shaders": []})");
    PlaylistStore st(dir);
    st.scan();
    REQUIRE(st.playlists().size() == size_t(1));
    CHECK((st.playlists()[0].ids == std::vector<std::string>{"x", "y"}));
}

// ------------------------------------------------------------------ player

TEST(player_next_previous_wrap) {
    PlaylistPlayer p(1);
    p.setItems(ids(3));
    CHECK_EQ(p.next(), std::string("s0"));  // nothing current: start at the top
    CHECK_EQ(p.next(), std::string("s1"));
    CHECK_EQ(p.next(), std::string("s2"));
    CHECK_EQ(p.next(), std::string("s0"));
    CHECK_EQ(p.previous(), std::string("s2"));
    CHECK_EQ(p.previous(), std::string("s1"));
    p.setCurrent("s0");
    CHECK_EQ(p.next(), std::string("s1"));
}

TEST(player_skips_failed_and_stops_when_all_failed) {
    PlaylistPlayer p(1);
    p.setItems(ids(4));
    p.setCurrent("s0");
    p.markFailed("s1");
    p.markFailed("s2");
    CHECK_EQ(p.next(), std::string("s3"));
    CHECK_EQ(p.next(), std::string("s0"));
    CHECK_EQ(p.previous(), std::string("s3"));
    p.markFailed("s0");
    p.markFailed("s3");
    CHECK_EQ(p.playableCount(), size_t(0));
    CHECK(p.next().empty());
    CHECK(p.previous().empty());
    CHECK(p.random().empty());
    p.clearFailed();
    CHECK(!p.next().empty());
}

TEST(player_shuffle_plays_each_once_per_round) {
    PlaylistPlayer p(42);
    p.setItems(ids(10));
    p.setShuffle(true);
    std::string last;
    for (int round = 0; round < 5; ++round) {
        std::set<std::string> seen;
        for (int i = 0; i < 10; ++i) {
            const std::string id = p.next();
            CHECK(id != last);  // never the same twice in a row, even across rounds
            last = id;
            seen.insert(id);
        }
        CHECK_EQ(seen.size(), size_t(10));
    }
}

TEST(player_shuffle_previous_uses_history) {
    PlaylistPlayer p(7);
    p.setItems(ids(6));
    p.setShuffle(true);
    std::vector<std::string> played;
    for (int i = 0; i < 4; ++i) played.push_back(p.next());
    CHECK_EQ(p.previous(), played[2]);
    CHECK_EQ(p.previous(), played[1]);
    CHECK_EQ(p.previous(), played[0]);
}

TEST(player_random_avoids_current_and_failed) {
    PlaylistPlayer p(3);
    p.setItems(ids(3));
    p.setCurrent("s0");
    p.markFailed("s1");
    for (int i = 0; i < 20; ++i) {
        const std::string id = p.random();
        CHECK(id != "s1");
        CHECK(id != (i % 2 == 0 ? "s0" : "s2"));  // alternates between the two playable ones
    }
    PlaylistPlayer one(3);
    one.setItems({"only"});
    one.setCurrent("only");
    CHECK_EQ(one.random(), std::string("only"));
    PlaylistPlayer empty(3);
    CHECK(empty.next().empty());
}

TEST(player_set_items_keeps_current) {
    PlaylistPlayer p(1);
    p.setItems(ids(5));
    p.setCurrent("s3");
    p.setItems({"s1", "s3", "s9"});
    CHECK_EQ(p.current(), std::string("s3"));
    CHECK_EQ(p.next(), std::string("s9"));
    p.setItems({"s1"});
    CHECK(p.current().empty());
    CHECK_EQ(p.next(), std::string("s1"));
}

// ------------------------------------------------------------------ library metadata

TEST(library_favorite_and_duration_persist) {
    const fs::path dir = tempDir("sp_test_library_meta");
    ShaderLibrary lib(dir);
    lib.scan();
    std::string err;
    const std::string id = lib.add(json::parse(R"({"info": {"id": "-1", "name": "n"},
        "renderpass": [{"type": "image", "code": "void mainImage(out vec4 c, in vec2 f) { c = vec4(1); }"}]})"),
                                   "paste", &err);
    REQUIRE(!id.empty());
    CHECK(lib.setFavorite(id, true, &err));
    CHECK(lib.setDuration(id, 33, &err));
    CHECK(lib.find(id)->favorite);
    CHECK_EQ(lib.find(id)->duration, 33.0);
    CHECK(!lib.setDuration(id, -1, &err));
    CHECK(!lib.setFavorite("../x", true, &err));

    ShaderLibrary again(dir);
    again.scan();
    REQUIRE(again.find(id) != nullptr);
    CHECK(again.find(id)->favorite);
    CHECK_EQ(again.find(id)->duration, 33.0);
    // The shader itself is untouched.
    CHECK(again.load(id, nullptr, &err).has_value());

    CHECK(lib.setFavorite(id, false, &err));
    CHECK(lib.setDuration(id, 0, &err));
    std::string text;
    REQUIRE(readTextFile(dir / (id + ".json"), &text, &err));
    const json j = json::parse(text);
    CHECK(!j["shaderplayer"].contains("favorite"));
    CHECK(!j["shaderplayer"].contains("duration"));
}

// ------------------------------------------------------------------ image files

TEST(png_save_and_load_round_trip) {
    const fs::path dir = tempDir("sp_test_png");
    std::vector<uint8_t> px(3 * 2 * 4);
    for (size_t i = 0; i < px.size(); ++i) px[i] = static_cast<uint8_t>(i * 10);
    std::string err;
    REQUIRE(savePng(dir / "a.png", 3, 2, px.data(), &err));
    ImageData img;
    REQUIRE(loadImageFile(dir / "a.png", 4, &img, &err));
    CHECK(img.width == 3 && img.height == 2 && img.channels == 4);
    CHECK(img.pixels == px);
    ImageData gray;
    REQUIRE(loadImageFile(dir / "a.png", 1, &gray, &err));
    CHECK_EQ(gray.pixels.size(), size_t(6));

    writeText(dir / "bad.png", "not an image");
    CHECK(!loadImageFile(dir / "bad.png", 4, &img, &err));
    CHECK(!err.empty());
    CHECK(!loadImageFile(dir / "missing.png", 4, &img, &err));
}

// ------------------------------------------------------------------ crash guard

TEST(crash_guard_reports_a_leftover_once) {
    const fs::path dir = tempDir("sp_test_crash_guard");
    const fs::path file = dir / "pending.json";
    {
        CrashGuard g(file);
        CHECK(!g.takeLeftover());
        g.arm({"XsX3Rr", "Heavy one"});
        CHECK(g.armed());
        CHECK(fs::exists(file));
        // the process "dies" here: no disarm
    }
    CrashGuard next(file);
    const std::optional<PendingShader> left = next.takeLeftover();
    REQUIRE(left.has_value());
    CHECK_EQ(left->libraryId, std::string("XsX3Rr"));
    CHECK_EQ(left->name, std::string("Heavy one"));
    CHECK(!next.takeLeftover());  // reported once

    next.arm({"", "Pasted"});
    next.arm({"AbCdEf", "Second"});  // replaces the first
    next.disarm();
    CHECK(!next.armed());
    CHECK(!fs::exists(file));
    CHECK(!CrashGuard(file).takeLeftover());

    // A damaged marker still means the last run didn't end normally.
    std::ofstream(file) << "{not json";
    const auto damaged = CrashGuard(file).takeLeftover();
    REQUIRE(damaged.has_value());
    CHECK(damaged->libraryId.empty());
    CHECK(!fs::exists(file));
}

TEST(durable_write_round_trip) {
    const fs::path dir = tempDir("sp_test_durable");
    std::string err, text;
    CHECK(writeFileAtomically(dir / "sub" / "a.json", "{\"x\": 1}", &err, true));
    REQUIRE(readTextFile(dir / "sub" / "a.json", &text, &err));
    CHECK_EQ(text, std::string("{\"x\": 1}"));
    CHECK(!fs::exists(dir / "sub" / "a.json.tmp"));
}

TEST(library_crashed_flag_persists) {
    const fs::path dir = tempDir("sp_test_library_crashed");
    ShaderLibrary lib(dir);
    lib.scan();
    std::string err;
    const std::string id = lib.add(json::parse(R"({"info": {"id": "-1", "name": "n"},
        "renderpass": [{"type": "image", "code": "void mainImage(out vec4 c, in vec2 f) { c = vec4(1); }"}]})"),
                                   "paste", &err);
    REQUIRE(!id.empty());
    CHECK(!lib.find(id)->crashed);
    CHECK(lib.setCrashed(id, true, &err));
    CHECK(lib.find(id)->crashed);
    ShaderLibrary again(dir);
    again.scan();
    CHECK(again.find(id)->crashed);
    CHECK(again.setCrashed(id, false, &err));
    ShaderLibrary third(dir);
    third.scan();
    CHECK(!third.find(id)->crashed);
}

TEST(library_update_keeps_what_the_editor_does_not_show) {
    const fs::path dir = tempDir("sp_test_library_update");
    ShaderLibrary lib(dir);
    lib.scan();
    std::string err;
    const std::string id = lib.add(json::parse(R"({"ver": "0.1",
        "info": {"id": "XsX3Rr", "name": "Original", "username": "someone", "description": "keep me", "likes": 7},
        "renderpass": [
          {"type": "image", "name": "Image", "inputs": [], "outputs": [{"id": "4dfGRr", "channel": 0}],
           "code": "void mainImage(out vec4 c, in vec2 f) { c = vec4(1); }"},
          {"type": "sound", "name": "Sound", "inputs": [], "outputs": [],
           "code": "vec2 mainSound(int s, float t) { return vec2(0); }"}]})"),
                                   "import", &err);
    REQUIRE(id == "XsX3Rr");
    CHECK(lib.setFavorite(id, true, &err));
    CHECK(lib.setCrashed(id, true, &err));

    std::optional<Shader> s = lib.load(id, nullptr, &err);
    REQUIRE(s.has_value());
    s->info.name = "Edited";
    s->passes[0].code = "void mainImage(out vec4 c, in vec2 f) { c = vec4(0.5); }";
    REQUIRE(lib.update(id, *s, &err));

    const LibraryEntry* e = lib.find(id);
    REQUIRE(e != nullptr);
    CHECK_EQ(e->name, std::string("Edited"));
    CHECK(e->favorite);
    CHECK(!e->crashed);
    std::string text;
    REQUIRE(readTextFile(dir / "XsX3Rr.json", &text, &err));
    const json j = json::parse(text);
    CHECK_EQ(j["info"]["description"], json("keep me"));
    CHECK_EQ(j["info"]["likes"], json(7));
    CHECK_EQ(j["info"]["id"], json("XsX3Rr"));
    bool sound = false, edited = false;
    for (const json& p : j["renderpass"]) {
        if (p["type"] == "sound") sound = true;
        if (p["type"] == "image" && p["code"].get<std::string>().find("0.5") != std::string::npos) edited = true;
    }
    CHECK(sound);
    CHECK(edited);
    CHECK(!lib.update("../x", *s, &err));
    CHECK(!lib.update("AbCdEf", *s, &err));  // not in the library
}
