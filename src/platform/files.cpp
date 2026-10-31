#include "platform/files.h"

#include <fcntl.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

namespace sp {

namespace {

void syncPath(const fs::path& p, int flags) {
    const int fd = ::open(p.c_str(), flags | O_CLOEXEC);
    if (fd < 0) return;
    ::fsync(fd);
    ::close(fd);
}

}  // namespace

bool readTextFile(const fs::path& path, std::string* out, std::string* error, std::uintmax_t maxBytes) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (!ec && size > maxBytes) {
        if (error) *error = path.string() + " is too large";
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path.string();
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

bool writeFileAtomically(const fs::path& path, const std::string& text, std::string* error, bool durable) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (error) *error = "cannot write " + tmp.string();
            return false;
        }
        f << text;
        f.flush();
        if (!f) {
            if (error) *error = "write failed for " + tmp.string() + " (disk full?)";
            f.close();
            fs::remove(tmp, ec);
            return false;
        }
    }
    if (durable) syncPath(tmp, O_RDONLY);
    fs::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = "cannot save " + path.string() + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    if (durable) syncPath(path.has_parent_path() ? path.parent_path() : fs::path("."), O_RDONLY | O_DIRECTORY);
    return true;
}

}  // namespace sp
