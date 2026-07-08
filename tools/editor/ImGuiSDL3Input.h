// ImGuiSDL3Input - translates SDL3 events into Dear ImGui IO events.
//
// Deliberately hand-rolled instead of imgui's own SDL3 platform backend:
// pulling the imgui[sdl3-binding] vcpkg feature would make the OGRE port
// build depend on SDL3 and break it. Only the platform side is needed anyway,
// rendering is done by ImGuiFacadeRenderer (DrawLayer2D on either backend).
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
		//! requires a live ImGui context (ImGui::CreateContext first)
		explicit ImGuiSDL3Input(SDL_Window* window);
		~ImGuiSDL3Input();

		ImGuiSDL3Input(ImGuiSDL3Input const&) = delete;
		ImGuiSDL3Input& operator=(ImGuiSDL3Input const&) = delete;

		//! Call once per frame before ImGui::NewFrame(). Refreshes
		//! io.DisplaySize / io.DisplayFramebufferScale / io.DeltaTime and
		//! the SDL-window-points -> render-target-pixels mouse scale from
		//! the current render target size (in pixels).
		void newFrame(float renderTargetWidth, float renderTargetHeight);

		//! Translate one SDL event into ImGui IO events.
		//! @returns true if ImGui wants to capture the event (mouse events
		//! while io.WantCaptureMouse, key/text events while
		//! io.WantCaptureKeyboard/WantTextInput) - do not forward those to
		//! the engine input pipeline.
		bool processEvent(SDL_Event const& event);

		//! @brief mouse capture for the Scene panel's fly mode.
		//! true: switch the window into SDL relative mouse mode (cursor
		//! hidden + captured, motion arrives as raw xrel/yrel counts which
		//! accumulate here - see consumeRelativeDelta) and remember the
		//! cursor position; ImGui stops receiving absolute mouse positions
		//! so the UI cursor state stays frozen instead of fighting the
		//! capture. false: restore the cursor to the remembered pre-capture
		//! position (SDL_WarpMouseInWindow, warp-before-disable per the SDL
		//! docs) and re-sync ImGui's mouse position. Idempotent; main
		//! thread only (SDL requirement). If SDL cannot enter relative mode
		//! (logged), the deltas still accumulate from the motion events'
		//! xrel/yrel - the look keeps working, only the cursor stays
		//! visible/unconstrained.
		void setRelativeMode(bool enabled);

		//! is the window in fly-mode relative mouse capture?
		bool isRelativeMode() const { return mRelativeMode; }

		//! @brief fetch-and-clear the relative motion accumulated since the
		//! last call (raw SDL counts - NOT scaled by the render-target/
		//! backing-store factor, unlike the absolute positions ImGui gets).
		void consumeRelativeDelta(float& deltaX, float& deltaY);

	private:
		SDL_Window* mWindow;
		float mMouseScaleX = 1.0f;
		float mMouseScaleY = 1.0f;
		unsigned long long mLastFrameCounter = 0;	//!< SDL performance counter of the last newFrame (io.DeltaTime)
		bool mRelativeMode = false;		//!< fly-mode capture active?
		float mRelativeDeltaX = 0.0f;	//!< accumulated xrel (raw counts)
		float mRelativeDeltaY = 0.0f;	//!< accumulated yrel (raw counts)
		float mRestoreMouseX = 0.0f;	//!< cursor position (window points)
		float mRestoreMouseY = 0.0f;	//!< remembered for capture exit
	};
}
