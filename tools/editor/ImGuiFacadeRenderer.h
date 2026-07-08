// ImGuiFacadeRenderer - Dear ImGui drawn through the engine_render facade.
//
// Replaces the classic-only Ogre::ImGuiOverlay integration: ImGui draw data
// is textured triangles + scissor rects, which is exactly the DrawLayer2D
// contract (per-batch texture binding, analytic scissor clipping, painter
// ordering). One renderer instance owns ONE 2D layer; every frame the
// editor calls ImGui::Render() and hands the draw data to render(), which
// resubmits it as layer batches. Because DrawLayer2D exists on every render
// backend, the editor UI now runs unchanged on classic OGRE and Ogre-Next.
//
// Texture model (ImTextureID = registry index + 1):
// - the font atlas uploads once through RenderSystem::createTexture2D (raw
//   RGBA8 pixels under a resource name) and registers as the atlas TexID -
//   the legacy imgui texture protocol, same as ImGuiOverlay used;
// - facade RenderTextures (the Scene panel's RTT) register via
//   textureIdFor(): batches carry the HANDLE, the backend binds the
//   target's CURRENT texture per draw - resize-by-recreate is safe and the
//   id stays stable for the target's whole lifetime.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#ifndef __ImGuiFacadeRenderer_h__9_7_2026__10_00_00__
#define __ImGuiFacadeRenderer_h__9_7_2026__10_00_00__

#include <engine_render/RenderPrerequisites.h>
#include <engine_render/DrawLayer2D.h>
#include <core_util/String.h>

#include <imgui.h>

#include <vector>

namespace Orkige
{
	class ImGuiFacadeRenderer
	{
	public:
		//! resource name the font atlas uploads under
		static constexpr char const * FONT_ATLAS_NAME = "ImGui/FontAtlas";

		//! frees the font atlas texture + the 2D layer while the render
		//! system is still alive (strict backends like Vulkan assert on
		//! GPU allocations outliving teardown)
		~ImGuiFacadeRenderer();

		//! @brief build + upload the font atlas and create the 2D layer.
		//! Call once after ImGui fonts were configured and the render
		//! system exists (post Engine::setup); sets the atlas TexID.
		//! @param zOrder the layer's zOrder among 2D layers (the editor UI
		//! wants to composite over everything - fastgui HUDs never run
		//! inside the editor window, but keep it high anyway)
		bool initialise(int zOrder);

		//! @brief stable ImGui texture id for a facade render texture
		//! (registers it on the first call). The Scene panel feeds this to
		//! ImGui::Image; dead targets render untextured (honest fallback).
		ImTextureID textureIdFor(optr<RenderTexture> const & texture);

		//! @brief resubmit this frame's ImGui draw data into the 2D layer
		//! (call after ImGui::Render(), before the engine renders the frame)
		void render(ImDrawData const * drawData);

	private:
		//! one registered texture: EITHER a plain resource name (font
		//! atlas) OR a facade render target (weak - the renderer must not
		//! keep dead panels' targets alive; batches hold their own ref
		//! for the frame in flight)
		struct Entry
		{
			String				resourceName;
			woptr<RenderTexture>	renderTexture;
		};

		optr<DrawLayer2D>	mLayer;
		std::vector<Entry>	mEntries;		//!< ImTextureID = index + 1
		std::vector<DrawLayer2D::Vertex2D>	mVertexScratch;	//!< per-list conversion buffer (capacity kept)
	};
}

#endif //__ImGuiFacadeRenderer_h__9_7_2026__10_00_00__
