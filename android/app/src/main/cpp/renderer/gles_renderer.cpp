#include "renderer/gles_renderer.hpp"

#include "native/filters/filter_pipeline.hpp"
#include "native/rendering/compositor_math.hpp"

#include <GLES2/gl2ext.h>

namespace trisvax::android {

namespace {

const char* kVertexShaderSrc = R"GLSL(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
uniform mat4 uTransform;
out vec2 vTexCoord;
void main() {
  gl_Position = uTransform * vec4(aPosition, 0.0, 1.0);
  vTexCoord = aTexCoord;
}
)GLSL";

GLuint compileShader(GLenum type, const char* src) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

} // namespace

GlesRenderer::~GlesRenderer() {
  detach();
}

bool GlesRenderer::attachSurface(ANativeWindow* window, int width, int height) {
  detach();
  if (!window) return false;
  window_ = window;
  ANativeWindow_acquire(window_);
  width_ = width;
  height_ = height;

  display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
    detach();
    return false;
  }

  const EGLint attribs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_NONE};
  EGLConfig cfg{};
  EGLint numCfg = 0;
  if (!eglChooseConfig(display_, attribs, &cfg, 1, &numCfg) || numCfg == 0) {
    detach();
    return false;
  }

  const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  context_ = eglCreateContext(display_, cfg, EGL_NO_CONTEXT, ctxAttribs);
  surface_ = eglCreateWindowSurface(display_, cfg, window_, nullptr);
  if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE) {
    detach();
    return false;
  }
  if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
    detach();
    return false;
  }
  return initGlResources();
}

bool GlesRenderer::attachOffscreen(int width, int height) {
  detach();
  width_ = width;
  height_ = height;
  display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
    detach();
    return false;
  }
  const EGLint attribs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_NONE};
  EGLConfig cfg{};
  EGLint numCfg = 0;
  if (!eglChooseConfig(display_, attribs, &cfg, 1, &numCfg) || numCfg == 0) {
    detach();
    return false;
  }
  const EGLint pbufAttribs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
  const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  context_ = eglCreateContext(display_, cfg, EGL_NO_CONTEXT, ctxAttribs);
  surface_ = eglCreatePbufferSurface(display_, cfg, pbufAttribs);
  if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE ||
      !eglMakeCurrent(display_, surface_, surface_, context_)) {
    detach();
    return false;
  }
  return initGlResources();
}

bool GlesRenderer::initGlResources() {
  const std::string fragSrc = FilterPipeline::buildGlslEs300FragmentShader();
  GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc.c_str());
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return false;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);

  static const float kQuad[] = {
      -1.0f, -1.0f, 0.0f, 1.0f,
       1.0f, -1.0f, 1.0f, 1.0f,
      -1.0f,  1.0f, 0.0f, 0.0f,
       1.0f,  1.0f, 1.0f, 0.0f,
  };
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<const void*>(2 * sizeof(float)));

  glGenTextures(1, &externalTexId_);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, externalTexId_);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return true;
}

void GlesRenderer::renderFrame(
    GLuint externalTexId,
    int srcWidth,
    int srcHeight,
    const CropSettings& crop,
    const FilterSettings& filter,
    const TransitionSpec& transition,
    float transitionAlpha) {
  if (!program_ || display_ == EGL_NO_DISPLAY) return;
  eglMakeCurrent(display_, surface_, surface_, context_);
  glViewport(0, 0, width_, height_);
  glClearColor(0.043f, 0.031f, 0.027f, 1.0f); // #0B0807 TRISVAX background
  glClear(GL_COLOR_BUFFER_BIT);

  glUseProgram(program_);
  const auto mvp = CompositorMath::computeTransformMatrix(srcWidth, srcHeight, width_, height_, crop);
  glUniformMatrix4fv(glGetUniformLocation(program_, "uTransform"), 1, GL_FALSE, mvp.data());

  const FilterSettings f = FilterPipeline::clamp(filter);
  glUniform1f(glGetUniformLocation(program_, "uBrightness"), f.brightness);
  glUniform1f(glGetUniformLocation(program_, "uContrast"), f.contrast);
  glUniform1f(glGetUniformLocation(program_, "uSaturation"), f.saturation);
  glUniform1f(glGetUniformLocation(program_, "uTemperature"), f.temperature);
  glUniform1f(glGetUniformLocation(program_, "uExposure"), f.exposure);
  glUniform1f(glGetUniformLocation(program_, "uSharpness"), f.sharpness);
  glUniform2f(glGetUniformLocation(program_, "uTexelSize"),
              1.0f / static_cast<float>(std::max(1, srcWidth)),
              1.0f / static_cast<float>(std::max(1, srcHeight)));
  glUniform1i(glGetUniformLocation(program_, "uTransitionType"), static_cast<int>(transition.type));
  glUniform1f(glGetUniformLocation(program_, "uTransitionAlpha"), transitionAlpha);
  glUniform1i(glGetUniformLocation(program_, "uUsePrevTexture"), 0);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, externalTexId ? externalTexId : externalTexId_);
  glUniform1i(glGetUniformLocation(program_, "uTexture"), 0);

  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  eglSwapBuffers(display_, surface_);
}

void GlesRenderer::detach() {
  if (display_ != EGL_NO_DISPLAY) {
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (externalTexId_) { glDeleteTextures(1, &externalTexId_); externalTexId_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
    if (surface_ != EGL_NO_SURFACE) { eglDestroySurface(display_, surface_); surface_ = EGL_NO_SURFACE; }
    if (context_ != EGL_NO_CONTEXT) { eglDestroyContext(display_, context_); context_ = EGL_NO_CONTEXT; }
    eglTerminate(display_);
    display_ = EGL_NO_DISPLAY;
  }
  if (window_) {
    ANativeWindow_release(window_);
    window_ = nullptr;
  }
}

} // namespace trisvax::android
