/*
 * Window and input for the PC port (SDL2).
 *
 * Opens the game window, turns keyboard and game controller input into the
 * DualShock 2 state the game reads (pc_pad_*, see pc_sdk.c), and shows the
 * frame buffer the GS displays (port/src/gs) at every V-blank. F11 toggles
 * fullscreen, F12 saves a screenshot, Tab held fast-forwards.
 *
 * Without SDL2, or with CVX_HEADLESS set, the game runs without a window.
 */
#include "pc_window.h"

#include "../gs/gs.h"
#include "../host/pc_host.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DualShock 2 buttons as stored in pc_pad_buttons: the high byte is the
 * first button byte of the controller report. Buttons are active low. */
enum {
    PAD_SELECT = 0x0100, PAD_L3 = 0x0200, PAD_R3 = 0x0400, PAD_START = 0x0800,
    PAD_UP = 0x1000, PAD_RIGHT = 0x2000, PAD_DOWN = 0x4000, PAD_LEFT = 0x8000,
    PAD_L2 = 0x0001, PAD_R2 = 0x0002, PAD_L1 = 0x0004, PAD_R1 = 0x0008,
    PAD_TRIANGLE = 0x0010, PAD_CIRCLE = 0x0020, PAD_CROSS = 0x0040, PAD_SQUARE = 0x0080,
};

extern unsigned short pc_pad_buttons;
extern unsigned char pc_pad_sticks[4]; /* right x, right y, left x, left y */
extern void (*pc_frame_hook)(void);

#ifdef CVX_HAVE_SDL
#define SDL_MAIN_HANDLED /* main() is pc_main.c's */
#include <SDL.h>

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_GameController *controller;
static SDL_Texture *screen;
static int screen_w, screen_h;
static uint32_t pixels[GS_DISPLAY_MAX_W * GS_DISPLAY_MAX_H];

static const struct {
    SDL_Scancode key;
    unsigned short button;
} key_map[] = {
    { SDL_SCANCODE_UP, PAD_UP },
    { SDL_SCANCODE_DOWN, PAD_DOWN },
    { SDL_SCANCODE_LEFT, PAD_LEFT },
    { SDL_SCANCODE_RIGHT, PAD_RIGHT },
    { SDL_SCANCODE_RETURN, PAD_START },
    { SDL_SCANCODE_BACKSPACE, PAD_SELECT },
    { SDL_SCANCODE_SPACE, PAD_CROSS },     /* action / confirm */
    { SDL_SCANCODE_LSHIFT, PAD_SQUARE },   /* run */
    { SDL_SCANCODE_ESCAPE, PAD_CIRCLE },   /* cancel */
    { SDL_SCANCODE_E, PAD_TRIANGLE },
    { SDL_SCANCODE_Q, PAD_L1 },
    { SDL_SCANCODE_LCTRL, PAD_R1 },        /* aim */
    { SDL_SCANCODE_1, PAD_L2 },
    { SDL_SCANCODE_3, PAD_R2 },
};

static const struct {
    SDL_GameControllerButton pad;
    unsigned short button;
} controller_map[] = {
    { SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_UP },
    { SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_DOWN },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_LEFT },
    { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_RIGHT },
    { SDL_CONTROLLER_BUTTON_START, PAD_START },
    { SDL_CONTROLLER_BUTTON_BACK, PAD_SELECT },
    { SDL_CONTROLLER_BUTTON_A, PAD_CROSS },
    { SDL_CONTROLLER_BUTTON_B, PAD_CIRCLE },
    { SDL_CONTROLLER_BUTTON_X, PAD_SQUARE },
    { SDL_CONTROLLER_BUTTON_Y, PAD_TRIANGLE },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_L1 },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_R1 },
    { SDL_CONTROLLER_BUTTON_LEFTSTICK, PAD_L3 },
    { SDL_CONTROLLER_BUTTON_RIGHTSTICK, PAD_R3 },
};

static unsigned char axis_to_pad(Sint16 v)
{
    return (unsigned char)((v + 32768) >> 8); /* 0..255, 128 at rest */
}

static void read_input(void)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    unsigned short held = 0;
    unsigned char lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80;

    for (size_t i = 0; i < sizeof(key_map) / sizeof(key_map[0]); i++) {
        if (keys[key_map[i].key])
            held |= key_map[i].button;
    }
    /* WASD as the left stick. */
    if (keys[SDL_SCANCODE_A]) lx = 0x00;
    if (keys[SDL_SCANCODE_D]) lx = 0xff;
    if (keys[SDL_SCANCODE_W]) ly = 0x00;
    if (keys[SDL_SCANCODE_S]) ly = 0xff;

    if (controller != NULL) {
        for (size_t i = 0; i < sizeof(controller_map) / sizeof(controller_map[0]); i++) {
            if (SDL_GameControllerGetButton(controller, controller_map[i].pad))
                held |= controller_map[i].button;
        }
        if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384)
            held |= PAD_L2;
        if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384)
            held |= PAD_R2;
        Sint16 x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 y = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
        if (x < -8000 || x > 8000) lx = axis_to_pad(x);
        if (y < -8000 || y > 8000) ly = axis_to_pad(y);
        rx = axis_to_pad(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX));
        ry = axis_to_pad(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY));
    }

    pc_pad_buttons = (unsigned short)~held;
    pc_pad_sticks[0] = rx;
    pc_pad_sticks[1] = ry;
    pc_pad_sticks[2] = lx;
    pc_pad_sticks[3] = ly;
}

static void save_screenshot(void)
{
    static int count;
    char name[64];
    SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormatFrom(pixels, screen_w, screen_h, 32, screen_w * 4,
                                                           SDL_PIXELFORMAT_RGBA32);

    if (shot == NULL)
        return;
    snprintf(name, sizeof(name), "cvx_screenshot_%03d.bmp", count++);
    if (SDL_SaveBMP(shot, name) == 0)
        printf("window: saved %s\n", name);
    SDL_FreeSurface(shot);
}

static void frame(void)
{
    SDL_Event ev;

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            pc_quit_requested = 1;
            exit(0);
        case SDL_CONTROLLERDEVICEADDED:
            if (controller == NULL)
                controller = SDL_GameControllerOpen(ev.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (controller != NULL &&
                SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == ev.cdevice.which) {
                SDL_GameControllerClose(controller);
                controller = NULL;
            }
            break;
        case SDL_KEYDOWN:
            if (ev.key.keysym.scancode == SDL_SCANCODE_F12)
                save_screenshot();
            if (ev.key.keysym.scancode == SDL_SCANCODE_F11) {
                Uint32 full = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            break;
        }
    }
    read_input();

    /* Fast-forward: no pacing, and only every 8th frame is shown. */
    static unsigned skipped;
    pc_turbo = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_TAB];
    if (pc_turbo && (++skipped & 7) != 0)
        return;

    int w, h;
    gs_read_display(pixels, &w, &h);
    if (screen == NULL || w != screen_w || h != screen_h) {
        if (screen != NULL)
            SDL_DestroyTexture(screen);
        screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
        screen_w = w;
        screen_h = h;
    }
    SDL_UpdateTexture(screen, NULL, pixels, w * 4);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, screen, NULL, NULL); /* stretched to 4:3 */
    SDL_RenderPresent(renderer);
}

int pc_window_open(void)
{
    if (getenv("CVX_HEADLESS") != NULL)
        return 0;
    /* Keep Ctrl+C working even while the game is busy between frames. */
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "window: SDL_Init failed (%s), running without a window\n", SDL_GetError());
        return 0;
    }
    window = SDL_CreateWindow("Resident Evil Code: Veronica X", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 1280, 960, SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        fprintf(stderr, "window: %s, running without a window\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer != NULL)
        SDL_RenderSetLogicalSize(renderer, 640, 480); /* the PS2 picture is 4:3 */
    if (renderer == NULL) {
        fprintf(stderr, "window: %s, running without a window\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
    printf("window: SDL %s video\n", SDL_GetCurrentVideoDriver());
    pc_frame_hook = frame;
    return 1;
}

#else /* !CVX_HAVE_SDL */

int pc_window_open(void)
{
    return 0;
}

#endif
