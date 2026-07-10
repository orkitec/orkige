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
	//! a later deformable phase can perturb the contours and re-push through the
	//! same VectorMesh::setMesh refill contract with no facade change.
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
	protected:
		//! component override: create the child scene node (SpriteComponent recipe)
		virtual void onAdd();
		//! component override: drop the mesh + node
		virtual void onRemove();
		//! deactivated GameObjects hide their shape (setShapeVisible state kept)
		virtual void onSetActive(bool activeInHierarchy);
		//! tessellate mRegions (tint + feather) into mBuilt and push to the mesh
		void rebuildMesh();
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
