/**************************************************************
	created:	2026/07/27 at 10:00
	filename: 	PlanarReflectionGuard.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PlanarReflectionGuard_h__27_7_2026__10_00_00__
#define __PlanarReflectionGuard_h__27_7_2026__10_00_00__

namespace Orkige
{
	//! @brief pure one-shot guard that suppresses the next-backend's nested
	//! planar-reflection workspace update for exactly ONE frame after the
	//! window compositor workspace is (re)built.
	//! @remarks The next backend renders the mirror-of-scene planar water
	//! reflection by nesting a workspace _update from inside the WINDOW
	//! workspace's preUpdate (@see PlanarReflectionUpdater), which culls against
	//! the scene manager. A mid-play scene switch that rebuilds the window
	//! workspace (a refractive-water surface flips the workspace's pass
	//! structure) can leave the nested reflection update running against a
	//! just-rebuilt workspace's not-yet-repopulated cull state - a
	//! use-after-rebuild the window's own pass has not yet reconciled. Skipping
	//! the nested update for the single frame in which a rebuild happened costs
	//! one frame of stale mirror (visually free during a scene-switch wipe) and
	//! removes the hazard; the mirror resumes the very next frame.
	//! @remarks Provably INERT in the steady state: with no rebuild the pending
	//! skip is never set, so consumeSkip() is always false and the mirror
	//! renders every frame byte-identically. The guard costs nothing until a
	//! rebuild actually occurs. Renderer-independent (no Ogre types) so it unit
	//! tests headlessly.
	class PlanarReflectionGuard
	{
	public:
		//! @brief record that the window workspace was (re)built: the next
		//! planar reflection update skips its nested render once. Coalescing -
		//! several rebuilds before the next update still cost a SINGLE skipped
		//! frame.
		void noteWorkspaceRebuilt() { mSkipPending = true; }

		//! @brief the planar reflection updater asks whether to skip this
		//! frame's nested update. Returns true (and clears the pending skip)
		//! exactly once per noteWorkspaceRebuilt(); false in the steady state.
		//! Consumed by the FIRST real update after a rebuild (call it only where
		//! the update would otherwise do work), so the one skipped frame lands
		//! on the mirror update, regardless of how the rebuild and the
		//! subsystem-creation happen to interleave.
		bool consumeSkip()
		{
			if(!mSkipPending)
			{
				return false;
			}
			mSkipPending = false;
			return true;
		}

		//! @brief whether a skip is currently pending (inspection/test only;
		//! does not consume it).
		bool skipPending() const { return mSkipPending; }

	private:
		//! true while a rebuild is awaiting its one skipped planar update
		bool mSkipPending = false;
	};
}

#endif //__PlanarReflectionGuard_h__27_7_2026__10_00_00__
