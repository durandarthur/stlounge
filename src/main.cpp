#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "app_info.h"

namespace {

void printHelp() {
    std::printf(
        "%s %s - ShaderToy shader player\n\n"
        "Usage: %s [options]\n\n"
        "  --verbose     print debug messages to the terminal (the log file always has them)\n"
        "  --safe-mode   don't load the last shader on startup\n"
        "  --version     print the version and exit\n"
        "  --help        show this help\n\n"
        "Environment: SDL_VIDEODRIVER=x11 or =wayland forces a video backend.\n",
        APP_DISPLAY_NAME, APP_VERSION, APP_NAME);
}

}  // namespace

int main(int argc, char** argv) {
    sp::AppOptions opts;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!std::strcmp(a, "--verbose") || !std::strcmp(a, "-v")) {
            opts.verbose = true;
        } else if (!std::strcmp(a, "--safe-mode")) {
            opts.safeMode = true;
        } else if (!std::strcmp(a, "--version")) {
            std::printf("%s %s\n", APP_NAME, APP_VERSION);
            return 0;
        } else if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) {
            printHelp();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option: %s (try --help)\n", a);
            return 2;
        }
    }
    sp::App app;
    return app.run(opts);
}
