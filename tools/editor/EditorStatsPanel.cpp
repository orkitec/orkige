// EditorStatsPanel.cpp - the Stats panel (4 Hz windowed avg/min/max frame
// stats + rolling frame-time plot).
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <engine_render/RenderSystem.h>

// Stats panel: samples getFrameStats() every frame but refreshes the
// DISPLAYED numbers at ~4 Hz - lastFPS is the reciprocal of a single frame's
// duration, so printing it raw turns sub-millisecond timing noise into a
// wildly flickering label. The window shows avg/min/max over the refresh
// interval, frame time in ms (the budget number) next to FPS, and a rolling
// frame-time plot of the last 120 samples.
void drawStatsPanel(bool* visible)
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
	}
	ImGui::End();
}
