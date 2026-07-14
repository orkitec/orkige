/********************************************************************
	created:	Saturday 2026/07/12 at 18:00
	filename: 	AnimationPreviewPanel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file AnimationPreviewPanel.cpp
//! @brief the shared `.oanim` preview WIDGET: choose a clip and scrub/play it,
//! and try a same-rig blend - all on the stage's OWN clock (the editor never
//! ticks GameObjects). The pose is CPU-rasterized (VectorShapeRaster) and shown
//! here; the SAME AnimationPreviewStage backs the preview_animation MCP verb, so
//! the Inspector's animation section and an agent's screenshots see one
//! evaluator. (The standalone Animation Preview panel was retired - the
//! Inspector shows this widget when a .oanim asset is selected.)

#include "AnimationPreviewStage.h"
#include "EditorApp.h"
#include "ImGuiFacadeRenderer.h"

#include <imgui.h>

#include <algorithm>
#include <string>

void drawAnimationPreviewBody(OrkigeEditor::AnimationPreviewStage& stage)
{
	const OrkigeEditor::AnimationPreviewInfo info = stage.getInfo();

	//--- clip + playback ---------------------------------------------------
	const int clipIndex = info.clipIndex;
	const char* clipLabel = (clipIndex >= 0 &&
		clipIndex < static_cast<int>(info.clipNames.size()))
		? info.clipNames[clipIndex].c_str() : "(none)";
	ImGui::SetNextItemWidth(200.0f);
	if (ImGui::BeginCombo("Clip", clipLabel))
	{
		for (int each = 0; each < static_cast<int>(info.clipNames.size()); ++each)
		{
			const bool sel = (each == clipIndex);
			if (ImGui::Selectable(info.clipNames[each].c_str(), sel))
			{
				stage.setClipIndex(each);
			}
			if (sel)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	bool playing = stage.isPlaying();
	ImGui::SameLine();
	if (ImGui::Button(playing ? "Pause" : "Play"))
	{
		stage.setPlaying(!playing);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset"))
	{
		stage.setTimeSeconds(0.0f);
	}

	// scrub slider over the clip's seconds (pausing while dragging)
	float t = info.timeSeconds;
	const float clipSeconds = info.clipDurationSeconds > 0.0f
		? info.clipDurationSeconds : 1.0f;
	ImGui::SetNextItemWidth(300.0f);
	if (ImGui::SliderFloat("Time (s)", &t, 0.0f, clipSeconds, "%.2f"))
	{
		stage.setPlaying(false);
		stage.setTimeSeconds(t);
	}

	//--- blend try-out: a SECOND clip mixed in at the same time (stage-backed)
	const int blendClip = info.blendClipIndex;
	const char* blendLabel = (blendClip >= 0 &&
		blendClip < static_cast<int>(info.clipNames.size()))
		? info.clipNames[blendClip].c_str() : "(none)";
	ImGui::SetNextItemWidth(200.0f);
	if (ImGui::BeginCombo("Blend clip", blendLabel))
	{
		if (ImGui::Selectable("(none)", blendClip < 0))
		{
			stage.clearBlend();
		}
		for (int each = 0; each < static_cast<int>(info.clipNames.size()); ++each)
		{
			const bool sel = (each == blendClip);
			if (ImGui::Selectable(info.clipNames[each].c_str(), sel))
			{
				stage.setBlend(info.clipNames[each], info.blendWeight);
			}
		}
		ImGui::EndCombo();
	}
	if (blendClip >= 0)
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(200.0f);
		float weight = info.blendWeight;
		if (ImGui::SliderFloat("Weight", &weight, 0.0f, 1.0f, "%.2f"))
		{
			stage.setBlend(info.clipNames[blendClip], weight);
		}
	}

	// advance the OWN clock and re-raster the pose into a texture
	stage.tick(ImGui::GetIO().DeltaTime);
	const std::string uploadName = stage.uploadTexture();

	//--- status + image ----------------------------------------------------
	ImGui::Separator();
	ImGui::Text("frame %.1f / %.0f  |  %d layers, %d shapes, %d verts%s",
		info.frame, info.durationFrames, info.layerCount, info.shapeCount,
		info.vertexCount,
		info.blending ? "  |  blending" : (info.atEnd ? "  |  ended" : ""));

	if (!uploadName.empty() && gImGuiRenderer)
	{
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float side = std::max(64.0f, std::min(avail.x, avail.y));
		const float offsetX = (avail.x - side) * 0.5f;
		if (offsetX > 0.0f)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
		}
		ImGui::Image(gImGuiRenderer->textureIdForResource(uploadName),
			ImVec2(side, side));
	}
}
