#pragma once
// Offscreen GLES 3 context for tests, no window or display needed.
// Tries, in order: Mesa surfaceless platform, EGL device platform, default display.

#include <string>

namespace sptest {

class HeadlessGl {
public:
    ~HeadlessGl();
    bool create(std::string* error);
    std::string description() const { return description_; }

private:
    void* display_ = nullptr;
    void* context_ = nullptr;
    void* surface_ = nullptr;
    std::string description_;
};

// Shared context for all GL tests in the process. SKIPs the test if no context.
HeadlessGl& requireGl();

}  // namespace sptest
