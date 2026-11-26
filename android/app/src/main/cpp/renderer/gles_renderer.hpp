#pragma once

#include "native/engine/types.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>

namespace trisvax::android {

class GlesRenderer {
public:
  GlesRenderer() = default;
  ~GlesRenderer();

  bool attachSurface(ANativeWindow* window, int width, int height);
  bool attachOffscreen(int width, int height);
  void detach();

  void renderFrame(
      GLuint externalTexId,
      int srcWidth,
      int srcHeight,
      const CropSettings& crop,
      const FilterSettings& filter,
      const TransitionSpec& transition,
      float transitionAlpha);

  int width() const noexcept { return width_; }
  int height() const noexcept { return height_; }
  GLuint inputExternalTextureId() const noexcept { return externalTexId_; }

private:
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext context_ = EGL_NO_CONTEXT;
  EGLSurface surface_ = EGL_NO_SURFACE;
  ANativeWindow* window_ = nullptr;
  GLuint program_ = 0;
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint externalTexId_ = 0;
  int width_ = 0;
  int height_ = 0;

  bool initGlResources();
};

} // namespace trisvax::android
