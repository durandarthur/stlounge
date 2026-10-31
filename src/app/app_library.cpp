// App: shader library and sources (paste dialog, JSON file import).

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

#include "app/app.h"
#include "platform/log.h"
#include "shadertoy/json_format.h"
#include "shadertoy/shader_ref.h"

namespace sp {

namespace {

constexpr std::uintmax_t kMaxImportBytes = 16u * 1024u * 1024u;  // no real shader file is this big

}  // namespace

void App::setStatus(const std::string& message, bool error) {
    status_ = message;
    statusIsError_ = error;
    statusTime_ = static_cast<double>(SDL_GetTicks()) / 1000.0;
    if (error) LOG_WARN("%s", message.c_str());
    else LOG_INFO("%s", message.c_str());
}

void App::startFetch(const std::string& input) {
    const std::optional<std::string> id = parseShaderRef(input);
    if (!id) {
        pasteDialog_.setStatus("That isn't a shadertoy.com shader link or a 6-character shader ID.");
        return;
    }
    // The library is the cache: a shader that's already there isn't fetched again.
    if (const LibraryEntry* e = library_->find(*id); e && e->loadError.empty()) {
        pasteDialog_.close();
        setStatus("'" + e->name + "' is already in the library");
        playFromLibrary(*id);
        return;
    }
    if (!fetcher_) fetcher_ = std::make_unique<ShadertoyFetcher>();
    std::string why;
    if (!fetcher_->start(*id, static_cast<double>(SDL_GetTicks()) / 1000.0, &why)) {
        pasteDialog_.setStatus(why);
        return;
    }
    pasteDialog_.setStatus("");
    pasteDialog_.setFetching(true);
}

void App::pollFetch() {
    if (!fetcher_) return;
    const std::optional<FetchResult> r = fetcher_->poll(static_cast<double>(SDL_GetTicks()) / 1000.0);
    if (!r) return;
    pasteDialog_.setFetching(false);
    if (r->kind == FetchResult::Kind::Cancelled || !pasteDialog_.isOpen()) return;
    if (r->kind != FetchResult::Kind::Ok) {
        pasteDialog_.setStatus(r->message);
        return;
    }
    if (!pasteDialog_.fillFromJson(r->json, "Fetched from shadertoy.com. Check it, then add it to the library.",
                                   "fetch"))
        pasteDialog_.setStatus("The fetched shader can't be read.");
}

void App::playFromLibrary(const std::string& id, const LoadRequest& request) {
    LoadRequest req = request;
    req.libraryId = id;
    std::vector<std::string> warnings;
    std::string error;
    std::optional<Shader> s = library_->load(id, &warnings, &error);
    if (!s) {
        setStatus("Can't load " + id + ": " + error, true);
        if (req.fromPlaylist) {
            player_.markFailed(id);
            skipRequested_ = req.direction;  // next frame, so failures can't recurse
        }
        return;
    }
    for (const std::string& w : warnings) LOG_DEBUG("import note: %s", w.c_str());
    loadShader(*s, req);
}

void App::openPasteDialog(const std::string& clipboard) {
    if (fetcher_) fetcher_->cancel();  // a late result must not fill the new dialog
    pasteDialog_.open(clipboard);
    setUiVisible(true);
}

void App::importFiles(const std::vector<std::string>& paths) {
    ImportStats stats;
    for (const std::string& path : paths) {
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec || size > kMaxImportBytes) {
            ++stats.failed;
            stats.lastError = path + (ec ? ": " + ec.message() : " is too large to be a shader file");
            continue;
        }
        std::ifstream f(path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        importText(ss.str(), fs::path(path).filename().string(), stats);
    }
    reportImport(stats, paths.size() == 1 ? fs::path(paths[0]).filename().string()
                                          : std::to_string(paths.size()) + " files");
}

void App::importText(const std::string& text, const std::string& what, ImportStats& stats) {
    std::string error;
    std::vector<nlohmann::json> objs;
    try {
        objs = shaderObjects(text, &error);
    } catch (const std::exception& e) {
        error = e.what();
    }
    if (objs.empty()) {
        ++stats.failed;
        stats.lastError = what + ": " + error;
        return;
    }
    for (const nlohmann::json& obj : objs) {
        ImportResult r;
        try {
            r = shaderFromJson(obj);
        } catch (const std::exception& e) {
            r.error = e.what();
        }
        if (!r.ok) {
            ++stats.failed;
            stats.lastError = what + ": " + r.error;
            continue;
        }
        const std::string id = library_->add(obj, "import", &error, /*rescan=*/false);
        if (id.empty()) {
            ++stats.failed;
            stats.lastError = what + ": " + error;
            continue;
        }
        for (const std::string& w : r.warnings) LOG_WARN("'%s': %s", r.shader.info.name.c_str(), w.c_str());
        stats.warned += r.warnings.empty() ? 0 : 1;
        if (stats.firstId.empty()) stats.firstId = id;
        ++stats.imported;
    }
}

void App::reportImport(const ImportStats& st, const std::string& what) {
    library_->scan();
    playlistDirty_ = true;
    std::string msg = "Imported " + std::to_string(st.imported) + " shader" + (st.imported == 1 ? "" : "s") +
                      " from " + what;
    if (st.warned) msg += " (" + std::to_string(st.warned) + " with warnings, see the log)";
    if (st.failed) msg += "; " + std::to_string(st.failed) + " failed, last: " + st.lastError;
    setStatus(msg, st.imported == 0);
    if (!st.firstId.empty()) playFromLibrary(st.firstId);
}

}  // namespace sp
