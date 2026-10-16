// =====================================================================
//  ХАЙВЕЙ РЕЙСЕР — ночная трасса, трафик, опасные обгоны и множитель очков (3D)
//  C++ + SDL для ccdroid / C4droid. Один файл, внешних библиотек нет.
//  Если не собирается: ниже поставь SDL_MODE 1 (SDL 1.2) или 2 (SDL2).
// =====================================================================
#ifndef SDL_MODE
#define SDL_MODE 0            // 0 = авто, 1 = SDL 1.2, 2 = SDL2
#endif
#if SDL_MODE == 0
  #if defined(__has_include)
    #if __has_include(<SDL2/SDL.h>)
      #undef SDL_MODE
      #define SDL_MODE 2
    #endif
  #endif
#endif
#if SDL_MODE == 0
  #undef SDL_MODE
  #define SDL_MODE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#if defined(__GNUC__) && !defined(__clang__) && !defined(__OPTIMIZE__)
  #pragma GCC optimize("O2")      // если среда собирает без оптимизации, включаем её для этого файла
#endif
#include <vector>
#include <cstddef>
#include <map>
#if defined(__unix__) || defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
  #include <unistd.h>
  #define HAVE_GETCWD 1
#endif
#if SDL_MODE == 2
  #include <SDL2/SDL.h>
#else
  #include <SDL/SDL.h>
#endif

// ---------- Утилиты ----------
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline float fminv(float a, float b) { return a < b ? a : b; }
static inline float fmaxv(float a, float b) { return a > b ? a : b; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static float frand(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }

struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }
static inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 operator*(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline V3 vnorm(V3 a) { float l = sqrtf(dot(a, a)); if (l < 1e-8f) l = 1; return a * (1.0f / l); }

typedef uint32_t u32;
// ---- видеокарта (OpenGL ES 2): 3D рисует GPU, а 2D-интерфейс кладётся поверх прозрачной картинкой ----
static bool gGL = false;          // рисуем ли 3D видеокартой
static bool gWantGL = true;       // пробовать ли видеокарту при запуске (можно выключить в меню)
static bool gOverlay = false;     // fb - прозрачный слой интерфейса поверх 3D (альфа в старшем байте)
struct GV { float x, y, z; unsigned char r, g, b, a; };
static std::vector<GV> gVerts;    // треугольники 3D за кадр (в координатах камеры)
static unsigned gSkyCol = 0;
static float gNearZ = 0.3f;   // ближе этой дистанции полигоны обрезаются (в квартире - 0.06, чтобы стены не просвечивали)
static inline u32 RGB(int r, int g, int b) {
  r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
  return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}
static inline int CR(u32 c) { return (c >> 16) & 255; }
static inline int CG(u32 c) { return (c >> 8) & 255; }
static inline int CB(u32 c) { return c & 255; }
static u32 mixc(u32 a, u32 b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  return RGB((int)(CR(a) + (CR(b) - CR(a)) * t), (int)(CG(a) + (CG(b) - CG(a)) * t),
             (int)(CB(a) + (CB(b) - CB(a)) * t));
}
static u32 shadec(u32 c, float f) { return RGB((int)(CR(c) * f), (int)(CG(c) * f), (int)(CB(c) * f)); }

// ---------- Экран и буферы ----------
static int SW = 800, SH = 480, IW = 400, IH = 240, fs = 2;
static int LW = 800, LH = 480;      // логический (горизонтальный) размер экрана
static bool rotMode = false;        // экран вертикальный -> картинку поворачиваем
static int rotDir = 1;              // 1 = по часовой, -1 = против
static int quality = 1;              // 0 низкая, 1 средняя, 2 высокая (разрешение картинки)
static bool aaOn = true;             // сглаживание краёв (можно выключить в меню, если телефон тормозит)
static int wheelN = 10;              // число граней у колёс и фар (растёт с уровнем графики)
static float gProf3D = 0, gProfPr = 0;   // мс за кадр: 3D-превью и вывод на экран (для счётчика FPS)
static bool soundOn = true;           // звук мотора
static bool fpsOn = true;            // счётчик FPS в углу экрана
static bool gDry = false;            // пробный проход: считаем, где фары относительно поверхности, ничего не рисуя
static bool autoPerf = true;         // автоматически упрощать картинку, если телефон не успевает
static int gPerf = 0;                // 0 - полная картинка, 1 - без сглаживания и суперсэмплинга, 2 - ещё и проще колёса, 3 - ещё и картинка на ступень мельче (только пока не хватает скорости)
static int gResDrop = 0;              // на сколько ступеней качества временно уменьшено разрешение (авто-режим, уровень 3)
static bool gApplyQ = false;
static int insL = 0, insR = 0;      // поля экрана: сколько физ. пикселей не видно с начала/конца оси X картинки
static u32 *fb = NULL;
static float *zb = NULL;
static int clipX0, clipY0, clipX1, clipY1;
static float focal = 300, vcx = 0, vcy = 0;
static u32 fogCol = 0;
static float fogA = 1e9f, fogB = 2e9f;
static float gTime = 0;

// ---------- Ввод ----------
struct Ev { int type, x, y; };   // 0 выход, 1 нажатие, 2 отпускание, 3 движение, 4 назад
struct Ptr {
  bool down, pressed, clicked;
  int x, y, sx, sy, cx, cy, dx, dy;
};
static Ptr P;

// ---------- Бэкенд SDL ----------
#if SDL_MODE == 2
static SDL_Window *gWin = NULL;
static SDL_Renderer *gRen = NULL;
static SDL_Texture *gTex = NULL;
static SDL_GLContext gCtx = NULL;

// ======================= OpenGL ES 2 =======================
typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei; typedef float GLfloat; typedef unsigned char GLboolean; typedef char GLchar; typedef unsigned int GLbitfield; typedef ptrdiff_t GLsizeiptr;
#define GLF(ret, name, args) typedef ret (*PFN_##name) args; static PFN_##name p_##name = NULL;
GLF(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) GLF(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) GLF(void, glClear, (GLbitfield))
GLF(void, glEnable, (GLenum)) GLF(void, glDisable, (GLenum)) GLF(void, glDepthFunc, (GLenum)) GLF(void, glDepthMask, (GLboolean)) GLF(void, glBlendFunc, (GLenum, GLenum))
GLF(GLuint, glCreateShader, (GLenum)) GLF(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) GLF(void, glCompileShader, (GLuint))
GLF(void, glGetShaderiv, (GLuint, GLenum, GLint *)) GLF(GLuint, glCreateProgram, (void)) GLF(void, glAttachShader, (GLuint, GLuint))
GLF(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *)) GLF(void, glLinkProgram, (GLuint)) GLF(void, glGetProgramiv, (GLuint, GLenum, GLint *))
GLF(void, glUseProgram, (GLuint)) GLF(GLint, glGetUniformLocation, (GLuint, const GLchar *)) GLF(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))
GLF(void, glUniform2f, (GLint, GLfloat, GLfloat)) GLF(void, glUniform1i, (GLint, GLint)) GLF(void, glBindBuffer, (GLenum, GLuint))
GLF(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) GLF(void, glEnableVertexAttribArray, (GLuint)) GLF(void, glDisableVertexAttribArray, (GLuint))
GLF(void, glDrawArrays, (GLenum, GLint, GLsizei)) GLF(void, glGenTextures, (GLsizei, GLuint *)) GLF(void, glBindTexture, (GLenum, GLuint)) GLF(void, glDeleteTextures, (GLsizei, const GLuint *))
GLF(void, glTexParameteri, (GLenum, GLenum, GLint)) GLF(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))
GLF(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) GLF(void, glActiveTexture, (GLenum)) GLF(void, glPixelStorei, (GLenum, GLint))
GLF(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))
#undef GLF
static GLuint glProg3D = 0, glProg2D = 0, glTexUI = 0; static GLint uP = -1, uNF = -1, uTex = -1; static int glTexW = 0, glTexH = 0, glVW = 0, glVH = 0;
static GLuint glMakeProg(const char *vs, const char *fs2) {
  GLuint v = p_glCreateShader(0x8B31), f = p_glCreateShader(0x8B30); GLint ok = 0;
  p_glShaderSource(v, 1, &vs, NULL); p_glCompileShader(v); p_glGetShaderiv(v, 0x8B81, &ok); if (!ok) return 0;
  p_glShaderSource(f, 1, &fs2, NULL); p_glCompileShader(f); p_glGetShaderiv(f, 0x8B81, &ok); if (!ok) return 0;
  GLuint p = p_glCreateProgram(); p_glAttachShader(p, v); p_glAttachShader(p, f);
  p_glBindAttribLocation(p, 0, "aP"); p_glBindAttribLocation(p, 1, "aC"); p_glLinkProgram(p); p_glGetProgramiv(p, 0x8B82, &ok);
  return ok ? p : 0;
}
// loader - функция, которая по имени даёт адрес функции GL (SDL_GL_GetProcAddress или eglGetProcAddress)
static bool glSetup(void *(*loader)(const char *)) {
  #define LD(n) p_##n = (PFN_##n)loader(#n); if (!p_##n) return false;
  LD(glViewport) LD(glClearColor) LD(glClear) LD(glEnable) LD(glDisable) LD(glDepthFunc) LD(glDepthMask) LD(glBlendFunc) LD(glCreateShader) LD(glShaderSource)
  LD(glCompileShader) LD(glGetShaderiv) LD(glCreateProgram) LD(glAttachShader) LD(glBindAttribLocation) LD(glLinkProgram) LD(glGetProgramiv) LD(glUseProgram)
  LD(glGetUniformLocation) LD(glUniform4f) LD(glUniform2f) LD(glUniform1i) LD(glBindBuffer) LD(glVertexAttribPointer) LD(glEnableVertexAttribArray)
  LD(glDisableVertexAttribArray) LD(glDrawArrays) LD(glGenTextures) LD(glBindTexture) LD(glDeleteTextures) LD(glTexParameteri) LD(glTexImage2D) LD(glTexSubImage2D)
  LD(glActiveTexture) LD(glPixelStorei)
  #undef LD
  p_glReadPixels = (PFN_glReadPixels)loader("glReadPixels");
  // 3D: точки уже в координатах камеры; проекция как у программного движка (focal, vcx, vcy), глубина - видеокартой
  glProg3D = glMakeProg(
    "attribute vec3 aP; attribute vec4 aC; uniform vec4 uP; uniform vec2 uNF; varying vec4 vC;\n"
    "void main(){ vC = aC; gl_Position = vec4(aP.x * uP.x + uP.y * aP.z, aP.y * uP.z + uP.w * aP.z, aP.z * uNF.x + uNF.y, aP.z); }\n",
    "precision mediump float; varying vec4 vC; void main(){ gl_FragColor = vC; }\n");
  // 2D: картинка интерфейса поверх (в памяти байты B,G,R,A - переставляем)
  glProg2D = glMakeProg(
    "attribute vec2 aP; varying vec2 vT; void main(){ vT = vec2(aP.x * 0.5 + 0.5, 0.5 - aP.y * 0.5); gl_Position = vec4(aP, 0.0, 1.0); }\n",
    "precision mediump float; varying vec2 vT; uniform sampler2D uTex; void main(){ vec4 t = texture2D(uTex, vT); gl_FragColor = vec4(t.b, t.g, t.r, t.a); }\n");
  if (!glProg3D || !glProg2D) return false;
  uP = p_glGetUniformLocation(glProg3D, "uP"); uNF = p_glGetUniformLocation(glProg3D, "uNF"); uTex = p_glGetUniformLocation(glProg2D, "uTex");
  p_glGenTextures(1, &glTexUI);
  return true;
}
static void glUITex(int w, int h) {
  if (glTexW == w && glTexH == h) return;
  p_glBindTexture(0x0DE1, glTexUI);
  p_glTexParameteri(0x0DE1, 0x2801, 0x2600); p_glTexParameteri(0x0DE1, 0x2800, 0x2600);      // NEAREST: пиксельный шрифт чёткий
  p_glTexParameteri(0x0DE1, 0x2802, 0x812F); p_glTexParameteri(0x0DE1, 0x2803, 0x812F);
  p_glTexImage2D(0x0DE1, 0, 0x1908, w, h, 0, 0x1908, 0x1401, NULL); glTexW = w; glTexH = h;
}
// кадр: очистка, все 3D-треугольники одним вызовом, потом интерфейс поверх
static void glFrame(int vw, int vh) {
  p_glViewport(0, 0, vw, vh);
  p_glClearColor(((gSkyCol >> 16) & 255) / 255.0f, ((gSkyCol >> 8) & 255) / 255.0f, (gSkyCol & 255) / 255.0f, 1.0f);
  p_glClear(0x4000 | 0x100);
  if (!gVerts.empty()) {
    p_glEnable(0x0B71); p_glDepthFunc(0x0203); p_glDepthMask(1); p_glDisable(0x0BE2); p_glDisable(0x0B44);
    p_glUseProgram(glProg3D); p_glBindBuffer(0x8892, 0);
    float n = fmaxv(0.02f, gNearZ), f = 120.0f;
    p_glUniform4f(uP, 2.0f * focal / IW, 2.0f * vcx / IW - 1.0f, 2.0f * focal / IH, 1.0f - 2.0f * vcy / IH);
    p_glUniform2f(uNF, (f + n) / (f - n), -2.0f * f * n / (f - n));
    p_glEnableVertexAttribArray(0); p_glEnableVertexAttribArray(1);
    p_glVertexAttribPointer(0, 3, 0x1406, 0, sizeof(GV), &gVerts[0].x);
    p_glVertexAttribPointer(1, 4, 0x1401, 1, sizeof(GV), &gVerts[0].r);
    p_glDrawArrays(4, 0, (GLsizei)gVerts.size());
    p_glDisableVertexAttribArray(1);
  }
  p_glDisable(0x0B71); p_glEnable(0x0BE2); p_glBlendFunc(0x0302, 0x0303);
  p_glUseProgram(glProg2D); p_glActiveTexture(0x84C0); glUITex(IW, IH); p_glBindTexture(0x0DE1, glTexUI);
  p_glPixelStorei(0x0CF5, 4); p_glTexSubImage2D(0x0DE1, 0, 0, 0, IW, IH, 0x1908, 0x1401, fb); p_glUniform1i(uTex, 0);
  static const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
  p_glEnableVertexAttribArray(0); p_glVertexAttribPointer(0, 2, 0x1406, 0, 0, quad); p_glDrawArrays(5, 0, 4);
  gVerts.clear();
  memset(fb, 0, sizeof(u32) * IW * IH);          // интерфейс следующего кадра - на прозрачном фоне
}

static void *sdlGLProc(const char *n) { return SDL_GL_GetProcAddress(n); }
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  if (gWantGL) {                                   // пробуем видеокарту; не вышло - обычный режим
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    gWin = SDL_CreateWindow("Remont", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (gWin) { gCtx = SDL_GL_CreateContext(gWin);
      if (!gCtx) { SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16); gCtx = SDL_GL_CreateContext(gWin); }
      if (gCtx && glSetup(sdlGLProc)) { SDL_GL_SetSwapInterval(1); SDL_GetWindowSize(gWin, &SW, &SH); gGL = true; gOverlay = true; return true; }
      if (gCtx) SDL_GL_DeleteContext(gCtx); gCtx = NULL; SDL_DestroyWindow(gWin); gWin = NULL; }
  }
  gWin = SDL_CreateWindow("Remont", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                          SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
  if (!gWin) return false;
  SDL_GetWindowSize(gWin, &SW, &SH);
  gRen = SDL_CreateRenderer(gWin, -1, 0);
  if (!gRen) return false;
  return true;
}
static void gfxTex() {
  if (gGL) return;
  if (gTex) SDL_DestroyTexture(gTex);
  gTex = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, IW, IH);
}
static void gfxResize() {}
static void gfxPresent() {
  if (gGL) { int dw = SW, dh = SH; SDL_GL_GetDrawableSize(gWin, &dw, &dh); glFrame(dw, dh); SDL_GL_SwapWindow(gWin); return; }
  SDL_UpdateTexture(gTex, NULL, fb, IW * 4);
  SDL_RenderClear(gRen);
  if (rotMode) {
    SDL_Rect d;
    int avail = SH - insL - insR;
    int cy = rotDir > 0 ? insL + avail / 2 : SH - insL - avail / 2;
    d.w = avail; d.h = SW; d.x = SW / 2 - d.w / 2; d.y = cy - d.h / 2;
    SDL_RenderCopyEx(gRen, gTex, NULL, &d, rotDir > 0 ? 90.0 : 270.0, NULL, SDL_FLIP_NONE);
  } else {
    SDL_Rect d;
    d.x = insL; d.y = 0; d.w = SW - insL - insR; d.h = SH;
    SDL_RenderCopy(gRen, gTex, NULL, &d);
  }
  SDL_RenderPresent(gRen);
}
static void gfxQuit() { SDL_Quit(); }
#else
static SDL_Surface *gScr = NULL;
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  gScr = SDL_SetVideoMode(0, 0, 32, SDL_SWSURFACE | SDL_FULLSCREEN);
  if (!gScr) gScr = SDL_SetVideoMode(800, 480, 32, SDL_SWSURFACE);
  if (!gScr) return false;
  SW = gScr->w;
  SH = gScr->h;
  return true;
}
static void gfxTex() {}
static u32 *convBuf = NULL, *rotBuf = NULL;
static int *axMap = NULL;
static void gfxResize() { free(convBuf); convBuf = NULL; free(rotBuf); rotBuf = NULL; }
static void gfxPresent() {
  if (SDL_MUSTLOCK(gScr)) SDL_LockSurface(gScr);
  SDL_PixelFormat *f = gScr->format;
  int bpp = f->BytesPerPixel;
  int n = IW * IH;
  if (!axMap) axMap = (int *)malloc(sizeof(int) * SW);
  const u32 *src = fb;
  bool native = bpp == 4 && f->Rshift == 16 && f->Gshift == 8 && f->Bshift == 0 && f->Rloss == 0 && f->Gloss == 0 && f->Bloss == 0;
  if (!native) {       // перевод в формат экрана (только если он не 0x00RRGGBB)
    if (!convBuf) convBuf = (u32 *)malloc(sizeof(u32) * n);
    for (int i = 0; i < n; i++) {
      u32 c = fb[i];
      convBuf[i] = (((c >> 16) & 255) >> f->Rloss << f->Rshift) | (((c >> 8) & 255) >> f->Gloss << f->Gshift) |
                   ((c & 255) >> f->Bloss << f->Bshift);
    }
    src = convBuf;
  }
  int PW = rotMode ? SH : SW, avail = imax(1, PW - insL - insR);
  if (rotMode) {       // транспонируем блоками: строки rotBuf = столбцы картинки (читать потом подряд)
    if (!rotBuf) rotBuf = (u32 *)malloc(sizeof(u32) * n);
    const int B = 16;
    for (int vb = 0; vb < IH; vb += B)
      for (int ub = 0; ub < IW; ub += B) {
        int ve = imin(vb + B, IH), ue = imin(ub + B, IW);
        for (int v = vb; v < ve; v++) {
          const u32 *sr = src + v * IW;
          for (int u = ub; u < ue; u++) rotBuf[u * IH + v] = sr[u];
        }
      }
    for (int x = 0; x < SW; x++) axMap[x] = imin((rotDir > 0 ? SW - 1 - x : x) * IH / SW, IH - 1);
  } else {
    for (int x = 0; x < SW; x++) axMap[x] = clampi((x - insL) * IW / avail, 0, IW - 1);
  }
  unsigned char *base = (unsigned char *)gScr->pixels;
  int prevBy = -1;
  unsigned char *prevRow = NULL;
  for (int y = 0; y < SH; y++) {
    unsigned char *row = base + y * gScr->pitch;
    int by;
    const u32 *sr;
    if (!rotMode) {
      by = imin(y * IH / SH, IH - 1);
      sr = src + by * IW;
    } else {
      int pu = rotDir > 0 ? y : SH - 1 - y;
      if (pu < insL || pu >= SH - insR) { memset(row, 0, SW * bpp); prevBy = -1; continue; }   // поле экрана
      by = clampi((pu - insL) * IW / avail, 0, IW - 1);
      sr = rotBuf + by * IH;
    }
    if (by == prevBy) { memcpy(row, prevRow, SW * bpp); continue; }
    if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < SW; x++) d[x] = sr[axMap[x]]; }
    else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < SW; x++) d[x] = (uint16_t)sr[axMap[x]]; }
    else for (int x = 0; x < SW; x++) { u32 v = sr[axMap[x]]; row[x * 3] = v & 255; row[x * 3 + 1] = (v >> 8) & 255; row[x * 3 + 2] = (v >> 16) & 255; }
    if (!rotMode) {   // поля слева и справа
      if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
      else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
    }
    prevBy = by;
    prevRow = row;
  }
  if (SDL_MUSTLOCK(gScr)) SDL_UnlockSurface(gScr);
  SDL_Flip(gScr);
}
static void gfxQuit() { SDL_Quit(); }
#endif

static bool pollEv(Ev &e) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_QUIT: e.type = 0; e.x = e.y = 0; return true;
      case SDL_MOUSEBUTTONDOWN: e.type = 1; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEBUTTONUP: e.type = 2; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEMOTION: e.type = 3; e.x = ev.motion.x; e.y = ev.motion.y; return true;
      case SDL_KEYDOWN:
        if (ev.key.keysym.sym == SDLK_ESCAPE) { e.type = 4; e.x = e.y = 0; return true; }
        break;
#if SDL_MODE == 2
      case SDL_APP_WILLENTERBACKGROUND:
      case SDL_APP_TERMINATING:
      case SDL_APP_LOWMEMORY:
        e.type = 5; e.x = e.y = 0; return true;
#else
      case SDL_ACTIVEEVENT:
        if (ev.active.gain == 0) { e.type = 5; e.x = e.y = 0; return true; }
        break;
#endif
      default: break;
    }
  }
  return false;
}

// окно на Android часто сначала меньше экрана (пока прячутся системные панели), а потом вырастает:
// если не пересчитать размер, справа остаётся чёрная полоса
static bool gfxCheckResize() {
#if SDL_MODE == 2
  int w = 0, h = 0; SDL_GetWindowSize(gWin, &w, &h);
  if (w > 0 && h > 0 && (w != SW || h != SH)) { SW = w; SH = h; return true; }
#endif
  return false;
}

static void mapPt(int x, int y, int &mx, int &my) {
  int pu, pv;   // pu - физ. координата вдоль оси X картинки, pv - вдоль оси Y
  if (!rotMode) { pu = x; pv = y; }
  else if (rotDir > 0) { pu = y; pv = SW - 1 - x; }
  else { pu = SH - 1 - y; pv = x; }
  int PW = rotMode ? SH : SW, PH = rotMode ? SW : SH;
  int avail = imax(1, PW - insL - insR);
  mx = clampi((pu - insL) * IW / avail, 0, IW - 1);
  my = clampi(pv * IH / imax(1, PH), 0, IH - 1);
}
static void computeSize() {
  static const int T[3] = {360, 480, 600};
  int md = imax(1, imin(SW, SH));
  int qe = imax(0, quality - gResDrop);
  IW = imax(200, LW * T[qe] / md);
  IH = imax(120, LH * T[qe] / md);
  fs = imax(1, imin(IW, IH) / 170);
}
static bool allocBuffers() {
  free(fb); free(zb);
  fb = (u32 *)malloc(sizeof(u32) * IW * IH);
  zb = (float *)malloc(sizeof(float) * IW * IH);
  if (!fb || !zb) return false;
  memset(fb, 0, sizeof(u32) * IW * IH);
  gfxResize();
  gfxTex();
  return true;
}
// Настройки экрана (поля) хранятся отдельно от игры
// Папка для файлов игры. На Android рабочая папка недоступна для записи, поэтому пишем во внутреннюю память приложения
// (иначе деньги и прогресс не сохранялись). На других системах - рядом с игрой, как раньше.
static char gBase[400] = "";
static char gPathBuf[4][520];
static int gPathI = 0;
static const char *pth(const char *name) {
  gPathI = (gPathI + 1) & 3;
  snprintf(gPathBuf[gPathI], sizeof gPathBuf[0], "%s%s", gBase, name);
  return gPathBuf[gPathI];
}
static const char *CFGF = "racer.cfg";
static int autoInset() { return rotMode ? SH * 19 / 1000 : 0; }
static void saveCfg() {
  FILE *f = fopen(pth(CFGF), "w");
  if (!f) return;
  fprintf(f, "%d %d %d %d %d %d %d %d 2\n", insL, insR, rotDir, quality, aaOn ? 1 : 0, fpsOn ? 1 : 0, autoPerf ? 1 : 0, soundOn ? 1 : 0);
  fclose(f);
}
static void loadCfg() {
  FILE *f = fopen(pth(CFGF), "r");
  if (!f) return;
  int a = 0, b = 0, d = 1, q = 1, aa = 1, fp = 1, ap = 1, so = 1, ver = 0;
  int got = fscanf(f, "%d %d %d %d %d %d %d %d %d", &a, &b, &d, &q, &aa, &fp, &ap, &so, &ver);
  if (got < 9 || ver < 2) { a = 0; b = 0; }          // старые поля экрана не берём: приложение теперь рисует на весь экран
  if (got >= 8) soundOn = so != 0;
  if (got >= 3) { insL = clampi(a, 0, 260); insR = clampi(b, 0, 260); rotDir = d < 0 ? -1 : 1; }
  if (got >= 4) quality = clampi(q, 0, 2);
  if (got >= 5) aaOn = aa != 0;
  if (got >= 6) fpsOn = fp != 0;
  if (got >= 7) autoPerf = ap != 0;
  fclose(f);
}

// ---------- Шрифт 5x7 (заглавные, латиница + кириллица) ----------
static const unsigned char FLAT[26][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}};
static const unsigned char FDIG[10][7] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
  {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}};
// А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я
static const unsigned char FCYR[32][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1F,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
  {0x1F,0x10,0x10,0x10,0x10,0x10,0x10},{0x06,0x0A,0x0A,0x0A,0x0A,0x1F,0x11},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
  {0x15,0x15,0x15,0x0E,0x15,0x15,0x15},{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},{0x11,0x11,0x13,0x15,0x19,0x11,0x11},
  {0x0A,0x11,0x13,0x15,0x19,0x11,0x11},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x07,0x09,0x09,0x09,0x09,0x09,0x11},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1F,0x11,0x11,0x11,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x0F,0x01,0x11,0x0E},{0x04,0x0E,0x15,0x15,0x15,0x0E,0x04},
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},{0x12,0x12,0x12,0x12,0x12,0x1F,0x01},{0x11,0x11,0x11,0x0F,0x01,0x01,0x01},
  {0x15,0x15,0x15,0x15,0x15,0x15,0x1F},{0x15,0x15,0x15,0x15,0x15,0x1F,0x01},{0x18,0x08,0x08,0x0E,0x09,0x09,0x0E},
  {0x11,0x11,0x11,0x19,0x15,0x15,0x19},{0x10,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x01,0x0F,0x01,0x11,0x0E},
  {0x12,0x15,0x15,0x1D,0x15,0x15,0x12},{0x0F,0x11,0x11,0x0F,0x05,0x09,0x11}};
static const unsigned char FYO[7] = {0x0A,0x1F,0x10,0x1E,0x10,0x10,0x1F};
struct PG { char c; unsigned char r[7]; };
static const PG FPUN[] = {
  {'.',{0,0,0,0,0,0x0C,0x0C}},{',',{0,0,0,0,0x0C,0x04,0x08}},{':',{0,0x0C,0x0C,0,0x0C,0x0C,0}},
  {'!',{0x04,0x04,0x04,0x04,0x04,0,0x04}},{'?',{0x0E,0x11,0x01,0x02,0x04,0,0x04}},{'-',{0,0,0,0x1F,0,0,0}},
  {'+',{0,0x04,0x04,0x1F,0x04,0x04,0}},{'/',{0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
  {'%',{0x18,0x19,0x02,0x04,0x08,0x13,0x03}},{'$',{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}},
  {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}},{')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
  {'<',{0x02,0x04,0x08,0x10,0x08,0x04,0x02}},{'>',{0x08,0x04,0x02,0x01,0x02,0x04,0x08}},
  {'=',{0,0,0x1F,0,0x1F,0,0}},{'|',{0x04,0x04,0x04,0x04,0x04,0x04,0x04}},{'_',{0,0,0,0,0,0,0x1F}},
  {'\'',{0x04,0x04,0x08,0,0,0,0}},{'*',{0,0x15,0x0E,0x1F,0x0E,0x15,0}},{'#',{0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0}},
  {'~',{0,0,0x08,0x15,0x02,0,0}},{'"',{0x0A,0x0A,0x0A,0,0,0,0}}};
static const int NPUN = sizeof(FPUN) / sizeof(FPUN[0]);

static const unsigned char *glyph(int cp) {
  static const unsigned char blank[7] = {0, 0, 0, 0, 0, 0, 0};
  if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
  if (cp == 0x451) cp = 0x401;
  if (cp >= 'a' && cp <= 'z') cp -= 32;
  if (cp >= 'A' && cp <= 'Z') return FLAT[cp - 'A'];
  if (cp >= '0' && cp <= '9') return FDIG[cp - '0'];
  if (cp >= 0x410 && cp <= 0x42F) return FCYR[cp - 0x410];
  if (cp == 0x401) return FYO;
  if (cp < 128)
    for (int i = 0; i < NPUN; i++)
      if (FPUN[i].c == (char)cp) return FPUN[i].r;
  return blank;
}
static int u8next(const char *&p) {
  unsigned char c = (unsigned char)*p;
  if (c < 0x80) { p++; return c; }
  if ((c & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) { int r = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2; return r; }
  if ((c & 0xF0) == 0xE0 && p[1] && p[2] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { int r = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; return r; }
  p++;
  return '?';
}
static int textW(const char *s, int sc) {
  int n = 0;
  while (*s) { u8next(s); n++; }
  return n * 6 * sc;
}
static inline void putpx(int x, int y, u32 c) {
  if ((unsigned)x < (unsigned)IW && (unsigned)y < (unsigned)IH) fb[y * IW + x] = c;
}
static void fillRect(int x, int y, int w, int h, u32 c) {
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    if (gOverlay) { u32 cc = c | 0xFF000000u; for (int i = x0; i < x1; i++) r[i] = cc; }
    else for (int i = x0; i < x1; i++) r[i] = c;
  }
}
static void fillRectA(int x, int y, int w, int h, u32 c, int a) {   // a: 0..255
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  int ia = 255 - a;
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    for (int i = x0; i < x1; i++) {
      u32 d = r[i];
      if (gOverlay) { u32 da = d >> 24; if (!da) { r[i] = (c & 0xFFFFFFu) | ((u32)a << 24); continue; }
        r[i] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255) | ((da + (255 - da) * a / 255) << 24); continue; }
      r[i] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
static void fillCircle(int cx, int cy, int r, u32 c, int a) {
  int ia = 255 - a;
  for (int j = -r; j <= r; j++) {
    int y = cy + j;
    if ((unsigned)y >= (unsigned)IH) continue;
    int w = (int)sqrtf((float)(r * r - j * j));
    for (int i = -w; i <= w; i++) {
      int x = cx + i;
      if ((unsigned)x >= (unsigned)IW) continue;
      u32 d = fb[y * IW + x];
      if (gOverlay) { u32 da = d >> 24; if (!da) { fb[y * IW + x] = (c & 0xFFFFFFu) | ((u32)a << 24); continue; }
        fb[y * IW + x] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255) | ((da + (255 - da) * a / 255) << 24); continue; }
      fb[y * IW + x] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
// align: 0 слева, 1 по центру, 2 справа
static void drawText(int x, int y, const char *s, int sc, u32 col, int align) {
  int w = textW(s, sc);
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  while (*s) {
    int cp = u8next(s);
    const unsigned char *g = glyph(cp);
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5; c++)
        if (g[r] & (0x10 >> c)) fillRect(x + c * sc, y + r * sc, sc, sc, col);
    x += 6 * sc;
  }
}
static void drawTextSh(int x, int y, const char *s, int sc, u32 col, int align) {
  drawText(x + sc, y + sc, s, sc, RGB(0, 0, 0), align);
  drawText(x, y, s, sc, col, align);
}
// ---------- Камера и растеризатор ----------
static V3 camEye, camF, camR, camU;
static void setCamera(V3 eye, V3 target) {
  camEye = eye;
  camF = vnorm(target - eye);
  camR = vnorm(cross(v3(0, 1, 0), camF));
  camU = cross(camF, camR);
}
static inline V3 toView(V3 p) {
  V3 d = p - camEye;
  return v3(dot(d, camR), dot(d, camU), dot(d, camF));
}
static bool projectPt(V3 p, int &sx, int &sy, float &z) {
  V3 v = toView(p);
  z = v.z;
  if (v.z < 0.5f) return false;
  sx = (int)(vcx + v.x * focal / v.z);
  sy = (int)(vcy - v.y * focal / v.z);
  return true;
}

static void rasterTri(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, u32 col) {
  // сортируем вершины по Y: a <= b <= c
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay);
  float dxac = (cx - ax) * iac, dwac = (cw - aw) * iac;
  float dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f;
    float xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f;
    float w = wl + dw * (xs + 0.5f - xl);
    float *zp = zb + y * IW + xs;
    u32 *fp = fb + y * IW + xs;
    int cnt = xe - xs + 1;
    for (; cnt >= 4; cnt -= 4, zp += 4, fp += 4, w += dw * 4.0f) {      // по 4 пикселя за проход
      float w1 = w + dw, w2 = w1 + dw, w3 = w2 + dw;
      if (w > zp[0]) { zp[0] = w; fp[0] = col; }
      if (w1 > zp[1]) { zp[1] = w1; fp[1] = col; }
      if (w2 > zp[2]) { zp[2] = w2; fp[2] = col; }
      if (w3 > zp[3]) { zp[3] = w3; fp[3] = col; }
    }
    for (; cnt > 0; cnt--, zp++, fp++, w += dw) {
      if (w > *zp) { *zp = w; *fp = col; }
    }
  }
}

// Мягкая тень: умножает цвет пикселей под многоугольником (k/256), глубину не пишет
static void rasterTriShadow(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, int k) {
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay), dxac = (cx - ax) * iac, dwac = (cw - aw) * iac, dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f, w = wl + dw * (xs + 0.5f - xl);
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw) {
      float zv = zb[idx];
      if (zv > 0.0f && w > zv * 0.9985f) {
        u32 c = fb[idx];
        fb[idx] = ((((c >> 16) & 255u) * (u32)k >> 8) << 16) | ((((c >> 8) & 255u) * (u32)k >> 8) << 8) | (((c & 255u) * (u32)k) >> 8);
      }
    }
  }
}
static void drawPolyView(const V3 *in, int n, u32 col, int shadowK = 0) {
  if (gDry) return;
  if (gGL) {
    if (shadowK || n < 3) return;
    bool any = false; for (int i = 0; i < n; i++) if (in[i].z > gNearZ) { any = true; break; } if (!any) return;   // целиком за камерой
    unsigned char cr = (col >> 16) & 255, cg = (col >> 8) & 255, cb = col & 255;
    for (int i = 1; i + 1 < n; i++) { const V3 *t[3] = {&in[0], &in[i], &in[i + 1]};
      for (int k = 0; k < 3; k++) { GV g; g.x = t[k]->x; g.y = t[k]->y; g.z = t[k]->z; g.r = cr; g.g = cg; g.b = cb; g.a = 255; gVerts.push_back(g); } }
    return;
  }
  V3 v[32];
  int m = 0;
  const float NZ = gNearZ;
  for (int i = 0; i < n; i++) {
    V3 a = in[i], b = in[(i + 1) % n];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) v[m++] = a;
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m++] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
    }
  }
  if (m < 3) return;
  float sx[32], sy[32], sw[32], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    sx[i] = vcx + v[i].x * focal * iz;
    sy[i] = vcy - v[i].y * focal * iz;
    sw[i] = iz;
    zs += v[i].z;
  }
  zs /= m;
  if (shadowK) {
    for (int i = 1; i + 1 < m; i++) rasterTriShadow(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], shadowK);
    return;
  }
  if (zs > fogA) col = mixc(col, fogCol, (zs - fogA) / (fogB - fogA));
  for (int i = 1; i + 1 < m; i++) rasterTri(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], col);
}

static V3 LDIR;
static float gLight = 1.0f, gNight = 0.0f;        // освещённость мира (1 - день) и «ночность» (0 день ... 1 ночь)
static float lightOf(V3 n) {
  float diff = fmaxv(0.0f, dot(n, LDIR));
  float amb = n.y < -0.4f ? 0.30f : 0.38f + 0.14f * n.y;
  return (amb + 0.62f * diff) * gLight;
}
// Глянец кодируется в старшем байте цвета (0 - матовый, 255 - зеркальный)
#define SHINE(c, sh) (((c) & 0xFFFFFFu) | ((u32)(sh) << 24))

// Грань с известной нормалью: отсечение обратных, свет, отрисовка
// Растеризатор с плавным цветом по треугольнику (освещение интерполируется между вершинами)
// вершина: [0]=x, [1]=y, [2]=1/z, [3]=R, [4]=G, [5]=B
static void rasterTriG(float *a, float *b, float *c) {
  float *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, dr = (R[2] - L[2]) * inv, dg = (R[3] - L[3]) * inv, db = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, rr = L[2] + dr * off, gg = L[3] + dg * off, bb = L[4] + db * off;
    int idx = y * IW + xs;
    {
      float n1 = (float)(xe - xs), r1 = rr + dr * n1, g1 = gg + dg * n1, b1 = bb + db * n1;
      if (rr >= 0.0f && rr <= 255.0f && r1 >= 0.0f && r1 <= 255.0f && gg >= 0.0f && gg <= 255.0f && g1 >= 0.0f && g1 <= 255.0f &&
          bb >= 0.0f && bb <= 255.0f && b1 >= 0.0f && b1 <= 255.0f) {      // быстрый путь: цвет не выходит за 0..255
        for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
          if (w > zb[idx]) { zb[idx] = w; fb[idx] = (u32)(((int)rr << 16) | ((int)gg << 8) | (int)bb); }
        }
        continue;
      }
    }
    for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
      if (w > zb[idx]) {
        zb[idx] = w;
        int ir = (int)rr, ig = (int)gg, ib = (int)bb;
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir);
        ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig);
        ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
// Многоугольник с цветами в вершинах: отсечение по ближней плоскости, туман, веер треугольников
// Треугольник с текстурой (ливрея): атрибуты [0]=x, [1]=y, [2]=1/z, [3]=u, [4]=v, [5]=освещённость; tc - цвета уровня текстуры, rx0..ry1 - рамка проекции на этом уровне
static void rasterTriT(const float *a0, const float *b0, const float *c0, const u32 *tc, int stride, int rx0, int ry0, int rx1, int ry1, float invs, float fogT) {
  const float *a = a0, *b = b0, *c = c0, *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbl = (float)(fogCol & 255);
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, du = (R[2] - L[2]) * inv, dv = (R[3] - L[3]) * inv, dl = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, u = L[2] + du * off, v = L[3] + dv * off, l = L[4] + dl * off;
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw, u += du, v += dv, l += dl) {
      if (w > zb[idx]) {
        int tx = (int)(u * invs), ty = (int)(v * invs);
        tx = tx < rx0 ? rx0 : (tx > rx1 ? rx1 : tx);
        ty = ty < ry0 ? ry0 : (ty > ry1 ? ry1 : ty);
        u32 pc = tc[ty * stride + tx];
        float lk = l < 0.0f ? 0.0f : (l > 1.3f ? 1.3f : l);
        float cr = ((pc >> 16) & 255) * lk, cg = ((pc >> 8) & 255) * lk, cb = (pc & 255) * lk;
        if (fogT > 0.0f) { cr += (fr - cr) * fogT; cg += (fg - cg) * fogT; cb += (fbl - cb) * fogT; }
        int ir = cr > 255.0f ? 255 : (int)cr, ig = cg > 255.0f ? 255 : (int)cg, ib = cb > 255.0f ? 255 : (int)cb;
        zb[idx] = w; fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
static void drawPolyViewG(const V3 *in, const float (*cin)[3], int n) {
  if (gDry) return;
  if (n == 3 && in[0].z >= 0.3f && in[1].z >= 0.3f && in[2].z >= 0.3f) {      // целый треугольник перед камерой: без отсечения
    float vt3[3][6];
    float zs3 = (in[0].z + in[1].z + in[2].z) * (1.0f / 3.0f);
    for (int i = 0; i < 3; i++) {
      float iz = 1.0f / in[i].z;
      vt3[i][0] = vcx + in[i].x * focal * iz; vt3[i][1] = vcy - in[i].y * focal * iz; vt3[i][2] = iz;
      vt3[i][3] = cin[i][0]; vt3[i][4] = cin[i][1]; vt3[i][5] = cin[i][2];
    }
    if (zs3 > fogA) {
      float t = clampf((zs3 - fogA) / (fogB - fogA), 0.0f, 1.0f);
      float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
      for (int i = 0; i < 3; i++) { vt3[i][3] += (fr - vt3[i][3]) * t; vt3[i][4] += (fg - vt3[i][4]) * t; vt3[i][5] += (fbb - vt3[i][5]) * t; }
    }
    {   // мелкий треугольник (до ~20 пикселей): плавный цвет не виден, красим одним средним цветом - в разы быстрее
      float ar2 = fabsf((vt3[1][0] - vt3[0][0]) * (vt3[2][1] - vt3[0][1]) - (vt3[2][0] - vt3[0][0]) * (vt3[1][1] - vt3[0][1]));
      if (ar2 < 40.0f) {
        int ir = (int)((vt3[0][3] + vt3[1][3] + vt3[2][3]) * (1.0f / 3.0f)), ig = (int)((vt3[0][4] + vt3[1][4] + vt3[2][4]) * (1.0f / 3.0f)),
            ib = (int)((vt3[0][5] + vt3[1][5] + vt3[2][5]) * (1.0f / 3.0f));
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir); ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig); ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        rasterTri(vt3[0][0], vt3[0][1], vt3[0][2], vt3[1][0], vt3[1][1], vt3[1][2], vt3[2][0], vt3[2][1], vt3[2][2], (u32)((ir << 16) | (ig << 8) | ib));
        return;
      }
    }
    rasterTriG(vt3[0], vt3[1], vt3[2]);
    return;
  }
  V3 v[32];
  float cv[32][3];
  int m = 0;
  const float NZ = gNearZ;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    V3 a = in[i], b = in[j];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) { v[m] = a; cv[m][0] = cin[i][0]; cv[m][1] = cin[i][1]; cv[m][2] = cin[i][2]; m++; }
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
      for (int k = 0; k < 3; k++) cv[m][k] = cin[i][k] + (cin[j][k] - cin[i][k]) * t;
      m++;
    }
  }
  if (m < 3) return;
  float vt[32][6], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    vt[i][0] = vcx + v[i].x * focal * iz;
    vt[i][1] = vcy - v[i].y * focal * iz;
    vt[i][2] = iz;
    vt[i][3] = cv[i][0]; vt[i][4] = cv[i][1]; vt[i][5] = cv[i][2];
    zs += v[i].z;
  }
  zs /= m;
  if (zs > fogA) {
    float t = clampf((zs - fogA) / (fogB - fogA), 0.0f, 1.0f);
    float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
    for (int i = 0; i < m; i++) {
      vt[i][3] += (fr - vt[i][3]) * t; vt[i][4] += (fg - vt[i][4]) * t; vt[i][5] += (fbb - vt[i][5]) * t;
    }
  }
  for (int i = 1; i + 1 < m; i++) rasterTriG(vt[0], vt[i], vt[i + 1]);
}
// цвет в вершине: освещение по нормали вершины плюс глянец (те же формулы, что у плоской грани)
// Цвет «окружения» в направлении R: светлый горизонт и голубое небо сверху, тёмная дорога снизу
static void envColor(V3 R, float *o) {
  if (R.y >= 0) {
    float t = sqrtf(fminv(1.0f, R.y));
    o[0] = 238.0f + (40.0f - 238.0f) * t; o[1] = 244.0f + (96.0f - 244.0f) * t; o[2] = 250.0f + (196.0f - 250.0f) * t;
  } else {
    float t = fminv(1.0f, -R.y * 2.2f);
    o[0] = 120.0f + (22.0f - 120.0f) * t; o[1] = 124.0f + (24.0f - 124.0f) * t; o[2] = 130.0f + (28.0f - 130.0f) * t;
  }
}
// Глянец: отражение окружения (сильнее под скользящим углом, как у лака и стекла) и яркий блик солнца. c = r, g, b (0..255)
static void glossMix(V3 p, V3 nrm, int shi, float *c) {
  V3 vd = vnorm(camEye - p), hv = vnorm(vd + LDIR);
  float nd = dot(nrm, vd), fb = 1.0f - fmaxv(0.0f, nd), fr = fb * fb * fb, g = shi * (1.0f / 255.0f);
  V3 R = nrm * (2.0f * nd) - vd;
  float e[3];
  envColor(R, e);
  float kr = clampf(g * (0.30f + 0.70f * fr), 0.0f, 0.85f);
  c[0] += (e[0] - c[0]) * kr; c[1] += (e[1] - c[1]) * kr; c[2] += (e[2] - c[2]) * kr;
  float sb = fmaxv(0.0f, dot(nrm, hv)), s2 = sb * sb, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8;
  float k2 = clampf(s16 * s8 * s4 * g, 0.0f, 0.9f);
  c[0] += (255.0f - c[0]) * k2; c[1] += (255.0f - c[1]) * k2; c[2] += (255.0f - c[2]) * k2;
}
// цвет в вершине: освещение по нормали, затемнение у земли и глянец
static void shadeVertex(V3 p, V3 nrm, u32 col, int shi, float *o) {
  float l = lightOf(nrm);
  float c[3] = {((col >> 16) & 255) * l, ((col >> 8) & 255) * l, (col & 255) * l};
  if (shi) {
    float ao = 0.52f + 0.48f * clampf(p.y * (1.0f / 1.0f), 0.0f, 1.0f);     // низ кузова темнее: машина «стоит» на земле
    c[0] *= ao; c[1] *= ao; c[2] *= ao;
    glossMix(p, nrm, shi, c);
  }
  o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
}
// то же для плоской грани: возвращает готовый цвет
static u32 shadeFlat(V3 fc, V3 nrm, u32 col, int shi, bool lit) {
  col &= 0xFFFFFFu;
  if (!shi) return lit ? shadec(col, lightOf(nrm)) : col;
  float l = lit ? lightOf(nrm) : 1.0f;
  float ao = lit ? 0.52f + 0.48f * clampf(fc.y * (1.0f / 1.0f), 0.0f, 1.0f) : 1.0f;
  float c[3] = {((col >> 16) & 255) * l * ao, ((col >> 8) & 255) * l * ao, (col & 255) * l * ao};
  glossMix(fc, nrm, shi, c);
  return RGB((int)clampf(c[0], 0.0f, 255.0f), (int)clampf(c[1], 0.0f, 255.0f), (int)clampf(c[2], 0.0f, 255.0f));
}
static void faceSmooth(const V3 *p, const V3 *nv, int n, V3 fnrm, u32 col) {
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(fnrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[8];
  float cc[8][3];
  for (int i = 0; i < n && i < 8; i++) { vv[i] = toView(p[i]); shadeVertex(p[i], nv[i], col, shi, cc[i]); }
  drawPolyViewG(vv, cc, n);
}
static void faceN(const V3 *p, int n, V3 nrm, u32 col, bool lit) {
  if (gDry) return;
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(nrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[24];
  for (int i = 0; i < n; i++) vv[i] = toView(p[i]);
  u32 c2 = shadeFlat(fc, nrm, col, shi, lit);
  drawPolyView(vv, n, c2);
}
static void quadFace(V3 c, V3 u, V3 v, V3 n, u32 col, bool lit) {
  V3 p[4];
  p[0] = c - u - v; p[1] = c + u - v; p[2] = c + u + v; p[3] = c - u + v;
  faceN(p, 4, n, col, lit);
}
// Параллелепипед: центр, полуразмеры, поворот вокруг Y
static void drawBox(V3 c, V3 h, float yaw, u32 col, bool lit) {
  if (gDry) return;
  float cs = cosf(yaw), sn = sinf(yaw);
  V3 ax = v3(cs, 0, -sn), ay = v3(0, 1, 0), az = v3(sn, 0, cs);
  V3 ex = ax * h.x, ey = ay * h.y, ez = az * h.z;
  quadFace(c + ey, ex, ez, ay, col, lit);
  quadFace(c + ex, ey, ez, ax, col, lit);
  quadFace(c - ex, ey, ez, ax * -1.0f, col, lit);
  quadFace(c + ez, ex, ey, az, col, lit);
  quadFace(c - ez, ex, ey, az * -1.0f, col, lit);
}
// Призма (колесо) вокруг оси X кузова
static void drawWheel(V3 c, V3 ax, V3 ay, V3 az, float r, float hw, u32 col) {
  // мелкие детали (фары, колпаки) рисуются с меньшим числом граней - на картинке не видно, а полигонов втрое меньше
  int N = r < 0.13f ? imin(wheelN, 8) : (r < 0.25f ? imin(wheelN, 12) : wheelN);
  static float CT[25][24], ST[25][24];
  static bool tabOk[25];
  if (N < 6) N = 6;
  if (!tabOk[N]) {
    for (int i = 0; i < N; i++) { float t = 6.2831853f * i / N; CT[N][i] = cosf(t); ST[N][i] = sinf(t); }
    tabOk[N] = true;
  }
  V3 ring[24];
  for (int i = 0; i < N; i++) ring[i] = ay * (r * CT[N][i]) + az * (r * ST[N][i]);
  for (int i = 0; i < N; i++) {
    int j = (i + 1) % N;
    V3 p[4];
    p[0] = c + ring[i] - ax * hw; p[1] = c + ring[j] - ax * hw;
    p[2] = c + ring[j] + ax * hw; p[3] = c + ring[i] + ax * hw;
    V3 mid = vnorm(ring[i] + ring[j]);
    faceN(p, 4, mid, col, true);
  }
  V3 cap[24];
  for (int s = -1; s <= 1; s += 2) {
    for (int i = 0; i < N; i++) cap[i] = c + ring[i] + ax * (hw * s);
    faceN(cap, N, ax * (float)s, col, true);
  }
}

static void setClipRect(int x0, int y0, int x1, int y1) { clipX0 = x0; clipY0 = y0; clipX1 = x1; clipY1 = y1; }
static void clearDepth() {
  if (gGL) return;
  if (clipX0 == 0 && clipX1 == IW) { memset(zb + clipY0 * IW, 0, sizeof(float) * (size_t)(clipY1 - clipY0) * IW); return; }
  for (int y = clipY0; y < clipY1; y++) { float *r = zb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = 0; }
}
static void fillSky() {
  if (gGL) { gSkyCol = fogCol; return; }
  float horizon = vcy + focal * camF.y / sqrtf(fmaxv(0.05f, 1 - camF.y * camF.y));
  u32 top = mixc(RGB(60, 70, 110), RGB(6, 8, 26), gNight);
  for (int y = clipY0; y < clipY1; y++) {
    u32 c = y < horizon ? mixc(top, fogCol, clampf((y - clipY0) / fmaxv(1.0f, horizon - clipY0), 0, 1)) : fogCol;
    u32 *r = fb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = c;
  }
}
static void groundQuad(float x0, float z0, float x1, float z1, float y, u32 col) {
  if (gLight < 0.999f) col = RGB((int)(CR(col) * gLight), (int)(CG(col) * gLight), (int)(CB(col) * gLight));
  V3 p[4] = {v3(x0, y, z0), v3(x1, y, z0), v3(x1, y, z1), v3(x0, y, z1)};
  V3 vv[4]; for (int i = 0; i < 4; i++) vv[i] = toView(p[i]);
  drawPolyView(vv, 4, col);
}
static void discOct(float cx, float cz, float r, float y, u32 col) {
  V3 vv[8];
  for (int i = 0; i < 8; i++) { float t = 6.2831853f * i / 8 + 0.3927f; vv[i] = toView(v3(cx + cosf(t) * r, y, cz + sinf(t) * r)); }
  drawPolyView(vv, 8, col);
}
static bool hitR(int x, int y, int w, int h) {
  if (!P.clicked) return false;
  if (P.cx >= x && P.cx < x + w && P.cy >= y && P.cy < y + h && P.sx >= x && P.sx < x + w && P.sy >= y && P.sy < y + h) { P.clicked = false; return true; }
  return false;
}
static bool button(int x, int y, int w, int h, const char *label, bool en, u32 col) {
  u32 c = en ? col : RGB(58, 60, 70);
  fillRect(x, y, w, h, shadec(c, 0.55f));
  fillRect(x + 1, y + 1, w - 2, h - 2, c);
  fillRect(x + 1, y + 1, w - 2, imax(1, h / 8), shadec(c, 1.25f));
  char lines[3][64]; int n = 0; const char *p = label;
  while (n < 3) {
    const char *e = strchr(p, '\n'); int len = e ? (int)(e - p) : (int)strlen(p); if (len > 63) len = 63;
    memcpy(lines[n], p, len); lines[n][len] = 0; n++; if (!e) break; p = e + 1;
  }
  int sc = fs; while (sc > 1 && textW(lines[0], sc) > w - 4) sc--;
  int lh = 9 * sc, y0 = y + (h - n * lh + 2 * sc) / 2;
  for (int i = 0; i < n; i++) drawText(x + w / 2, y0 + i * lh, lines[i], sc, en ? RGB(255, 255, 255) : RGB(140, 142, 152), 1);
  return en && hitR(x, y, w, h);
}
static void drawLine(int x0, int y0, int x1, int y1, u32 c, int th) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy, n = 0;
  while (n++ < 4000) {
    fillRect(x0 - th / 2, y0 - th / 2, th, th, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err; if (e2 > -dy) { err -= dy; x0 += sx; } if (e2 < dx) { err += dx; y0 += sy; }
  }
}


// =====================================================================
//  ХАЙВЕЙ РЕЙСЕР: ночная трасса, трафик, опасные обгоны и множитель очков
// =====================================================================
#include <algorithm>
enum { GS_TITLE, GS_PLAY, GS_CRASH, GS_OVER, GS_PAUSE };
static int gs = GS_TITLE; static bool hasBest = false;
static int bestScores[3] = {0, 0, 0}, diff = 1;                          // рекорды по уровням и текущая сложность (0 легко, 1 нормально, 2 жёстко)
#define bestScore (bestScores[diff])
static const char *DIFFN[3] = {"ЛЕГКО", "НОРМАЛЬНО", "ЖЁСТКО"};
static float dSpd() { static const float t[3] = {0.88f, 1.0f, 1.1f}; return t[diff]; }       // скорость трассы
static int dCars() { static const int t[3] = {-2, 0, 3}; return t[diff]; }                    // плотность трафика
static float dLC() { static const float t[3] = {1.7f, 1.0f, 0.6f}; return t[diff]; }          // как часто машины перестраиваются
static float dScore() { static const float t[3] = {0.8f, 1.0f, 1.4f}; return t[diff]; }       // множитель очков
static const float LANE_W = 3.6f, ROAD_HALF = 7.2f, PW = 1.9f, PL = 4.4f;
static const int NLANE = 4;
static float laneX(int l) { return (l - 1.5f) * LANE_W; }
static unsigned hsh(unsigned a) { a ^= a >> 16; a *= 0x7feb352dU; a ^= a >> 15; a *= 0x846ca68bU; a ^= a >> 16; return a; }
static float hf(unsigned a) { return (hsh(a) & 0xFFFFFFu) / 16777216.0f; }
static void fmtNum(char *o, int n, long long v) {
  char t[32]; snprintf(t, sizeof t, "%lld", v < 0 ? -v : v); int l = (int)strlen(t), k = 0;
  if (v < 0 && k < n - 1) o[k++] = '-';
  for (int i = 0; i < l && k < n - 2; i++) { if (i && (l - i) % 3 == 0) o[k++] = ' '; o[k++] = t[i]; }
  o[k] = 0;
}
static volatile float sndF = 60, sndG = 0, sndN = 0, sndBurst = 0, sndPingF = 0, sndPingA = 0, sndWind = 0;
static bool audioOk = false;
static char msg[128] = ""; static float msgT = 0;
static void say(const char *s) { snprintf(msg, sizeof msg, "%s", s); msgT = 3.0f; }

// ---------- состояние заезда ----------
struct Car { float x, z, pz, v, len, wid; int type, lane; u32 col; float tx, lcT, blinkT; int blinkDir; bool moving; unsigned id; };
static std::vector<Car> traffic;
struct Pop { char txt[40]; int pts; float t; u32 col; };
static std::vector<Pop> pops;
struct Part { V3 p, v; float life; u32 col; };
static std::vector<Part> parts;
static float joyX = 0, joyY = 0; static bool actHeld = false;
static float carX = 0, carVX = 0, spd = 0, trip = 0, mult = 1, multT = 0, nitro = 1, crashT = 0, shake = 0, flash = 0, carYaw = 0, crashSpin = 0, crashLift = 0;
static double score = 0; static int passes = 0, closeCalls = 0, topKmh = 0; static float maxMult = 1;
static bool nitroOn = false, newBest = false;
static float lastPassT = -9, lastPassX = 0, lastPassZ = 0; static int lastPassSide = 0; static float runT = 0;
static unsigned carIdN = 1;
static const float DRAW_SW = 235.0f, DRAW_GL = 112.0f;
static float drawDist() { return gGL ? DRAW_GL : DRAW_SW; }

static void saveBest() { FILE *f = fopen(pth("racer.sav"), "wb"); if (f) { int d[5] = {bestScores[0], bestScores[1], bestScores[2], diff, soundOn ? 1 : 0}; fwrite(d, sizeof d, 1, f); fclose(f); } }
static bool loadBest() {
  FILE *f = fopen(pth("racer.sav"), "rb"); if (!f) return false; int d[5] = {0, 0, 0, 1, 1}; size_t n = fread(d, sizeof(int), 5, f); fclose(f);
  if (n == 1) { bestScores[1] = d[0] > 0 ? d[0] : 0; } else if (n >= 3) for (int k = 0; k < 3; k++) bestScores[k] = d[k] > 0 ? d[k] : 0;
  if (n >= 5) { diff = clampi(d[3], 0, 2); soundOn = d[4] != 0; }
  return n >= 1;
}

static void addPop(const char *t, int pts, u32 col) {
  Pop p; snprintf(p.txt, sizeof p.txt, "%s", t); p.pts = pts; p.t = 1.8f; p.col = col;
  pops.insert(pops.begin(), p); if (pops.size() > 3) pops.pop_back();
}
static void sparks(V3 p, int n, V3 dir, float spread) {
  for (int i = 0; i < n && parts.size() < 220; i++) {
    Part q; q.p = p; q.v = dir + v3(frand(-spread, spread), frand(0.0f, spread), frand(-spread, spread)); q.life = frand(0.3f, 0.9f);
    q.col = (rand() & 1) ? RGB(255, 190, 70) : RGB(255, 120, 40); parts.push_back(q);
  }
}

// ---------- трафик ----------
static float cruiseSpeed() { return (46.0f + 54.0f * (1.0f - expf(-trip / 7500.0f))) * dSpd(); }   // м/с: от 165 до 360 км/ч
static bool laneFree(int lane, float z, float len, const Car *except, float margin) {
  for (size_t i = 0; i < traffic.size(); i++) { const Car &c = traffic[i]; if (&c == except) continue;
    if (fabsf(c.x - laneX(lane)) < LANE_W * 0.7f && fabsf(c.z - z) < (c.len + len) * 0.5f + margin) return false; }
  return true;
}
static void spawnCar(float z, bool demo) {
  for (int tries = 0; tries < 6; tries++) {
    int lane = rand() % NLANE; if (demo && lane == 1) continue;
    int r = rand() % 100, type = r < 12 ? 2 : (r < 18 ? 5 : (r < 32 ? 1 : (r < 42 ? 3 : (r < 52 ? 4 : 0))));   // 0 седан, 1 фургон, 2 фура, 3 такси, 4 спорткар, 5 автобус
    float len = type == 2 ? 12.5f : (type == 5 ? 11.0f : (type == 1 ? 5.4f : (type == 4 ? 4.3f : 4.4f))), wid = (type == 2 || type == 5) ? 2.5f : (type == 1 ? 2.0f : (type == 4 ? 1.95f : 1.85f));
    if (!laneFree(lane, z, len, NULL, 34.0f)) continue;
    int occ = 0; for (int l = 0; l < NLANE; l++) if (!laneFree(l, z, len, NULL, 22.0f)) occ++;
    if (occ >= 2) continue;                                                  // всегда оставляем проезд
    Car c; c.x = c.tx = laneX(lane); c.z = c.pz = z; c.lane = lane; c.type = type; c.len = len; c.wid = wid;
    c.v = type == 2 ? frand(17.0f, 24.0f) : (type == 5 ? frand(18.0f, 25.0f) : (type == 4 ? frand(32.0f, 44.0f) : frand(24.0f, 38.0f)));
    static const u32 CC[] = {RGB(30, 34, 46), RGB(160, 164, 170), RGB(120, 24, 28), RGB(24, 64, 120), RGB(210, 212, 214), RGB(40, 90, 70), RGB(190, 150, 40), RGB(60, 60, 66)}, SC[] = {RGB(240, 120, 20), RGB(40, 170, 220), RGB(230, 230, 40), RGB(150, 40, 200)}, BC[] = {RGB(190, 40, 40), RGB(40, 90, 170), RGB(220, 220, 225)};
    static const u32 TC[] = {RGB(220, 222, 226), RGB(40, 80, 150), RGB(180, 60, 40), RGB(70, 74, 80)};
    c.col = type == 2 ? TC[rand() % 4] : (type == 5 ? BC[rand() % 3] : (type == 3 ? RGB(236, 190, 30) : (type == 4 ? SC[rand() % 4] : CC[rand() % 8])));
    c.lcT = frand(3.0f, 12.0f) * dLC(); c.blinkT = 0; c.blinkDir = 0; c.moving = false; c.id = carIdN++;
    traffic.push_back(c); return;
  }
}
static void updateTraffic(float dt, bool demo) {
  float DR = drawDist();
  for (size_t i = 0; i < traffic.size(); i++) {
    Car &c = traffic[i]; c.pz = c.z; c.z += (c.v - spd) * dt;
    if (c.type != 2 && c.type != 5) {                                       // перестроения (с поворотником)
      c.lcT -= dt;
      if (!c.moving && c.blinkDir == 0 && c.lcT <= 0 && c.z > 20 && c.z < DR) {
        int dir = (rand() & 1) ? 1 : -1, nl = c.lane + dir;
        if (nl >= 0 && nl < NLANE && !(demo && nl == 1) && laneFree(nl, c.z, c.len, &c, 18.0f)) { c.blinkDir = dir; c.blinkT = 1.0f; c.tx = laneX(nl); }
        else c.lcT = frand(2.0f, 5.0f) * dLC();
      }
      if (c.blinkDir && !c.moving) { c.blinkT -= dt; if (c.blinkT <= 0) { c.moving = true; c.lane += c.blinkDir; } }
      if (c.moving) { float d = c.tx - c.x, st = 2.6f * dt; if (fabsf(d) <= st) { c.x = c.tx; c.moving = false; c.blinkDir = 0; c.lcT = frand(6.0f, 14.0f) * dLC(); } else c.x += d > 0 ? st : -st; }
    }
  }
  for (size_t i = 0; i < traffic.size();) { if (traffic[i].z < -70.0f || traffic[i].z > DR + 160.0f) traffic.erase(traffic.begin() + i); else i++; }
  int maxCars = 9 + dCars() + imin(9, (int)(trip / 1800.0f)); if (gGL) maxCars = imax(7, maxCars - 3);
  for (int k = 0; k < 2 && (int)traffic.size() < maxCars; k++) spawnCar(DR + 12.0f + frand(0.0f, 70.0f), demo);
}

// ---------- запуск ----------
static void startRun() {
  traffic.clear(); pops.clear(); parts.clear();
  carX = laneX(1) + LANE_W * 0.5f; carVX = 0; spd = 38; trip = 0; mult = 1; multT = 0; nitro = 1; crashT = 0; shake = 0; flash = 0; crashSpin = 0; crashLift = 0; carYaw = 0;
  score = 0; passes = 0; closeCalls = 0; topKmh = 0; maxMult = 1; nitroOn = false; newBest = false; runT = 0; lastPassT = -9;
  for (int i = 0; i < 9; i++) spawnCar(60.0f + i * 22.0f + frand(0, 10), false);
  gs = GS_PLAY;
}
static void crashNow(V3 where) {
  gs = GS_CRASH; crashT = 0; flash = 1.0f; shake = 1.0f; nitroOn = false;
  sparks(where, 70, v3(0, 2.0f, 6.0f), 7.0f); sndBurst = 1.0f;
  if ((int)score > bestScore) { bestScore = (int)score; newBest = true; hasBest = true; saveBest(); }
}

// ---------- геометрия ----------
static void quadV(const V3 *p, u32 col) { V3 vv[4]; for (int i = 0; i < 4; i++) vv[i] = toView(p[i]); drawPolyView(vv, 4, col); }
static void flatQ(float xa0, float xa1, float za, float xb0, float xb1, float zb, float y, u32 col) {
  V3 p[4] = {v3(xa0, y, za), v3(xa1, y, za), v3(xb1, y, zb), v3(xb0, y, zb)}; quadV(p, col);
}
static void wallQ(float x0, float z0, float x1, float z1, float y0, float y1, u32 col) {
  V3 p[4] = {v3(x0, y0, z0), v3(x1, y0, z1), v3(x1, y1, z1), v3(x0, y1, z0)}; quadV(p, col);
}
static void glowAt(V3 p, float rad, u32 col, int a) {
  int sx, sy; float z; if (!projectPt(p, sx, sy, z)) return;
  if (sx < -40 || sy < -40 || sx > IW + 40 || sy > IH + 40) return;
  if (!gGL && sx >= 0 && sy >= 0 && sx < IW && sy < IH && zb[sy * IW + sx] > (1.0f / z) * 1.03f) return;   // закрыто предметом
  int r = (int)(rad * focal / z); if (r < 1) return; if (r > IH / 4) r = IH / 4;
  fillCircle(sx, sy, r, col, a); if (r > 3) fillCircle(sx, sy, imax(1, r / 3), mixc(col, RGB(255, 255, 255), 0.65f), imin(255, a * 3 / 2));
}
static V3 rotOff(float yaw, float ox, float oy, float oz) { float cs = cosf(yaw), sn = sinf(yaw); return v3(cs * ox + sn * oz, oy, -sn * ox + cs * oz); }

// машина: вид сзади. type 0 седан, 1 фургон, 2 фура
static void drawCarModel(float x, float z, float yaw, float w, float l, int type, u32 col, bool brake, int blink, float lift, bool player) {
  V3 c = v3(x, lift, z); u32 dark = RGB(10, 12, 18);
  float hw = w * 0.5f, hl = l * 0.5f;
  groundQuad(x - hw * 1.15f, z - hl * 1.05f, x + hw * 1.15f, z + hl * 1.05f, 0.012f, RGB(5, 5, 7));
  u32 tail = brake ? RGB(255, 40, 40) : RGB(190, 16, 22);
  if (type == 2) {                                                          // фура: кабина и прицеп
    drawBox(c + rotOff(yaw, 0, 2.1f, -hl * 0.12f), v3(hw, 1.45f, hl * 0.78f), yaw, col, true);
    drawBox(c + rotOff(yaw, 0, 0.55f, 0), v3(hw * 0.92f, 0.3f, hl), yaw, RGB(30, 32, 36), true);
    drawBox(c + rotOff(yaw, 0, 1.55f, hl * 0.9f), v3(hw * 0.95f, 1.1f, hl * 0.12f), yaw, mixc(col, RGB(0, 0, 0), 0.25f), true);
    for (int s = -1; s <= 1; s += 2) {
      drawBox(c + rotOff(yaw, s * (hw - 0.35f), 0.95f, -hl * 0.9f - 0.05f), v3(0.28f, 0.09f, 0.05f), yaw, tail, false);
      drawBox(c + rotOff(yaw, s * (hw - 0.2f), 0.4f, -hl * 0.55f), v3(0.14f, 0.4f, 0.55f), yaw, RGB(6, 6, 8), true);
      drawBox(c + rotOff(yaw, s * (hw - 0.2f), 0.4f, -hl * 0.25f), v3(0.14f, 0.4f, 0.55f), yaw, RGB(6, 6, 8), true);
    }
    for (int k = 0; k < 4; k++) drawBox(c + rotOff(yaw, (k - 1.5f) * 0.55f, 1.15f, -hl * 0.9f - 0.06f), v3(0.18f, 0.06f, 0.03f), yaw, (k & 1) ? RGB(210, 210, 210) : RGB(220, 40, 30), false);
    if (blink) { bool on = fmodf(gTime, 0.5f) < 0.25f; if (on) drawBox(c + rotOff(yaw, blink * (hw - 0.1f), 1.15f, -hl * 0.9f - 0.07f), v3(0.14f, 0.1f, 0.04f), yaw, RGB(255, 170, 20), false); }
    return;
  }
  if (type == 5) {                                                          // автобус
    drawBox(c + rotOff(yaw, 0, 1.9f, 0), v3(hw, 1.5f, hl), yaw, col, true);
    drawBox(c + rotOff(yaw, 0, 0.55f, 0), v3(hw * 0.92f, 0.3f, hl), yaw, RGB(30, 32, 36), true);
    drawBox(c + rotOff(yaw, 0, 2.5f, -hl - 0.02f), v3(hw * 0.82f, 0.5f, 0.02f), yaw, dark, false);
    for (int s = -1; s <= 1; s += 2) { drawBox(c + rotOff(yaw, s * (hw - 0.35f), 1.0f, -hl - 0.03f), v3(0.28f, 0.09f, 0.03f), yaw, tail, false);
      drawBox(c + rotOff(yaw, s * (hw - 0.15f), 0.4f, -hl * 0.55f), v3(0.14f, 0.4f, 0.55f), yaw, RGB(6, 6, 8), true); drawBox(c + rotOff(yaw, s * (hw - 0.15f), 0.4f, hl * 0.4f), v3(0.14f, 0.4f, 0.55f), yaw, RGB(6, 6, 8), true); }
    if (blink && fmodf(gTime, 0.5f) < 0.25f) drawBox(c + rotOff(yaw, blink * (hw - 0.1f), 1.2f, -hl - 0.04f), v3(0.14f, 0.1f, 0.04f), yaw, RGB(255, 170, 20), false);
    return;
  }
  float bodyH = type == 1 ? 0.62f : (type == 4 ? 0.2f : (player ? 0.28f : 0.34f));
  drawBox(c + rotOff(yaw, 0, 0.38f + bodyH, 0), v3(hw, bodyH, hl), yaw, col, true);                                    // кузов
  float cabH = type == 1 ? 0.55f : (type == 4 ? 0.17f : (player ? 0.22f : 0.26f)), cabY = 0.38f + bodyH * 2 + cabH;
  drawBox(c + rotOff(yaw, 0, cabY, type == 1 ? -hl * 0.05f : -hl * 0.08f), v3(hw * (type == 1 ? 0.96f : 0.8f), cabH, hl * (type == 1 ? 0.8f : 0.32f)), yaw, mixc(col, RGB(0, 0, 0), 0.15f), true);
  float rz = (type == 1 ? -hl * 0.05f - hl * 0.8f : -hl * 0.08f - hl * 0.32f) - 0.012f;
  drawBox(c + rotOff(yaw, 0, cabY + 0.02f, rz), v3(hw * (type == 1 ? 0.8f : 0.66f), cabH * 0.72f, 0.02f), yaw, dark, false);   // заднее стекло
  for (int s = -1; s <= 1; s += 2) {
    drawBox(c + rotOff(yaw, s * (hw - 0.34f), 0.38f + bodyH * 1.45f, -hl - 0.025f), v3(0.3f, 0.07f, 0.03f), yaw, tail, false);
    drawBox(c + rotOff(yaw, s * (hw + 0.02f), 0.33f, -hl * 0.58f), v3(0.13f, 0.33f, 0.36f), yaw, RGB(6, 6, 8), true);   // колёса
    drawBox(c + rotOff(yaw, s * (hw + 0.02f), 0.33f, hl * 0.52f), v3(0.13f, 0.33f, 0.36f), yaw, RGB(6, 6, 8), true);
  }
  drawBox(c + rotOff(yaw, 0, 0.38f + bodyH * 1.45f, -hl - 0.02f), v3(hw - 0.55f, 0.025f, 0.02f), yaw, tail, false);       // полоса стоп-сигнала
  if (blink && fmodf(gTime, 0.5f) < 0.25f) drawBox(c + rotOff(yaw, blink * (hw - 0.12f), 0.38f + bodyH * 1.05f, -hl - 0.03f), v3(0.12f, 0.07f, 0.03f), yaw, RGB(255, 170, 20), false);
  if (type == 3) drawBox(c + rotOff(yaw, 0, cabY + cabH + 0.1f, -hl * 0.08f), v3(0.28f, 0.08f, 0.12f), yaw, RGB(255, 245, 180), false);   // шашечки на крыше
  if (player && nitroOn && gs == GS_PLAY) { float f = 0.5f + 0.5f * sinf(gTime * 60.0f); for (int s = -1; s <= 1; s += 2) drawBox(c + rotOff(yaw, s * 0.5f, 0.4f, -hl - 0.4f - f * 0.4f), v3(0.1f, 0.1f, 0.4f + f * 0.4f), yaw, RGB(90, 170, 255), false); }
}

// ---------- мир ----------
static void drawBuilding(int side, float cx, float cz, float w, float d, float h, u32 col, unsigned seed, float dist) {
  drawBox(v3(cx, h * 0.5f, cz), v3(w * 0.5f, h * 0.5f, d * 0.5f), 0, col, true);
  float DR = drawDist(); if (dist > DR * 0.9f) return;
  float fx = cx - side * (w * 0.5f + 0.04f);
  int rows = imin((int)(h / 4.4f), dist < 80 ? 13 : (dist < 150 ? 7 : 4)), cols = clampi((int)(d / 3.4f), 2, 6);
  for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) {
    unsigned b = hsh(seed * 977u + r * 131u + c * 17u + 5u); if ((b & 7u) < 3u) continue;
    u32 wc = ((b >> 5) & 7u) < 5u ? RGB(255, 206, 120) : (((b >> 5) & 7u) < 7u ? RGB(160, 205, 255) : RGB(255, 110, 190));
    float z0 = cz - d * 0.5f + (c + 0.18f) * d / cols, z1 = z0 + d / cols * 0.62f, y0 = 1.6f + r * 4.4f;
    wallQ(fx, z0, fx, z1, y0, y0 + 2.3f, wc);
  }
  if (h > 62.0f && fmodf(gTime + (seed & 7u) * 0.3f, 1.4f) < 0.5f) drawBox(v3(cx, h + 0.4f, cz), v3(0.35f, 0.4f, 0.35f), 0, RGB(255, 40, 40), false);
}
static void drawWorld(bool demo) {
  float DR = drawDist();
  // небо и туман
  fogCol = RGB(88, 46, 48); fogA = 45.0f; fogB = DR + 25.0f; gLight = 0.80f; gNight = 1.0f; LDIR = vnorm(v3(-0.35f, 0.85f, -0.4f)); gNearZ = 0.3f;
  float spN = clampf(spd / 105.0f, 0, 1);
  float sx = 0, sy = 0; if (shake > 0) { sx = frand(-1, 1) * shake * 0.35f; sy = frand(-1, 1) * shake * 0.25f; }
  float camX = carX * 0.8f + sx; V3 eye = v3(camX, 2.4f + sy + 0.2f * spN, -7.6f - 1.4f * spN), tg = v3(camX + carVX * 0.05f, 1.15f, 24.0f);
  setClipRect(0, 0, IW, IH); setCamera(eye, tg);
  focal = IW * (0.66f - 0.15f * spN - (nitroOn ? 0.05f : 0.0f)); vcx = IW * 0.5f; vcy = IH * 0.47f;
  fillSky(); clearDepth();
  groundQuad(-400, -40, 400, DR + 80, -0.03f, RGB(11, 11, 15));
  // асфальт: пятна света фар у машины и мокрый отблеск
  float zs = -14.0f;
  for (int i = 0; zs < DR + 20; i++) {
    float z1 = zs + (zs < 40 ? 6.0f : 14.0f); float t = clampf(1.0f - (zs + 8.0f) / 70.0f, 0, 1);
    u32 rc = RGB(30 + (int)(26 * t) + ((i & 1) ? 2 : 0), 32 + (int)(23 * t), 40 + (int)(14 * t));
    flatQ(-ROAD_HALF - 0.9f, ROAD_HALF + 0.9f, zs, -ROAD_HALF - 0.9f, ROAD_HALF + 0.9f, z1 + 0.05f, 0.0f, rc); zs = z1;
  }
  // разметка
  float basePhase = fmodf(trip, 12.0f);
  for (float z = -basePhase - 12.0f; z < DR + 12.0f; z += 12.0f) for (int l = 1; l < NLANE; l++) {
    float x = (l - 2) * LANE_W; flatQ(x - 0.09f, x + 0.09f, z, x - 0.09f, x + 0.09f, z + 3.2f, 0.02f, RGB(205, 205, 190)); }
  for (int s = -1; s <= 1; s += 2) { float x = s * (ROAD_HALF - 0.25f); flatQ(x - 0.1f, x + 0.1f, -14, x - 0.1f, x + 0.1f, DR + 20, 0.02f, RGB(230, 200, 90)); }
  // барьеры
  for (float z = -fmodf(trip, 8.0f) - 8.0f; z < DR + 16.0f; z += 8.0f) for (int s = -1; s <= 1; s += 2) {
    drawBox(v3(s * (ROAD_HALF + 0.55f), 0.45f, z + 4.0f), v3(0.28f, 0.45f, 4.0f), 0, RGB(60, 62, 70), true);
    drawBox(v3(s * (ROAD_HALF + 0.27f), 0.62f, z + 4.0f), v3(0.02f, 0.05f, 3.6f), 0, RGB(210, 120, 40), false); }
  // здания
  const float SP = 34.0f; int s0 = (int)floorf(trip / SP) - 2;
  for (int s = s0; (s * SP - trip) < DR + 40.0f; s++) for (int side = -1; side <= 1; side += 2) for (int row = 0; row < 2; row++) {
    unsigned seed = (unsigned)(s * 7 + (side + 1) * 3 + row * 53) + 1000u; float zc = s * SP - trip + hf(seed) * 10.0f - 5.0f;
    if (zc < -60.0f) continue; if (row == 1 && zc > DR * 0.95f) continue;
    float w = 9.0f + hf(seed + 1) * 14.0f, d = 12.0f + hf(seed + 2) * 14.0f, h = (row ? 30.0f : 16.0f) + hf(seed + 3) * hf(seed + 4) * (row ? 130.0f : 70.0f);
    float cx = side * (ROAD_HALF + 12.0f + row * (w + 14.0f) + w * 0.5f + hf(seed + 5) * 6.0f);
    u32 col = RGB(30 + (int)(hf(seed + 6) * 22), 32 + (int)(hf(seed + 7) * 18), 52 + (int)(hf(seed + 8) * 34));
    drawBuilding(side, cx, zc, w, d, h, col, seed, zc);
  }
  // фонари и их отблески на мокрой дороге
  const float LS = 42.0f; int l0 = (int)floorf(trip / LS) - 1; V3 lampGlow[24]; int nl = 0;
  for (int s = l0; (s * LS - trip) < DR + 30.0f; s++) for (int side = -1; side <= 1; side += 2) {
    float zc = s * LS - trip + (side > 0 ? 0.0f : LS * 0.5f); if (zc < -20) continue;
    float px = side * (ROAD_HALF + 1.3f);
    drawBox(v3(px, 4.6f, zc), v3(0.1f, 4.6f, 0.1f), 0, RGB(50, 52, 58), true);
    drawBox(v3(px - side * 1.4f, 9.1f, zc), v3(1.5f, 0.07f, 0.09f), 0, RGB(50, 52, 58), true);
    drawBox(v3(px - side * 2.6f, 8.98f, zc), v3(0.5f, 0.07f, 0.22f), 0, RGB(255, 214, 140), false);
    flatQ(px - side * 5.2f - 3.6f, px - side * 5.2f + 3.6f, zc - 9.0f, px - side * 5.2f - 3.6f, px - side * 5.2f + 3.6f, zc + 9.0f, 0.008f, RGB(58, 52, 46));
    flatQ(px - side * 4.2f - 0.55f, px - side * 4.2f + 0.55f, zc - 24.0f, px - side * 4.2f - 0.3f, px - side * 4.2f + 0.3f, zc + 1.0f, 0.012f, RGB(84, 60, 38));
    if (nl < 24) lampGlow[nl++] = v3(px - side * 2.6f, 8.9f, zc);
  }
  // дорожные указатели над трассой
  const float GS = 760.0f; int g0 = (int)floorf((trip - 40.0f) / GS);
  for (int s = g0; (s * GS + 380.0f - trip) < DR + 20.0f; s++) { float zc = s * GS + 380.0f - trip; if (zc < -20) continue;
    for (int sd = -1; sd <= 1; sd += 2) drawBox(v3(sd * (ROAD_HALF + 1.6f), 3.2f, zc), v3(0.2f, 3.2f, 0.2f), 0, RGB(60, 62, 70), true);
    drawBox(v3(0, 6.5f, zc), v3(ROAD_HALF + 1.8f, 0.22f, 0.22f), 0, RGB(60, 62, 70), true);
    for (int p = 0; p < 2; p++) { float x0 = -6.0f + p * 6.3f; wallQ(x0, zc - 0.3f, x0 + 5.7f, zc - 0.3f, 4.6f, 6.3f, RGB(16, 110, 72));
      for (int k = 0; k < 3; k++) wallQ(x0 + 0.5f, zc - 0.34f, x0 + 2.2f + k * 0.9f, zc - 0.34f, 5.0f + k * 0.45f, 5.15f + k * 0.45f, RGB(225, 235, 225)); } }
  // свет фар игрока на дороге
  { float yy = 0.011f; flatQ(carX - 1.5f, carX + 1.5f, 2.0f, carX - 2.4f, carX + 2.4f, 30.0f, yy, RGB(52, 52, 56)); }
  // трафик
  for (size_t i = 0; i < traffic.size(); i++) { Car &c = traffic[i]; if (c.z > DR + 10 || c.z < -30) continue;
    for (int s = -1; s <= 1; s += 2) flatQ(c.x + s * (c.wid * 0.5f - 0.35f) - 0.2f, c.x + s * (c.wid * 0.5f - 0.35f) + 0.2f, c.z - c.len * 0.5f - 10.0f, c.x + s * (c.wid * 0.5f - 0.35f) - 0.12f, c.x + s * (c.wid * 0.5f - 0.35f) + 0.12f, c.z - c.len * 0.5f, 0.014f, RGB(105, 14, 16));
    drawCarModel(c.x, c.z, 0, c.wid, c.len, c.type, c.col, false, c.blinkDir, 0, false); }
  // машина игрока
  { bool br = joyY > 0.35f && gs == GS_PLAY;
    for (int s = -1; s <= 1; s += 2) flatQ(carX + s * 0.7f - 0.2f, carX + s * 0.7f + 0.2f, -PL * 0.5f - 10.0f, carX + s * 0.7f - 0.12f, carX + s * 0.7f + 0.12f, -PL * 0.5f, 0.014f, br ? RGB(190, 22, 22) : RGB(110, 14, 16));
    drawCarModel(carX, 0, carYaw + crashSpin, PW, PL, 0, RGB(176, 24, 32), br, 0, crashLift, true); }
  // частицы
  for (size_t i = 0; i < parts.size(); i++) drawBox(parts[i].p, v3(0.07f, 0.07f, 0.07f), 0, parts[i].col, false);
  // блики поверх (экранное свечение)
  for (int i = 0; i < nl; i++) glowAt(lampGlow[i], 0.8f, RGB(255, 190, 100), 50);
  for (size_t i = 0; i < traffic.size(); i++) { Car &c = traffic[i]; if (c.z > 130 || c.z < -3) continue;
    for (int s = -1; s <= 1; s += 2) glowAt(v3(c.x + s * (c.wid * 0.5f - 0.34f), c.type == 2 ? 0.95f : 0.8f, c.z - c.len * 0.5f), 0.32f, RGB(255, 30, 30), 55); }
  { bool br = joyY > 0.35f && gs == GS_PLAY; for (int s = -1; s <= 1; s += 2) glowAt(v3(carX + s * 0.6f, 0.75f, -PL * 0.5f), br ? 0.5f : 0.3f, RGB(255, 30, 30), br ? 100 : 55); }
  (void)demo;
}

// ---------- звук: мотор, ветер, нитро, авария, «пинг» за обгон (всё синтезируется) ----------
static void audioCb(void *, Uint8 *st, int len) {
  short *o = (short *)st; int n = len / 2;
  static float ph = 0, ph2 = 0, pp = 0, f = 60, g = 0, burst = 0, pa = 0, pf = 800, lp = 0, wd = 0, nz2 = 0; static unsigned rs = 12345u;
  if (sndBurst > 0) { burst = sndBurst; sndBurst = 0; }
  if (sndPingA > 0) { pa = sndPingA; pf = sndPingF; sndPingA = 0; pp = 0; }
  for (int i = 0; i < n; i++) {
    f += (sndF - f) * 0.0008f; g += (sndG - g) * 0.0015f; wd += (sndWind - wd) * 0.0008f; nz2 += (sndN - nz2) * 0.001f;
    ph += f / 22050.0f; if (ph >= 1.0f) ph -= 1.0f; ph2 += f * 2.01f / 22050.0f; if (ph2 >= 1.0f) ph2 -= 1.0f;
    rs = rs * 1664525u + 1013904223u; float nz = ((rs >> 9) & 0xFFFF) / 32768.0f - 1.0f; lp += (nz - lp) * 0.10f;
    float eng = (ph * 2.0f - 1.0f) * 0.30f + (ph2 * 2.0f - 1.0f) * 0.14f + sinf(6.2832f * ph) * 0.3f;
    float v = eng * g * 0.5f + lp * wd * 0.45f + nz * nz2 * 0.10f * g + nz * burst * 0.7f;
    if (pa > 0.001f) { v += sinf(pp) * pa * 0.25f; pp += 6.2832f * pf / 22050.0f; pa *= 0.9996f; }
    burst *= 0.99985f; if (burst < 0.001f) burst = 0;
    if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f; o[i] = (short)(v * 20000.0f);
  }
}
static void audioInit() {
  if (audioOk) return; SDL_InitSubSystem(SDL_INIT_AUDIO);
  SDL_AudioSpec want; memset(&want, 0, sizeof want); want.freq = 22050; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audioCb;
  if (SDL_OpenAudio(&want, NULL) == 0) { audioOk = true; SDL_PauseAudio(0); }
}
static void soundUpdate() {
  if (!audioOk) return;
  if (!soundOn || gs == GS_PAUSE || gs == GS_OVER) { sndG = 0; sndWind = 0; sndN = 0; return; }
  bool live = gs == GS_PLAY || gs == GS_CRASH || gs == GS_TITLE;
  sndF = 48.0f + spd * 2.4f + (nitroOn ? 22.0f : 0.0f); sndG = gs == GS_PLAY ? 0.9f : (gs == GS_CRASH ? 0.25f : 0.3f);
  sndWind = live ? clampf(spd / 120.0f, 0, 1) : 0.0f; sndN = nitroOn ? 1.0f : 0.0f;
}

// ---------- ввод пальцами: слева руль (плавающий стик), справа нитро ----------
struct Finger { long long id; int role, x, y, sx, sy; bool on; };   // роль: 0 руль, 2 кнопка меню, 3 нитро
static Finger fing[6];
static int JR() { return imin(IW, IH) / 6; }
static void nitroRect(int &cx, int &cy, int &r) { r = (int)(JR() * 0.8f); cx = IW - r - fs * 8; cy = IH - r - fs * 8; }
static void fingerDown(long long id, int x, int y) {
  for (int i = 0; i < 6; i++) if (!fing[i].on) {
    Finger &f = fing[i]; f.on = true; f.id = id; f.x = f.sx = x; f.y = f.sy = y;
    if (gs == GS_PLAY) {
      if (x >= IW / 2 - fs * 12 && x < IW / 2 + fs * 12 && y < fs * 32) { gs = GS_PAUSE; actHeld = false; joyX = joyY = 0; for (int k = 0; k < 6; k++) fing[k].on = false; return; }   // кнопка паузы
      if (x < IW / 2) f.role = 0; else { f.role = 3; actHeld = true; } }
    else { f.role = 2; P.down = true; P.pressed = true; P.x = P.sx = x; P.y = P.sy = y; }
    return;
  }
}
static void fingerMove(long long id, int x, int y) {
  for (int i = 0; i < 6; i++) if (fing[i].on && fing[i].id == id) { Finger &f = fing[i]; if (f.role == 2) { P.x = x; P.y = y; } f.x = x; f.y = y; }
}
static void fingerUp(long long id, int x, int y) {
  for (int i = 0; i < 6; i++) if (fing[i].on && fing[i].id == id) { Finger &f = fing[i]; f.on = false;
    if (f.role == 2) { P.down = false; P.cx = x; P.cy = y; P.clicked = true; }
    if (f.role == 3) { bool any = false; for (int k = 0; k < 6; k++) if (fing[k].on && fing[k].role == 3) any = true; actHeld = any; }
    if (f.role == 0) { joyX = joyY = 0; } }
}
static void updateJoy() {
  joyX = joyY = 0;
  for (int i = 0; i < 6; i++) if (fing[i].on && fing[i].role == 0) {
    float R = JR() * 1.1f, dx = (fing[i].x - fing[i].sx) / R, dy = (fing[i].y - fing[i].sy) / R; float l = sqrtf(dx * dx + dy * dy); if (l > 1) { dx /= l; dy /= l; }
    joyX = dx; joyY = dy; }
}

// ---------- игровая логика ----------
static void updateParts(float dt, float relSpd) {
  for (size_t i = 0; i < parts.size();) { Part &q = parts[i]; q.v.y -= 14.0f * dt; q.p = q.p + q.v * dt; q.p.z -= relSpd * dt;
    if (q.p.y < 0.05f) { q.p.y = 0.05f; q.v.y *= -0.3f; q.v.x *= 0.7f; } q.life -= dt; if (q.life <= 0) parts.erase(parts.begin() + i); else i++; }
}
static void updatePops(float dt) { for (size_t i = 0; i < pops.size();) { pops[i].t -= dt; if (pops[i].t <= 0) pops.erase(pops.begin() + i); else i++; } }
static void updatePlay(float dt) {
  runT += dt; float steer = joyX; if (fabsf(steer) < 0.1f) steer = 0; else steer = (steer > 0 ? steer - 0.1f : steer + 0.1f) / 0.9f;
  nitroOn = actHeld && nitro > 0.02f;
  float target = cruiseSpeed() + (nitroOn ? 24.0f : 0.0f), spN = clampf(spd / 105.0f, 0, 1);
  if (joyY > 0.35f) spd -= 40.0f * dt; else if (spd < target) spd += (nitroOn ? 30.0f : 15.0f) * dt; else spd -= 8.0f * dt;
  spd = clampf(spd, 18.0f, 140.0f);
  if (nitroOn) nitro = fmaxv(0.0f, nitro - 0.28f * dt); else nitro = fminv(1.0f, nitro + 0.05f * dt);
  float tvx = steer * (12.5f - 4.5f * spN); carVX += (tvx - carVX) * fminv(1.0f, 10.0f * dt); carX += carVX * dt;
  float lim = ROAD_HALF - PW * 0.5f - 0.05f;
  if (fabsf(carX) > lim) { float sd = carX > 0 ? 1.0f : -1.0f; carX = sd * lim; carVX *= -0.1f; spd -= 25.0f * dt * 3.0f; shake = fmaxv(shake, 0.4f); sparks(v3(carX + sd * 0.9f, 0.4f, 0), 6, v3(0, 1.5f, 4.0f), 3.0f); }
  carYaw = clampf(carVX * 0.022f, -0.2f, 0.2f);
  trip += spd * dt; score += spd * dt * mult * 1.2f * dScore(); topKmh = imax(topKmh, (int)(spd * 3.6f));
  multT -= dt; if (multT <= 0 && mult > 1.0f) mult = fmaxv(1.0f, mult - dt * 1.2f);
  shake = fmaxv(0.0f, shake - dt * 1.5f); flash = fmaxv(0.0f, flash - dt * 2.0f);
  updateTraffic(dt, false);
  for (size_t i = 0; i < traffic.size(); i++) { Car &c = traffic[i];
    float halfW = (c.wid + PW) * 0.5f, halfL = (c.len + PL) * 0.5f, zmin = fminv(c.pz, c.z), zmax = fmaxv(c.pz, c.z), dx = c.x - carX;
    if (zmax > -halfL && zmin < halfL && fabsf(dx) < halfW - 0.03f) { crashNow(v3(carX + dx * 0.5f, 0.8f, 0)); return; }
    if (c.pz > 0 && c.z <= 0) {                                           // обогнали машину: считаем, как близко
      float gap = fabsf(dx) - halfW; passes++;
      if (gap < 1.9f) { const char *lbl; int base; float add = 0.5f;
        if (gap < 0.45f) { lbl = "БРИТВА"; base = 1000; add = 1.0f; } else if (gap < 1.0f) { lbl = "ВПРИТЫК"; base = 500; } else { lbl = "БЛИЗКО"; base = 200; }
        if (c.type == 2 || c.type == 5) { base = base * 3 / 2; }
        int side = dx > 0 ? 1 : -1; char t[40]; snprintf(t, sizeof t, "%s%s", lbl, c.type == 2 ? " ФУРА" : (c.type == 5 ? " АВТОБУС" : ""));
        int pts = (int)(base * mult * dScore()); u32 col = gap < 0.45f ? RGB(255, 90, 70) : (gap < 1.0f ? RGB(255, 200, 60) : RGB(140, 230, 255));
        if (runT - lastPassT < 0.5f && side != lastPassSide) { int bonus = (int)(1500 * mult * dScore()); pts += bonus; snprintf(t, sizeof t, "В ИГОЛЬНОЕ УШКО"); col = RGB(255, 80, 220); add += 1.0f; }
        score += pts; addPop(t, pts, col); mult = fminv(10.0f, mult + add); multT = 4.5f; maxMult = fmaxv(maxMult, mult); nitro = fminv(1.0f, nitro + 0.08f); closeCalls++; shake = fmaxv(shake, 0.25f);
        lastPassT = runT; lastPassSide = side; sndPingF = gap < 0.45f ? 1320.0f : (gap < 1.0f ? 990.0f : 740.0f); sndPingA = 1.0f; }
    } }
  updateParts(dt, spd); updatePops(dt);
}
static void updateCrash(float dt) {
  crashT += dt; spd *= expf(-2.4f * dt); trip += spd * dt; crashSpin += 6.0f * dt * fmaxv(0.0f, 1.2f - crashT); crashLift = fminv(1.4f, crashT * 3.0f) * fmaxv(0.0f, 1.0f - crashT * 0.4f);
  carX += carVX * dt; shake = fmaxv(0.0f, shake - dt * 0.8f); flash = fmaxv(0.0f, flash - dt * 1.4f);
  for (size_t i = 0; i < traffic.size(); i++) { traffic[i].pz = traffic[i].z; traffic[i].z += (traffic[i].v - spd) * dt; }
  updateParts(dt, spd); updatePops(dt);
  if (crashT > 1.8f) gs = GS_OVER;
}

// ---------- интерфейс ----------
static void drawHud() {
  char b[96], m[48];
  fillRectA(0, 0, IW, fs * 33, RGB(8, 8, 14), 120);
  drawTextSh(fs * 4, fs * 2, "ОЧКИ", fs, RGB(180, 200, 255), 0);
  fmtNum(m, sizeof m, bestScore); snprintf(b, sizeof b, "РЕКОРД %s", m); drawTextSh(fs * 4 + textW("ОЧКИ", fs) + fs * 8, fs * 2, b, fs, RGB(255, 220, 120), 0);
  fmtNum(m, sizeof m, (long long)score); drawTextSh(fs * 4, fs * 11, m, fs * 3, RGB(255, 255, 255), 0);
  // множитель
  snprintf(b, sizeof b, "X%.1f", mult); u32 mc = mult >= 7 ? RGB(255, 90, 70) : (mult >= 3.5f ? RGB(255, 210, 60) : RGB(200, 240, 120));
  drawTextSh(IW - fs * 4, fs * 3, b, fs * 4, mc, 2);
  if (multT > 0 && mult > 1) { int bw = fs * 50; fillRect(IW - fs * 4 - bw, fs * 26, bw, fs * 3, RGB(30, 30, 40)); fillRect(IW - fs * 4 - bw, fs * 26, (int)(bw * clampf(multT / 4.5f, 0, 1)), fs * 3, mc); }
  snprintf(b, sizeof b, "%.1f КМ", trip / 1000.0f); drawTextSh(IW / 2, fs * 4, b, fs * 2, RGB(200, 210, 230), 1);
  fillRectA(IW / 2 - fs * 11, fs * 18, fs * 22, fs * 14, RGB(20, 24, 36), 170); fillRect(IW / 2 - fs * 5, fs * 21, fs * 3, fs * 8, RGB(230, 235, 245)); fillRect(IW / 2 + fs * 2, fs * 21, fs * 3, fs * 8, RGB(230, 235, 245));   // кнопка паузы
  // всплывающие сообщения
  for (size_t i = 0; i < pops.size(); i++) { Pop &p = pops[i]; float a = clampf(p.t / 0.5f, 0, 1); int y = IH / 4 + (int)i * fs * 13 - (int)((1.8f - p.t) * fs * 5); u32 c = mixc(RGB(0, 0, 0), p.col, a);
    char t[64]; snprintf(t, sizeof t, "%s +%d", p.txt, p.pts); drawTextSh(IW / 2, y, t, i == 0 ? fs * 2 : fs, c, 1); }
  // скорость
  int kmh = (int)(spd * 3.6f); snprintf(b, sizeof b, "%d", kmh); drawTextSh(IW / 2, IH - fs * 38, b, fs * 4, nitroOn ? RGB(120, 200, 255) : RGB(255, 255, 255), 1);
  drawTextSh(IW / 2 + textW(b, fs * 4) / 2 + fs * 4, IH - fs * 18, "КМ/Ч", fs, RGB(170, 180, 200), 0);
  int nw = fs * 90, nx = IW / 2 - nw / 2, ny = IH - fs * 5; fillRect(nx, ny, nw, fs * 2, RGB(30, 34, 46)); fillRect(nx, ny, (int)(nw * nitro), fs * 2, nitroOn ? RGB(120, 210, 255) : RGB(60, 150, 230));
  // управление
  int cx, cy, r; nitroRect(cx, cy, r);
  fillCircle(cx, cy, r, nitroOn ? RGB(110, 190, 255) : RGB(60, 120, 200), nitroOn ? 120 : 55); drawTextSh(cx, cy - fs * 3, "НИТРО", fs, RGB(255, 255, 255), 1);
  for (int i = 0; i < 6; i++) if (fing[i].on && fing[i].role == 0) { fillCircle(fing[i].sx, fing[i].sy, JR(), RGB(255, 255, 255), 28); int kx = fing[i].sx + (int)(joyX * JR()), ky = fing[i].sy + (int)(joyY * JR()); fillCircle(kx, ky, JR() / 2, RGB(255, 255, 255), 90); }
  if (runT < 6.0f) { drawTextSh(IW / 4, IH * 2 / 3, "РУЛЬ", fs * 2, RGB(255, 255, 255), 1); drawTextSh(IW / 4, IH * 2 / 3 + fs * 15, "ВНИЗ - ТОРМОЗ", fs, RGB(200, 210, 230), 1); }
  if (flash > 0) fillRectA(0, 0, IW, IH, RGB(255, 60, 40), (int)(flash * 140));
}
static void drawFx() {                                                       // дождь и линии скорости
  float spN = clampf(spd / 105.0f, 0, 1);
  int n = gGL ? 26 : 46; for (int i = 0; i < n; i++) { unsigned h = hsh(i * 77u + 3u); float t = fmodf(gTime * (2.2f + (h & 7) * 0.12f) + (h >> 8 & 255) / 255.0f, 1.0f);
    int x = (int)((h >> 16 & 1023) / 1023.0f * IW) - (int)(t * fs * 14), y = (int)(t * IH); drawLine(x, y, x - fs * 2 - (int)(spN * fs * 3), y + fs * 7, RGB(120, 140, 170), 1); }
  if (spN > 0.45f) { int m2 = gGL ? 14 : 24; for (int i = 0; i < m2; i++) { unsigned h = hsh(i * 131u + (unsigned)(gTime * 18.0f) * 7u); float a = (h & 1023) / 1023.0f * 6.2832f, r0 = IH * (0.32f + (h >> 10 & 255) / 255.0f * 0.2f), r1 = r0 + IH * 0.25f * spN;
      int x0 = (int)(IW * 0.5f + cosf(a) * r0 * 1.5f), y0 = (int)(vcy + sinf(a) * r0), x1 = (int)(IW * 0.5f + cosf(a) * r1 * 1.5f), y1 = (int)(vcy + sinf(a) * r1); drawLine(x0, y0, x1, y1, nitroOn ? RGB(110, 170, 230) : RGB(90, 100, 130), 1); } }
  if (nitroOn) { fillRectA(0, 0, IW, fs * 6, RGB(80, 160, 255), 60); fillRectA(0, IH - fs * 6, IW, fs * 6, RGB(80, 160, 255), 60); }
}
static float fpsShow = 0, fpsAcc = 0; static int fpsN = 0;
static void drawFps() {
  if (!fpsOn || fpsShow <= 0) return; char b[32]; snprintf(b, sizeof b, "FPS %d", (int)(fpsShow + 0.5f));
  int sc = imax(1, fs - 1), w = textW(b, sc) + sc * 4; fillRectA(IW - w - fs, IH - sc * 12, w, sc * 10, RGB(0, 0, 0), 120);
  drawText(IW - w / 2 - fs, IH - sc * 10, b, sc, fpsShow >= 25 ? RGB(140, 240, 140) : fpsShow >= 15 ? RGB(255, 220, 110) : RGB(255, 120, 110), 1);
}

// ---------- кадры ----------
static void framePause() {
  drawWorld(false); fillRectA(0, 0, IW, IH, RGB(0, 0, 10), 150);
  int pw = imin(IW - fs * 8, fs * 160), ph = IH / 2, px0 = IW / 2 - pw / 2, py0 = IH / 4;
  fillRect(px0, py0, pw, ph, RGB(18, 20, 28)); fillRect(px0, py0, pw, fs * 3, RGB(90, 160, 240));
  drawTextSh(IW / 2, py0 + fs * 10, "ПАУЗА", fs * 5, RGB(255, 255, 255), 1);
  int bw = pw - fs * 16, bh = fs * 17, bx = px0 + fs * 8;
  if (button(bx, py0 + ph / 2 - fs * 2, bw, bh, "ПРОДОЛЖИТЬ", true, RGB(60, 150, 70))) { gs = GS_PLAY; actHeld = false; }
  if (button(bx, py0 + ph / 2 + bh + fs * 4, bw, bh, "В МЕНЮ", true, RGB(90, 90, 110))) gs = GS_TITLE;
}
static void framePlay(float dt) { updateJoy(); updatePlay(dt); drawWorld(false); drawFx(); drawHud(); }
static void frameCrash(float dt) { updateCrash(dt); drawWorld(false); drawFx(); drawHud(); }
static void demoStep(float dt) {
  spd = 58.0f; trip += spd * dt; carX = laneX(1) + sinf(gTime * 0.5f) * 0.8f; carVX = cosf(gTime * 0.5f) * 0.4f; carYaw = carVX * 0.03f; shake = 0; nitroOn = false; joyY = 0;
  updateTraffic(dt, true); for (size_t i = 0; i < traffic.size(); i++) traffic[i].pz = traffic[i].z;
}
static void frameTitle(float dt) {
  demoStep(dt); drawWorld(true); drawFx(); fillRectA(0, 0, IW, IH, RGB(4, 4, 12), 105);
  drawTextSh(IW / 2, IH / 10, "ХАЙВЕЙ", fs * 5, RGB(255, 220, 90), 1); drawTextSh(IW / 2, IH / 10 + fs * 40, "РЕЙСЕР", fs * 5, RGB(255, 90, 70), 1);
  drawText(IW / 2, IH / 10 + fs * 84, "НОЧНАЯ ТРАССА. ОБГОНЯЙ ВПРИТЫК, КОПИ МНОЖИТЕЛЬ", fs, RGB(220, 225, 240), 1);
  int bw = IW / 3, bh = IH / 8, y = IH * 3 / 5;
  if (button(IW / 2 - bw / 2, y, bw, bh, "ЕХАТЬ", true, RGB(60, 150, 70))) startRun();
  { int y2 = y + bh + fs * 4, sw = IW / 3, sh = fs * 14; char t[64];
    snprintf(t, sizeof t, "СЛОЖНОСТЬ: %s", DIFFN[diff]); if (button(IW / 2 - sw - fs * 2, y2, sw, sh, t, true, diff == 0 ? RGB(60, 130, 80) : (diff == 1 ? RGB(150, 120, 40) : RGB(170, 50, 50)))) { diff = (diff + 1) % 3; saveBest(); }
    snprintf(t, sizeof t, "ЗВУК: %s", soundOn ? "ВКЛ" : "ВЫКЛ"); if (button(IW / 2 + fs * 2, y2, sw, sh, t, true, soundOn ? RGB(60, 120, 170) : RGB(90, 90, 100))) { soundOn = !soundOn; if (soundOn) audioInit(); saveBest(); }
    char b[64], m[32]; fmtNum(m, sizeof m, bestScore); snprintf(b, sizeof b, "РЕКОРД: %s", m); drawTextSh(IW / 2, y2 + sh + fs * 5, b, fs * 2, RGB(255, 220, 120), 1); }
  { char gb[96]; snprintf(gb, sizeof gb, gGL ? "ВИДЕОКАРТА: ВКЛ" : (gWantGL ? "ВИДЕОКАРТА: НЕ ЗАПУСТИЛАСЬ" : "ВИДЕОКАРТА: ВЫКЛ"));
    if (button(fs * 4, IH - fs * 16, IW / 3, fs * 12, gb, true, gGL ? RGB(60, 120, 170) : RGB(90, 90, 100))) {
      gWantGL = !gWantGL; FILE *g = fopen(pth("racer_gl.cfg"), "w"); if (g) { fprintf(g, "%d\n", gWantGL ? 1 : 0); fclose(g); }
      say(gWantGL ? "ВКЛЮЧЕНО: ПЕРЕЗАПУСТИ ИГРУ" : "ВЫКЛЮЧЕНО: ПЕРЕЗАПУСТИ ИГРУ"); } }
  if (msgT > 0) { msgT -= dt; drawText(IW / 2, IH - fs * 24, msg, fs, RGB(255, 220, 140), 1); }
}
static void frameOver() {
  drawWorld(false); fillRectA(0, 0, IW, IH, RGB(10, 0, 0), 150);
  int pw = imin(IW - fs * 8, fs * 250), ph = IH * 3 / 4, px0 = IW / 2 - pw / 2, py0 = IH / 8;
  fillRect(px0, py0, pw, ph, RGB(18, 20, 28)); fillRect(px0, py0, pw, fs * 3, RGB(230, 60, 50));
  char b[96], m[32];
  drawTextSh(IW / 2, py0 + fs * 8, "АВАРИЯ", fs * 5, RGB(255, 90, 70), 1);
  fmtNum(m, sizeof m, (long long)score); drawTextSh(IW / 2, py0 + fs * 46, m, fs * 5, RGB(255, 255, 255), 1);
  if (newBest) drawTextSh(IW / 2, py0 + fs * 86, "НОВЫЙ РЕКОРД!", fs * 2, RGB(255, 220, 90), 1);
  else { fmtNum(m, sizeof m, bestScore); snprintf(b, sizeof b, "РЕКОРД %s", m); drawText(IW / 2, py0 + fs * 88, b, fs, RGB(200, 210, 230), 1); }
  snprintf(b, sizeof b, "ДИСТАНЦИЯ %.2f КМ  МАКС. %d КМ/Ч", trip / 1000.0f, topKmh); drawText(IW / 2, py0 + fs * 108, b, fs, RGB(200, 210, 230), 1);
  snprintf(b, sizeof b, "ОБГОНОВ %d  ОПАСНЫХ %d  МНОЖИТЕЛЬ X%.1f", passes, closeCalls, maxMult); drawText(IW / 2, py0 + fs * 120, b, fs, RGB(200, 210, 230), 1);
  int bw = pw / 2 - fs * 6, bh = fs * 17, by = py0 + ph - bh - fs * 8;
  if (button(px0 + fs * 4, by, bw, bh, "ЕХАТЬ СНОВА", true, RGB(60, 150, 70))) startRun();
  if (button(px0 + pw - bw - fs * 4, by, bw, bh, "В МЕНЮ", true, RGB(90, 90, 110))) gs = GS_TITLE;
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
#if defined(__ANDROID__) && defined(SDL_MAJOR_VERSION) && SDL_MAJOR_VERSION >= 2
  { const char *ip = SDL_AndroidGetInternalStoragePath(); if (ip && *ip) snprintf(gBase, sizeof gBase, "%s/", ip); }
#endif
  { FILE *g = fopen(pth("racer_gl.cfg"), "r"); if (g) { int v = 1; if (fscanf(g, "%d", &v) == 1) gWantGL = v != 0; fclose(g); } }
  if (!gfxInit()) return 1;
  rotMode = SH > SW; LW = rotMode ? SH : SW; LH = rotMode ? SW : SH;
  computeSize(); if (!allocBuffers()) return 1;
  srand((unsigned)time(NULL)); hasBest = loadBest(); if (soundOn) audioInit();
  memset(&P, 0, sizeof P); memset(fing, 0, sizeof fing);
  bool running = true; Uint32 last = SDL_GetTicks();
  while (running) {
    Uint32 now = SDL_GetTicks();
    float dt = clampf((now - last) / 1000.0f, 0.001f, 0.05f); last = now; gTime += dt;
    if (gfxCheckResize()) { rotMode = SH > SW; LW = rotMode ? SH : SW; LH = rotMode ? SW : SH; computeSize(); if (!allocBuffers()) break; }
    P.pressed = false; P.clicked = false; P.dx = P.dy = 0;
#if SDL_MODE == 2
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      int mx, my;
      switch (ev.type) {
        case SDL_QUIT: running = false; break;
        case SDL_KEYDOWN: if (ev.key.keysym.sym == SDLK_ESCAPE) { if (gs == GS_TITLE) running = false; else if (gs == GS_PLAY) gs = GS_PAUSE; else gs = GS_TITLE; } break;
        case SDL_FINGERDOWN: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerDown((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_FINGERMOTION: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerMove((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_FINGERUP: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerUp((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_MOUSEBUTTONDOWN: if (ev.button.which != SDL_TOUCH_MOUSEID) { mapPt(ev.button.x, ev.button.y, mx, my); fingerDown(-1, mx, my); } break;
        case SDL_MOUSEMOTION: if (ev.motion.which != SDL_TOUCH_MOUSEID) { mapPt(ev.motion.x, ev.motion.y, mx, my); fingerMove(-1, mx, my); } break;
        case SDL_MOUSEBUTTONUP: if (ev.button.which != SDL_TOUCH_MOUSEID) { mapPt(ev.button.x, ev.button.y, mx, my); fingerUp(-1, mx, my); } break;
        default: break;
      }
    }
#else
    Ev e;
    while (pollEv(e)) {
      if (e.type == 0) running = false;
      else if (e.type == 4) { if (gs == GS_TITLE) running = false; else if (gs == GS_PLAY) gs = GS_PAUSE; else gs = GS_TITLE; }
      else if (e.type != 5) { int mx, my; mapPt(e.x, e.y, mx, my); if (e.type == 1) fingerDown(-1, mx, my); else if (e.type == 3) fingerMove(-1, mx, my); else fingerUp(-1, mx, my); }
    }
#endif
    if (gs == GS_TITLE) frameTitle(dt); else if (gs == GS_PLAY) framePlay(dt); else if (gs == GS_CRASH) frameCrash(dt); else if (gs == GS_PAUSE) framePause(); else frameOver();
    soundUpdate(); drawFps();
    gfxPresent();
    Uint32 spent2 = SDL_GetTicks() - now; { fpsAcc += dt; fpsN++; if (fpsAcc >= 2.0f) { fpsShow = fpsN / fpsAcc; fpsAcc = 0; fpsN = 0; } }
    // авто-скорость: если кадр рисуется дольше 33 мс, уменьшаем разрешение картинки
    { static float accT = 0, accW = 0; static int accN = 0; accT += dt; accW += (float)spent2; accN++;
      if (accT > 2.0f) { float avg = accW / accN; if (autoPerf && !gGL && avg > 33.0f && quality - gResDrop > 0) { gResDrop++; computeSize(); if (!allocBuffers()) break; } accT = accW = 0; accN = 0; } }
    if (spent2 < 16) SDL_Delay(16 - spent2);
  }
  if (audioOk) SDL_CloseAudio();
  gfxQuit();
  return 0;
}
