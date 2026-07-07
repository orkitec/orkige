// ImGuiSDL3Input - SDL3 -> ImGui IO translation (see header for why this is
// hand-rolled instead of imgui's own SDL3 backend).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "ImGuiSDL3Input.h"

#include <SDL3/SDL.h>

#include <cfloat>

namespace Orkige
{
	namespace
	{
		//! SDL3 keycode/scancode -> ImGuiKey (same mapping the reference
		//! imgui_impl_sdl3 backend uses, condensed)
		ImGuiKey translateKey(SDL_Keycode key, SDL_Scancode scancode)
		{
			// printable keys that SDL reports layout-dependent: map the common ones
			if (key >= SDLK_A && key <= SDLK_Z)
				return static_cast<ImGuiKey>(ImGuiKey_A + (key - SDLK_A));
			if (key >= SDLK_1 && key <= SDLK_9)
				return static_cast<ImGuiKey>(ImGuiKey_1 + (key - SDLK_1));
			if (key == SDLK_0)
				return ImGuiKey_0;
			if (key >= SDLK_F1 && key <= SDLK_F12)
				return static_cast<ImGuiKey>(ImGuiKey_F1 + (key - SDLK_F1));
			if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
				return static_cast<ImGuiKey>(ImGuiKey_Keypad1 + (scancode - SDL_SCANCODE_KP_1));

			switch (key)
			{
			case SDLK_TAB: return ImGuiKey_Tab;
			case SDLK_LEFT: return ImGuiKey_LeftArrow;
			case SDLK_RIGHT: return ImGuiKey_RightArrow;
			case SDLK_UP: return ImGuiKey_UpArrow;
			case SDLK_DOWN: return ImGuiKey_DownArrow;
			case SDLK_PAGEUP: return ImGuiKey_PageUp;
			case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
			case SDLK_HOME: return ImGuiKey_Home;
			case SDLK_END: return ImGuiKey_End;
			case SDLK_INSERT: return ImGuiKey_Insert;
			case SDLK_DELETE: return ImGuiKey_Delete;
			case SDLK_BACKSPACE: return ImGuiKey_Backspace;
			case SDLK_SPACE: return ImGuiKey_Space;
			case SDLK_RETURN: return ImGuiKey_Enter;
			case SDLK_ESCAPE: return ImGuiKey_Escape;
			case SDLK_APOSTROPHE: return ImGuiKey_Apostrophe;
			case SDLK_COMMA: return ImGuiKey_Comma;
			case SDLK_MINUS: return ImGuiKey_Minus;
			case SDLK_PERIOD: return ImGuiKey_Period;
			case SDLK_SLASH: return ImGuiKey_Slash;
			case SDLK_SEMICOLON: return ImGuiKey_Semicolon;
			case SDLK_EQUALS: return ImGuiKey_Equal;
			case SDLK_LEFTBRACKET: return ImGuiKey_LeftBracket;
			case SDLK_BACKSLASH: return ImGuiKey_Backslash;
			case SDLK_RIGHTBRACKET: return ImGuiKey_RightBracket;
			case SDLK_GRAVE: return ImGuiKey_GraveAccent;
			case SDLK_CAPSLOCK: return ImGuiKey_CapsLock;
			case SDLK_SCROLLLOCK: return ImGuiKey_ScrollLock;
			case SDLK_NUMLOCKCLEAR: return ImGuiKey_NumLock;
			case SDLK_PRINTSCREEN: return ImGuiKey_PrintScreen;
			case SDLK_PAUSE: return ImGuiKey_Pause;
			case SDLK_KP_0: return ImGuiKey_Keypad0;
			case SDLK_KP_PERIOD: return ImGuiKey_KeypadDecimal;
			case SDLK_KP_DIVIDE: return ImGuiKey_KeypadDivide;
			case SDLK_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
			case SDLK_KP_MINUS: return ImGuiKey_KeypadSubtract;
			case SDLK_KP_PLUS: return ImGuiKey_KeypadAdd;
			case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
			case SDLK_KP_EQUALS: return ImGuiKey_KeypadEqual;
			case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
			case SDLK_LSHIFT: return ImGuiKey_LeftShift;
			case SDLK_LALT: return ImGuiKey_LeftAlt;
			case SDLK_LGUI: return ImGuiKey_LeftSuper;
			case SDLK_RCTRL: return ImGuiKey_RightCtrl;
			case SDLK_RSHIFT: return ImGuiKey_RightShift;
			case SDLK_RALT: return ImGuiKey_RightAlt;
			case SDLK_RGUI: return ImGuiKey_RightSuper;
			case SDLK_APPLICATION: return ImGuiKey_Menu;
			default: return ImGuiKey_None;
			}
		}

		//! SDL mouse button index -> ImGui mouse button index
		int translateMouseButton(Uint8 button)
		{
			switch (button)
			{
			case SDL_BUTTON_LEFT: return 0;
			case SDL_BUTTON_RIGHT: return 1;
			case SDL_BUTTON_MIDDLE: return 2;
			case SDL_BUTTON_X1: return 3;
			case SDL_BUTTON_X2: return 4;
			default: return -1;
			}
		}

		void updateKeyModifiers(ImGuiIO& io, SDL_Keymod mods)
		{
			io.AddKeyEvent(ImGuiMod_Ctrl, (mods & SDL_KMOD_CTRL) != 0);
			io.AddKeyEvent(ImGuiMod_Shift, (mods & SDL_KMOD_SHIFT) != 0);
			io.AddKeyEvent(ImGuiMod_Alt, (mods & SDL_KMOD_ALT) != 0);
			io.AddKeyEvent(ImGuiMod_Super, (mods & SDL_KMOD_GUI) != 0);
		}
	}

	ImGuiSDL3Input::ImGuiSDL3Input(SDL_Window* window)
		: mWindow(window)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.BackendPlatformName = "OrkigeImGuiSDL3Input";
		// SDL3 delivers SDL_EVENT_TEXT_INPUT only while text input is started
		SDL_StartTextInput(mWindow);
	}

	ImGuiSDL3Input::~ImGuiSDL3Input()
	{
		SDL_StopTextInput(mWindow);
	}

	void ImGuiSDL3Input::newFrame(float renderTargetWidth, float renderTargetHeight)
	{
		ImGuiIO& io = ImGui::GetIO();
		// Note: Ogre::ImGuiOverlay::NewFrame() re-derives io.DisplaySize from
		// the OverlayManager viewport, so this mainly keeps the first frame
		// (before anything rendered) and the mouse scale coherent.
		io.DisplaySize = ImVec2(renderTargetWidth, renderTargetHeight);
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

		// SDL mouse coordinates are in window points; ImGui works in render
		// target pixels (what the overlay projects with). Identical on
		// non-retina; differs when the GL surface gets a scaled backing store.
		int windowWidth = 0;
		int windowHeight = 0;
		SDL_GetWindowSize(mWindow, &windowWidth, &windowHeight);
		mMouseScaleX = (windowWidth > 0 && renderTargetWidth > 0.0f)
			? renderTargetWidth / static_cast<float>(windowWidth) : 1.0f;
		mMouseScaleY = (windowHeight > 0 && renderTargetHeight > 0.0f)
			? renderTargetHeight / static_cast<float>(windowHeight) : 1.0f;
	}

	bool ImGuiSDL3Input::processEvent(SDL_Event const& event)
	{
		ImGuiIO& io = ImGui::GetIO();
		switch (event.type)
		{
		case SDL_EVENT_MOUSE_MOTION:
			io.AddMousePosEvent(event.motion.x * mMouseScaleX,
				event.motion.y * mMouseScaleY);
			return io.WantCaptureMouse;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
		{
			const int button = translateMouseButton(event.button.button);
			if (button >= 0)
			{
				io.AddMousePosEvent(event.button.x * mMouseScaleX,
					event.button.y * mMouseScaleY);
				io.AddMouseButtonEvent(button,
					event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
			}
			return io.WantCaptureMouse;
		}

		case SDL_EVENT_MOUSE_WHEEL:
		{
			float wheelX = event.wheel.x;
			float wheelY = event.wheel.y;
			if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
			{
				wheelX = -wheelX;
				wheelY = -wheelY;
			}
			io.AddMouseWheelEvent(wheelX, wheelY);
			return io.WantCaptureMouse;
		}

		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		{
			updateKeyModifiers(io, event.key.mod);
			const ImGuiKey key = translateKey(event.key.key, event.key.scancode);
			if (key != ImGuiKey_None)
			{
				io.AddKeyEvent(key, event.type == SDL_EVENT_KEY_DOWN);
			}
			return io.WantCaptureKeyboard;
		}

		case SDL_EVENT_TEXT_INPUT:
			io.AddInputCharactersUTF8(event.text.text);
			return io.WantTextInput;

		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			return false;

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			io.AddFocusEvent(true);
			return false;

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			io.AddFocusEvent(false);
			return false;

		default:
			return false;
		}
	}
}
