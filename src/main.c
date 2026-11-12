// NetSurfX fase 1: janela SDL 640x480, cursor com analogico, estado da rede.
#include <SDL.h>
#include <hal/video.h>
#include <hal/debug.h>
#include <hal/xbox.h>
#include <nxdk/net.h>
#include <stdbool.h>

#define W 640
#define H 480

int main(void)
{
    XVideoSetMode(W, H, 32, REFRESH_DEFAULT);

    bool net_ok = (nxNetInit(NULL) == 0);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        debugPrint("SDL_Init falhou: %s\n", SDL_GetError());
        Sleep(5000);
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("NetSurfX", 0, 0, W, H, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);

    SDL_GameController *pad = NULL;
    float cx = W / 2, cy = H / 2;
    bool running = true;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad)
                pad = SDL_GameControllerOpen(e.cdevice.which);
            if (e.type == SDL_CONTROLLERBUTTONDOWN &&
                e.cbutton.button == SDL_CONTROLLER_BUTTON_START)
                running = false;
        }
        if (pad) {
            float ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32768.0f;
            float ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32768.0f;
            if (ax > -0.15f && ax < 0.15f) ax = 0;
            if (ay > -0.15f && ay < 0.15f) ay = 0;
            cx += ax * 8; cy += ay * 8;
            if (cx < 0) cx = 0; if (cx > W - 1) cx = W - 1;
            if (cy < 0) cy = 0; if (cy > H - 1) cy = H - 1;
        }

        SDL_SetRenderDrawColor(ren, 245, 245, 245, 255);
        SDL_RenderClear(ren);

        // barra de estado: verde = rede iniciada, vermelho = falhou
        SDL_Rect bar = {0, 0, W, 24};
        if (net_ok) SDL_SetRenderDrawColor(ren, 40, 160, 60, 255);
        else        SDL_SetRenderDrawColor(ren, 190, 40, 40, 255);
        SDL_RenderFillRect(ren, &bar);

        // cursor
        SDL_Rect cur = {(int)cx, (int)cy, 10, 10};
        SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
        SDL_RenderFillRect(ren, &cur);

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
