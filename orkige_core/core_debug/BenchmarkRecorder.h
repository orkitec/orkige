/**************************************************************
	created:	2026/07/12 at 12:00
	filename: 	BenchmarkRecorder.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __BenchmarkRecorder_h__12_7_2026__12_00_00__
#define __BenchmarkRecorder_h__12_7_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_debug/ProfileManager.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"

#include <cstddef>
#include <fstream>
#include <map>
#include <utility>
#include <vector>

namespace Orkige
{
	//! @brief one per-frame measurement handed to the recorder. Built by the
	//! player from the landed instruments (ProfileManager frame ms + depth-0
	//! phase times, MemoryManager alloc total, MemorySampler RSS) plus the
	//! render facade's FrameStats (triangles/batches/texture memory), which the
	//! core layer cannot see. A pure value so the aggregation math stays
	//! headless-testable: feed synthetic samples, assert the outputs.
	struct BenchmarkFrameSample
	{
		double			frameMs = 0.0;		//!< whole-frame wall time (ProfileManager)
		std::size_t		allocCount = 0;		//!< tracked alloc events this frame
		std::size_t		rssBytes = 0;		//!< process resident set (0 = n/a)
		unsigned int	triangles = 0;		//!< FrameStats triangles submitted
		unsigned int	batches = 0;		//!< FrameStats draw batches
		float			texMemMB = 0.0f;	//!< FrameStats texture memory (MB)
		//! depth-0 tick-phase times (name -> ms) for this frame; the recorder
		//! folds these into per-phase means (input/scripts/physics/render/...)
		std::vector<std::pair<String, double>> phasesMs;
	};

	//! @brief the compiled-in identity written to the artifact's "meta" line.
	//! Everything here is CALLER-supplied: the core layer has no SDL, no render
	//! facade and no build-sha define, so the player gathers device/OS/GPU,
	//! flavor, render system, build config, platform and the sha (from the
	//! ORKIGE_BUILD_SHA env the runner sets - no compiled-in sha machinery
	//! exists) and hands them over before the run.
	struct BenchmarkMeta
	{
		String	utc			= "";			//!< run start, ISO 8601 UTC ("" = omit)
		String	engineSha	= "unknown";	//!< ORKIGE_BUILD_SHA env, else "unknown"
		String	flavor		= "unknown";	//!< "next" / "classic"
		String	renderSystem= "unknown";	//!< "Metal" / "Vulkan" / "GL3Plus" / ...
		String	build		= "unknown";	//!< "Debug" / "Release"
		String	platform	= "unknown";	//!< "macos" / "ios" / "android" / ...
		String	deviceModel	= "unknown";	//!< hardware model where known
		String	deviceOs	= "unknown";	//!< OS name/version
		String	deviceGpu	= "unknown";	//!< GPU string where known
		String	scenario	= "full";		//!< run mode ("full"/"smoke"/...)
		String	project		= "";			//!< the open project's name
	};

	//! @brief the pure per-scene aggregator: accumulates BenchmarkFrameSamples
	//! and reduces them to the artifact's numbers (frame-ms min/avg/p50/p95/p99/
	//! max, per-phase means, alloc mean+peak, RSS peak, triangle/batch/texture
	//! means). No I/O, no singletons - the unit tests drive this directly.
	//! @remarks Percentiles use the nearest-rank convention with INTEGER math
	//! (index = floor(p * n / 100), clamped to the last element) so the result
	//! is a real observed sample and carries no floating-point rounding
	//! fragility - the value a test can assert exactly.
	class ORKIGE_CORE_DLL BenchmarkSceneStats
	{
	public:
		void reset();
		//! fold one frame's sample into the running aggregates
		void addFrame(BenchmarkFrameSample const & sample);

		std::size_t frames() const { return this->mFrameMs.size(); }
		//! wall seconds spanned (sum of the per-frame times)
		double totalSeconds() const;
		double minFrameMs() const;
		double maxFrameMs() const;
		double avgFrameMs() const;
		//! nearest-rank percentile (pct in 0..100), integer index, clamped
		double percentileFrameMs(double pct) const;
		//! average frames per second over the scene (0 when no frames)
		double avgFps() const;
		double allocPerFrameAvg() const;
		std::size_t allocPeakFrame() const;
		std::size_t rssPeakBytes() const;
		double trianglesAvg() const;
		double batchesAvg() const;
		double texMemMBAvg() const;
		//! per-phase mean ms, sorted by phase name (deterministic order)
		std::vector<std::pair<String, double>> phaseMeansMs() const;

	private:
		std::vector<double>	mFrameMs;			//!< every frame's ms (for percentiles)
		double				mAllocSum = 0.0;	//!< sum of allocCount
		std::size_t			mAllocPeak = 0;		//!< worst per-frame allocCount
		std::size_t			mRssPeak = 0;		//!< worst RSS seen
		double				mTrisSum = 0.0;		//!< sum of triangles
		double				mBatchesSum = 0.0;	//!< sum of batches
		double				mTexMemSum = 0.0;	//!< sum of texMemMB
		//! phase name -> {summed ms, frame count}
		std::map<String, std::pair<double, std::size_t>> mPhases;
	};

	//! @brief per-scene performance capture to a machine-readable results
	//! artifact - the benchmark counterpart to Breadcrumbs (JSON Lines, written
	//! to the writable app dir, flushed per record so a crash mid-run still
	//! leaves the scenes recorded so far). OPT-IN and dormant: it records only
	//! once armed with an output file (the player arms it from an env; the
	//! editor never sets a file, so every entry point is an honest no-op there).
	//! @remarks Artifact = one JSON object per line: a "meta" line (device/OS/
	//! GPU, flavor, render system, build sha, scenario), one "scene" line per
	//! recorded scene (the BenchmarkSceneStats reduction), and a closing
	//! "summary" line. A scene boundary is a level switch (the player calls
	//! beginScene at boot and on each deferred scene load) OR an explicit Lua
	//! marker (benchmark.begin / benchmark.endScene), which composes: begin
	//! renames/restarts the current scene aggregation.
	//! @remarks Unlike Breadcrumbs the file name carries a UTC stamp
	//! (benchmark-<stamp>.jsonl), so each run is a fresh file - no rotation.
	class ORKIGE_CORE_DLL BenchmarkRecorder : public Singleton<BenchmarkRecorder>
	{
		DECL_OSINGLETON(BenchmarkRecorder);
	public:
		BenchmarkRecorder();
		virtual ~BenchmarkRecorder();

		//! set the compiled-in identity written to the "meta" line (call once
		//! before the run; a later call replaces it until meta is written)
		void setMeta(BenchmarkMeta const & meta);
		BenchmarkMeta const & getMeta() const { return this->mMeta; }

		//! @brief arm by opening the output file ("" disarms / closes). Armed =
		//! a file is open; the Lua surface and the per-frame sampler are honest
		//! no-ops until then.
		void setFile(String const & path);
		bool isArmed() const { return this->mStream.is_open(); }
		String const & getFile() const { return this->mFile; }

		//! @brief scene boundary: close any open scene (writing its record),
		//! then start a fresh aggregation under name. No-op when disarmed.
		void beginScene(String const & name);
		//! close the current scene and write its record; no-op if none open
		void endScene();
		//! is a scene currently accumulating
		bool sceneOpen() const { return this->mSceneOpen; }

		//! @brief per-frame convenience sampler: reads ProfileManager (frame ms
		//! + depth-0 phase times), MemoryManager (alloc total) and MemorySampler
		//! (RSS), pairs them with the caller's FrameStats numbers and folds one
		//! sample into the current scene. No-op when disarmed or no scene open.
		void sampleFrame(unsigned int triangles, unsigned int batches,
			float texMemMB);
		//! low-level feed (tests): add a fully-built sample to the current scene
		void addFrame(BenchmarkFrameSample const & sample);

		//! @brief finalize the run: close any open scene and write the "summary"
		//! line (aborted marks an incomplete run). No-op when disarmed.
		void finish(bool aborted);

		//! the current scene's live aggregates (tests / introspection)
		BenchmarkSceneStats const & currentScene() const { return this->mScene; }
		//! scenes written so far
		int scenesWritten() const { return this->mScenesWritten; }
	protected:
	private:
		void writeLine(String const & line);
		void writeMetaLineIfNeeded();
		void writeSceneLine(String const & name, BenchmarkSceneStats const & stats);

		BenchmarkMeta		mMeta;			//!< the identity for the meta line
		String				mFile;			//!< live artifact path ("" = disarmed)
		std::ofstream		mStream;		//!< append stream, flushed per line
		bool				mMetaWritten = false;
		bool				mFinished = false;	//!< a summary line was written
		bool				mSceneOpen = false;
		String				mSceneName;		//!< the open scene's name
		BenchmarkSceneStats	mScene;			//!< the open scene's aggregator
		int					mScenesWritten = 0;
		double				mTotalSeconds = 0.0;
		//! reusable scratch for the per-frame ProfileManager snapshot
		std::vector<ProfileManager::SnapshotNode> mSnapshotScratch;
	};
}

#endif //__BenchmarkRecorder_h__12_7_2026__12_00_00__
