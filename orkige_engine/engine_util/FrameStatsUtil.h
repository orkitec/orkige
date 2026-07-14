/**************************************************************
	created:	2026/07/08 at 10:00
	filename: 	FrameStatsUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __FrameStatsUtil_h__8_7_2026__10_00_00__
#define __FrameStatsUtil_h__8_7_2026__10_00_00__

#include "engine_module/EnginePrerequisites.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace Orkige
{
	//! @brief tiny frame-time statistics collector for the game apps'
	//! performance hooks (jumper / jumper-native / player).
	//! @remarks feed the REAL (unclamped) frame duration every frame; the
	//! first WARMUP_FRAMES are discarded (resource loading spikes). Two
	//! consumers:
	//!  - ORKIGE_DEMO_FPS_LOG=1: logAtExit() prints frames / avg / p95 ms
	//!    (and the fps both imply) when the loop ends - the measurement hook.
	//!  - maybeWarnSlow(): one-time honest hint after the warm-up when the
	//!    average frame time is bad - names the build config and, in Debug,
	//!    points at the release preset (Debug runs 10-20x slower, see
	//!    CLAUDE.md "Build speed").
	class FrameStatsUtil
	{
	public:
		static constexpr unsigned long WARMUP_FRAMES = 20;
		//! frames measured before the slow-build warning may fire
		static constexpr unsigned long WARN_SAMPLE_FRAMES = 100;
		//! "bad" threshold: below ~50 fps the game feels sluggish
		static constexpr float WARN_AVERAGE_MS = 20.0f;

		FrameStatsUtil()
			: mLogRequested(std::getenv("ORKIGE_DEMO_FPS_LOG") != nullptr)
		{
			mFrameMs.reserve(4096);
		}

		//! record one REAL frame duration (seconds, before any clamping)
		void addFrame(float seconds)
		{
			++mFrameCount;
			if (mFrameCount <= WARMUP_FRAMES)
			{
				return;
			}
			mFrameMs.push_back(seconds * 1000.0f);
		}

		float averageMs() const
		{
			if (mFrameMs.empty())
			{
				return 0.0f;
			}
			float sum = 0.0f;
			for (float ms : mFrameMs)
			{
				sum += ms;
			}
			return sum / static_cast<float>(mFrameMs.size());
		}

		float percentileMs(float percentile) const
		{
			if (mFrameMs.empty())
			{
				return 0.0f;
			}
			std::vector<float> sorted = mFrameMs;
			std::sort(sorted.begin(), sorted.end());
			const std::size_t index = std::min(sorted.size() - 1,
				static_cast<std::size_t>(percentile / 100.0f *
					static_cast<float>(sorted.size())));
			return sorted[index];
		}

		// the SLOW FRAMES hint and the frame-stats line below stay on SDL_Log by
		// policy: they are demo/perf MEASUREMENT output (the sanctioned class),
		// not operational diagnostics - do not reroute to oDebug* on a re-audit.

		//! one-time hint when the measured frame times make the game feel
		//! bad; call once per frame (no-op until enough samples arrived)
		void maybeWarnSlow(char const* appName)
		{
			if (mWarned || mFrameMs.size() < WARN_SAMPLE_FRAMES)
			{
				return;
			}
			mWarned = true;
			const float average = averageMs();
			if (average <= WARN_AVERAGE_MS)
			{
				return;
			}
#ifdef NDEBUG
			SDL_Log("%s: SLOW FRAMES - avg %.1f ms (%.0f fps) in a Release "
				"build; try a smaller window or a different "
				"ORKIGE_RENDERSYSTEM", appName, average, 1000.0f / average);
#else
			SDL_Log("%s: SLOW FRAMES - avg %.1f ms (%.0f fps) in a DEBUG "
				"build. Debug is for development; build and run the release "
				"preset for actual play (cmake --build --preset "
				"macos-release) - it is roughly 10-20x faster", appName,
				average, 1000.0f / average);
#endif
		}

		//! the ORKIGE_DEMO_FPS_LOG measurement line, printed when the run ends
		void logAtExit(char const* appName) const
		{
			if (!mLogRequested || mFrameMs.empty())
			{
				return;
			}
			const float average = averageMs();
			const float p95 = percentileMs(95.0f);
			SDL_Log("%s: frame stats - %zu frames (after %lu warm-up), "
				"avg %.2f ms (%.0f fps), p95 %.2f ms (%.0f fps)", appName,
				mFrameMs.size(), WARMUP_FRAMES, average,
				average > 0.0f ? 1000.0f / average : 0.0f, p95,
				p95 > 0.0f ? 1000.0f / p95 : 0.0f);
		}

	private:
		std::vector<float>	mFrameMs;
		unsigned long		mFrameCount = 0;
		bool				mWarned = false;
		bool				mLogRequested = false;
	};
}

#endif // __FrameStatsUtil_h__8_7_2026__10_00_00__
