#include "AndroidGpuPreviewRenderer.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace nairoclip_android_preview {
namespace {

const char* kVertexShader =
    "attribute vec2 aPosition;\n"
    "attribute vec2 aTexCoord;\n"
    "varying vec2 vTexCoord;\n"
    "void main(){ gl_Position=vec4(aPosition,0.0,1.0); vTexCoord=aTexCoord; }\n";

const char* kFragmentShader =
    "precision mediump float;\n"
    "uniform sampler2D uTexture;\n"
    "varying vec2 vTexCoord;\n"
    "void main(){ gl_FragColor=texture2D(uTexture,vTexCoord); }\n";

struct Renderer {
    std::mutex mutex;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    GLuint program = 0;
    GLuint texture = 0;
    GLint position = -1;
    GLint texCoord = -1;
    GLint sampler = -1;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    bool initialized = false;
};

Renderer g_renderer;

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool createProgram(Renderer& state) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        return false;
    }
    state.program = glCreateProgram();
    glAttachShader(state.program, vertex);
    glAttachShader(state.program, fragment);
    glBindAttribLocation(state.program, 0, "aPosition");
    glBindAttribLocation(state.program, 1, "aTexCoord");
    glLinkProgram(state.program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(state.program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        glDeleteProgram(state.program);
        state.program = 0;
        return false;
    }
    state.position = glGetAttribLocation(state.program, "aPosition");
    state.texCoord = glGetAttribLocation(state.program, "aTexCoord");
    state.sampler = glGetUniformLocation(state.program, "uTexture");
    return state.position >= 0 && state.texCoord >= 0 && state.sampler >= 0;
}

void destroyLocked(Renderer& state) {
    if (state.display != EGL_NO_DISPLAY && state.context != EGL_NO_CONTEXT &&
        state.surface != EGL_NO_SURFACE) {
        eglMakeCurrent(state.display, state.surface, state.surface, state.context);
        if (state.texture != 0) glDeleteTextures(1, &state.texture);
        if (state.program != 0) glDeleteProgram(state.program);
        eglMakeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (state.display != EGL_NO_DISPLAY) {
        if (state.surface != EGL_NO_SURFACE) eglDestroySurface(state.display, state.surface);
        if (state.context != EGL_NO_CONTEXT) eglDestroyContext(state.display, state.context);
        eglTerminate(state.display);
    }
    state.display = EGL_NO_DISPLAY;
    state.context = EGL_NO_CONTEXT;
    state.surface = EGL_NO_SURFACE;
    state.program = 0;
    state.texture = 0;
    state.position = -1;
    state.texCoord = -1;
    state.sampler = -1;
    state.surfaceWidth = 0;
    state.surfaceHeight = 0;
    state.textureWidth = 0;
    state.textureHeight = 0;
    state.initialized = false;
}

bool makeCurrent(Renderer& state) {
    return state.display != EGL_NO_DISPLAY && state.surface != EGL_NO_SURFACE &&
           state.context != EGL_NO_CONTEXT &&
           eglMakeCurrent(state.display, state.surface, state.surface, state.context) == EGL_TRUE;
}

}  // namespace

bool attach(ANativeWindow* window, int width, int height) {
    if (window == nullptr || width <= 0 || height <= 0) return false;
    std::lock_guard<std::mutex> lock(g_renderer.mutex);
    Renderer& state = g_renderer;
    destroyLocked(state);

    state.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state.display == EGL_NO_DISPLAY) {
        destroyLocked(state);
        return false;
    }
    EGLint major = 0;
    EGLint minor = 0;
    if (eglInitialize(state.display, &major, &minor) != EGL_TRUE) {
        destroyLocked(state);
        return false;
    }

    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (eglChooseConfig(state.display, configAttributes, &config, 1, &configCount) != EGL_TRUE ||
        configCount < 1) {
        destroyLocked(state);
        return false;
    }
    EGLint visualId = 0;
    if (eglGetConfigAttrib(state.display, config, EGL_NATIVE_VISUAL_ID, &visualId) != EGL_TRUE ||
        ANativeWindow_setBuffersGeometry(window, width, height, visualId) != 0) {
        destroyLocked(state);
        return false;
    }

    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    state.context = eglCreateContext(state.display, config, EGL_NO_CONTEXT, contextAttributes);
    if (state.context == EGL_NO_CONTEXT) {
        destroyLocked(state);
        return false;
    }
    state.surface = eglCreateWindowSurface(state.display, config, window, nullptr);
    if (state.surface == EGL_NO_SURFACE || !makeCurrent(state)) {
        destroyLocked(state);
        return false;
    }
    if (!createProgram(state)) {
        destroyLocked(state);
        return false;
    }
    glGenTextures(1, &state.texture);
    if (state.texture == 0) {
        destroyLocked(state);
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, state.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    state.surfaceWidth = width;
    state.surfaceHeight = height;
    state.initialized = true;
    eglMakeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return true;
}

void detach() {
    std::lock_guard<std::mutex> lock(g_renderer.mutex);
    destroyLocked(g_renderer);
}

bool draw(const std::uint8_t* rgba, int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) return false;
    std::lock_guard<std::mutex> lock(g_renderer.mutex);
    Renderer& state = g_renderer;
    if (!state.initialized) return false;
    if (!makeCurrent(state)) {
        // Release a broken EGL window surface before JNI attempts ANativeWindow CPU fallback.
        destroyLocked(state);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, state.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (state.textureWidth != width || state.textureHeight != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        state.textureWidth = width;
        state.textureHeight = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }

    const float surfaceAspect = static_cast<float>(state.surfaceWidth) /
                                static_cast<float>(state.surfaceHeight);
    const float frameAspect = static_cast<float>(width) / static_cast<float>(height);
    int viewportWidth = state.surfaceWidth;
    int viewportHeight = state.surfaceHeight;
    if (frameAspect > surfaceAspect) {
        viewportHeight = std::max(1, static_cast<int>(std::lround(
            static_cast<float>(state.surfaceWidth) / frameAspect)));
    } else {
        viewportWidth = std::max(1, static_cast<int>(std::lround(
            static_cast<float>(state.surfaceHeight) * frameAspect)));
    }
    const int viewportX = (state.surfaceWidth - viewportWidth) / 2;
    const int viewportY = (state.surfaceHeight - viewportHeight) / 2;
    glViewport(0, 0, state.surfaceWidth, state.surfaceHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);

    static const GLfloat vertices[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f
    };
    glUseProgram(state.program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state.texture);
    glUniform1i(state.sampler, 0);
    glEnableVertexAttribArray(static_cast<GLuint>(state.position));
    glEnableVertexAttribArray(static_cast<GLuint>(state.texCoord));
    glVertexAttribPointer(static_cast<GLuint>(state.position), 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), vertices);
    glVertexAttribPointer(static_cast<GLuint>(state.texCoord), 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), vertices + 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(static_cast<GLuint>(state.position));
    glDisableVertexAttribArray(static_cast<GLuint>(state.texCoord));

    const bool rendered = glGetError() == GL_NO_ERROR;
    const bool swapped = rendered && eglSwapBuffers(state.display, state.surface) == EGL_TRUE;
    if (!swapped) {
        // A failed EGL surface must not remain attached when the CPU fallback locks the window.
        destroyLocked(state);
        return false;
    }
    eglMakeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return true;
}

bool ready() {
    std::lock_guard<std::mutex> lock(g_renderer.mutex);
    return g_renderer.initialized;
}

}  // namespace nairoclip_android_preview
