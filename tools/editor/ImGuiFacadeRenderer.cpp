// ImGuiFacadeRenderer - Dear ImGui drawn through the engine_render facade
// (see header for the design).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "ImGuiFacadeRenderer.h"

#include <engine_render/RenderSystem.h>
#include <engine_render/RenderTexture.h>

#include <SDL3/SDL_log.h>

#include <cmath>

namespace Orkige
{
	ImGuiFacadeRenderer::~ImGuiFacadeRenderer()
	{
		this->mLayer.reset();
		if(this->mEntries.empty())
		{
			return;	// never initialised
		}
		if(RenderSystem* render = RenderSystem::get())
		{
			render->destroyTexture2D(FONT_ATLAS_NAME);
		}
	}
	//---------------------------------------------------------
	bool ImGuiFacadeRenderer::initialise(int zOrder)
	{
		RenderSystem* render = RenderSystem::get();
		if(!render)
		{
			SDL_Log("ImGuiFacadeRenderer: no render system - initialise "
				"after Engine::setup");
			return false;
		}
		// the legacy imgui texture protocol (no RendererHasTextures):
		// build the atlas once, upload it, register the TexID. Fonts must
		// be fully configured before this runs - late additions would need
		// an atlas re-upload nothing triggers (same rule ImGuiOverlay had).
		ImGuiIO& io = ImGui::GetIO();
		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		if(!pixels || width <= 0 || height <= 0)
		{
			SDL_Log("ImGuiFacadeRenderer: font atlas build failed");
			return false;
		}
		if(!render->createTexture2D(FONT_ATLAS_NAME, pixels,
			static_cast<unsigned int>(width),
			static_cast<unsigned int>(height)))
		{
			SDL_Log("ImGuiFacadeRenderer: font atlas upload failed");
			return false;
		}
		this->mEntries.push_back(Entry{ FONT_ATLAS_NAME, {} });
		io.Fonts->SetTexID(static_cast<ImTextureID>(this->mEntries.size()));
		this->mLayer = render->createDrawLayer2D(zOrder);
		return this->mLayer != nullptr;
	}
	//---------------------------------------------------------
	ImTextureID ImGuiFacadeRenderer::textureIdFor(
		optr<RenderTexture> const & texture)
	{
		if(!texture)
		{
			return 0;
		}
		for(std::size_t each = 0; each < this->mEntries.size(); ++each)
		{
			if(this->mEntries[each].renderTexture.lock() == texture)
			{
				return static_cast<ImTextureID>(each + 1);
			}
		}
		this->mEntries.push_back(Entry{ String(), texture });
		return static_cast<ImTextureID>(this->mEntries.size());
	}
	//---------------------------------------------------------
	ImTextureID ImGuiFacadeRenderer::textureIdForResource(
		String const & resourceName)
	{
		if(resourceName.empty())
		{
			return 0;
		}
		// dedup by name: a named entry with no render target is a resource-
		// name binding (the font atlas and every thumbnail live here)
		for(std::size_t each = 0; each < this->mEntries.size(); ++each)
		{
			if(!this->mEntries[each].renderTexture.lock() &&
				this->mEntries[each].resourceName == resourceName)
			{
				return static_cast<ImTextureID>(each + 1);
			}
		}
		this->mEntries.push_back(Entry{ resourceName, {} });
		return static_cast<ImTextureID>(this->mEntries.size());
	}
	//---------------------------------------------------------
	void ImGuiFacadeRenderer::render(ImDrawData const * drawData)
	{
		if(!this->mLayer)
		{
			return;
		}
		// immediate-mode consumer: drop last frame's batches, resubmit
		this->mLayer->clear();
		if(!drawData || drawData->CmdListsCount == 0)
		{
			return;
		}
		for(int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
		{
			ImDrawList const * list = drawData->CmdLists[listIndex];
			// ImDrawVert -> facade Vertex2D (positions are already drawable
			// pixels: io.DisplaySize is the drawable, FramebufferScale 1)
			this->mVertexScratch.clear();
			this->mVertexScratch.reserve(
				static_cast<std::size_t>(list->VtxBuffer.Size));
			for(int vertex = 0; vertex < list->VtxBuffer.Size; ++vertex)
			{
				ImDrawVert const & in = list->VtxBuffer.Data[vertex];
				// IM_COL32 packs R in the lowest byte, A in the highest
				const float inv255 = 1.0f / 255.0f;
				this->mVertexScratch.push_back(DrawLayer2D::Vertex2D(
					in.pos.x, in.pos.y, in.uv.x, in.uv.y,
					Color(((in.col >> IM_COL32_R_SHIFT) & 0xFF) * inv255,
						((in.col >> IM_COL32_G_SHIFT) & 0xFF) * inv255,
						((in.col >> IM_COL32_B_SHIFT) & 0xFF) * inv255,
						((in.col >> IM_COL32_A_SHIFT) & 0xFF) * inv255)));
			}
			for(int commandIndex = 0; commandIndex < list->CmdBuffer.Size;
				++commandIndex)
			{
				ImDrawCmd const & command = list->CmdBuffer.Data[commandIndex];
				if(command.UserCallback)
				{
					// ResetRenderState is a backend hint (nothing to reset
					// here); other callbacks run as the API promises
					if(command.UserCallback != ImDrawCallback_ResetRenderState)
					{
						command.UserCallback(list, &command);
					}
					continue;
				}
				if(command.ElemCount == 0 || command.VtxOffset != 0)
				{
					// VtxOffset stays 0 while the backend does not declare
					// RendererHasVtxOffset (imgui splits lists instead)
					continue;
				}
				// clip rect -> pixel scissor (the layer clips analytically)
				DrawLayer2D::ScissorRect scissor;
				scissor.left = static_cast<int>(
					std::floor(command.ClipRect.x));
				scissor.top = static_cast<int>(
					std::floor(command.ClipRect.y));
				scissor.width = static_cast<int>(
					std::ceil(command.ClipRect.z)) - scissor.left;
				scissor.height = static_cast<int>(
					std::ceil(command.ClipRect.w)) - scissor.top;
				if(scissor.width <= 0 || scissor.height <= 0)
				{
					continue;
				}
				// resolve the registered texture (0/unknown = untextured)
				const std::size_t entryIndex =
					static_cast<std::size_t>(command.GetTexID());
				Entry const * entry =
					(entryIndex >= 1 && entryIndex <= this->mEntries.size())
					? &this->mEntries[entryIndex - 1] : nullptr;
				unsigned short const * indices =
					list->IdxBuffer.Data + command.IdxOffset;
				optr<RenderTexture> renderTexture;
				if(entry && (renderTexture = entry->renderTexture.lock()))
				{
					this->mLayer->addTriangles(renderTexture,
						this->mVertexScratch.data(),
						this->mVertexScratch.size(),
						indices, command.ElemCount, &scissor);
				}
				else
				{
					this->mLayer->addTriangles(
						entry ? entry->resourceName : String(),
						this->mVertexScratch.data(),
						this->mVertexScratch.size(),
						indices, command.ElemCount, &scissor);
				}
			}
		}
	}
}
