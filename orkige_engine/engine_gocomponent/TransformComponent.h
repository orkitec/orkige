/**************************************************************
	created:	2010/08/31 at 10:41
	filename: 	TransformComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __TransformComponent_h__31_8_2010__10_41_33__
#define __TransformComponent_h__31_8_2010__10_41_33__

#include "engine_module/EnginePrerequisites.h"
#include <core_game/GameObject.h>
#include "engine_util/SceneNodeGuard.h"

namespace Orkige
{
	//! @brief basic Transformation component for all GameObjects in 3D Space
	//! @remarks owns a facade
	//! RenderNode (via the reshaped SceneNodeGuard base) instead of a raw
	//! Ogre::SceneNode. The node carries `this` as its user pointer - ray
	//! query hits and editors resolve a node back to its component through
	//! RenderNode::findUserPointerUpwards (@see getComponentFromNode).
	class ORKIGE_ENGINE_DLL TransformComponent : public GameObjectComponent, public SceneNodeGuard
	{
		OOBJECT(TransformComponent,GameObjectComponent)
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		//! the reflected mobility flag (@see setStaticFlag)
		bool	mStatic = false;
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor
		TransformComponent();
		//! destructor
		virtual ~TransformComponent();
		//! get the TransformComponent a facade node belongs to, or NULL
		//! if traverseParents is true parents will be checked until a TransformComponent is found or the root is reached
		//! @remarks resolves through the RenderNode user pointer - within the
		//! engine ONLY TransformComponent tags scene nodes, so the cast is safe
		static TransformComponent* getComponentFromNode(optr<RenderNode> const & node, bool traverseParents = true);
		//--- WORLD-SPACE SETTERS (hierarchy-aware) ---
		//! world-space scale (local scales composed through the ancestor chain)
		Vec3 getWorldScale() const;
		//! @brief set the world-space position - the LOCAL position (what
		//! getPosition returns and the scene serializes) is recomputed through
		//! the parent chain; identical to setPosition for root objects
		void setWorldPosition(Vec3 const & worldPosition);
		//! @brief set the world-space orientation (@see setWorldPosition)
		void setWorldOrientation(Quat const & worldOrientation);
		//! @brief teleport this transform to a world-space pose AND snap every
		//! rigid body in the GameObject subtree to its resulting world pose
		//! @remarks the hierarchy-level "move the world" API: physics bodies
		//! live in world space, so sliding a parent must teleport the children's
		//! collision geometry along - this works while PhysicsWorld is PAUSED,
		//! like RigidBodyComponent::teleport (which handles the self-owned body
		//! the same way and delegates the subtree part here)
		void teleport(Vec3 const & worldPosition, Quat const & worldOrientation);
		//! @brief snap every rigid body attached at or below the given
		//! GameObject to its TransformComponent's current world pose, killing
		//! the momentum of non-static bodies (@see teleport)
		static void syncSubtreeBodies(GameObject * gameObject);

		//--- MOBILITY (the reflected `static` flag) ---
		//! @see TransformComponent::setStaticFlag
		inline bool getStaticFlag() const { return this->mStatic; }
		//! @brief declare this object's world transform IMMUTABLE so the
		//! renderer can take its immobile fast path (next: SCENE_STATIC
		//! memory managers skip per-frame transform derivation and cull
		//! prep; classic: mesh content bakes into shared StaticGeometry
		//! regions - fewer draw calls). Applies to the object's OWN node and
		//! the content nodes its sibling components own; child GameObjects
		//! flag themselves.
		//! @remarks THE MOBILITY CONTRACT: "static means static" - a runtime
		//! move of a static object stays correct but logs one warning per
		//! node and pays the backend's repair path (@see
		//! RenderNode::setStatic). HIERARCHY RULE, validated here with an
		//! honest error: a static object requires a static (or absent)
		//! parent - a static child under a dynamic parent would freeze while
		//! the parent moves - and a static parent cannot go dynamic while
		//! static children depend on it. The `r.staticScene` cvar gates the
		//! backend apply (read at apply time; the flag itself always
		//! round-trips) - the editor runs with it OFF so edit-mode gizmo
		//! moves never fight the contract.
		void setStaticFlag(bool isStatic);
		//! @brief the pure hierarchy rule behind setStaticFlag: the reason a
		//! flag change is refused, or NULL when it is allowed
		//! @param wantStatic the value being set
		//! @param parentStatic the parent object's flag (true when no parent)
		//! @param anyChildStatic any direct child object's flag set
		static char const * staticFlagChangeError(bool wantStatic,
			bool parentStatic, bool anyChildStatic);
	protected:
		//! @brief push the current flag onto the render nodes: the owned node
		//! plus the sibling components' content nodes below it (facade
		//! children WITHOUT a user pointer; child TransformComponents carry
		//! one and stay untouched - they flag themselves). No-op while
		//! detached or while `r.staticScene` is off.
		void applyStaticFlag();
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//! @brief maps the GameObject tree onto the render node graph: attaches
		//! the node under the new parent's TransformComponent node (world root
		//! when unparented); with keepWorldTransform the local TRS is recomputed
		//! so the world pose is preserved
		virtual void onParentChanged(GameObject * newParent, bool keepWorldTransform);
		//! the node the owner's CURRENT parent GameObject provides (world root
		//! when unparented or when the parent has no TransformComponent)
		optr<RenderNode> resolveParentNode();
		//! re-parents all TransformComponents attached below my node to the world root
		void detachTransformComponents(optr<RenderNode> const & node, bool traverseChildren = true);
		//--- SERIALIZATION ---
		//! save position/orientation/scale to Archive
		virtual void save(optr<IArchive> const & ar);
		//! load position/orientation/scale from Archive
		virtual void load(optr<IArchive> const & ar);
	private:
	};

}

#endif //__TransformComponent_h__31_8_2010__10_41_33__
