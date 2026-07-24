/**************************************************************
	created:	2026/07/24 at 10:00
	filename: 	PlayMirror.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PlayMirror_h__24_7_2026__10_00_00__
#define __PlayMirror_h__24_7_2026__10_00_00__

#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace OrkigeEditor
{
	//! @brief one object's LOCAL transform on the wire / in a snapshot:
	//! position (0..2), orientation quaternion w/x/y/z (3..6), scale (7..9) -
	//! the exact 10 floats MSG_SCENE_TRANSFORMS carries per id.
	struct MirrorTransform
	{
		std::array<float, 10> m{};
	};

	//! @brief parse a MSG_SCENE_TRANSFORMS entry ("px py pz qw qx qy qz sx sy
	//! sz", 10 space-separated floats) into a MirrorTransform; false (out
	//! untouched) on anything short of 10 parseable floats. Pure.
	bool parseMirrorTransform(std::string const& text, MirrorTransform& out);

	//! @brief format a MirrorTransform back to the wire string (round-trip
	//! precision, %.9g), the inverse of parseMirrorTransform. Pure.
	std::string formatMirrorTransform(MirrorTransform const& transform);

	//! @brief resolve each object's EFFECTIVE active-in-hierarchy from the
	//! parallel hierarchy lists MSG_HIERARCHY streams (ids, parent id per id
	//! with "" = root, activeSelf "1"/"0" per id): an object is effective-active
	//! iff its own activeSelf is set AND every ancestor is too. Missing/short
	//! lists degrade to "all active". Pure - the visibility half of the mirror
	//! feeds off this.
	std::map<std::string, bool> computeEffectiveActive(
		std::vector<std::string> const& ids,
		std::vector<std::string> const& parents,
		std::vector<std::string> const& actives);

	//! @brief one reflected property of a runtime-spawned object's component,
	//! exactly the record dialect the prefab baseline capture produces
	//! (component + name key it under; kind/value/reference are the wire triple)
	struct MirrorSpawnProperty
	{
		std::string component;	//!< component type/kind name
		std::string name;		//!< property name
		int kind = 0;			//!< PropertyKind as int
		std::string value;		//!< canonical value string
		std::string reference;	//!< AssetRef resolving id ("" otherwise)
	};

	//! @brief the visual identity of ONE runtime-spawned object as
	//! MSG_SCENE_SPAWNS describes it: id, parent, component kinds and the
	//! reflected property records - enough for the editor to instantiate a
	//! lightweight mirror stand-in.
	struct MirrorSpawnDesc
	{
		std::string id;
		std::string parent;
		std::vector<std::string> components;	//!< component kind names
		std::vector<MirrorSpawnProperty> properties;
	};

	//! @brief parse one MSG_SCENE_SPAWNS message's parallel lists into
	//! descriptors: ids/parents/components are per object (components a
	//! space-separated kind-name string), the per-property quintuple rides
	//! propObjects (decimal index into ids) / propKeys ("<Component>.<prop>") /
	//! propKinds / propValues / propRefs. Malformed records (a non-numeric or
	//! out-of-range object index, a key without the '.' separator, short
	//! parallel lists) are skipped - never a crash, never a partial guess. Pure.
	std::vector<MirrorSpawnDesc> parseSpawnDescriptors(
		std::vector<std::string> const& ids,
		std::vector<std::string> const& parents,
		std::vector<std::string> const& components,
		std::vector<std::string> const& propObjects,
		std::vector<std::string> const& propKeys,
		std::vector<std::string> const& propKinds,
		std::vector<std::string> const& propValues,
		std::vector<std::string> const& propRefs);

	//! @brief is this component kind part of a mirror stand-in's VISUAL
	//! identity? The mirror instantiates only what determines looks (transform,
	//! renderables, light) - behavioral components (scripts, bodies, sound...)
	//! stay out: the editor never ticks, and a stand-in must never simulate.
	//! Pure - the one allowlist, shared by the shell and the unit tests.
	bool isMirrorVisualComponent(std::string const& componentKind);

	//! @brief the editor scene a PlayMirror drives, abstracted so the mirror's
	//! snapshot/apply/restore logic stays pure (unit-tested against a fake) and
	//! the real implementation (over the editor's GameObjectManager render
	//! nodes) lives in the editor shell. All ids are GameObject ids; a call for
	//! an id the scene does not hold is a silent no-op.
	class MirrorScene
	{
	public:
		virtual ~MirrorScene() = default;
		//! every object that carries a transform (the mirror's key set)
		virtual std::vector<std::string> transformableIds() const = 0;
		//! does the scene hold an object under this id (any component set)?
		virtual bool hasObject(std::string const& id) const = 0;
		//! read an object's current LOCAL transform (false when it has none)
		virtual bool getLocalTransform(std::string const& id,
			MirrorTransform& out) const = 0;
		//! write an object's LOCAL transform (no-op for an unknown id)
		virtual void setLocalTransform(std::string const& id,
			MirrorTransform const& transform) = 0;
		//! the object's AUTHORED effective visibility (activeInHierarchy) - the
		//! baseline a restore returns it to
		virtual bool getBaselineVisible(std::string const& id) const = 0;
		//! show/hide only this object's OWN drawable content (never a cascade
		//! across the object hierarchy - each object owns its own visibility so
		//! the effect is order-independent)
		virtual void setVisible(std::string const& id, bool visible) = 0;
		//! @brief create a MIRROR-ONLY stand-in for a runtime-spawned object:
		//! the descriptor's VISUAL components (isMirrorVisualComponent) with
		//! their reflected properties applied, parented under desc.parent when
		//! that object exists. False when the object cannot be created (id
		//! taken, no component registered). Never touches undo/dirty - the
		//! stand-in is not part of the document.
		virtual bool spawnObject(MirrorSpawnDesc const& desc) = 0;
		//! destroy a mirror stand-in again (no-op for an unknown id)
		virtual void destroyObject(std::string const& id) = 0;
	};

	//! @brief mirrors a running play session's object motion into the editor's
	//! Scene view and restores the authored scene EXACTLY on stop.
	//! @remarks THE CONTRACT: the edit document is never touched. The mirror
	//! drives the editor scene's RENDER state only - the render node behind each
	//! object's TransformComponent - after snapshotting every object's AUTHORED
	//! local transform + effective visibility ONCE (beginIfNeeded, at the first
	//! apply, when the edit world still holds authored truth: the editor never
	//! ticks its own world and play mode routes editing to the remote panels).
	//! restore() writes those exact snapshot floats back, so the scene returns
	//! byte-for-byte to how it was authored (the .oscene on disk is never read
	//! or written by any of this). Objects present in BOTH the authored scene
	//! and a stream update are moved (matched by id); a RUNTIME-SPAWNED id
	//! becomes a tracked MIRROR INSTANCE - a stand-in the shell creates from a
	//! MSG_SCENE_SPAWNS descriptor (applySpawns), driven by the same transform/
	//! visibility streams, pruned when its id vanishes from the hierarchy and
	//! DESTROYED (never restored, never serialized) by restore().
	class PlayMirror
	{
	public:
		//! is a snapshot captured (i.e. the scene is currently mirrored)?
		bool active() const { return this->mActive; }
		//! how many objects the snapshot holds (0 until the first apply)
		std::size_t trackedCount() const { return this->mSnapshot.size(); }
		//! how many mirror stand-ins for runtime-spawned objects exist
		std::size_t instanceCount() const
		{
			return this->mMirrorInstances.size();
		}
		//! is this id a tracked mirror stand-in (not an authored object)?
		bool isMirrorInstance(std::string const& id) const
		{
			return this->mMirrorInstances.count(id) != 0;
		}

		//! @brief capture the authored transform + baseline visibility of every
		//! transformable object, ONCE. A no-op once active. Both apply* paths
		//! call this first, so whichever stream arrives first takes the snapshot
		//! (both read authored truth, so the order does not matter).
		void beginIfNeeded(MirrorScene& scene);

		//! @brief drive the editor nodes from a MSG_SCENE_TRANSFORMS delta:
		//! ids parallel to transforms (each the wire string). Snapshots first.
		//! Moves authored objects AND mirror instances (matched by id).
		void applyTransforms(MirrorScene& scene,
			std::vector<std::string> const& ids,
			std::vector<std::string> const& transforms);

		//! @brief drive object visibility from the effective-active map derived
		//! off MSG_HIERARCHY (computeEffectiveActive). Snapshots first. Only
		//! objects whose effective active DIFFERS from their authored baseline
		//! are toggled (and remembered, so restore returns them to baseline).
		void applyActive(MirrorScene& scene,
			std::map<std::string, bool> const& effective);

		//! @brief which streamed hierarchy ids need a MSG_QUERY_SPAWNS ask:
		//! ids the scene does not hold (not authored, not yet a stand-in) that
		//! were not already asked about. Snapshots first. Marks the returned
		//! ids as asked (idempotent per id; a pruned id may be asked again).
		std::vector<std::string> idsToQuery(MirrorScene& scene,
			std::vector<std::string> const& streamedIds);

		//! @brief materialize mirror stand-ins from parsed MSG_SCENE_SPAWNS
		//! descriptors: parents-before-children ordering across the batch, an
		//! id the scene already holds is skipped (a race with the streams), a
		//! successful spawn becomes a tracked instance with a visible baseline.
		void applySpawns(MirrorScene& scene,
			std::vector<MirrorSpawnDesc> const& descriptors);

		//! @brief destroy every mirror stand-in whose id no longer appears in
		//! the streamed hierarchy (the running game destroyed the object); the
		//! id may be re-asked if it ever reappears.
		void pruneInstances(MirrorScene& scene,
			std::vector<std::string> const& streamedIds);

		//! @brief destroy every mirror stand-in, write every snapshotted
		//! authored transform + baseline visibility back and drop the snapshot
		//! (the single exact-restore path, called on Stop / crash / teardown /
		//! the mid-play save guard). A no-op when not active and no stand-ins
		//! exist.
		void restore(MirrorScene& scene);

	private:
		bool mActive = false;
		//! authored local transform per object (the exact-restore source)
		std::map<std::string, MirrorTransform> mSnapshot;
		//! authored effective visibility per object (the restore target);
		//! mirror instances join with a true baseline so the active stream can
		//! hide them, but they are destroyed - never restored
		std::map<std::string, bool> mVisibleBaseline;
		//! objects whose visibility the mirror currently overrides (so a restore
		//! only touches what it changed)
		std::set<std::string> mVisibilityChanged;
		//! the runtime-spawned stand-ins this mirror created (destroyed on
		//! restore/prune; excluded from the authored snapshot by construction)
		std::set<std::string> mMirrorInstances;
		//! ids already asked about via MSG_QUERY_SPAWNS (never ask twice while
		//! the id stays unresolved; pruning an instance clears its entry)
		std::set<std::string> mSpawnsQueried;
	};
}

#endif //__PlayMirror_h__24_7_2026__10_00_00__
