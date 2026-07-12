// EditorStatsPanel.cpp - the Stats panel (4 Hz windowed avg/min/max frame
// stats + rolling frame-time plot).
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <engine_render/RenderSystem.h>

#include <cstdio>
#include <string>

// Stats panel: samples getFrameStats() every frame but refreshes the
// DISPLAYED numbers at ~4 Hz - lastFPS is the reciprocal of a single frame's
// duration, so printing it raw turns sub-millisecond timing noise into a
// wildly flickering label. The window shows avg/min/max over the refresh
// interval, frame time in ms (the budget number) next to FPS, and a rolling
// frame-time plot of the last 120 samples.
// format a byte count as a compact human string (KB/MB/GB, two decimals)
static std::string formatBytes(long long bytes)
{
	const double value = static_cast<double>(bytes);
	char buffer[32];
	if (bytes >= 1024LL * 1024LL * 1024LL)
	{
		std::snprintf(buffer, sizeof(buffer), "%.2f GB",
			value / (1024.0 * 1024.0 * 1024.0));
	}
	else if (bytes >= 1024LL * 1024LL)
	{
		std::snprintf(buffer, sizeof(buffer), "%.1f MB",
			value / (1024.0 * 1024.0));
	}
	else
	{
		std::snprintf(buffer, sizeof(buffer), "%.0f KB", value / 1024.0);
	}
	return buffer;
}

void drawStatsPanel(PlaySession const& play, bool* visible)
{
	if (ImGui::Begin("Stats", visible))
	{
		const Orkige::RenderSystem::FrameStats stats =
			Orkige::RenderSystem::get()->getFrameStats();

		// rolling frame-time history (ms), fed every frame
		static float sFrameTimes[120] = {};
		static int sFrameTimeOffset = 0;
		const float frameMs =
			(stats.lastFPS > 0.0f) ? 1000.0f / stats.lastFPS : 0.0f;
		sFrameTimes[sFrameTimeOffset] = frameMs;
		sFrameTimeOffset = (sFrameTimeOffset + 1) % 120;

		// accumulate since the last display refresh
		static int sSampleCount = 0;
		static float sSumMs = 0.0f;
		static float sMinMs = 0.0f;
		static float sMaxMs = 0.0f;
		if (sSampleCount == 0)
		{
			sMinMs = frameMs;
			sMaxMs = frameMs;
		}
		++sSampleCount;
		sSumMs += frameMs;
		sMinMs = std::min(sMinMs, frameMs);
		sMaxMs = std::max(sMaxMs, frameMs);

		// the displayed values, refreshed every 250ms
		static float sShownAvgMs = 0.0f;
		static float sShownMinMs = 0.0f;
		static float sShownMaxMs = 0.0f;
		static size_t sShownTriangles = 0;
		static size_t sShownBatches = 0;
		static std::chrono::steady_clock::time_point sLastRefresh;
		const std::chrono::steady_clock::time_point now =
			std::chrono::steady_clock::now();
		if (now - sLastRefresh >= std::chrono::milliseconds(250))
		{
			sShownAvgMs = (sSampleCount > 0)
				? sSumMs / static_cast<float>(sSampleCount) : 0.0f;
			sShownMinMs = sMinMs;
			sShownMaxMs = sMaxMs;
			sShownTriangles = stats.triangleCount;
			sShownBatches = stats.batchCount;
			sSampleCount = 0;
			sSumMs = 0.0f;
			sLastRefresh = now;
		}

		const float shownFPS =
			(sShownAvgMs > 0.0f) ? 1000.0f / sShownAvgMs : 0.0f;
		ImGui::Text("FPS: %.1f (%.2f ms)", shownFPS, sShownAvgMs);
		ImGui::Text("Frame min/max: %.2f / %.2f ms", sShownMinMs, sShownMaxMs);
		ImGui::PlotLines("##frametimes", sFrameTimes, 120, sFrameTimeOffset,
			"frame time (ms)", 0.0f, FLT_MAX,
			ImVec2(-FLT_MIN, 60.0f));
		ImGui::Text("Triangles: %zu", sShownTriangles);
		ImGui::Text("Batches: %zu", sShownBatches);

		// running-game memory (streamed from the player over the debug link
		// while Play is up): the process resident set size + the session peak.
		// The FPS/triangle/batch numbers above are the EDITOR's own frame; this
		// block is the RUNTIME, so it only shows during a live session. -1 =
		// no reading yet (a player that has not streamed one, or a platform
		// without a memory query) - shown honestly as n/a.
		if (play.isActive())
		{
			ImGui::SeparatorText("Game memory");
			if (play.remoteMemRss >= 0)
			{
				ImGui::Text("RSS: %s",
					formatBytes(play.remoteMemRss).c_str());
			}
			else
			{
				ImGui::TextUnformatted("RSS: n/a");
			}
			if (play.remoteMemRssPeak >= 0)
			{
				ImGui::Text("Peak: %s",
					formatBytes(play.remoteMemRssPeak).c_str());
			}
			else
			{
				ImGui::TextUnformatted("Peak: n/a");
			}

			// the RUNTIME frame: wall time, engine-level allocation events
			// (tracked seams, per frame - not libc totals) and the CPU
			// profiler's frame breakdown, all streamed on the stats cadence
			ImGui::SeparatorText("Game performance");
			if (play.remoteFrameMs >= 0.0)
			{
				ImGui::Text("Frame: %.2f ms", play.remoteFrameMs);
			}
			else
			{
				ImGui::TextUnformatted("Frame: n/a");
			}
			if (play.remoteAllocPerFrame >= 0)
			{
				ImGui::Text("Allocs/frame: %lld (peak %lld)",
					play.remoteAllocPerFrame, play.remoteAllocPeak);
				for (std::size_t i = 0; i < play.remoteAllocTags.size() &&
					i < play.remoteAllocCounts.size(); ++i)
				{
					if (play.remoteAllocCounts[i] > 0)
					{
						ImGui::Text("  %s: %lld",
							play.remoteAllocTags[i].c_str(),
							play.remoteAllocCounts[i]);
					}
				}
			}
			else
			{
				ImGui::TextUnformatted("Allocs/frame: n/a");
			}
			if (!play.remoteProfile.empty())
			{
				ImGui::Text("Profile (last frame, %u snapshots):",
					play.profileSeq);
				for (PlaySession::RemoteProfileNode const& node :
					play.remoteProfile)
				{
					if (node.calls == 0 && node.milliseconds <= 0.0)
					{
						continue;	// an idle scope this frame - keep the list short
					}
					ImGui::Text("%*s%s: %.3f ms (x%lld)",
						node.depth * 2, "", node.name.c_str(),
						node.milliseconds, node.calls);
				}
			}
		}
	}
	ImGui::End();
}
