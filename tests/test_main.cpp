#include <cstdio>
#include <cstring>
#include <exception>

#include "test.h"

namespace sptest {

namespace {
int g_caseFailures = 0;
}

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

void reportFailure(const char* file, int line, const std::string& message) {
    ++g_caseFailures;
    std::printf("    FAIL %s:%d: %s\n", file, line, message.c_str());
}

int runAll(const char* filter) {
    int passed = 0, failed = 0, skipped = 0;
    for (const Case& c : registry()) {
        if (filter && *filter && !std::strstr(c.name, filter)) continue;
        g_caseFailures = 0;
        std::printf("[ RUN  ] %s\n", c.name);
        std::fflush(stdout);
        bool skip = false;
        try {
            c.fn();
        } catch (const Skipped& s) {
            skip = true;
            std::printf("[ SKIP ] %s: %s\n", c.name, s.why.c_str());
        } catch (const RequireFailed&) {
        } catch (const std::exception& e) {
            reportFailure(__FILE__, __LINE__, std::string("exception: ") + e.what());
        } catch (...) {
            reportFailure(__FILE__, __LINE__, "unknown exception");
        }
        if (skip) {
            ++skipped;
        } else if (g_caseFailures) {
            ++failed;
            std::printf("[ FAIL ] %s\n", c.name);
        } else {
            ++passed;
            std::printf("[  OK  ] %s\n", c.name);
        }
    }
    std::printf("\n%d passed, %d failed, %d skipped\n", passed, failed, skipped);
    if (failed) return 1;
    if (passed == 0 && skipped > 0) return 77;
    return 0;
}

}  // namespace sptest

int main(int argc, char** argv) { return sptest::runAll(argc > 1 ? argv[1] : nullptr); }
