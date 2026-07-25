/********************************************************************
	created:	Friday 2026/07/25 at 16:00
	filename: 	WorldTextComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __WorldTextComponent_h__25_7_2026__16_00_00__
#define __WorldTextComponent_h__25_7_2026__16_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/SpriteBatch.h"
#include "engine_gui/WorldTextLayout.h"

#include <vector>

namespace Orkige
{
	class UiFont;

	//! @brief text placed IN the 3D scene - floating damage numbers, name tags,
	//! world-space labels - as camera-facing (or transform-oriented) glyph quads
	//! @remarks A composition of two existing systems, not a new mechanism:
	//! GLYPHS come from the gui's baked font page (engine_gui/FontAtlas / UiFont -
	//! kerning, metrics and lazy CJK paging included), so world text SHARES the
	//! engine-default font page rather than baking a second one; the QUADS are
	//! CPU-billboarded camera-facing textured quads through the facade SpriteBatch
	//! (world-space Vec3 corners, billboard axes from the window camera's view
	//! matrix) - the SAME recipe ParticleComponent::writeQuads3D uses for 3D
	//! particles, with glyph UVs in place of a particle atlas frame. The layout
	//! itself is the pure, headless-tested WorldTextLayout.
	//!
	//! This is AUTHORED content (like SpriteComponent / ParticleComponent art),
	//! NOT a runtime-only effect: it renders in edit mode and in Game Preview /
	//! Play (no editor-only visibility bit). The reflected look - `text`, `size`
	//! (world units per line height), `colour`, `billboard` - rides the ONE
	//! property registry (inspector, serialization, debug protocol, MCP). `text`
	//! is the LITERAL string: a script sets self.worldtext.text = loc("key"), so
	//! the component carries no localisation coupling.
	//!
	//! TWO placement modes:
	//!  - billboard = true (default): the glyph quads are world-space and re-faced
	//!    to the camera each frame (position from the transform, orientation from
	//!    the camera) - the batch hangs off the world root, like the 3D particles.
	//!  - billboard = false: the quads lie in the object's LOCAL XY plane and the
	//!    transform orients them (the batch is attached to the transform node).
	//! Text LAYOUT rebuilds only on a text/size change; a moving camera or object
	//! only re-runs the cheap per-frame quad refresh (allocation-free steady
	//! state). The text is CENTER-anchored on the node (v1; multi-line is
	//! center-justified).
	//!
	//! TRANSPARENCY: like SpriteComponent, the glyph quads are alpha-blended and
	//! depth-TESTED but not depth-sorted against other transparent 3D content, so
	//! two overlapping world-text labels (or a label behind other alpha-blended
	//! sprites) can resolve in submission order rather than strict back-to-front.
	//! Keep labels from overlapping when the ordering matters.
	class ORKIGE_ENGINE_DLL WorldTextComponent : public GameObjectComponent
	{
		OOBJECT(WorldTextComponent, GameObjectComponent)
		//--- Variables ---------------------------------------------
	public:
	protected:
		optr<SpriteBatch>		mBatch;			//!< the glyph batch or NULL (created lazily)
		String					mText;			//!< the literal text (may be UTF-8/multi-line)
		float					mSize;			//!< world units per line height
		Color					mColour;		//!< text colour (multiplied over the glyph coverage)
		bool					mBillboard;		//!< face the camera (true) or lie in the local XY plane
		bool					mVisible;		//!< own visibility flag (AND-ed with owner active)
		WorldTextLayout::Result	mLayout;		//!< the built glyph quads (text-local 2D)
		bool					mLayoutDirty;	//!< text/size changed - a rebuild is owed
		bool					mTicked;		//!< a runtime ticks us (defer uploads to onUpdate)
		bool					mFreshBuild;	//!< a synchronous submit mapped the batch; defer the next tick's dynamic upload one frame (the next backend forbids two maps between renders)
		std::vector<SpriteBatch::Vertex>	mVertexScratch;	//!< reused per-refresh vertex buffer
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		WorldTextComponent();
		//! destructor
		virtual ~WorldTextComponent();

		//--- reflected look (the ONE property registry surface) ---
		//! set the displayed text (literal; '\n' opens a new line). A script
		//! passes loc("key") here - the component stays localisation-free.
		void setText(String const & text);
		//! @see WorldTextComponent::mText
		inline String const & getText() const { return this->mText; }
		//! set the line height in WORLD units (rebuilds the layout)
		void setSize(float size);
		//! @see WorldTextComponent::mSize
		inline float getSize() const { return this->mSize; }
		//! set the text colour (four floats - the Lua/native surface)
		void setColour(float red, float green, float blue, float alpha);
		//! reflected colour setter (Color -> the four-float setColour)
		inline void setColourValue(Color const & colour)
		{
			this->setColour(colour.r, colour.g, colour.b, colour.a);
		}
		//! @see WorldTextComponent::mColour
		inline Color const & getColour() const { return this->mColour; }
		//! camera-facing (true) or transform-oriented in the local XY plane (false)
		void setBillboard(bool billboard);
		//! @see WorldTextComponent::mBillboard
		inline bool getBillboard() const { return this->mBillboard; }
		//! show/hide the text (its scene visibility)
		void setTextVisible(bool visible);
		//! @see WorldTextComponent::mVisible
		inline bool isTextVisible() const { return this->mVisible; }

		//--- introspection (selfcheck / tools) ---
		//! how many glyph quads the current text lays out to (= inked, non-space
		//! glyph count). 0 until a text is set / a font page exists.
		inline std::size_t getQuadCount() const { return this->mLayout.quads.size(); }
		//! quads actually submitted to the live batch (0 when detached)
		std::size_t getSubmittedQuadCount() const;

		//--- editor drive ---
		//! @brief re-face the billboard text to an explicit camera basis and
		//! resubmit (the editor per-frame seam calls this with the preview/scene
		//! camera axes so edit-mode text faces the EDITOR camera). A no-op for
		//! non-billboard text (the transform already orients it). Allocation-free.
		void faceCamera(Vec3 const & cameraRight, Vec3 const & cameraUp);
	protected:
		//! component override - build the batch when a world is up
		virtual void onAdd();
		//! component override - drop the batch
		virtual void onRemove();
		//! deactivated GameObjects hide their text (the visible flag is kept)
		virtual void onSetActive(bool activeInHierarchy);
		//! @brief the per-frame re-face site under a ticking runtime (billboard
		//! text follows the moving camera/object here). Dormant unless a runtime
		//! ticks GameObjects; the editor re-faces through faceCamera instead.
		virtual void onUpdateComponent(float deltaTime);

		//! create the glyph batch on the shared font page and attach it to the
		//! right node (root for billboard, the transform node otherwise); idempotent
		void ensureBatch();
		//! (re)run the pure layout from text+size against the shared font, then
		//! flush any lazily-baked glyphs to the page. Clears mLayoutDirty.
		void rebuildLayout();
		//! @brief map the laid-out glyph quads onto the current placement basis
		//! and submit them (rebuilds the layout first if dirty). @p fresh forces
		//! a full-rebuild upload (a fresh batch buffer, safe to repeat between
		//! renders - the synchronous editor/pre-tick path); false rides the
		//! dynamic in-place upload (the once-per-frame runtime path).
		void refreshQuads(bool fresh);
		//! the current billboard axes from the window camera (identity if none)
		void windowCameraAxes(Vec3 & outRight, Vec3 & outUp) const;
		//! turn mLayout into world/local vertices for `cameraRight/Up` and submit
		//! (@p fresh @see refreshQuads)
		void submitQuads(Vec3 const & cameraRight, Vec3 const & cameraUp,
			bool fresh);
		//! attach the batch to the placement node matching mBillboard
		void attachBatch();
		//! apply the EFFECTIVE visibility (own flag AND owner active) to the batch
		void applyVisibility();
		//--- SERIALIZATION (reflection-driven; all fields are reflected) ---
		//! save the reflected block (text/size/colour/billboard/visible)
		virtual void save(optr<IArchive> const & ar);
		//! load it and (re)build on the first refresh
		virtual void load(optr<IArchive> const & ar);
	private:
		//! @brief the shared, engine-default font page for world text (baked
		//! ONCE, lazily, from the engine-default TTF via engine_gui/FontAtlas -
		//! the SAME baker the gui uses). @returns the baked UiFont or NULL when
		//! no render system / font resource is available (headless). The page
		//! also carries the lazily-paged CJK glyphs.
		static UiFont const * sharedFont();
		//! resource name of the shared font page (the batch binds by name)
		static String sharedFontTextureName();
		//! upload any glyphs baked-on-demand since the last flush (CJK paging);
		//! called after a layout rebuild so new glyphs reach the GPU
		static void flushSharedFont();
	};
}

#endif //__WorldTextComponent_h__25_7_2026__16_00_00__
