/********************************************************************
	created:	Thursday 2026/07/10 at 12:00
	filename: 	VectorShapeComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorShapeComponent_h__10_7_2026__12_00_00__
#define __VectorShapeComponent_h__10_7_2026__12_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/VectorMesh.h"
#include "engine_util/SceneNodeGuard.h"
#include "core_util/VectorTessellator.h"
#include "core_util/VectorShapeAsset.h"
#include "core_util/SoftBodyDeform.h"

#include <vector>

namespace Orkige
{
	//! @brief a flat-colour organic vector shape in the XY plane - a 2D
	//! building block alongside SpriteComponent
	//! @remarks Structurally a SpriteComponent twin: needs a sibling
	//! TransformComponent, owns a child scene node, references a shape asset
	//! (`.oshape`) by name and shows its tessellated fill on that node. The
	//! shape is loaded and triangulated (VectorTessellator) at load into a
	//! static mesh with a baked alpha-feather edge (portable anti-aliasing -
	//! the engine forces FSAA 0); it renders through the facade VectorMesh,
	//! carrying the same honest 2D rules as SpriteComponent - unlit,
	//! alpha-blended, two-sided, zOrder = painter's-algorithm sorting shared
	//! with sprites. A per-instance tint multiplies the asset's design colours,
	//! so one asset recolours per instance.
	//!
	//! The component keeps the parsed contours (mRegions) AND the built mesh so
	//! the deformable path can perturb them and re-push through the VectorMesh
	//! refill contract with no facade change.
	//!
	//! SOFT BODY (opt-in, `softBody` property): the shape becomes a soft,
	//! deformable organic shape. The rest mesh is tessellated ONCE and skinned to
	//! a handful of control points (@see SoftBodyDeform); per gameplay tick only
	//! the control points move - from wobble springs, a physics-driven
	//! squash/stretch and morph blending - and the deformed vertices upload
	//! through the DYNAMIC VectorMesh::updateVertices fast path. A sibling
	//! RigidBodyComponent drives it: a contact squashes the shape along the
	//! impact and kicks the wobble; the body's velocity stretches it along the
	//! motion. The physics body itself stays a rigid shape - the softness is
	//! purely visual. Like ScriptComponent this is DORMANT unless a runtime ticks
	//! GameObjects (the editor never deforms), so authoring stays static/WYSIWYG.
	class ORKIGE_ENGINE_DLL VectorShapeComponent
		: public GameObjectComponent, public SceneNodeGuard
	{
		OOBJECT(VectorShapeComponent, GameObjectComponent)
		//--- Variables ---------------------------------------------
	public:
		//! zOrder 0 renders in the middle of the sprite queue window; the
		//! clamp range mirrors SpriteQuad::ZORDER_MIN/MAX (shared 2D window)
		static const int ZORDER_MIN;	//!< lowest accepted zOrder (-40)
		static const int ZORDER_MAX;	//!< highest accepted zOrder (+40)
	protected:
		optr<VectorMesh>	mMesh;			//!< the facade mesh or NULL
		String				mShapeName;		//!< ".oshape" resource name or empty
		std::vector<VectorTessellator::Region>	mRegions;	//!< parsed contours (deform seam)
		VectorTessellator::Mesh	mBuilt;		//!< last built mesh (cached geometry)
		Color				mTint;			//!< multiplied over the asset's fill colours (default white)
		float				mScale;			//!< uniform shape scale in world units (default 1)
		float				mEdgeSoftness;	//!< feather width, shape-local (0 = auto from bounds)
		int					mZOrder;		//!< shape sort order (see remarks)
		bool				mVisible;		//!< shape visibility (applied to the scene node)

		//--- soft body (@see the class remarks) ---
		std::vector<VectorShapeAsset::MorphTarget>	mMorphTargets;	//!< parsed morph poses (kept for a deformer rebuild)
		SoftBodyDeform		mDeform;		//!< the fixed-topology deformer (built when soft-body is on)
		SoftBodyDeform::Params	mDeformParams;	//!< live wobble/squash tunables
		bool				mSoftBody;		//!< is the shape a soft, deformable body
		int					mMorphClip;		//!< selected morph target index (-1 = none)
		float				mMorphSpeed;	//!< morph blend speed (phase per second)
		bool				mMorphLoop;		//!< ping-pong the selected morph
		float				mBodyVelX;		//!< cached sibling body velocity (impact magnitude + stretch)
		float				mBodyVelY;
		bool				mDeformDirty;	//!< the deform uploaded geometry last tick (one settle upload owed)
		bool				mDeformFreshBuild;	//!< a setMesh happened; defer the first dynamic upload one frame (the next backend forbids mapping a buffer twice per frame)
		std::vector<VectorMesh::Vertex>	mDeformVertices;	//!< reused upload buffer (fixed tinted colours, moving positions)
		std::vector<VectorTessellator::Point>	mDeformPositions;	//!< reused deformed-position scratch
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		VectorShapeComponent();
		//! destructor
		virtual ~VectorShapeComponent();

		//! @brief load + tessellate an `.oshape` (resolved across ALL resource
		//! groups, AUTODETECT - engine media and project assets both work). A
		//! missing/malformed shape is an error log, not a crash: the shape stays
		//! empty. Replaces any current shape.
		void loadShape(String const & shapeName);
		//! remove the shape mesh (keeps the node)
		void removeShape();
		//! @see VectorShapeComponent::mShapeName
		inline String const & getShapeName() const;
		//! is a shape mesh currently showing
		inline bool hasShape() const;
		//! triangles in the built mesh (0 when empty) - selfcheck/introspection
		std::size_t getTriangleCount() const;

		//! colour tint, multiplied over the asset's fill colours (default white)
		void setTint(float red, float green, float blue, float alpha);
		//! current tint
		inline Color const & getTint() const;
		//! @brief uniform shape scale in world units (applied to the node)
		void setScale(float scale);
		//! @see VectorShapeComponent::mScale
		inline float getScale() const;
		//! @brief feather (soft edge) width in shape-local units; <= 0 derives a
		//! default from the shape bounds. Rebuilds the mesh.
		void setEdgeSoftness(float width);
		//! @see VectorShapeComponent::mEdgeSoftness
		inline float getEdgeSoftness() const;
		//! shape sort order (clamped to ZORDER_MIN..ZORDER_MAX; see remarks)
		void setZOrder(int zOrder);
		//! @see VectorShapeComponent::mZOrder
		inline int getZOrder() const;
		//! show/hide the shape (the scene node's visibility)
		void setShapeVisible(bool visible);
		//! is the shape visible (true when no mesh exists yet - it will show)
		bool isShapeVisible() const;

		//--- reflected property accessors ---
		//! @brief set the shape REFERENCE by name (the reflected AssetRef
		//! setter): empty removes the shape; a name loads it when the scene node
		//! exists, otherwise records the reference (detached load). Tolerant
		//! where loadShape asserts, so the property drive can set it.
		void setShapeReference(String const & shapeName);
		//! reflected tint setter (Color -> the four-float setTint)
		inline void setTintColor(Color const & tint)
		{
			this->setTint(tint.r, tint.g, tint.b, tint.a);
		}

		//--- soft body ---------------------------------------------
		//! @brief turn the shape into a soft, deformable body (or back to a
		//! static shape). Enabling builds the deformer over the current shape and
		//! starts ticking; disabling restores the exact rest pose and stops.
		void setSoftBodyEnabled(bool enabled);
		//! is the shape a soft, deformable body
		inline bool isSoftBodyEnabled() const { return this->mSoftBody; }
		//! wobble spring stiffness (higher = faster, tighter jiggle)
		void setWobbleStiffness(float stiffness);
		//! @see VectorShapeComponent::setWobbleStiffness
		inline float getWobbleStiffness() const
		{
			return this->mDeformParams.wobbleStiffness;
		}
		//! wobble spring damping (higher = the jiggle settles sooner)
		void setWobbleDamping(float damping);
		//! @see VectorShapeComponent::setWobbleDamping
		inline float getWobbleDamping() const
		{
			return this->mDeformParams.wobbleDamping;
		}
		//! how strongly a contact kicks the wobble (0 = no jiggle)
		void setWobbleAmount(float amount);
		//! @see VectorShapeComponent::setWobbleAmount
		inline float getWobbleAmount() const
		{
			return this->mDeformParams.wobbleAmount;
		}
		//! how deeply a contact squashes the shape (0 = no squash)
		void setSquashAmount(float amount);
		//! @see VectorShapeComponent::setSquashAmount
		inline float getSquashAmount() const
		{
			return this->mDeformParams.squashAmount;
		}
		//! selected morph target index (-1 = none); see playMorph
		void setMorphClip(int index);
		//! @see VectorShapeComponent::setMorphClip
		inline int getMorphClip() const { return this->mMorphClip; }
		//! morph blend speed in phase units per second
		void setMorphSpeed(float speed);
		//! @see VectorShapeComponent::setMorphSpeed
		inline float getMorphSpeed() const { return this->mMorphSpeed; }
		//! ping-pong the selected morph target
		void setMorphLoop(bool loop);
		//! @see VectorShapeComponent::setMorphLoop
		inline bool getMorphLoop() const { return this->mMorphLoop; }

		//--- soft body: Lua / scripted drive ---
		//! @brief kick a squash+wobble along (dirX,dirY) with the given magnitude
		//! (the manual, non-physics impulse hook - a jump landing, a hit). No-op
		//! unless the shape is a soft body.
		void impulse(float dirX, float dirY, float magnitude);
		//! @brief start blending toward morph target index at speed, optionally
		//! looping (ping-pong). An invalid index is ignored.
		void playMorph(int index, float speed, bool loop);
		//! @brief stop the morph blend (holds the current pose)
		void stopMorph();

		//--- soft body: introspection (selfcheck / MCP / debug) ---
		//! largest current vertex displacement from rest (0 at rest)
		float getDeformDisplacement() const;
		//! current squash fraction applied this tick (0 at rest)
		float getSquash() const;
		//! is the deform currently off its rest pose (soft body, not settled)
		bool isDeforming() const;
		//! control points the deformer skins to (0 unless a soft body is built)
		std::size_t getControlPointCount() const;
		//! registered morph targets (from the shape asset)
		std::size_t getMorphTargetCount() const;
	protected:
		//! component override: create the child scene node (SpriteComponent recipe)
		virtual void onAdd();
		//! component override: drop the mesh + node
		virtual void onRemove();
		//! deactivated GameObjects hide their shape (setShapeVisible state kept)
		virtual void onSetActive(bool activeInHierarchy);
		//! tessellate mRegions (tint + feather) into mBuilt and push to the mesh
		void rebuildMesh();
		//! per-tick soft-body deform: advance the springs/squash/morph and upload
		//! the moved vertices through the DYNAMIC path (dormant unless soft-body
		//! is on and a runtime ticks GameObjects)
		virtual void onUpdateComponent(float deltaTime);
		//! @brief (re)build the deformer over the current rest mesh + morph
		//! targets and prime the reused upload buffers; enables ticking. No-op
		//! unless soft-body is on and a mesh exists.
		void rebuildDeformer();
		//! push the live wobble/squash tunables to the built deformer
		void applyDeformParams();
		//! @brief sibling-RigidBody contact: squash along the cached approach
		//! velocity (the impact normal) and kick the wobble. @see the class remarks
		bool onContactBegan(Event const & event);
		//! apply the EFFECTIVE visibility to the node (own flag AND owner active)
		void applyVisibility();
		//! push zOrder + node scale onto the live mesh/node
		void applyStateToMesh();
		//--- SERIALIZATION ---
		//! save tint, scale, edge softness, zOrder, visibility and the shape
		//! AssetRef (its stable id rides the record for rename survival)
		virtual void save(optr<IArchive> const & ar);
		//! load the shape state from Archive (and load the shape when attached)
		virtual void load(optr<IArchive> const & ar);
	private:
	};
	//---------------------------------------------------------------
	inline String const & VectorShapeComponent::getShapeName() const
	{
		return this->mShapeName;
	}
	//---------------------------------------------------------------
	inline bool VectorShapeComponent::hasShape() const
	{
		return this->mMesh != nullptr;
	}
	//---------------------------------------------------------------
	inline Color const & VectorShapeComponent::getTint() const
	{
		return this->mTint;
	}
	//---------------------------------------------------------------
	inline float VectorShapeComponent::getScale() const
	{
		return this->mScale;
	}
	//---------------------------------------------------------------
	inline float VectorShapeComponent::getEdgeSoftness() const
	{
		return this->mEdgeSoftness;
	}
	//---------------------------------------------------------------
	inline int VectorShapeComponent::getZOrder() const
	{
		return this->mZOrder;
	}
	//---------------------------------------------------------------
}

#endif //__VectorShapeComponent_h__10_7_2026__12_00_00__
