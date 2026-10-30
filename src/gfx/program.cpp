#include "gfx/program.h"

#include <EGL/egl.h>

#include <vector>

#ifndef GL_COMPLETION_STATUS_KHR
#define GL_COMPLETION_STATUS_KHR 0x91B1
#endif

namespace sp {

void enableParallelCompile() {
    using Fn = void (*)(GLuint);
    auto fn = reinterpret_cast<Fn>(eglGetProcAddress("glMaxShaderCompilerThreadsKHR"));
    if (fn) fn(0xFFFFFFFFu);
}

namespace {

std::string shaderLog(GLuint shader) {
    GLint len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1) return {};
    std::vector<char> buf(static_cast<size_t>(len));
    glGetShaderInfoLog(shader, len, nullptr, buf.data());
    return std::string(buf.data());
}

std::string programLog(GLuint program) {
    GLint len = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1) return {};
    std::vector<char> buf(static_cast<size_t>(len));
    glGetProgramInfoLog(program, len, nullptr, buf.data());
    return std::string(buf.data());
}

GLuint compile(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    return s;
}

}  // namespace

void ProgramBuild::start(const std::string& vertexSrc, const std::string& fragmentSrc, bool parallel) {
    cancel();
    started_ = std::chrono::steady_clock::now();
    log_.clear();
    vs_ = compile(GL_VERTEX_SHADER, vertexSrc);
    fs_ = compile(GL_FRAGMENT_SHADER, fragmentSrc);
    program_ = glCreateProgram();
    glAttachShader(program_, vs_);
    glAttachShader(program_, fs_);
    glLinkProgram(program_);
    status_ = Status::Pending;
    if (!parallel) finish();
}

ProgramBuild::Status ProgramBuild::poll() {
    if (status_ != Status::Pending) return status_;
    GLint done = GL_FALSE;
    glGetProgramiv(program_, GL_COMPLETION_STATUS_KHR, &done);
    if (glGetError() != GL_NO_ERROR) done = GL_TRUE;  // extension missing after all: just block
    if (done) finish();
    return status_;
}

void ProgramBuild::finish() {
    GLint vsOk = GL_FALSE, fsOk = GL_FALSE, linked = GL_FALSE;
    glGetShaderiv(vs_, GL_COMPILE_STATUS, &vsOk);
    glGetShaderiv(fs_, GL_COMPILE_STATUS, &fsOk);
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    elapsedMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_).count();

    const std::string fsLog = shaderLog(fs_);
    const std::string vsLog = shaderLog(vs_);
    const std::string linkLog = programLog(program_);
    if (!vsLog.empty()) log_ += "[vertex]\n" + vsLog + "\n";
    if (!fsLog.empty()) log_ += fsLog + "\n";
    // A link log after a failed compile only repeats "uncompiled shader"; skip it.
    if (!linkLog.empty() && vsOk && fsOk && !linked) log_ += "[link]\n" + linkLog + "\n";

    glDetachShader(program_, vs_);
    glDetachShader(program_, fs_);
    glDeleteShader(vs_);
    glDeleteShader(fs_);
    vs_ = fs_ = 0;

    if (vsOk && fsOk && linked) {
        status_ = Status::Ready;
    } else {
        if (log_.empty()) log_ = "compile or link failed (driver gave no log)";
        glDeleteProgram(program_);
        program_ = 0;
        status_ = Status::Failed;
    }
}

GLuint ProgramBuild::takeProgram() {
    if (status_ != Status::Ready) return 0;
    GLuint p = program_;
    program_ = 0;
    status_ = Status::Idle;
    return p;
}

void ProgramBuild::cancel() {
    if (vs_) glDeleteShader(vs_);
    if (fs_) glDeleteShader(fs_);
    if (program_) glDeleteProgram(program_);
    vs_ = fs_ = program_ = 0;
    status_ = Status::Idle;
}

}  // namespace sp
