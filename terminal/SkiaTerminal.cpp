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
#elif defined(SK_BUILD_FOR_WIN)
#include <windows.h>
#include <GL/gl.h>
#endif

#if defined(SK_BUILD_FOR_WIN)
#include <winsock2.h>
#ifndef EXTENDED_STARTUPINFO_PRESENT
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#endif
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE \
  ProcThreadAttributeValue(22, FALSE, TRUE, FALSE)
#endif

typedef HRESULT (__stdcall *PFNCREATEPSEUDOCONSOLE)(COORD c, HANDLE hIn, HANDLE hOut, DWORD dwFlags, HPCON* phpcon);
typedef HRESULT (__stdcall *PFNRESIZEPSEUDOCONSOLE)(HPCON hpc, COORD newSize);
typedef void (__stdcall *PFNCLOSEPSEUDOCONSOLE)(HPCON hpc);

HANDLE EnsureKernel32Loaded() {
    return LoadLibraryExW(L"kernel32.dll", 0, 0);
}
#else
#include <errno.h>
#include <string.h>

#include <fcntl.h>
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

#include <cstdlib>

#include <libtsm.h>

#define DEFAULT_ROW 80
#define DEFAULT_COL 24

#ifdef SK_BUILD_FOR_WIN
#define DEFAULT_FONT "Consolas"
#else
#define DEFAULT_FONT "monospace"
#endif

static bool resize_conpty(int dw, int dh, int ws_row, int ws_col, int fd);

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
    double fWidthScale;
    double fHeightScale;
    int32_t fRow;
    int32_t fCol;
    int32_t fDw;
    int32_t fDh;
#ifdef SK_BUILD_FOR_WIN
    HANDLE fSendThread;
    HANDLE fRecvThread;
#endif
};

static void handle_error() {
    const char* error = SDL_GetError();
    SkDebugf("SDL Error: %s\n", error);
    SDL_ClearError();
}

static SkFont *gFont;
static ApplicationState *gState;

static void handle_size_change(ApplicationState* state, SDL_Window* window, SkCanvas* canvas,
                               int fd, struct tsm_screen* screen, struct tsm_vte* vte) {
    int dw, dh;
    state->fFontAdvanceWidth = gFont->measureText("X", 1U, SkTextEncoding::kUTF8, NULL);
    state->fFontSpacing = std::min(1.0f, gFont->getSpacing());

    SDL_GL_GetDrawableSize(window, &dw, &dh);

    glViewport(0, 0, dw, dh);
    glClearColor(1, 1, 1, 1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    gState->fRow = (dw / state->fWidthScale) / state->fFontAdvanceWidth;
    gState->fCol = (dh / state->fHeightScale + state->fFontSpacing) / (state->fFontSize + state->fFontSpacing) - 1;
    gState->fDw = dw;
    gState->fDh = dh;

    SkDebugf("resize row %d col %d\n", gState->fRow, gState->fCol);

    if (!resize_conpty(dw, dh, gState->fRow, gState->fCol, fd)) {
        SkDebugf("resize_conpty: failed to resize conpty");
        return;
    }
    tsm_screen_resize(screen, gState->fRow, gState->fCol);
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
                // SDL_Scancode scancode = SDL_GetScancodeFromKey(key);
                uint16_t modifier = event.key.keysym.mod;

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

#if !defined(SK_BUILD_FOR_WIN)
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
#endif

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
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_RESIZED:
                        // Use SDL_GL_GetDrawableSize to measure the layout change
                        handle_size_change(state, window, canvas, fd, screen, vte);
                        break;
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

// Create pty
#if defined(SK_BUILD_FOR_WIN)
// Initializes the specified startup info struct with the required properties and
// updates its thread attribute list with the specified ConPTY handle
HRESULT InitializeStartupInfoAttachedToConPTY(STARTUPINFOEXW* siEx, HPCON hPC)
{
    size_t size = 0;
    bool fSuccess;
    HRESULT hr = S_OK;
    std::unique_ptr<BYTE[]> attrList;

    // Create the appropriately sized thread attribute list
    ::InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    attrList = std::make_unique<BYTE[]>(size);

    // Set startup info's attribute list & initialize it
    siEx->lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attrList.release());
    fSuccess = ::InitializeProcThreadAttributeList(
        siEx->lpAttributeList, 1, 0, (PSIZE_T)&size);
    if (!fSuccess) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto cleanup;
    }

    // Set thread attribute list's Pseudo Console to the specified ConPTY
    fSuccess = ::UpdateProcThreadAttribute(siEx->lpAttributeList, 0,
                    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                    hPC,
                    sizeof(hPC),
                    NULL,
                    NULL);
    if (!fSuccess) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto cleanup;
    }

cleanup:
    return hr;
}

struct listen_ctx {
    HANDLE outPipeOurSide, inPipeOurSide;
    HANDLE hPC;
    HANDLE hThread, hProcess;
    int socket;
};

static listen_ctx *gListenCtx;

HRESULT WriteFileN(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped) {
    DWORD numberOfBytesWritten;
    HRESULT hr = S_OK;
    *lpNumberOfBytesWritten = 0;
    while (nNumberOfBytesToWrite) {
        numberOfBytesWritten = 0;
        if (!::WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, &numberOfBytesWritten, lpOverlapped)) {
            // TODO: handle with GetLastError() == ERROR_IO_PENDING
            int lastError = GetLastError();
            if (lastError == ERROR_IO_PENDING) {
                continue;
            }
            hr = HRESULT_FROM_WIN32(lastError);
            return hr;
        }

        *lpNumberOfBytesWritten += numberOfBytesWritten;
        lpBuffer = reinterpret_cast<const char*>(lpBuffer) + numberOfBytesWritten;
        nNumberOfBytesToWrite -= numberOfBytesWritten;
    }
    return hr;
}

void __cdecl send_wndc(LPVOID lp) {
    listen_ctx *ctx { reinterpret_cast<listen_ctx*>(lp) };
    HRESULT hr = S_OK;

    const DWORD BUFF_SIZE{ 512 };
    char szBuffer[BUFF_SIZE]{};

    DWORD dwBytesWritten{};
    DWORD dwBytesRead{};
    BOOL fRead{ FALSE };
    OVERLAPPED ovWrite = {};

    do
    {
        // Read from the pipe
        fRead = ::ReadFile(ctx->outPipeOurSide, szBuffer, BUFF_SIZE, &dwBytesRead, NULL);
        SkDebugf("ReadFile(): read stdout %d\n", dwBytesRead);
        if (!fRead) {
            break;
        }
        hr = WriteFileN(reinterpret_cast<HANDLE>(ctx->socket), szBuffer, dwBytesRead, &dwBytesWritten, &ovWrite);
        if (FAILED(hr)) {
            SkDebugf("WriteFileN(): write tsm error %s\n",
                std::system_category().message(hr).c_str());
            break;
        }
        SkDebugf("WriteFileN(): write tsm %d\n", dwBytesWritten);
    } while (fRead && dwBytesRead >= 0);

    SkDebugf("send EOF\n");
    gState->fQuit = true;
}

void __cdecl recv_wndc(LPVOID lp) {
    listen_ctx *ctx { reinterpret_cast<listen_ctx*>(lp) };
    HRESULT hr = S_OK;

    const DWORD BUFF_SIZE{ 512 };
    char szBuffer[BUFF_SIZE]{};

    DWORD dwBytesWritten{};
    DWORD dwBytesRead{};
    BOOL fRead{ FALSE };
    OVERLAPPED ovRead = {};

    fd_set rfds;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10 * 1000;
    do
    {
        FD_ZERO(&rfds);
        FD_SET(reinterpret_cast<int>(ctx->socket), &rfds);
        int retVal = select(1, &rfds, NULL, NULL, &tv);
        if (retVal == -1) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            SkDebugf("select(): read tsm error 0x%x %s\n", hr,
                std::system_category().message(hr).c_str());
            break;
        } else if (retVal == 0) {
            /* No data/event to socket */
            fRead = true;
            continue;
        }
        // Write received text to the Console
        // Note: Write to the Console using WriteFile(hConsole...), not printf()/puts() to
        // prevent partially-read VT sequences from corrupting output
        fRead = ::ReadFile(reinterpret_cast<HANDLE>(ctx->socket), szBuffer, BUFF_SIZE, &dwBytesRead, &ovRead);
        if (!fRead) {
            int lastError = GetLastError();
            if (lastError == ERROR_IO_PENDING) {
                fRead = true;
                continue;
            }
            hr = HRESULT_FROM_WIN32(lastError);
            SkDebugf("ReadFile(): read tsm error 0x%x %s\n", hr,
                std::system_category().message(hr).c_str());
            break;
        }
        SkDebugf("ReadFile(): read tsm %d\n", dwBytesRead);
        ::WriteFile(ctx->inPipeOurSide, szBuffer, dwBytesRead, &dwBytesWritten, NULL);
        SkDebugf("WriteFile(): stdin %d\n", dwBytesWritten);
    } while (fRead && dwBytesRead >= 0);

    SkDebugf("recv EOF\n");
    gState->fQuit = true;
}

// inspired by vim's channel.c
#pragma comment(lib, "Ws2_32.lib")
int socketpair(int *sfd, int *cfd) {
    //-------------------------
    // Initialize Winsock
    WSADATA wsaData;
    int iResult;

    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != NO_ERROR) {
      SkDebugf("Error at WSAStartup()\n");
      return -1;
    }

    int fd, accepted_fd = -1, server_fd;
    struct sockaddr_in server {}, client {};
    int client_len = sizeof(client), server_len = sizeof(server);
    u_long val = 1;

    fd = ::socket(AF_INET, SOCK_STREAM, 0);
    server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1 || server_fd == -1) {
        goto fail;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server.sin_port = htons(0);
    if (::bind(fd, (struct sockaddr*)&server, sizeof(server)) < 0 ||
        ::listen(fd, 1) < 0 ||
        ::getsockname(fd, (struct sockaddr*)&server, &server_len) < 0 ||
        ::ioctlsocket(server_fd, FIONBIO, &val) < 0) /* Make connect() non-blocking. */ {
        SkDebugf("Error at bind()\n");
        goto fail;
    }
    if (::connect(server_fd, (struct sockaddr*)&server, sizeof(server)) < 0) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS && err != WSAEINTR) {
            SkDebugf("Error at connect()\n");
            goto fail;
        }
    }

    if ((accepted_fd = ::accept(fd, (struct sockaddr*)&client, &client_len)) < 0 ||
        ::ioctlsocket(accepted_fd, FIONBIO, &val) < 0) {
        SkDebugf("Error at accept()\n");
        goto fail;
    }

    ::closesocket(fd);
    *sfd = accepted_fd;
    *cfd = server_fd;

    return 0;
fail:
    ::closesocket(fd);
    ::closesocket(server_fd);
    ::closesocket(accepted_fd);
    return -1;
}

static bool create_conpty(int dw, int dh, int ws_row, int ws_col, int *fd, ApplicationState *state) {
    HANDLE hLibrary = EnsureKernel32Loaded();
    HRESULT hr = S_OK;
    PFNCREATEPSEUDOCONSOLE const CreatePseudoConsole = (PFNCREATEPSEUDOCONSOLE)GetProcAddress((HMODULE)hLibrary, "CreatePseudoConsole");
    if (CreatePseudoConsole == nullptr) {
        return false;
    }

    HANDLE outPipeOurSide, inPipeOurSide;
    HANDLE outPipePseudoConsoleSide, inPipePseudoConsoleSide;
    HPCON hPC = 0;
    COORD consize;
    BOOL fSuccess;
    int client;
    listen_ctx *ctx;

    // Create the in/out pipes:
    if (!::CreatePipe(&inPipePseudoConsoleSide, &inPipeOurSide, NULL, 0) ||
        !::CreatePipe(&outPipeOurSide, &outPipePseudoConsoleSide, NULL, 0)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        SkDebugf("conpty: CreatePipe %s\n",
            std::system_category().message(hr).c_str());
        return false;
    }

    // Create the Pseudo Console, using the pipes
    consize.X = ws_row;
    consize.Y = ws_col;
    hr = CreatePseudoConsole(consize, inPipePseudoConsoleSide, outPipePseudoConsoleSide, 0, &hPC);
    if (FAILED(hr)) {
        SkDebugf("conpty: CreatePseudoConsole %n\n",
            std::system_category().message(hr).c_str());
        return false;
    }

    // Prepare the StartupInfoEx structure attached to the ConPTY.
    STARTUPINFOEXW startupInfoEx {};
    startupInfoEx.StartupInfo.cb = sizeof(startupInfoEx);

    hr = InitializeStartupInfoAttachedToConPTY(&startupInfoEx, hPC);
    if (FAILED(hr)) {
        SkDebugf("conpty: InitializeStartupInfoAttachedToConPTY %s\n",
            std::system_category().message(hr).c_str());
        ::CloseHandle(inPipeOurSide);
        ::CloseHandle(outPipeOurSide);
        ::CloseHandle(inPipePseudoConsoleSide);
        ::CloseHandle(outPipePseudoConsoleSide);
        return false;
    }

    // Create the client application, using startup info containing ConPTY info
    wchar_t expanded_commandline[MAX_PATH];
    const wchar_t *commandline = L"%WINDIR%\\system32\\cmd.exe";
    ::ExpandEnvironmentStringsW(commandline, expanded_commandline, sizeof(expanded_commandline));

    PROCESS_INFORMATION process_information {};

    fSuccess = ::CreateProcessW(
                    nullptr,                       // No module ame - use Command Line
                    expanded_commandline,          // Command Line
                    nullptr,                       // Process handle not inheriable
                    nullptr,                       // Thread handle not inheriable
                    TRUE,                          // Inherit handles
                    EXTENDED_STARTUPINFO_PRESENT,  // Creation flags
                    nullptr,                       // Use parent's environment block
                    nullptr,                       // Use parent's starting directory
                    &startupInfoEx.StartupInfo,    // Pointer to STARTUPINFO
                    &process_information);         // Pointer to PROCESS_INFORMATION

    if (!fSuccess) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        SkDebugf("conpty: CreateProcessW %s\n",
            std::system_category().message(hr).c_str());
        ::CloseHandle(inPipeOurSide);
        ::CloseHandle(outPipeOurSide);
        goto cleanup;
    }
    ctx = new listen_ctx;
    ctx->outPipeOurSide = outPipeOurSide;
    ctx->inPipeOurSide = inPipeOurSide;
    ctx->hPC = hPC;
    ctx->hThread = process_information.hThread;
    ctx->hProcess = process_information.hProcess;
    if (socketpair(&ctx->socket, &client) < 0) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        SkDebugf("conpty: socketpair %s\n",
            std::system_category().message(hr).c_str());
        fSuccess = false;
        goto cleanup;
    }
    *fd = client;

    // Create & start thread to listen to the incoming pipe
    // Note: Using CRT-safe _beginthread() rather than CreateThread()
    gListenCtx = ctx;
    gState->fSendThread = reinterpret_cast<HANDLE>(_beginthread(send_wndc, 0, ctx));
    SkDebugf("send thread began\n");
    gState->fRecvThread = reinterpret_cast<HANDLE>(_beginthread(recv_wndc, 0, ctx));
    SkDebugf("recv thread began\n");

cleanup:
    ::DeleteProcThreadAttributeList(startupInfoEx.lpAttributeList);
    delete[] (BYTE*)startupInfoEx.lpAttributeList;
    ::CloseHandle(inPipePseudoConsoleSide);
    ::CloseHandle(outPipePseudoConsoleSide);
    return fSuccess;
}

static bool resize_conpty(int dw, int dh, int ws_row, int ws_col, int /*fd*/) {
    HANDLE hLibrary = EnsureKernel32Loaded();
    HRESULT hr = S_OK;
    PFNRESIZEPSEUDOCONSOLE const ResizePseudoConsole = (PFNRESIZEPSEUDOCONSOLE)GetProcAddress((HMODULE)hLibrary, "ResizePseudoConsole");
    if (ResizePseudoConsole == nullptr) {
        return false;
    }

    // Retrieve width and height dimensions of display in
    // characters using theoretical height/width functions
    // that can retrieve the properties from the display
    // attached to the event.
    COORD consize;
    consize.X = ws_row;
    consize.Y = ws_col;

    hr = ResizePseudoConsole(gListenCtx->hPC, consize);
    if (FAILED(hr)) {
        SkDebugf("resize: ResizePseudoConsole %s",
            std::system_category().message(hr).c_str());
        return false;
    }
    return true;
}

static void close_conpty(int /*fd*/) {
    listen_ctx* ctx = gListenCtx;
    // Close ConPTY - this will terminate client process if running
    HANDLE hLibrary = EnsureKernel32Loaded();
    PFNCLOSEPSEUDOCONSOLE const ClosePseudoConsole = (PFNCLOSEPSEUDOCONSOLE)GetProcAddress((HMODULE)hLibrary, "ClosePseudoConsole");
    if (ClosePseudoConsole == nullptr) {
        return;
    }

    // Close ConPTY - this will terminate client process if running
    ClosePseudoConsole(ctx->hPC);
    // Clean-up the pipes
    ::CloseHandle(ctx->inPipeOurSide);
    ::CloseHandle(ctx->outPipeOurSide);
    ::closesocket(ctx->socket);
    // Now safe to clean-up client app's process-info & thread
    ::CloseHandle(ctx->hThread);
    ::TerminateProcess(ctx->hProcess, /*uExitCode*/ 0);
    ::CloseHandle(ctx->hProcess);

    delete ctx;
    gListenCtx = nullptr;
}
#else
static bool create_conpty(int dw, int dh, int ws_row, int ws_col, int *fd, ApplicationState *state) {
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

    ws.ws_row = ws_row;
    ws.ws_col = ws_col;
    ws.ws_xpixel = dw;
    ws.ws_ypixel = dh;

    // Using the same way just like Terminal.app
    pid_t pid = ::forkpty(fd, NULL, &term, &ws);
    if (pid == 0) {
        ::setenv("TERM", "xterm-256color", 1);
        const char* childArgv[] = {"/bin/bash", "-la", NULL};
        ::execve(childArgv[0], (char**)childArgv, (char**)environ);
        return false;
    }

    if (pid < 0) {
        SkDebugf("something wrong with forkpty: %s\n", strerror(errno));
        return false;
    }
    fcntl(*fd, F_SETFL, O_NONBLOCK);

    return true;
}

static bool resize_conpty(int dw, int dh, int ws_row, int ws_col, int fd) {
    struct winsize ws;

    ws.ws_row = ws_row;
    ws.ws_col = ws_col;
    ws.ws_xpixel = dw;
    ws.ws_ypixel = dh;

    if (ioctl(fd, TIOCSWINSZ, &ws) < 0) {
        SkDebugf("resize_conpty: TIOCSWINSZ %s", strerror(errno));
        return false;
    }
    return true;
}

static void close_conpty(int fd) {
    close(fd);
}
#endif

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

#if defined(SK_BUILD_FOR_WIN)
static void term_write_cb(struct tsm_vte* vte, const char* u8, size_t len, void* data) {
    int fd = reinterpret_cast<intptr_t>(data);
    int send_len = send(fd, u8, len, 0);
    if (send_len < 0 || send_len < len) {
        int lastError = GetLastError();
        if (lastError != ERROR_IO_PENDING && lastError != WSAEWOULDBLOCK) {
            HRESULT hr = HRESULT_FROM_WIN32(lastError);
            SkDebugf("term_write_cb: send %s\n",
                std::system_category().message(hr).c_str());
        }
    }
}
static int term_read_cb(struct tsm_vte* vte, char* u8, size_t len, int fd) {
    int read_len = recv(fd, u8, len, 0);
    if (read_len < 0) {
        int lastError = GetLastError();
        if (lastError != ERROR_IO_PENDING && lastError != WSAEWOULDBLOCK) {
            HRESULT hr = HRESULT_FROM_WIN32(lastError);
            SkDebugf("term_read_cb: recv %s\n",
                std::system_category().message(hr).c_str());
        }
    }
    return read_len;
}
#else
static void term_write_cb(struct tsm_vte* vte, const char* u8, size_t len, void* data) {
    int fd = reinterpret_cast<intptr_t>(data);
    write(fd, u8, len);
}
static int term_read_cb(struct tsm_vte* vte, char* u8, size_t len, int fd) {
    return read(fd, u8, len);
}
#endif

enum vte_color {
    VTE_COLOR_BLACK,
    VTE_COLOR_RED,
    VTE_COLOR_GREEN,
    VTE_COLOR_YELLOW,
    VTE_COLOR_BLUE,
    VTE_COLOR_MAGENTA,
    VTE_COLOR_CYAN,
    VTE_COLOR_LIGHT_GREY,
    VTE_COLOR_DARK_GREY,
    VTE_COLOR_LIGHT_RED,
    VTE_COLOR_LIGHT_GREEN,
    VTE_COLOR_LIGHT_YELLOW,
    VTE_COLOR_LIGHT_BLUE,
    VTE_COLOR_LIGHT_MAGENTA,
    VTE_COLOR_LIGHT_CYAN,
    VTE_COLOR_WHITE,
    VTE_COLOR_FOREGROUND,
    VTE_COLOR_BACKGROUND,
    VTE_COLOR_NUM
};

static uint8_t VTE_COLOR_palette[VTE_COLOR_NUM][3] = {
        [VTE_COLOR_BLACK] = {0, 0, 0},             /* black */
        [VTE_COLOR_RED] = {205, 0, 0},             /* red */
        [VTE_COLOR_GREEN] = {0, 205, 0},           /* green */
        [VTE_COLOR_YELLOW] = {205, 205, 0},        /* yellow */
        [VTE_COLOR_BLUE] = {0, 0, 238},            /* blue */
        [VTE_COLOR_MAGENTA] = {205, 0, 205},       /* magenta */
        [VTE_COLOR_CYAN] = {0, 205, 205},          /* cyan */
        [VTE_COLOR_LIGHT_GREY] = {229, 229, 229},  /* light grey */
        [VTE_COLOR_DARK_GREY] = {127, 127, 127},   /* dark grey */
        [VTE_COLOR_LIGHT_RED] = {255, 0, 0},       /* light red */
        [VTE_COLOR_LIGHT_GREEN] = {0, 255, 0},     /* light green */
        [VTE_COLOR_LIGHT_YELLOW] = {255, 255, 0},  /* light yellow */
        [VTE_COLOR_LIGHT_BLUE] = {92, 92, 255},    /* light blue */
        [VTE_COLOR_LIGHT_MAGENTA] = {255, 0, 255}, /* light magenta */
        [VTE_COLOR_LIGHT_CYAN] = {0, 255, 255},    /* light cyan */
        [VTE_COLOR_WHITE] = {255, 255, 255},       /* white */

        [VTE_COLOR_FOREGROUND] = {229, 229, 229}, /* light grey */
        [VTE_COLOR_BACKGROUND] = {0, 0, 0},       /* black */
};
#if 0
static uint8_t VTE_COLOR_palette_solarized[VTE_COLOR_NUM][3] = {
        [VTE_COLOR_BLACK] = {7, 54, 66},             /* black */
        [VTE_COLOR_RED] = {220, 50, 47},             /* red */
        [VTE_COLOR_GREEN] = {133, 153, 0},           /* green */
        [VTE_COLOR_YELLOW] = {181, 137, 0},          /* yellow */
        [VTE_COLOR_BLUE] = {38, 139, 210},           /* blue */
        [VTE_COLOR_MAGENTA] = {211, 54, 130},        /* magenta */
        [VTE_COLOR_CYAN] = {42, 161, 152},           /* cyan */
        [VTE_COLOR_LIGHT_GREY] = {238, 232, 213},    /* light grey */
        [VTE_COLOR_DARK_GREY] = {0, 43, 54},         /* dark grey */
        [VTE_COLOR_LIGHT_RED] = {203, 75, 22},       /* light red */
        [VTE_COLOR_LIGHT_GREEN] = {88, 110, 117},    /* light green */
        [VTE_COLOR_LIGHT_YELLOW] = {101, 123, 131},  /* light yellow */
        [VTE_COLOR_LIGHT_BLUE] = {131, 148, 150},    /* light blue */
        [VTE_COLOR_LIGHT_MAGENTA] = {108, 113, 196}, /* light magenta */
        [VTE_COLOR_LIGHT_CYAN] = {147, 161, 161},    /* light cyan */
        [VTE_COLOR_WHITE] = {253, 246, 227},         /* white */

        [VTE_COLOR_FOREGROUND] = {238, 232, 213}, /* light grey */
        [VTE_COLOR_BACKGROUND] = {7, 54, 66},     /* black */
};

static uint8_t VTE_COLOR_palette_solarized_black[VTE_COLOR_NUM][3] = {
        [VTE_COLOR_BLACK] = {0, 0, 0},               /* black */
        [VTE_COLOR_RED] = {220, 50, 47},             /* red */
        [VTE_COLOR_GREEN] = {133, 153, 0},           /* green */
        [VTE_COLOR_YELLOW] = {181, 137, 0},          /* yellow */
        [VTE_COLOR_BLUE] = {38, 139, 210},           /* blue */
        [VTE_COLOR_MAGENTA] = {211, 54, 130},        /* magenta */
        [VTE_COLOR_CYAN] = {42, 161, 152},           /* cyan */
        [VTE_COLOR_LIGHT_GREY] = {238, 232, 213},    /* light grey */
        [VTE_COLOR_DARK_GREY] = {0, 43, 54},         /* dark grey */
        [VTE_COLOR_LIGHT_RED] = {203, 75, 22},       /* light red */
        [VTE_COLOR_LIGHT_GREEN] = {88, 110, 117},    /* light green */
        [VTE_COLOR_LIGHT_YELLOW] = {101, 123, 131},  /* light yellow */
        [VTE_COLOR_LIGHT_BLUE] = {131, 148, 150},    /* light blue */
        [VTE_COLOR_LIGHT_MAGENTA] = {108, 113, 196}, /* light magenta */
        [VTE_COLOR_LIGHT_CYAN] = {147, 161, 161},    /* light cyan */
        [VTE_COLOR_WHITE] = {253, 246, 227},         /* white */

        [VTE_COLOR_FOREGROUND] = {238, 232, 213}, /* light grey */
        [VTE_COLOR_BACKGROUND] = {0, 0, 0},       /* black */
};

static uint8_t VTE_COLOR_palette_solarized_white[VTE_COLOR_NUM][3] = {
        [VTE_COLOR_BLACK] = {7, 54, 66},             /* black */
        [VTE_COLOR_RED] = {220, 50, 47},             /* red */
        [VTE_COLOR_GREEN] = {133, 153, 0},           /* green */
        [VTE_COLOR_YELLOW] = {181, 137, 0},          /* yellow */
        [VTE_COLOR_BLUE] = {38, 139, 210},           /* blue */
        [VTE_COLOR_MAGENTA] = {211, 54, 130},        /* magenta */
        [VTE_COLOR_CYAN] = {42, 161, 152},           /* cyan */
        [VTE_COLOR_LIGHT_GREY] = {238, 232, 213},    /* light grey */
        [VTE_COLOR_DARK_GREY] = {0, 43, 54},         /* dark grey */
        [VTE_COLOR_LIGHT_RED] = {203, 75, 22},       /* light red */
        [VTE_COLOR_LIGHT_GREEN] = {88, 110, 117},    /* light green */
        [VTE_COLOR_LIGHT_YELLOW] = {101, 123, 131},  /* light yellow */
        [VTE_COLOR_LIGHT_BLUE] = {131, 148, 150},    /* light blue */
        [VTE_COLOR_LIGHT_MAGENTA] = {108, 113, 196}, /* light magenta */
        [VTE_COLOR_LIGHT_CYAN] = {147, 161, 161},    /* light cyan */
        [VTE_COLOR_WHITE] = {253, 246, 227},         /* white */

        [VTE_COLOR_FOREGROUND] = {7, 54, 66},     /* black */
        [VTE_COLOR_BACKGROUND] = {238, 232, 213}, /* light grey */
};
#endif

static SkColor term_get_fc_from_attr(const struct tsm_screen_attr* attr) {
    uint8_t fr = attr->fr, fg = attr->fg, fb = attr->fb;

    if (attr->fccode >= 0) {
        uint8_t code = attr->fccode;
        /* bold causes light colors */
        if (attr->bold && code < 8) code += 8;

        if (code >= VTE_COLOR_NUM) code = VTE_COLOR_FOREGROUND;

        fr = VTE_COLOR_palette[code][0];
        fg = VTE_COLOR_palette[code][1];
        fb = VTE_COLOR_palette[code][2];
    }

    return SkColorSetARGB(0xFF, fr, fg, fb);
}

static SkColor term_get_bc_from_attr(const struct tsm_screen_attr* attr) {
    uint8_t br = attr->br, bg = attr->bg, bb = attr->bb;

    if (attr->bccode >= 0) {
        uint8_t code = attr->bccode;
        /* bold causes light colors */

        if (code >= VTE_COLOR_NUM) code = VTE_COLOR_BACKGROUND;

        br = VTE_COLOR_palette[code][0];
        bg = VTE_COLOR_palette[code][1];
        bb = VTE_COLOR_palette[code][2];
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

/* Used by atexit handler */
static SDL_GLContext glContext;
static SDL_Window* window;

#if defined(SK_BUILD_FOR_ANDROID) || defined(SK_BUILD_FOR_WIN)
int SDL_main(int argc, char** argv) {
#else
int main(int argc, char** argv) {
#endif

    uint32_t windowFlags = 0;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    glContext = nullptr;
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

    ApplicationState state {};
    gState = &state;

    sk_sp<SkTypeface> typeface = SkTypeface::MakeFromName(DEFAULT_FONT, SkFontStyle::Normal());
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

    // SkASSERT(typeface->isFixedPitch());
    state.fFontAdvanceWidth = gFont->measureText("X", 1U, SkTextEncoding::kUTF8, NULL);
    state.fFontSpacing = std::min(1.0f, gFont->getSpacing());

    SkDebugf("default: row %d col %d\n", DEFAULT_ROW, DEFAULT_COL);
    dm.w = std::min<float>(dm.w, state.fFontAdvanceWidth * DEFAULT_ROW);
    dm.h = std::min<float>(dm.h, (state.fFontSize + state.fFontSpacing) * (DEFAULT_COL + 1) - state.fFontSpacing);
    SkDebugf("dm: dw %d dh %d\n", dm.w, dm.h);

    window = SDL_CreateWindow("SkTerminal", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, dm.w, dm.h, windowFlags);

    if (!window) {
        handle_error();
        return 1;
    }

    // To go fullscreen
    // SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

    // Disable resize until we handle it correctly (as Terminal.app)
    SDL_SetWindowResizable(window, SDL_FALSE);

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
    SkDebugf("init: dw %d dh %d\n", dw, dh);

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
    state.fWidthScale = (float)dw / dm.w;
    state.fHeightScale = (float)dh / dm.h;
    SkDebugf("scale: width: %.02f, height: %.02f\n", state.fWidthScale, state.fHeightScale);

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

    int fd {-1};
    int ws_row = (float)(dm.w) / state.fFontAdvanceWidth;
    int ws_col = (float)(dm.h + state.fFontSpacing) / (state.fFontSize + state.fFontSpacing) - 1;
    SkDebugf("init: row %d col %d\n", ws_row, ws_col);
    if (!create_conpty(dw, dh, ws_row, ws_col, &fd, &state)) {
        SkDebugf("failed to create conpty\n");
        return -1;
    }

    // create a software-based virtual terminal
    struct tsm_screen* screen;
    struct tsm_vte* vte;

    tsm_screen_new(&screen, log_tsm, screen);
    tsm_screen_set_max_sb(screen, 10240);
    tsm_screen_resize(screen, ws_row, ws_col);

    tsm_vte_new(&vte, screen, term_write_cb, reinterpret_cast<void*>(static_cast<intptr_t>(fd)), log_tsm, screen);

    sk_sp<SkImage> termImage;

    int rotation = 0;

    while (!state.fQuit) {  // Our application loop
        state.fRedraw = false;

        SkRandom rand;
        canvas->clear(SK_ColorWHITE);
        handle_events(&state, window, canvas, fd, screen, vte);

        int read_len = -1;
        char buffer[4096];
        read_len = term_read_cb(vte, buffer, sizeof(buffer), fd);
        if (read_len > 0) {
            SkDebugf("term_read_cb: %d\n", read_len);
            tsm_vte_input(vte, buffer, read_len);
            state.fRedraw = true;
        } else if (read_len == 0) {
            break;
        }

        if (state.fRedraw) {
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
        canvas->translate(state.fDw, state.fDh);
        canvas->rotate(rotation++);
        canvas->drawImage(image, -50.0f, -50.0f);
        canvas->restore();

        canvas->flush();

        SDL_GL_SwapWindow(window);
    }

#ifdef SK_BUILD_FOR_WIN
    TerminateThread(state.fSendThread, /*dwExitCode*/ 0);
    WaitForSingleObject(state.fSendThread, INFINITE);
    SkDebugf("send thread exited\n");
    TerminateThread(state.fRecvThread, /*dwExitCode*/ 0);
    WaitForSingleObject(state.fRecvThread, INFINITE);
    SkDebugf("recv thread exited\n");
#endif

    close_conpty(fd);

    std::atexit([]() {
        if (glContext) {
            SDL_GL_DeleteContext(glContext);
        }

        // Destroy window
        SDL_DestroyWindow(window);

        // Quit SDL subsystems
        SDL_Quit();
        SkDebugf("main thread exited\n");
    });

    return 0;
}
