/* SDL falso para testes no PC: comando controlado por um guiao (NSX_SCRIPT) */
#ifndef FAKE_SDL_H
#define FAKE_SDL_H
#include <stdint.h>
typedef int SDL_bool;
typedef int32_t Sint32; typedef int16_t Sint16; typedef uint32_t Uint32; typedef uint8_t Uint8;
typedef struct _SDL_GameController SDL_GameController;
typedef enum { SDL_CONTROLLER_AXIS_INVALID = -1, SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
  SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY, SDL_CONTROLLER_AXIS_TRIGGERLEFT,
  SDL_CONTROLLER_AXIS_TRIGGERRIGHT, SDL_CONTROLLER_AXIS_MAX } SDL_GameControllerAxis;
typedef enum { SDL_CONTROLLER_BUTTON_INVALID = -1, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
  SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE,
  SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
  SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_DPAD_UP,
  SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
  SDL_CONTROLLER_BUTTON_MAX } SDL_GameControllerButton;
enum { SDL_CONTROLLERDEVICEADDED = 0x653, SDL_CONTROLLERDEVICEREMOVED, SDL_CONTROLLERBUTTONDOWN = 0x651,
  SDL_CONTROLLERBUTTONUP = 0x652 };
typedef struct { Uint32 type; Uint32 timestamp; Sint32 which; Uint8 button; Uint8 state; } SDL_ControllerButtonEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 which; } SDL_ControllerDeviceEvent;
typedef union { Uint32 type; SDL_ControllerButtonEvent cbutton; SDL_ControllerDeviceEvent cdevice; char pad[56]; } SDL_Event;
#define SDL_INIT_GAMECONTROLLER 0x2000
#define SDL_INIT_EVENTS 0x4000
int SDL_InitSubSystem(Uint32 f);
int SDL_PollEvent(SDL_Event *e);
Uint32 SDL_GetTicks(void);
int SDL_NumJoysticks(void);
SDL_bool SDL_IsGameController(int i);
SDL_GameController *SDL_GameControllerOpen(int i);
void SDL_GameControllerClose(SDL_GameController *c);
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *c);
Sint16 SDL_GameControllerGetAxis(SDL_GameController *c, SDL_GameControllerAxis a);
Uint8 SDL_GameControllerGetButton(SDL_GameController *c, SDL_GameControllerButton b);
void SDL_GameControllerUpdate(void);
#endif
