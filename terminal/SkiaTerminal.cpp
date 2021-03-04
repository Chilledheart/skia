/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 */

#include "SDL.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkSurface.h"
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/utils/SkRandom.h"

#include "include/gpu/gl/GrGLInterface.h"
#include "src/gpu/gl/GrGLUtil.h"

#if defined(SK_BUILD_FOR_ANDROID)
#include <GLES/gl.h>
#elif defined(SK_BUILD_FOR_UNIX)
#include <GL/gl.h>
#elif defined(SK_BUILD_FOR_MAC)
#include <OpenGL/gl.h>
#elif defined(SK_BUILD_FOR_IOS)
#include <OpenGLES/ES2/gl.h>
#endif

#ifndef SK_BUILD_FOR_WIN
#include <fcntl.h>
#include <libtsm.h>
#include <sys/ioctl.h>
#include <sys/ttycom.h>
#include <sys/ttydefaults.h>
#include <unistd.h>
#include <termios.h>
extern char **environ;
#if defined(SK_BUILD_FOR_MAC)
#include <util.h>
#else
#include <pty.h>
#endif
#endif

#define DEFAULT_ROW 80
#define DEFAULT_COL 24

/*
 * This application is a simple example of how to combine SDL and Skia it demonstrates:
 *   how to setup gpu rendering to the main window
 *   how to perform cpu-side rendering and draw the result to the gpu-backed screen
 *   draw simple primitives (rectangles)
 *   draw more complex primitives (star)
 */

struct ApplicationState {
    ApplicationState() : fQuit(false), fRedraw(false), fFontSize(12.0), fFontAdvanceWidth(), fFontSpacing() {}
    // Storage for the user created rectangles. The last one may still be being edited.
    SkTArray<SkRect> fRects;
    bool fQuit;
    bool fRedraw;
    float fFontSize;
    float fFontAdvanceWidth;
    float fFontSpacing;
};

static void handle_error() {
    const char* error = SDL_GetError();
    SkDebugf("SDL Error: %s\n", error);
    SDL_ClearError();
}

static SkFont *gFont;

static void handle_size_change(ApplicationState* state, SDL_Window* window, SkCanvas* canvas,
                               int fd, struct tsm_screen* screen, struct tsm_vte* vte) {
    int dw, dh;
    struct winsize ws;

    state->fFontAdvanceWidth = gFont->measureText("X", 1U, SkTextEncoding::kUTF8, NULL);
    state->fFontSpacing = std::min(1.0f, gFont->getSpacing());

    SDL_GL_GetDrawableSize(window, &dw, &dh);

    ws.ws_row = (float)(dw) / state->fFontAdvanceWidth;
    ws.ws_col = (float)(dh + state->fFontSpacing) / (state->fFontSize + state->fFontSpacing) - 1;
    ws.ws_xpixel = dw;
    ws.ws_ypixel = dh;
    SkDebugf("resize row %d col %d\n", ws.ws_row, ws.ws_col);
    tsm_screen_resize(screen, ws.ws_row, ws.ws_col);
    ioctl(fd, TIOCSWINSZ, &ws);
    state->fRedraw = true;
}

static void handle_events(ApplicationState* state, SDL_Window* window, SkCanvas* canvas,
                          int fd, struct tsm_screen* screen, struct tsm_vte* vte) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_MOUSEMOTION:
                if (event.motion.state == SDL_PRESSED) {
                    SkRect& rect = state->fRects.back();
                    rect.fRight = event.motion.x;
                    rect.fBottom = event.motion.y;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.state == SDL_PRESSED) {
                    state->fRects.push_back() = SkRect::MakeLTRB(SkIntToScalar(event.button.x),
                                                                 SkIntToScalar(event.button.y),
                                                                 SkIntToScalar(event.button.x),
                                                                 SkIntToScalar(event.button.y));
                }
                break;
            case SDL_KEYDOWN: {
                SDL_Keycode key = event.key.keysym.sym;
                SDL_Scancode scancode = SDL_GetScancodeFromKey(key);
                uint16_t modifier = event.key.keysym.mod;
                SkDebugf("%d %d\n", key, scancode);

                /* SHIFT + UP/DOWN/PAGEUP/PAGEDOWN */
                if (modifier & (KMOD_LSHIFT | KMOD_RSHIFT)) {
                    switch (key) {
                        case SDLK_UP:
                            tsm_screen_sb_up(screen, 1);
                            state->fRedraw = true;
                            return;
                        case SDLK_DOWN:
                            tsm_screen_sb_down(screen, 1);
                            state->fRedraw = true;
                            return;
                        case SDLK_PAGEUP:
                            tsm_screen_sb_page_up(screen, 1);
                            state->fRedraw = true;
                            return;
                        case SDLK_PAGEDOWN:
                            tsm_screen_sb_page_down(screen, 1);
                            state->fRedraw = true;
                            return;
                    }
                }

                /*  CTRL+SHIFT +/-  Zoom */
                if (modifier & KMOD_SHIFT && modifier & KMOD_CTRL &&
                    !(modifier & KMOD_ALT)) {
                    if (key == '=' /*SDLK_PLUS*/ && state->fFontSize + 1.0f <= 32.0) {
                        state->fFontSize += 1.0;
                        gFont->setSize(state->fFontSize);
                        handle_size_change(state, window, canvas, fd, screen, vte);
                        return;
                    } else if (key == SDLK_MINUS && state->fFontSize - 1.0f >= 8.0) {
                        state->fFontSize -= 1.0;
                        gFont->setSize(state->fFontSize);
                        handle_size_change(state, window, canvas, fd, screen, vte);
                        return;
                    }
                }

                if (modifier & KMOD_SHIFT &&
                    !(modifier & KMOD_CTRL) && !(modifier & KMOD_ALT)) {
                    // TBD read keyboard configuration
                    char map[] = {
                            ')', '!', '@', '#', '$', '%', '^', '&', '*', '(',
                    };
                    if ('0' <= key && key <= '9') {
                        key = map[key - '0'];
                    } else if ('a' <= key && key <= 'z') {
                        key -= 'a' - 'A';
                    } else if (key == '`') {
                        key = '~';
                    } else if (key == '-') {
                        key = '_';
                    } else if (key == '=') {
                        key = '+';
                    } else if (key == '[') {
                        key = '{';
                    } else if (key == ']') {
                        key = '}';
                    } else if (key == '\\') {
                        key = '|';
                    } else if (key == ';') {
                        key = ':';
                    } else if (key == '\'') {
                        key = '"';
                    } else if (key == ',') {
                        key = '<';
                    } else if (key == '.') {
                        key = '>';
                    } else if (key == '/') {
                        key = '?';
                    }
                }

                // following sys/ttydefaulys.h
                if (!(modifier & KMOD_SHIFT) &&
                    modifier & KMOD_CTRL && !(modifier & KMOD_ALT)
                    && SDLK_a <= key && key <= SDLK_z) {
                    key = CTRL(key);
                }

                if (!(modifier & KMOD_SHIFT) &&
                    modifier & KMOD_CTRL && !(modifier & KMOD_ALT)
                    && key == SDLK_BACKSLASH) {
                    key = CQUIT;
                }

                if (!(modifier & KMOD_SHIFT) &&
                    modifier & KMOD_CTRL && !(modifier & KMOD_ALT)
                    && key == SDLK_DELETE) {
                    key = SDLK_DELETE;
                }

#if defined(SK_BUILD_FOR_MAC)
                if (modifier & KMOD_GUI && key == SDLK_c) {
                    // TBD copy from selection
                }
                if (modifier & KMOD_GUI && key == SDLK_v) {
                    // TBD paste to vte
                }
#endif

                if (tsm_vte_handle_keyboard(vte, key, 0, 0, key)) {
                  tsm_screen_sb_reset(screen);
                }
                state->fRedraw = true;
                break;
            }
#if 0
            case SDL_WINDOWEVENT: {
                uint8_t windowevent = event.window.event;
                if (windowevent == SDL_WINDOWEVENT_RESIZED) {
                    // Use SDL_GL_GetDrawableSize to measure the layout change
                    uint32_t dw = event.window.data1;
                    uint32_t dh = event.window.data2;
                    handle_size_change(state, window, canvas, fd, screen, vte);
                }
                break;
            }
#endif
            case SDL_QUIT:
                state->fQuit = true;
                break;
            default:
                break;
        }
    }
}

// Creates a star type shape using a SkPath
static SkPath create_star() {
    static const int kNumPoints = 5;
    SkPath concavePath;
    SkPoint points[kNumPoints] = {{0, SkIntToScalar(-50)}};
    SkMatrix rot;
    rot.setRotate(SkIntToScalar(360) / kNumPoints);
    for (int i = 1; i < kNumPoints; ++i) {
        rot.mapPoints(points + i, points + i - 1, 1);
    }
    concavePath.moveTo(points[0]);
    for (int i = 0; i < kNumPoints; ++i) {
        concavePath.lineTo(points[(2 * i) % kNumPoints]);
    }
    concavePath.setFillType(SkPathFillType::kEvenOdd);
    SkASSERT(!concavePath.isConvex());
    concavePath.close();
    return concavePath;
}

#define KMSG_LINE_MAX (1024 - 32)

static void log_tsm(void* data, const char* file, int line, const char* fn, const char* subs,
                    unsigned int sev, const char* format, va_list args) {
    char buffer[KMSG_LINE_MAX];
    int len = snprintf(buffer, KMSG_LINE_MAX, "<%i>sdl[%d]: %s: ", sev, getpid(), subs);
    if (len < 0) return;
    if (len < KMSG_LINE_MAX - 1) vsnprintf(buffer + len, KMSG_LINE_MAX - len, format, args);
    SkDebugf("%s\n", buffer);
}

static void term_write_cb(struct tsm_vte* vte, const char* u8, size_t len, void* data) {
    int* pFd = reinterpret_cast<int*>(data);
    write(*pFd, u8, len);
}

enum vte_color {
    COLOR_BLACK,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_YELLOW,
    COLOR_BLUE,
    COLOR_MAGENTA,
    COLOR_CYAN,
    COLOR_LIGHT_GREY,
    COLOR_DARK_GREY,
    COLOR_LIGHT_RED,
    COLOR_LIGHT_GREEN,
    COLOR_LIGHT_YELLOW,
    COLOR_LIGHT_BLUE,
    COLOR_LIGHT_MAGENTA,
    COLOR_LIGHT_CYAN,
    COLOR_WHITE,
    COLOR_FOREGROUND,
    COLOR_BACKGROUND,
    COLOR_NUM
};

static uint8_t color_palette[COLOR_NUM][3] = {
        [COLOR_BLACK] = {0, 0, 0},             /* black */
        [COLOR_RED] = {205, 0, 0},             /* red */
        [COLOR_GREEN] = {0, 205, 0},           /* green */
        [COLOR_YELLOW] = {205, 205, 0},        /* yellow */
        [COLOR_BLUE] = {0, 0, 238},            /* blue */
        [COLOR_MAGENTA] = {205, 0, 205},       /* magenta */
        [COLOR_CYAN] = {0, 205, 205},          /* cyan */
        [COLOR_LIGHT_GREY] = {229, 229, 229},  /* light grey */
        [COLOR_DARK_GREY] = {127, 127, 127},   /* dark grey */
        [COLOR_LIGHT_RED] = {255, 0, 0},       /* light red */
        [COLOR_LIGHT_GREEN] = {0, 255, 0},     /* light green */
        [COLOR_LIGHT_YELLOW] = {255, 255, 0},  /* light yellow */
        [COLOR_LIGHT_BLUE] = {92, 92, 255},    /* light blue */
        [COLOR_LIGHT_MAGENTA] = {255, 0, 255}, /* light magenta */
        [COLOR_LIGHT_CYAN] = {0, 255, 255},    /* light cyan */
        [COLOR_WHITE] = {255, 255, 255},       /* white */

        [COLOR_FOREGROUND] = {229, 229, 229}, /* light grey */
        [COLOR_BACKGROUND] = {0, 0, 0},       /* black */
};

static uint8_t color_palette_solarized[COLOR_NUM][3] = {
        [COLOR_BLACK] = {7, 54, 66},             /* black */
        [COLOR_RED] = {220, 50, 47},             /* red */
        [COLOR_GREEN] = {133, 153, 0},           /* green */
        [COLOR_YELLOW] = {181, 137, 0},          /* yellow */
        [COLOR_BLUE] = {38, 139, 210},           /* blue */
        [COLOR_MAGENTA] = {211, 54, 130},        /* magenta */
        [COLOR_CYAN] = {42, 161, 152},           /* cyan */
        [COLOR_LIGHT_GREY] = {238, 232, 213},    /* light grey */
        [COLOR_DARK_GREY] = {0, 43, 54},         /* dark grey */
        [COLOR_LIGHT_RED] = {203, 75, 22},       /* light red */
        [COLOR_LIGHT_GREEN] = {88, 110, 117},    /* light green */
        [COLOR_LIGHT_YELLOW] = {101, 123, 131},  /* light yellow */
        [COLOR_LIGHT_BLUE] = {131, 148, 150},    /* light blue */
        [COLOR_LIGHT_MAGENTA] = {108, 113, 196}, /* light magenta */
        [COLOR_LIGHT_CYAN] = {147, 161, 161},    /* light cyan */
        [COLOR_WHITE] = {253, 246, 227},         /* white */

        [COLOR_FOREGROUND] = {238, 232, 213}, /* light grey */
        [COLOR_BACKGROUND] = {7, 54, 66},     /* black */
};

static uint8_t color_palette_solarized_black[COLOR_NUM][3] = {
        [COLOR_BLACK] = {0, 0, 0},               /* black */
        [COLOR_RED] = {220, 50, 47},             /* red */
        [COLOR_GREEN] = {133, 153, 0},           /* green */
        [COLOR_YELLOW] = {181, 137, 0},          /* yellow */
        [COLOR_BLUE] = {38, 139, 210},           /* blue */
        [COLOR_MAGENTA] = {211, 54, 130},        /* magenta */
        [COLOR_CYAN] = {42, 161, 152},           /* cyan */
        [COLOR_LIGHT_GREY] = {238, 232, 213},    /* light grey */
        [COLOR_DARK_GREY] = {0, 43, 54},         /* dark grey */
        [COLOR_LIGHT_RED] = {203, 75, 22},       /* light red */
        [COLOR_LIGHT_GREEN] = {88, 110, 117},    /* light green */
        [COLOR_LIGHT_YELLOW] = {101, 123, 131},  /* light yellow */
        [COLOR_LIGHT_BLUE] = {131, 148, 150},    /* light blue */
        [COLOR_LIGHT_MAGENTA] = {108, 113, 196}, /* light magenta */
        [COLOR_LIGHT_CYAN] = {147, 161, 161},    /* light cyan */
        [COLOR_WHITE] = {253, 246, 227},         /* white */

        [COLOR_FOREGROUND] = {238, 232, 213}, /* light grey */
        [COLOR_BACKGROUND] = {0, 0, 0},       /* black */
};

static uint8_t color_palette_solarized_white[COLOR_NUM][3] = {
        [COLOR_BLACK] = {7, 54, 66},             /* black */
        [COLOR_RED] = {220, 50, 47},             /* red */
        [COLOR_GREEN] = {133, 153, 0},           /* green */
        [COLOR_YELLOW] = {181, 137, 0},          /* yellow */
        [COLOR_BLUE] = {38, 139, 210},           /* blue */
        [COLOR_MAGENTA] = {211, 54, 130},        /* magenta */
        [COLOR_CYAN] = {42, 161, 152},           /* cyan */
        [COLOR_LIGHT_GREY] = {238, 232, 213},    /* light grey */
        [COLOR_DARK_GREY] = {0, 43, 54},         /* dark grey */
        [COLOR_LIGHT_RED] = {203, 75, 22},       /* light red */
        [COLOR_LIGHT_GREEN] = {88, 110, 117},    /* light green */
        [COLOR_LIGHT_YELLOW] = {101, 123, 131},  /* light yellow */
        [COLOR_LIGHT_BLUE] = {131, 148, 150},    /* light blue */
        [COLOR_LIGHT_MAGENTA] = {108, 113, 196}, /* light magenta */
        [COLOR_LIGHT_CYAN] = {147, 161, 161},    /* light cyan */
        [COLOR_WHITE] = {253, 246, 227},         /* white */

        [COLOR_FOREGROUND] = {7, 54, 66},     /* black */
        [COLOR_BACKGROUND] = {238, 232, 213}, /* light grey */
};

static SkColor term_get_fc_from_attr(const struct tsm_screen_attr* attr) {
    uint8_t fr = attr->fr, fg = attr->fg, fb = attr->fb;

    if (attr->fccode >= 0) {
        uint8_t code = attr->fccode;
        /* bold causes light colors */
        if (attr->bold && code < 8) code += 8;

        if (code >= COLOR_NUM) code = COLOR_FOREGROUND;

        fr = color_palette_solarized_white[code][0];
        fg = color_palette_solarized_white[code][1];
        fb = color_palette_solarized_white[code][2];
    }

    return SkColorSetARGB(0xFF, fr, fg, fb);
}

static SkColor term_get_bc_from_attr(const struct tsm_screen_attr* attr) {
    uint8_t br = attr->br, bg = attr->bg, bb = attr->bb;

    if (attr->bccode >= 0) {
        uint8_t code = attr->bccode;
        /* bold causes light colors */

        if (code >= COLOR_NUM) code = COLOR_BACKGROUND;

        br = color_palette_solarized_white[code][0];
        bg = color_palette_solarized_white[code][1];
        bb = color_palette_solarized_white[code][2];
    }

    return SkColorSetARGB(0xFF, br, bg, bb);
}

struct draw_ctx {
    SkCanvas *canvas;
    ApplicationState *state;
    SkPaint *paint;
    bool bcOnly;
};

static int draw_cb(struct tsm_screen* con,
                   uint32_t id,
                   const uint32_t* ch,
                   size_t len,
                   unsigned int width,
                   unsigned int posx,
                   unsigned int posy,
                   const struct tsm_screen_attr* attr,
                   tsm_age_t age,
                   void* data) {
    if (len == 0) {
        static uint32_t chs[] = { ' ', NULL };
        len = 1;
        ch = chs;
    }
    draw_ctx *ctx = reinterpret_cast<draw_ctx*>(data);
    SkCanvas* canvas = ctx->canvas;
    ApplicationState *state = ctx->state;
    SkPaint *paint = ctx->paint;
    bool bcOnly = ctx->bcOnly;

    SkString string;

    while (len--) {
        string.appendUnichar(*ch++);
    }

    float xoff = (posx) * state->fFontAdvanceWidth;
    float yoff = (posy + 1) * state->fFontSize + posy * state->fFontSpacing;

    SkColor bc = term_get_bc_from_attr(attr);
    SkColor fc = term_get_fc_from_attr(attr);

    if (attr->inverse) {
        std::swap(bc, fc);
    }

    if (bcOnly) {
        SkRect bounds;
        bounds.fLeft = xoff;
        bounds.fTop = yoff - state->fFontSize + state->fFontSpacing * 2;
        bounds.fRight = xoff + state->fFontAdvanceWidth + state->fFontSpacing;
        bounds.fBottom = yoff + state->fFontSpacing * 3;

        paint->setColor(bc);
        canvas->drawRect(bounds, *paint);
        return 0;
    }

    if (attr->underline) {
        SkRect bounds;
        bounds.fLeft = xoff;
        bounds.fTop = yoff + state->fFontSpacing * 2;
        bounds.fRight = xoff + state->fFontAdvanceWidth + state->fFontSpacing;
        bounds.fBottom = yoff + state->fFontSpacing * 3;

        paint->setColor(fc);
        canvas->drawRect(bounds, *paint);
    }

    if (attr->bold) {
        // TBD
    }

    if (attr->protect) {
        // TBD
    }

    if (attr->blink) {
        // TBD
    }

    paint->setColor(fc);

    canvas->drawString(string, xoff, yoff, *gFont, *paint);

    return 0;
}

#if defined(SK_BUILD_FOR_ANDROID)
int SDL_main(int argc, char** argv) {
#else
int main(int argc, char** argv) {
#endif
    uint32_t windowFlags = 0;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_GLContext glContext = nullptr;
#if defined(SK_BUILD_FOR_ANDROID) || defined(SK_BUILD_FOR_IOS)
    // For Android/iOS we need to set up for OpenGL ES and we make the window hi res & full screen
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS |
                  SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI;
#else
    // For all other clients we use the core profile and operate in a window
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
#endif
    static const int kStencilBits = 8;  // Skia needs 8 stencil bits
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, kStencilBits);

    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

    // If you want multisampling, uncomment the below lines and set a sample count
    static const int kMsaaSampleCount = 0;  // 4;
    // SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    // SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, kMsaaSampleCount);

    /*
     * In a real application you might want to initialize more subsystems
     */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        handle_error();
        return 1;
    }

    ApplicationState state;

    sk_sp<SkTypeface> typeface = SkTypeface::MakeFromName("monospace", SkFontStyle::Normal());
    SkFont font(typeface, state.fFontSize);
    font.setEdging(SkFont::Edging::kAntiAlias);
    // font.setHinting(SkFontHinting::kFull);

    gFont = &font;

    // Setup window
    // This code will create a window with the same resolution as the user's desktop.
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) != 0) {
        handle_error();
        return 1;
    }

    assert(typeface->isFixedPitch());
    state.fFontAdvanceWidth = gFont->measureText("X", 1U, SkTextEncoding::kUTF8, NULL);
    state.fFontSpacing = std::min(1.0f, gFont->getSpacing());

    dm.w = std::min<float>(dm.w, state.fFontAdvanceWidth * DEFAULT_ROW);
    dm.h = std::min<float>(dm.h, (state.fFontSize + state.fFontSpacing) * (DEFAULT_COL + 1) - state.fFontSpacing);

    SDL_Window* window = SDL_CreateWindow("SkTerminal", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, dm.w, dm.h, windowFlags);

    if (!window) {
        handle_error();
        return 1;
    }

    // To go fullscreen
    // SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

    // try and setup a GL context
    glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        handle_error();
        return 1;
    }

    int success = SDL_GL_MakeCurrent(window, glContext);
    if (success != 0) {
        handle_error();
        return success;
    }

    uint32_t windowFormat = SDL_GetWindowPixelFormat(window);
    int contextType;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &contextType);

    int dw, dh;
    SDL_GL_GetDrawableSize(window, &dw, &dh);

    SkDebugf("dw %d dh %d\n", dw, dh);

    glViewport(0, 0, dw, dh);
    glClearColor(1, 1, 1, 1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // setup GrContext
    auto interface = GrGLMakeNativeInterface();

    // setup contexts
    sk_sp<GrDirectContext> grContext(GrDirectContext::MakeGL(interface));
    SkASSERT(grContext);

    // Wrap the frame buffer object attached to the screen in a Skia render target so Skia can
    // render to it
    GrGLint buffer;
    GR_GL_GetIntegerv(interface.get(), GR_GL_FRAMEBUFFER_BINDING, &buffer);
    GrGLFramebufferInfo info;
    info.fFBOID = (GrGLuint)buffer;
    SkColorType colorType;

    // SkDebugf("%s", SDL_GetPixelFormatName(windowFormat));
    // TODO: the windowFormat is never any of these?
    if (SDL_PIXELFORMAT_RGBA8888 == windowFormat) {
        info.fFormat = GR_GL_RGBA8;
        colorType = kRGBA_8888_SkColorType;
    } else {
        colorType = kBGRA_8888_SkColorType;
        if (SDL_GL_CONTEXT_PROFILE_ES == contextType) {
            info.fFormat = GR_GL_BGRA8;
        } else {
            // We assume the internal format is RGBA8 on desktop GL
            info.fFormat = GR_GL_RGBA8;
        }
    }

    GrBackendRenderTarget target(dw, dh, kMsaaSampleCount, kStencilBits, info);

    // setup SkSurface
    // To use distance field text, use commented out SkSurfaceProps instead
    // SkSurfaceProps props(SkSurfaceProps::kUseDeviceIndependentFonts_Flag,
    //                      SkSurfaceProps::kUnknown_SkPixelGeometry);
    SkSurfaceProps props;

    sk_sp<SkSurface> surface(SkSurface::MakeFromBackendRenderTarget(grContext.get(), target,
                                                                    kBottomLeft_GrSurfaceOrigin,
                                                                    colorType, nullptr, &props));

    SkCanvas* canvas = surface->getCanvas();
    canvas->scale((float)dw / dm.w, (float)dh / dm.h);

    SkPaint paint;
    paint.setAntiAlias(true);

    // create a surface for CPU rasterization
    sk_sp<SkSurface> cpuSurface(SkSurface::MakeRaster(canvas->imageInfo()));

    SkCanvas* offscreen = cpuSurface->getCanvas();
    offscreen->save();
    offscreen->translate(50.0f, 50.0f);
    offscreen->drawPath(create_star(), paint);
    offscreen->restore();

    sk_sp<SkImage> image = cpuSurface->makeImageSnapshot();

    struct termios term {};
    struct winsize ws {};

    term.c_iflag = TTYDEF_IFLAG | IUTF8;
    term.c_oflag = TTYDEF_OFLAG;
    term.c_lflag = TTYDEF_LFLAG | ECHONL | PENDIN;
    term.c_cflag = TTYDEF_CFLAG;

    term.c_ispeed = TTYDEF_SPEED;
    term.c_ospeed = TTYDEF_SPEED;

    term.c_cc[VEOF] = CEOF;          // ICANON
    term.c_cc[VEOL] = CEOL;          // ICANON
    term.c_cc[VEOL2] = CEOL;         // ICANON together with IEXTEN
    term.c_cc[VERASE] = CERASE;      // ICANON
    term.c_cc[VINTR] = CINTR;        // ISIG
#ifdef VSTATUS
    term.c_cc[VSTATUS] = CSTATUS;    // ICANON together with IEXTEN
#endif
    term.c_cc[VKILL] = CKILL;        // ICANON
    term.c_cc[VMIN] = CMIN;          // !ICANON
    term.c_cc[VQUIT] = CQUIT;        // ISIG
#ifdef VSUSP
    term.c_cc[VSUSP] = CSUSP;        // ISIG
#endif
    term.c_cc[VTIME] = CTIME;        // !ICANON
#ifdef VDSUSP
    term.c_cc[VDSUSP] = CDSUSP;      // ISIG together with IEXTEN
#endif
    term.c_cc[VSTART] = CSTART;      // IXON, IXOFF
    term.c_cc[VSTOP] = CSTOP;        // IXON, IXOFF
    term.c_cc[VLNEXT] = CLNEXT;      // IEXTEN
    term.c_cc[VDISCARD] = CDISCARD;  // IEXTEN
    term.c_cc[VWERASE] = CWERASE;    // ICANON together with IEXTEN
    term.c_cc[VREPRINT] = CREPRINT;  // ICANON together with IEXTEN

    ws.ws_row = (float)(dw) / state.fFontAdvanceWidth;
    ws.ws_col = (float)(dh + state.fFontSpacing) / (state.fFontSize + state.fFontSpacing) - 1;
    ws.ws_xpixel = dw;
    ws.ws_ypixel = dh;
    int fd = -1;

    // Using the same way just like Terminal.app
    pid_t pid = forkpty(&fd, NULL, &term, &ws);
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        const char* childArgv[] = {"/bin/bash", "-la", NULL};
        execve(childArgv[0], (char**)childArgv, (char**)environ);
        return 0;
    }
    if (pid < 0) {
        SkDebugf("something wrong with forkpty\n");
        return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);

    // create a software-based virtual terminal
    struct tsm_screen* screen;
    struct tsm_vte* vte;

    tsm_screen_new(&screen, log_tsm, screen);
    tsm_screen_set_max_sb(screen, 10240);

    SkDebugf("resize row %d col %d\n", ws.ws_row, ws.ws_col);
    tsm_screen_resize(screen, ws.ws_row, ws.ws_col);

    tsm_vte_new(&vte, screen, term_write_cb, &fd, log_tsm, screen);

    sk_sp<SkImage> termImage;

    int rotation = 0;

    while (!state.fQuit) {  // Our application loop
        state.fRedraw = false;

        SkRandom rand;
        canvas->clear(SK_ColorWHITE);
        handle_events(&state, window, canvas, fd, screen, vte);

        char buffer[4096];
        ssize_t read_len = read(fd, buffer, sizeof(buffer));

        if (read_len > 0) {
            SkDebugf("read %d %ld\n", fd, read_len);
            tsm_vte_input(vte, buffer, read_len);
            state.fRedraw = true;
        } else if (read_len == 0) {
            break;
        }

        if (state.fRedraw) {
            SkDebugf("redraw term\n");
            struct tsm_screen_attr a;
            tsm_vte_get_def_attr(vte, &a);
            SkColor bc = term_get_bc_from_attr(&a);

            offscreen->save();
            offscreen->clear(bc);
            // offscreen->clear(SK_ColorTRANSPARENT);

            struct draw_ctx ctx = { offscreen, &state, &paint, true };
            // draw background
            tsm_screen_draw(screen, draw_cb, &ctx);

            // draw frontground
            ctx.bcOnly = false;
            tsm_screen_draw(screen, draw_cb, &ctx);

            offscreen->restore();
            termImage = cpuSurface->makeImageSnapshot();
        }

        // draw terminal canvas
        canvas->save();
        canvas->drawImage(termImage, 0, 0);
        canvas->restore();

        for (int i = 0; i < state.fRects.count(); i++) {
            paint.setColor(rand.nextU() | 0x44808080);
            canvas->drawRect(state.fRects[i], paint);
        }

        // draw offscreen canvas
        canvas->save();
        canvas->translate(dm.w / 2.0, dm.h / 2.0);
        canvas->rotate(rotation++);
        canvas->drawImage(image, -50.0f, -50.0f);
        canvas->restore();

        canvas->flush();

        SDL_GL_SwapWindow(window);
    }

    if (glContext) {
        SDL_GL_DeleteContext(glContext);
    }

    // Destroy window
    SDL_DestroyWindow(window);

    // Quit SDL subsystems
    SDL_Quit();
    return 0;
}
