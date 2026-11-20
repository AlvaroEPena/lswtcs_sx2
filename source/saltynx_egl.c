/* SaltyNX telemetry for static Mesa and Android EGL import bridges. */
#include <stdint.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "saltynx.h"
#include "port_menu.h"
#include <switch.h>

extern EGLBoolean __real_eglSwapBuffers(EGLDisplay display, EGLSurface surface);
extern __eglMustCastToProperFunctionPointerType __real_eglGetProcAddress(const char *name);

EGLBoolean __wrap_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    GLint viewport[4] = {0, 0, 0, 0};
    EGLint width = 0, height = 0;
    if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
        /* Resolve through EGL so this also works with SDL renderers that load
         * desktop GL dynamically instead of linking GLES entry-point stubs. */
        typedef void (*GetInteger)(GLenum, GLint *);
        GetInteger get_integer = (GetInteger)__real_eglGetProcAddress("glGetIntegerv");
        if (get_integer)
            get_integer(GL_VIEWPORT, viewport);
        width = viewport[2];
        height = viewport[3];
    }
    if (width <= 0 || height <= 0) {
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);
    }
    EGLint surface_width = 0, surface_height = 0;
    PortMenuSnapshot ui;
    port_menu_snapshot(&ui);
    if ((ui.open || ui.show_fps) && eglGetCurrentContext() != EGL_NO_CONTEXT &&
        eglQuerySurface(display, surface, EGL_WIDTH, &surface_width) == EGL_TRUE &&
        eglQuerySurface(display, surface, EGL_HEIGHT, &surface_height) == EGL_TRUE)
        port_overlay_draw(surface_width, surface_height);
    const EGLBoolean result = __real_eglSwapBuffers(display, surface);
    if (result == EGL_TRUE) {
        port_menu_frame_presented(armGetSystemTick(), armGetSystemTickFreq());
        if (width > 0 && height > 0)
            saltynx_set_resolution((uint32_t)width, (uint32_t)height);
        saltynx_frame(2); /* SaltyNX OpenGL / OpenGL ES API identifier. */
    }
    return result;
}

__eglMustCastToProperFunctionPointerType __wrap_eglGetProcAddress(const char *name) {
    /* Some guests and SDL resolve EGL entry points through this path. */
    if (name && strcmp(name, "eglSwapBuffers") == 0)
        return (__eglMustCastToProperFunctionPointerType)__wrap_eglSwapBuffers;
    return __real_eglGetProcAddress(name);
}
