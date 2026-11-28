#pragma once

#include <android/native_window.h>
#include <cstdint>

namespace nairoclip_android_preview {

// Owns an EGL/GLES2 presentation context for the supplied Android Surface.
// The ANativeWindow remains owned by the JNI SurfaceState caller.
bool attach(ANativeWindow* window, int width, int height);
void detach();
bool draw(const std::uint8_t* rgba, int width, int height);
bool ready();

}  // namespace nairoclip_android_preview
