#include "headless_gl.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstdio>
#include <cstring>
#include <memory>

#include "test.h"

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

namespace sptest {

namespace {

bool hasExt(const char* list, const char* ext) {
    if (!list) return false;
    const size_t n = std::strlen(ext);
    for (const char* p = list; (p = std::strstr(p, ext)); p += n) {
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return true;
    }
    return false;
}

}  // namespace

HeadlessGl::~HeadlessGl() {
    EGLDisplay dpy = static_cast<EGLDisplay>(display_);
    if (!dpy) return;
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface_) eglDestroySurface(dpy, static_cast<EGLSurface>(surface_));
    if (context_) eglDestroyContext(dpy, static_cast<EGLContext>(context_));
    eglTerminate(dpy);
}

bool HeadlessGl::create(std::string* error) {
    const char* clientExts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    auto getPlatformDisplay =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));

    EGLDisplay dpy = EGL_NO_DISPLAY;
    if (getPlatformDisplay && hasExt(clientExts, "EGL_MESA_platform_surfaceless")) {
        dpy = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (dpy != EGL_NO_DISPLAY && eglInitialize(dpy, nullptr, nullptr)) {
            description_ = "EGL surfaceless platform";
        } else {
            dpy = EGL_NO_DISPLAY;
        }
    }
    if (dpy == EGL_NO_DISPLAY && getPlatformDisplay && hasExt(clientExts, "EGL_EXT_platform_device")) {
        auto queryDevices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(eglGetProcAddress("eglQueryDevicesEXT"));
        EGLDeviceEXT devices[8];
        EGLint count = 0;
        if (queryDevices && queryDevices(8, devices, &count) && count > 0) {
            for (EGLint i = 0; i < count && dpy == EGL_NO_DISPLAY; ++i) {
                dpy = getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr);
                if (dpy != EGL_NO_DISPLAY && eglInitialize(dpy, nullptr, nullptr))
                    description_ = "EGL device platform";
                else
                    dpy = EGL_NO_DISPLAY;
            }
        }
    }
    if (dpy == EGL_NO_DISPLAY) {
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)) {
            *error = "no usable EGL display";
            return false;
        }
        description_ = "EGL default display";
    }
    display_ = dpy;

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        *error = "eglBindAPI(GLES) failed";
        return false;
    }

    const EGLint configAttribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    const char* dpyExts = eglQueryString(dpy, EGL_EXTENSIONS);
    const bool noConfig = hasExt(dpyExts, "EGL_KHR_no_config_context");
    const bool surfaceless = hasExt(dpyExts, "EGL_KHR_surfaceless_context");
    if (!eglChooseConfig(dpy, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        config = nullptr;
        if (!noConfig || !surfaceless) {
            *error = "no GLES3 pbuffer config";
            return false;
        }
    }

    const EGLint ctxAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, config ? config : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctxAttribs);
    if (ctx == EGL_NO_CONTEXT) {
        *error = "eglCreateContext(GLES 3.0) failed";
        return false;
    }
    context_ = ctx;

    EGLSurface surf = EGL_NO_SURFACE;
    if (!surfaceless) {
        const EGLint pbAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        surf = eglCreatePbufferSurface(dpy, config, pbAttribs);
        if (surf == EGL_NO_SURFACE) {
            *error = "no surfaceless context support and pbuffer creation failed";
            return false;
        }
        surface_ = surf;
    }
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        *error = "eglMakeCurrent failed";
        return false;
    }
    return true;
}

HeadlessGl& requireGl() {
    static std::unique_ptr<HeadlessGl> gl;
    static bool tried = false;
    static std::string error;
    if (!tried) {
        tried = true;
        gl = std::make_unique<HeadlessGl>();
        if (gl->create(&error)) {
            std::printf("    (headless GL: %s)\n", gl->description().c_str());
        } else {
            gl.reset();
        }
    }
    if (!gl) SKIP("no headless GLES 3 context: " + error);
    return *gl;
}

}  // namespace sptest
