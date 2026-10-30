// App: shader library and sources (paste dialog, JSON file import).

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

#include "app/app.h"
#include "platform/log.h"
#include "shadertoy/json_format.h"

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

void App::playFromLibrary(const std::string& id) {
    std::vector<std::string> warnings;
    std::string error;
    std::optional<Shader> s = library_->load(id, &warnings, &error);
    if (!s) {
        setStatus("Can't load " + id + ": " + error, true);
        return;
    }
    for (const std::string& w : warnings) LOG_DEBUG("import note: %s", w.c_str());
    loadShader(*s, id);
}

void App::openPasteDialog(const std::string& clipboard) {
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
    std::string msg = "Imported " + std::to_string(st.imported) + " shader" + (st.imported == 1 ? "" : "s") +
                      " from " + what;
    if (st.warned) msg += " (" + std::to_string(st.warned) + " with warnings, see the log)";
    if (st.failed) msg += "; " + std::to_string(st.failed) + " failed, last: " + st.lastError;
    setStatus(msg, st.imported == 0);
    if (!st.firstId.empty()) playFromLibrary(st.firstId);
}

}  // namespace sp
