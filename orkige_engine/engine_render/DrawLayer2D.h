/********************************************************************
	created:	Wednesday 2026/07/08 at 22:00
	filename: 	DrawLayer2D.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __DrawLayer2D_h__8_7_2026__22_00_00__
#define __DrawLayer2D_h__8_7_2026__22_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>

namespace Orkige
{
	//! @brief screen-space dynamic 2D drawing: z-ordered layers of
	//! textured/coloured triangle batches in pixel coordinates
	//! @remarks THE facade home for every dynamic 2D overlay (gui
	//! HUDs today, the editor's ImGui next): a DrawLayer2D is a
	//! retained list of triangle BATCHES that the backend composites over
	//! the finished 3D frame of the main window every frame, until the
	//! caller replaces them (clear() + addTriangles()). Immediate-mode
	//! consumers (ImGui) resubmit every frame; retained consumers
	//! (gui) resubmit only when their content went dirty - the
	//! backend renders whatever the layer currently holds either way.
	//!
	//! Coordinate contract: positions are PIXELS in main-window drawable
	//! space, origin top-left, +x right, +y down (the gui/ImGui
	//! convention); UVs are the usual 0..1 with v running top-down;
	//! colours are straight (non-premultiplied) RGBA.
	//!
	//! Render contract (identical on every backend - the selfcheck's 2D
	//! pattern pixel-verifies it):
	//! - layers render over the 3D scene in ascending zOrder; equal
	//!   zOrder renders in layer-creation order,
	//! - batches of one layer render in submission order,
	//! - everything is alpha-blended (SRC_ALPHA/ONE_MINUS_SRC_ALPHA),
	//!   depth-ignored and two-sided,
	//! - textures sample point-filtered and clamped (crisp pixel UI -
	//!   the pixel-UI atlas rule; a filtering knob can join with ImGui if
	//!   its font atlas wants linear),
	//! - scissor rects clip exactly (implemented as analytic triangle
	//!   clipping at submission time, so every backend behaves
	//!   identically without touching render-state plumbing),
	//! - the layer only ever composites onto the MAIN WINDOW - offscreen
	//!   RenderTextures and other cameras never see 2D layers.
	//!
	//! Textures bind per batch BY RESOURCE NAME (resolved through the
	//! resource system across all groups, like SpriteQuad); an empty name
	//! draws untextured vertex colours. Textures that exist only as
	//! backend objects under a name - RenderSystem::createTexture2D's
	//! raw-pixel uploads (the ImGui font atlas) - resolve too: the
	//! backends probe the texture registry before the resource system.
	//! Offscreen RenderTextures bind through the dedicated addTriangles
	//! overload taking the facade handle (the editor's Scene panel inside
	//! ImGui): the backend re-resolves the target's CURRENT texture, so
	//! resize-by-recreate never leaves a batch pointing at a dead
	//! incarnation.
	//!
	//! Backend mapping (whole class): classic = RenderQueueListener after
	//! RENDER_QUEUE_OVERLAY + SceneManager::manualRender of dynamic
	//! vertex buffers with generated unlit materials (Gorilla's proven
	//! machinery, generalized); next = one v2 ManualObject per batch +
	//! generated HlmsUnlit datablocks in a dedicated late render-queue
	//! band drawn by a second scene pass (pixel-space ortho camera) of
	//! the window workspace; filament = a dedicated UI View with an ortho
	//! camera + dynamic vertex buffers and prebuilt unlit .filamat.
	class ORKIGE_ENGINE_DLL DrawLayer2D
	{
		//--- Types -------------------------------------------------
	public:
		//! one 2D vertex: pixel-space position, 0..1 UV, straight RGBA
		struct ORKIGE_ENGINE_DLL Vertex2D
		{
			Real	x, y;		//!< position in window pixels (origin top-left, +y down)
			Real	u, v;		//!< texture coordinates (v runs top-down)
			Color	colour;		//!< straight RGBA, components 0..1

			Vertex2D() : x(0), y(0), u(0), v(0), colour(1, 1, 1, 1) {}
			Vertex2D(Real _x, Real _y, Real _u, Real _v, Color const & _colour)
				: x(_x), y(_y), u(_u), v(_v), colour(_colour) {}
		};
		//! pixel-space clip rectangle (window coordinates, top-left origin)
		struct ORKIGE_ENGINE_DLL ScissorRect
		{
			int		left = 0;
			int		top = 0;
			int		width = 0;	//!< <= 0 clips everything away
			int		height = 0;	//!< <= 0 clips everything away
		};
	protected:
		//! backend state - defined only inside the selected backend
		struct Impl;
		//! the selected backend's plumbing (@see RenderPrerequisites.h)
		friend struct RenderBackend;
	private:
		//--- Variables ---------------------------------------------
	protected:
		Impl*	mImpl;	//!< backend guts
		//--- Methods -----------------------------------------------
	public:
		//! destroying the handle removes the layer from the screen (RAII,
		//! like every facade handle)
		~DrawLayer2D();

		//--- layer control ---
		//! hide/show the whole layer (batches stay submitted)
		//! map: classic=listener skips the layer | next=per-batch MovableObject visibility | filament=View skip
		void setVisible(bool visible);
		bool isVisible() const;
		//! layers composite in ascending zOrder (ties: creation order)
		//! map: classic=listener draw order | next=painter depth in the UI pass | filament=draw order
		void setZOrder(int zOrder);
		int getZOrder() const;

		//--- batch submission (retained until the next clear) ---
		//! drop all submitted batches (the start of a resubmission)
		void clear();
		//! @brief append one batch of triangles
		//! @param textureName resource name of the texture to bind (any
		//! resource group; empty = untextured vertex colours). Unresolvable
		//! names log once per name and draw untextured (honest fallback).
		//! @param vertices pixel-space vertices (copied)
		//! @param indices optional: 3 per triangle into vertices; NULL
		//! treats vertices as a plain triangle list (vertexCount % 3 == 0)
		//! @param scissor optional pixel clip rect; NULL = no clipping
		void addTriangles(String const & textureName,
			Vertex2D const * vertices, size_t vertexCount,
			unsigned short const * indices = NULL, size_t indexCount = 0,
			ScissorRect const * scissor = NULL);
		//! @brief append one batch textured by an offscreen RenderTexture
		//! (the editor's Scene panel: the RTT drawn inside the ImGui layer)
		//! @param texture the facade target to sample; the batch keeps it
		//! ALIVE until clear()/layer destruction (immediate-mode consumers
		//! resubmit per frame, so nothing outlives its use). The backend
		//! binds the target's CURRENT texture at render time - resizes are
		//! safe. NULL draws untextured vertex colours.
		//! @remarks RenderTexture batches composite OPAQUE, as an exception
		//! to the alpha-blend contract: a target's alpha channel is a
		//! rendering byproduct (content legally writes 0), not transparency
		//! - classic RTTs have no alpha channel at all, so its "blend" is a
		//! replace. Identical rule on every backend.
		//! map: classic=material bound to the live Ogre::TexturePtr per draw | next=per-target HlmsUnlit datablock re-pointed at the live TextureGpu | filament=per-target unlit MaterialInstance
		void addTriangles(optr<RenderTexture> const & texture,
			Vertex2D const * vertices, size_t vertexCount,
			unsigned short const * indices = NULL, size_t indexCount = 0,
			ScissorRect const * scissor = NULL);
	protected:
		//! constructed by the backend (RenderSystem::createDrawLayer2D) only
		DrawLayer2D();
	private:
		DrawLayer2D(DrawLayer2D const &);					// non-copyable
		DrawLayer2D & operator=(DrawLayer2D const &);		// non-copyable
	};
}

#endif //__DrawLayer2D_h__8_7_2026__22_00_00__
