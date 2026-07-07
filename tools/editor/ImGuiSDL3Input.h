// ImGuiSDL3Input - translates SDL3 events into Dear ImGui IO events.
//
// Deliberately hand-rolled instead of imgui's own SDL3 platform backend:
// pulling the imgui[sdl3-binding] vcpkg feature would make the OGRE port
// build depend on SDL3 and break it. Only the platform side is needed anyway,
// rendering is done by Ogre::ImGuiOverlay.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <imgui.h>

struct SDL_Window;
union SDL_Event;

namespace Orkige
{
	//! Feeds SDL3 events into the ImGui IO queue and answers whether ImGui
	//! wants the event for itself (so the app can decide not to forward it
	//! into the engine InputManager).
	class ImGuiSDL3Input
	{
	public:
		//! requires a live ImGui context (create the Ogre::ImGuiOverlay first)
		explicit ImGuiSDL3Input(SDL_Window* window);
		~ImGuiSDL3Input();

		ImGuiSDL3Input(ImGuiSDL3Input const&) = delete;
		ImGuiSDL3Input& operator=(ImGuiSDL3Input const&) = delete;

		//! Call once per frame before Ogre::ImGuiOverlay::NewFrame().
		//! Refreshes io.DisplaySize / io.DisplayFramebufferScale and the
		//! SDL-window-points -> render-target-pixels mouse scale from the
		//! current render target size (in pixels).
		void newFrame(float renderTargetWidth, float renderTargetHeight);

		//! Translate one SDL event into ImGui IO events.
		//! @returns true if ImGui wants to capture the event (mouse events
		//! while io.WantCaptureMouse, key/text events while
		//! io.WantCaptureKeyboard/WantTextInput) - do not forward those to
		//! the engine input pipeline.
		bool processEvent(SDL_Event const& event);

	private:
		SDL_Window* mWindow;
		float mMouseScaleX = 1.0f;
		float mMouseScaleY = 1.0f;
	};
}
