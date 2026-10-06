/*
 * Window and input for the PC port (SDL2).
 *
 * Opens the game window, turns keyboard and game controller input into the
 * DualShock 2 state the game reads (pc_pad_*, see pc_sdk.c), and shows the
 * frame buffer the GS displays (port/src/gs) at every V-blank. F11 toggles
 * fullscreen, F12 saves a screenshot, F10 dumps a frame of the GS (see
 * gs_dump_frame), Tab held fast-forwards.
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
extern unsigned char pc_pad_motor[2];  /* small (on/off), big (0-255) */
extern unsigned int pc_pad_motor_sets;
extern void (*pc_frame_hook)(void);

#ifdef CVX_HAVE_SDL
#define SDL_MAIN_HANDLED /* main() is pc_main.c's */
#include <SDL.h>

static SDL_Window *window;
static SDL_Renderer *renderer;

/* Every controller plugged in drives the game's controller 1, so one can
 * be swapped for another at any time. SDL knows most controllers (Xbox,
 * PlayStation, Switch, ...) as "game controllers" with a standard layout;
 * a gamecontrollerdb.txt next to the game adds more. The others are read
 * as plain joysticks with the usual layout of cheap USB controllers. */
#define MAX_PADS 8
static struct {
    SDL_GameController *gc; /* or NULL for a plain joystick */
    SDL_Joystick *joy;
    SDL_JoystickID id;
    int sticks; /* plain joystick: bit n, stick n is centred when plugged in */
} pads[MAX_PADS];
static int npads;
static int rumbling;
static unsigned int motor_sets, motor_idle;
static SDL_Texture *screen;
static int screen_w, screen_h;
static SDL_Texture *overlay; /* replacement movie picture (pc_overlay) */
static int overlay_w, overlay_h;
static unsigned int overlay_serial;
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

/* Plain joysticks: the usual button order of cheap USB controllers. */
static const unsigned short joystick_map[] = {
    PAD_TRIANGLE, PAD_CIRCLE, PAD_CROSS, PAD_SQUARE, PAD_L2, PAD_R2,
    PAD_L1, PAD_R1, PAD_SELECT, PAD_START, PAD_L3, PAD_R3,
};

static unsigned char axis_to_pad(Sint16 v)
{
    return (unsigned char)((v + 32768) >> 8); /* 0..255, 128 at rest */
}

/* The stick pushed furthest wins when several controllers are in use. */
static void stick(unsigned char *x, unsigned char *y, Sint16 ax, Sint16 ay, int dead)
{
    int nx = ax < -dead || ax > dead ? axis_to_pad(ax) : 0x80;
    int ny = ay < -dead || ay > dead ? axis_to_pad(ay) : 0x80;

    if (abs(nx - 0x80) > abs(*x - 0x80))
        *x = (unsigned char)nx;
    if (abs(ny - 0x80) > abs(*y - 0x80))
        *y = (unsigned char)ny;
}

static int find_pad(SDL_JoystickID id)
{
    for (int i = 0; i < npads; i++) {
        if (pads[i].id == id)
            return i;
    }
    return -1;
}

static void add_pad(int index, int as_controller)
{
    SDL_GameController *gc = NULL;
    SDL_Joystick *joy;
    const char *name;
    char guid[33];

    if (npads == MAX_PADS || find_pad(SDL_JoystickGetDeviceInstanceID(index)) >= 0)
        return;
    if (as_controller) {
        gc = SDL_GameControllerOpen(index);
        if (gc == NULL)
            return;
        joy = SDL_GameControllerGetJoystick(gc);
        name = SDL_GameControllerName(gc);
    } else {
        joy = SDL_JoystickOpen(index);
        if (joy == NULL)
            return;
        name = SDL_JoystickName(joy);
    }
    pads[npads].gc = gc;
    pads[npads].joy = joy;
    pads[npads].id = SDL_JoystickInstanceID(joy);
    /* Some devices that are not controllers (keyboards, wheels) show up as
     * joysticks with axes that never rest at the centre: those are not
     * read as sticks. */
    pads[npads].sticks = 0;
    for (int s = 0; s < 2 && 2 * s + 1 < SDL_JoystickNumAxes(joy); s++) {
        if (abs(SDL_JoystickGetAxis(joy, 2 * s)) < 8000 && abs(SDL_JoystickGetAxis(joy, 2 * s + 1)) < 8000)
            pads[npads].sticks |= 1 << s;
    }
    npads++;
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), guid, sizeof(guid));
    printf("pad: %s plugged in (%s, %d buttons, %d axes; %d controller%s now)\n",
           name != NULL ? name : "controller", as_controller ? "known" : "unknown, usual layout guessed",
           SDL_JoystickNumButtons(joy), SDL_JoystickNumAxes(joy), npads, npads > 1 ? "s" : "");
    if (!as_controller)
        printf("pad: add a line for GUID %s to gamecontrollerdb.txt to map it\n", guid);
}

static void remove_pad(SDL_JoystickID id)
{
    int i = find_pad(id);

    if (i < 0)
        return;
    if (pads[i].gc != NULL)
        SDL_GameControllerClose(pads[i].gc);
    else
        SDL_JoystickClose(pads[i].joy);
    pads[i] = pads[--npads];
    printf("pad: a controller was unplugged (%d left)\n", npads);
}

/* The game's vibration (on when its option is on), on every controller
 * that has motors. Each call lasts a little and is renewed every frame
 * while the game wants it; it stops if the game stops setting it. */
static void rumble(void)
{
    Uint16 lo = (Uint16)(pc_pad_motor[1] * 257), hi = pc_pad_motor[0] ? 0xffff : 0;

    if (pc_pad_motor_sets != motor_sets) {
        motor_sets = pc_pad_motor_sets;
        motor_idle = 0;
    } else if (++motor_idle > 10) {
        lo = hi = 0;
    }

    if (lo == 0 && hi == 0 && !rumbling)
        return;
    rumbling = lo != 0 || hi != 0;
    for (int i = 0; i < npads; i++)
        SDL_JoystickRumble(pads[i].joy, lo, hi, rumbling ? 200 : 0);
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

    for (int p = 0; p < npads; p++) {
        SDL_GameController *gc = pads[p].gc;
        SDL_Joystick *joy = pads[p].joy;

        if (gc != NULL) {
            for (size_t i = 0; i < sizeof(controller_map) / sizeof(controller_map[0]); i++) {
                if (SDL_GameControllerGetButton(gc, controller_map[i].pad))
                    held |= controller_map[i].button;
            }
            if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384)
                held |= PAD_L2;
            if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384)
                held |= PAD_R2;
            stick(&lx, &ly, SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX),
                  SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY), 8000);
            stick(&rx, &ry, SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX),
                  SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY), 2000);
            continue;
        }
        int nb = SDL_JoystickNumButtons(joy);
        for (int i = 0; i < nb && i < (int)(sizeof(joystick_map) / sizeof(joystick_map[0])); i++) {
            if (SDL_JoystickGetButton(joy, i))
                held |= joystick_map[i];
        }
        if (SDL_JoystickNumHats(joy) > 0) {
            Uint8 hat = SDL_JoystickGetHat(joy, 0);
            if (hat & SDL_HAT_UP) held |= PAD_UP;
            if (hat & SDL_HAT_DOWN) held |= PAD_DOWN;
            if (hat & SDL_HAT_LEFT) held |= PAD_LEFT;
            if (hat & SDL_HAT_RIGHT) held |= PAD_RIGHT;
        }
        if (pads[p].sticks & 1)
            stick(&lx, &ly, SDL_JoystickGetAxis(joy, 0), SDL_JoystickGetAxis(joy, 1), 8000);
        if (pads[p].sticks & 2)
            stick(&rx, &ry, SDL_JoystickGetAxis(joy, 2), SDL_JoystickGetAxis(joy, 3), 2000);
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

/* A replacement movie's picture, over its area of the game's picture
 * (640x448, shown stretched to the 640x480 logical screen). It is drawn
 * from its own pixels, so an HD picture stays sharp in a large window. */
static void draw_overlay(void)
{
    SDL_Rect dst;

    if (pc_overlay.rgba == NULL)
        return;
    if (overlay == NULL || pc_overlay.w != overlay_w || pc_overlay.h != overlay_h) {
        if (overlay != NULL)
            SDL_DestroyTexture(overlay);
        overlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                    pc_overlay.w, pc_overlay.h);
        if (overlay == NULL)
            return;
        SDL_SetTextureScaleMode(overlay, SDL_ScaleModeLinear);
        overlay_w = pc_overlay.w;
        overlay_h = pc_overlay.h;
        overlay_serial = pc_overlay.serial - 1;
    }
    if (overlay_serial != pc_overlay.serial) {
        SDL_UpdateTexture(overlay, NULL, pc_overlay.rgba, pc_overlay.w * 4);
        overlay_serial = pc_overlay.serial;
    }
    dst.x = pc_overlay.x;
    dst.y = pc_overlay.y * 480 / 448;
    dst.w = pc_overlay.cw;
    dst.h = pc_overlay.ch * 480 / 448;
    SDL_RenderCopy(renderer, overlay, NULL, &dst);
}

static void frame(void)
{
    SDL_Event ev;

    gs_dump_vblank();

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            pc_quit_requested = 1;
            exit(0);
        /* Also sent at start-up for the controllers already there. */
        case SDL_CONTROLLERDEVICEADDED:
            add_pad(ev.cdevice.which, 1);
            break;
        case SDL_JOYDEVICEADDED: /* a game controller's comes first or after */
            add_pad(ev.jdevice.which, SDL_IsGameController(ev.jdevice.which));
            break;
        case SDL_JOYDEVICEREMOVED: /* a game controller's too */
            remove_pad(ev.jdevice.which);
            break;
        case SDL_KEYDOWN:
            if (ev.key.keysym.scancode == SDL_SCANCODE_F12)
                save_screenshot();
            if (ev.key.keysym.scancode == SDL_SCANCODE_F10) {
                static int dumps;
                char name[64];
                snprintf(name, sizeof(name), "cvx_gsdump_%03d.bin", dumps++);
                gs_dump_frame(name);
            }
            if (ev.key.keysym.scancode == SDL_SCANCODE_F11) {
                Uint32 full = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            break;
        }
    }
    read_input();
    rumble();

    /* Fast-forward: no pacing, and only every 8th frame is shown. */
    static unsigned skipped;
    pc_turbo = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_TAB];
    if (pc_turbo && (++skipped & 7) != 0)
        return;

    int w, h;
    if (gs_read_display(pixels, &w, &h)) {
        if (screen == NULL || w != screen_w || h != screen_h) {
            if (screen != NULL)
                SDL_DestroyTexture(screen);
            screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                       w, h);
            screen_w = w;
            screen_h = h;
        }
        SDL_UpdateTexture(screen, NULL, pixels, w * 4);
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, screen, NULL, NULL); /* stretched to 4:3 */
    draw_overlay();
    SDL_RenderPresent(renderer);
}

/* gamecontrollerdb.txt (the community list of controller layouts), next
 * to the game or in the current folder, if there is one. */
static void load_mappings(void)
{
    char *base = SDL_GetBasePath();
    char path[1024];
    int n = -1;

    if (base != NULL) {
        snprintf(path, sizeof(path), "%sgamecontrollerdb.txt", base);
        n = SDL_GameControllerAddMappingsFromFile(path);
        SDL_free(base);
    }
    if (n < 0)
        n = SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
    if (n >= 0)
        printf("pad: %d controller layouts read from gamecontrollerdb.txt\n", n);
}

/* The audio device plays what pc_audio.c has queued. */
static void SDLCALL audio_callback(void *user, Uint8 *stream, int len)
{
    (void)user;
    pc_audio_pull((int16_t *)stream, len / 4);
}

static void open_audio(void)
{
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;

    if (getenv("CVX_NO_AUDIO") != NULL || SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return;
    memset(&want, 0, sizeof(want));
    want.freq = PC_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) {
        fprintf(stderr, "window: no sound (%s)\n", SDL_GetError());
        return;
    }
    pc_audio_open = 1;
    SDL_PauseAudioDevice(dev, 0);
}

int pc_window_open(void)
{
    if (getenv("CVX_HEADLESS") != NULL)
        return 0;
    /* Keep Ctrl+C working even while the game is busy between frames. */
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetMainReady();
    /* The bottom face button is always the confirm (cross), also on
     * Nintendo controllers, whose A is on the right. */
    SDL_SetHint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "0");
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
    load_mappings();
    open_audio();
    pc_frame_hook = frame;
    return 1;
}

#else /* !CVX_HAVE_SDL */

int pc_window_open(void)
{
    return 0;
}

#endif
