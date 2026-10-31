#include "library/crash_guard.h"

#include <nlohmann/json.hpp>

#include "platform/files.h"
#include "platform/log.h"

namespace sp {

using json = nlohmann::json;

std::optional<PendingShader> CrashGuard::takeLeftover() {
    std::error_code ec;
    if (!fs::exists(file_, ec)) return std::nullopt;
    std::string text, error;
    const bool read = readTextFile(file_, &text, &error, 64 * 1024);
    fs::remove(file_, ec);
    PendingShader p;
    const json j = read ? json::parse(text, nullptr, false) : json();
    if (j.is_object()) {
        if (j.contains("shader") && j["shader"].is_string()) p.libraryId = j["shader"].get<std::string>();
        if (j.contains("name") && j["name"].is_string()) p.name = j["name"].get<std::string>();
    }
    // Even an unreadable marker means the last run didn't end normally.
    return p;
}

void CrashGuard::arm(const PendingShader& shader) {
    std::string error;
    const json j = {{"shader", shader.libraryId}, {"name", shader.name}};
    if (!writeFileAtomically(file_, j.dump(), &error, /*durable=*/true)) {
        LOG_WARN("crash guard: %s", error.c_str());
        return;
    }
    armed_ = true;
}

void CrashGuard::disarm() {
    if (!armed_) return;
    std::error_code ec;
    fs::remove(file_, ec);
    armed_ = false;
}

}  // namespace sp
