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

void App::importFile(const std::string& path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) {
        setStatus("Can't read " + path + ": " + ec.message(), true);
        return;
    }
    if (size > kMaxImportBytes) {
        setStatus(path + " is too large to be a shader file", true);
        return;
    }
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    if (!f && !f.eof()) {
        setStatus("Can't read " + path, true);
        return;
    }
    importText(ss.str(), fs::path(path).filename().string());
}

void App::importText(const std::string& text, const std::string& what) {
    std::string error;
    std::vector<nlohmann::json> objs;
    try {
        objs = shaderObjects(text, &error);
    } catch (const std::exception& e) {
        error = e.what();
    }
    if (objs.empty()) {
        setStatus("Can't import " + what + ": " + error, true);
        return;
    }
    int imported = 0, failed = 0, warned = 0;
    std::string firstId, lastError;
    for (const nlohmann::json& obj : objs) {
        ImportResult r;
        try {
            r = shaderFromJson(obj);
        } catch (const std::exception& e) {
            r.error = e.what();
        }
        if (!r.ok) {
            ++failed;
            lastError = r.error;
            continue;
        }
        const std::string id = library_->add(obj, "import", &error);
        if (id.empty()) {
            ++failed;
            lastError = error;
            continue;
        }
        for (const std::string& w : r.warnings) LOG_WARN("'%s': %s", r.shader.info.name.c_str(), w.c_str());
        warned += r.warnings.empty() ? 0 : 1;
        if (firstId.empty()) firstId = id;
        ++imported;
    }

    std::string msg = "Imported " + std::to_string(imported) + " shader" + (imported == 1 ? "" : "s") + " from " + what;
    if (warned) msg += " (" + std::to_string(warned) + " with warnings, see the log)";
    if (failed) msg += "; " + std::to_string(failed) + " failed: " + lastError;
    setStatus(msg, imported == 0);
    if (!firstId.empty()) playFromLibrary(firstId);
}

}  // namespace sp
