#pragma once

// SDL event synthesis used only by declarative validation scripts.

#include "Engine/Common/CoreMinimal.hpp"
#include <SDL3/SDL.h>

namespace Runtime::Input::Synthetic
{
    struct FPoint
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    SDL_Keycode ResolveKeyCode(const std::string& code);
    SDL_Scancode ResolveScanCode(SDL_Keycode key, const std::string& code);
    SDL_Keymod ResolveModifiers(const std::vector<std::string>& mods);
    Uint8 ResolveMouseButton(const std::string& button);

    void PushKey(SDL_WindowID windowId, SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mods, bool down);
    void PushKeyPress(SDL_WindowID windowId, SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mods);
    void PushText(SDL_WindowID windowId, const std::string& utf8);

    void PushMouseMove(SDL_Window* window, FPoint from, FPoint to);
    void PushMouseButton(SDL_WindowID windowId, FPoint at, Uint8 button, bool down, Uint8 clicks = 1);
    void PushMouseWheel(SDL_WindowID windowId, FPoint at, float x, float y);

    // Device ids stamped on every synthetic event so watch mode can tell them from real input.
    inline constexpr SDL_MouseID syntheticMouseId = 0x676B6E78u;
    inline constexpr SDL_KeyboardID syntheticKeyboardId = 0x676B6E78u;

    // Keyboard, text, mouse button/motion and wheel events: what a person could inject.
    bool IsUserInputEvent(const SDL_Event& event);
    // True for events this module pushed. Text events carry no device id, so each
    // pushed text event is counted and the check consumes one count.
    bool ClaimSyntheticEvent(const SDL_Event& event);
}
