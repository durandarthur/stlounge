#pragma once
// Shader program building. With GL_KHR_parallel_shader_compile the driver
// compiles in the background and poll() checks progress without blocking,
// so the current shader keeps animating while the next one compiles.

#include <chrono>
#include <string>

#include "gfx/gl.h"

namespace sp {

// Asks the driver to use as many compiler threads as it likes. Call once
// after context creation if the extension is present.
void enableParallelCompile();

class ProgramBuild {
public:
    enum class Status { Idle, Pending, Ready, Failed };

    ProgramBuild() = default;
    ~ProgramBuild() { cancel(); }
    ProgramBuild(const ProgramBuild&) = delete;
    ProgramBuild& operator=(const ProgramBuild&) = delete;

    // parallel=false compiles and links synchronously inside start().
    void start(const std::string& vertexSrc, const std::string& fragmentSrc, bool parallel);
    // Non-blocking when parallel; returns the current status.
    Status poll();
    Status status() const { return status_; }

    // Hands over the linked program (caller deletes it). Only when Ready.
    GLuint takeProgram();
    // Compile/link log (vertex, fragment and link sections), empty if none.
    const std::string& log() const { return log_; }
    double elapsedMs() const { return elapsedMs_; }

    void cancel();

private:
    void finish();

    GLuint vs_ = 0, fs_ = 0, program_ = 0;
    Status status_ = Status::Idle;
    std::string log_;
    std::chrono::steady_clock::time_point started_;
    double elapsedMs_ = 0;
};

}  // namespace sp
