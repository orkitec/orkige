/********************************************************************
	created:	Saturday 2026/07/12 at 18:00
	filename: 	AnimationPreviewPanel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file AnimationPreviewPanel.cpp
//! @brief the editor's Animation Preview tab: pick a project `.oanim` rig,
//! choose a clip and scrub/play it, and try a same-rig blend - all on the
//! stage's OWN clock (the editor never ticks GameObjects). The pose is
//! CPU-rasterized (VectorShapeRaster) and shown here; the SAME
//! AnimationPreviewStage backs the preview_animation MCP verb, so the human
//! panel and an agent's screenshots see one evaluator.

#include "AnimationPreviewStage.h"
#include "EditorApp.h"
#include "ImGuiFacadeRenderer.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
	//! persistent panel UI state (one editor => one panel => a function static)
	struct AnimPanelState
	{
		std::string					selectedFile;	//!< project-relative .oanim ("" = none)
		std::string					projectRoot;	//!< the project the file list is for
		std::vector<std::string>	animFiles;		//!< project-relative .oanim paths
		std::string					appliedFile;	//!< the file currently loaded in the stage
		int							blendClip = -1;	//!< blend-clip index (-1 = none)
		float						blendWeight = 0.0f;	//!< blend mix 0..1
	};

	//! scan a project for `.oanim` rigs (project-relative, sorted)
	void scanAnimFiles(std::string const& root, std::vector<std::string>& out)
	{
		out.clear();
		if (root.empty())
		{
			return;
		}
		namespace fs = std::filesystem;
		std::error_code ec;
		const fs::path base(root);
		for (fs::recursive_directory_iterator it(base, ec), end;
			!ec && it != end; it.increment(ec))
		{
			if (it->is_regular_file(ec) && it->path().extension() == ".oanim")
			{
				out.push_back(fs::relative(it->path(), base, ec).generic_string());
			}
		}
		std::sort(out.begin(), out.end());
	}
}

void drawAnimationPreviewPanel(EditorState& state,
	OrkigeEditor::AnimationPreviewStage& stage, ViewSettings& viewSettings)
{
	static AnimPanelState ui;

	if (!ImGui::Begin("AnimationPreview", &viewSettings.showAnimationPreviewPanel))
	{
		ImGui::End();
		return;
	}

	const bool projectOpen = state.project.isLoaded();
	const std::string root = projectOpen ? state.project.getRootDirectory()
		: std::string();
	if (!projectOpen)
	{
		ImGui::TextDisabled("Open a project to preview its vector animations.");
		ImGui::End();
		return;
	}

	// refresh the .oanim list when the project changed (cheap; also on demand)
	if (ui.projectRoot != root)
	{
		ui.projectRoot = root;
		scanAnimFiles(root, ui.animFiles);
		ui.selectedFile.clear();
		ui.appliedFile.clear();
	}

	//--- file picker -------------------------------------------------------
	ImGui::SetNextItemWidth(240.0f);
	if (ImGui::BeginCombo("Animation", ui.selectedFile.empty()
		? "(none)" : ui.selectedFile.c_str()))
	{
		for (std::string const& file : ui.animFiles)
		{
			const bool sel = (file == ui.selectedFile);
			if (ImGui::Selectable(file.c_str(), sel))
			{
				ui.selectedFile = file;
			}
			if (sel)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
	{
		scanAnimFiles(root, ui.animFiles);
	}

	// (re)load the stage when the selection changed
	if (ui.selectedFile != ui.appliedFile)
	{
		std::string err;
		stage.load(root, ui.selectedFile, err);
		ui.appliedFile = ui.selectedFile;
		ui.blendClip = -1;
		ui.blendWeight = 0.0f;
		stage.clearBlend();
	}

	if (ui.selectedFile.empty())
	{
		ImGui::TextDisabled("Pick a .oanim rig above to preview it "
			"(imported from a Lottie .json, or authored directly).");
		ImGui::End();
		return;
	}
	if (!stage.isLoaded())
	{
		ImGui::TextWrapped("Could not preview '%s': %s",
			ui.selectedFile.c_str(), stage.getLastError().c_str());
		ImGui::End();
		return;
	}

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

	//--- blend try-out: a SECOND clip mixed in at the same time ------------
	const char* blendLabel = (ui.blendClip >= 0 &&
		ui.blendClip < static_cast<int>(info.clipNames.size()))
		? info.clipNames[ui.blendClip].c_str() : "(none)";
	ImGui::SetNextItemWidth(200.0f);
	if (ImGui::BeginCombo("Blend clip", blendLabel))
	{
		if (ImGui::Selectable("(none)", ui.blendClip < 0))
		{
			ui.blendClip = -1;
			stage.clearBlend();
		}
		for (int each = 0; each < static_cast<int>(info.clipNames.size()); ++each)
		{
			const bool sel = (each == ui.blendClip);
			if (ImGui::Selectable(info.clipNames[each].c_str(), sel))
			{
				ui.blendClip = each;
				stage.setBlend(info.clipNames[each], ui.blendWeight);
			}
		}
		ImGui::EndCombo();
	}
	if (ui.blendClip >= 0)
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::SliderFloat("Weight", &ui.blendWeight, 0.0f, 1.0f, "%.2f"))
		{
			stage.setBlend(info.clipNames[ui.blendClip], ui.blendWeight);
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

	ImGui::End();
}
