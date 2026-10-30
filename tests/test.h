#pragma once
// Minimal test runner: TEST(name) { CHECK(...); REQUIRE(...); SKIP("why"); }

#include <sstream>
#include <string>
#include <vector>

namespace sptest {

struct Case {
    const char* name;
    void (*fn)();
};
std::vector<Case>& registry();

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct RequireFailed {};
struct Skipped {
    std::string why;
};

void reportFailure(const char* file, int line, const std::string& message);

template <typename A, typename B>
std::string describe(const char* expr, const A& a, const B& b) {
    std::ostringstream s;
    s << expr << "  [" << a << " vs " << b << "]";
    return s.str();
}

// Runs all registered tests whose name contains `filter` (empty = all).
// Returns 0 if all passed, 1 on failures, 77 if everything was skipped.
int runAll(const char* filter);

}  // namespace sptest

#define SP_CONCAT2(a, b) a##b
#define SP_CONCAT(a, b) SP_CONCAT2(a, b)

#define TEST(name)                                                      \
    static void name();                                                 \
    static ::sptest::Registrar SP_CONCAT(name, _registrar)(#name, name); \
    static void name()

#define CHECK(cond) \
    do { if (!(cond)) ::sptest::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        const auto& sp_a_ = (a);                                                                    \
        const auto& sp_b_ = (b);                                                                    \
        if (!(sp_a_ == sp_b_))                                                                      \
            ::sptest::reportFailure(__FILE__, __LINE__, ::sptest::describe("CHECK_EQ(" #a ", " #b ")", sp_a_, sp_b_)); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                       \
    do {                                                                                            \
        const double sp_a_ = (a), sp_b_ = (b);                                                      \
        if (!(sp_a_ - sp_b_ <= (eps) && sp_b_ - sp_a_ <= (eps)))                                    \
            ::sptest::reportFailure(__FILE__, __LINE__, ::sptest::describe("CHECK_NEAR(" #a ", " #b ")", sp_a_, sp_b_)); \
    } while (0)

#define REQUIRE(cond)                                                                     \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            ::sptest::reportFailure(__FILE__, __LINE__, "REQUIRE(" #cond ")");          \
            throw ::sptest::RequireFailed{};                                              \
        }                                                                                 \
    } while (0)

#define SKIP(why) throw ::sptest::Skipped{why}
