// EditorTilePalettePanel.cpp - the Tile Palette panel and the 2D grid-paint
// input. The palette arms a project prefab (click a row) and stamps paint
// options (open edges + tags); the Scene panel then paints/erases prefab
// instances snapped to the grid in 2D editor mode. All authoring rides the
// EditorCore paint seams (paintPrefabAtCell/erasePrefabAtCell) and the generic
// prefab/reflection machinery - the editor knows nothing game-specific.
// Split out of main.cpp's panel set (see EditorApp.h).
#include "EditorApp.h"

#include <core_game/PrefabSerializer.h>
#include <core_game/TileComponent.h>
#include <core_project/AssetDatabase.h>

#include <engine_render/RenderCamera.h>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
	//! split a comma-separated tags field into trimmed, non-empty tags
	Orkige::StringVector parsePaintTags(char const* text)
	{
		Orkige::StringVector tags;
		std::string field = text ? text : "";
		std::size_t start = 0;
		while (start <= field.size())
		{
			std::size_t comma = field.find(',', start);
			if (comma == std::string::npos)
			{
				comma = field.size();
			}
			std::string tag = field.substr(start, comma - start);
			// trim surrounding whitespace
			std::size_t first = tag.find_first_not_of(" \t");
			std::size_t last = tag.find_last_not_of(" \t");
			if (first != std::string::npos)
			{
				tag = tag.substr(first, last - first + 1);
				if (!tag.empty())
				{
					tags.push_back(tag);
				}
			}
			start = comma + 1;
		}
		return tags;
	}
}

bool paletteArmPrefab(EditorState& state, Orkige::EditorCore& core,
	std::string const& absolutePath)
{
	TilePaletteState& palette = state.tilePalette;
	// disarm: "" clears the armed prefab and restores the translate tool
	if (absolutePath.empty())
	{
		palette.armedPrefabPath.clear();
		palette.armedPrefabRef.clear();
		palette.armedPrefabAssetId.clear();
		palette.prefabLocalIds.clear();
		palette.hasEdgeWalls = false;
		palette.rootHasTileComponent = false;
		palette.strokeActive = false;
		if (core.getActiveTool() == Orkige::EditorTool::Paint)
		{
			core.setActiveTool(Orkige::EditorTool::Translate);
		}
		return true;
	}

	Orkige::StringVector locals;
	Orkige::StringVector rootComponents;
	if (!Orkige::PrefabSerializer::listPrefabInfo(absolutePath, locals,
		rootComponents))
	{
		SDL_Log("orkige_editor: cannot arm '%s' - not a readable prefab file",
			absolutePath.c_str());
		return false;
	}

	// derive the project-relative reference + stable asset id (the same wiring
	// the Asset browser's instantiate uses), so a painted instance re-resolves
	// its prefab across renames/moves
	palette.armedPrefabPath = absolutePath;
	palette.armedPrefabRef.clear();
	palette.armedPrefabAssetId.clear();
	if (state.project.isLoaded())
	{
		palette.armedPrefabRef =
			state.project.makeProjectRelative(absolutePath);
		if (optr<Orkige::AssetDatabase> const& database =
			state.project.getAssetDatabase())
		{
			palette.armedPrefabAssetId =
				database->idForPath(palette.armedPrefabRef);
		}
	}
	palette.prefabLocalIds = locals;

	// edge-wall detection: all four conventional wall children present ->
	// the palette offers edge-open toggles for this prefab
	palette.hasEdgeWalls = true;
	for (int edge = 0; edge < Orkige::TileComponent::EDGE_COUNT; ++edge)
	{
		if (std::find(locals.begin(), locals.end(),
			Orkige::TileComponent::EDGE_WALL_LOCAL_IDS[edge]) == locals.end())
		{
			palette.hasEdgeWalls = false;
			break;
		}
	}
	palette.rootHasTileComponent = std::find(rootComponents.begin(),
		rootComponents.end(), "TileComponent") != rootComponents.end();
	palette.strokeActive = false;

	core.setActiveTool(Orkige::EditorTool::Paint);
	return true;
}

Orkige::EditorPaintDesc paletteMakePaintDesc(TilePaletteState const& palette)
{
	Orkige::EditorPaintDesc desc;
	desc.prefabFilePath = palette.armedPrefabPath;
	desc.prefabRef = palette.armedPrefabRef;
	desc.prefabAssetId = palette.armedPrefabAssetId;

	if (palette.hasEdgeWalls)
	{
		// an OPEN edge = its wall child suppressed on the instance AND its bit
		// set in the TileComponent.openEdges stamp
		int mask = 0;
		for (int edge = 0; edge < Orkige::TileComponent::EDGE_COUNT; ++edge)
		{
			if (palette.edgeOpen[edge])
			{
				desc.suppressedChildren.push_back(
					Orkige::TileComponent::EDGE_WALL_LOCAL_IDS[edge]);
				mask |= Orkige::TileComponent::edgeBitForIndex(edge);
			}
		}
		Orkige::EditorPaintStamp stamp;
		stamp.componentTypeName = "TileComponent";
		stamp.addComponent = !palette.rootHasTileComponent;
		stamp.propertyName = "openEdges";
		stamp.value = std::to_string(mask);
		desc.stamps.push_back(stamp);
	}

	desc.tags = parsePaintTags(palette.paintTags);
	return desc;
}

void drawTilePalettePanel(EditorState& state, Orkige::EditorCore& core,
	bool* visible)
{
	if (!ImGui::Begin("Tile Palette###TilePalette", visible))
	{
		ImGui::End();
		return;
	}
	TilePaletteState& palette = state.tilePalette;
	palette.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

	if (!state.project.isLoaded())
	{
		ImGui::TextDisabled("Open a project to paint its prefabs.");
		ImGui::TextDisabled("(File > Open Project...)");
		ImGui::End();
		return;
	}

	// the resolved paint grid (cells coincide with a LevelComponent's slots
	// when the scene carries one, else the translate snap step at the origin)
	const Orkige::EditorPaintGrid grid = core.resolvePaintGrid();
	const bool fromLevel = grid.originX != 0.0f || grid.originY != 0.0f ||
		grid.cellSize != core.getSnapTranslate();
	ImGui::Text("Grid: %.2g cells%s", grid.cellSize,
		fromLevel ? " (from Level)" : " (from snap step)");

	if (!state.tilePalette.armedPrefabPath.empty() &&
		!gViewSettings->editor2D)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
			"Painting needs the 2D view.");
		ImGui::SameLine();
		if (ImGui::SmallButton("Switch to 2D"))
		{
			gViewSettings->editor2D = true;
			gViewSettings->save();
		}
	}
	ImGui::Separator();

	// the prefab list: one selectable row per project prefab (arm/disarm)
	const std::vector<AssetBrowserItem> prefabs =
		searchAssets(state.project, state.project.getAssetsDirectory(), "",
			true, 1u << static_cast<unsigned>(AssetKind::Prefab));
	if (prefabs.empty())
	{
		ImGui::TextDisabled("No prefabs in assets/.");
	}
	ImGui::BeginChild("##prefablist", ImVec2(0.0f, 0.0f), false);
	for (AssetBrowserItem const& item : prefabs)
	{
		const std::string stem = fs::path(item.absolutePath).stem().string();
		const bool armed = palette.armedPrefabPath == item.absolutePath;
		if (ImGui::Selectable((stem + "###" + item.relativePath).c_str(),
			armed))
		{
			// clicking the armed prefab again disarms it
			paletteArmPrefab(state, core, armed ? std::string()
				: item.absolutePath);
		}
	}
	ImGui::EndChild();

	if (!palette.armedPrefabPath.empty())
	{
		ImGui::Separator();
		ImGui::Text("Armed: %s",
			fs::path(palette.armedPrefabPath).stem().string().c_str());
		if (palette.hasEdgeWalls)
		{
			ImGui::TextDisabled("Open edges (suppress the wall):");
			ImGui::Checkbox("Top", &palette.edgeOpen[0]);
			ImGui::SameLine();
			ImGui::Checkbox("Bottom", &palette.edgeOpen[1]);
			ImGui::Checkbox("Left", &palette.edgeOpen[2]);
			ImGui::SameLine();
			ImGui::Checkbox("Right", &palette.edgeOpen[3]);
		}
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##tags", "tags (comma separated)",
			palette.paintTags, sizeof(palette.paintTags));
		if (ImGui::SmallButton("Disarm"))
		{
			paletteArmPrefab(state, core, std::string());
		}
	}
	ImGui::End();
}

bool handleScenePaintInput(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera, ImVec2 const& rectMin,
	ImVec2 const& rectSize, bool editMode, ViewSettings const& viewSettings)
{
	TilePaletteState& palette = state.tilePalette;
	const bool paintMode = editMode && viewSettings.editor2D &&
		core.getActiveTool() == Orkige::EditorTool::Paint &&
		!palette.armedPrefabPath.empty();
	if (!paintMode)
	{
		palette.strokeActive = false;
		return false;
	}

	ImGuiIO& io = ImGui::GetIO();
	const float nx = (io.MousePos.x - rectMin.x) / rectSize.x;
	const float ny = (io.MousePos.y - rectMin.y) / rectSize.y;
	// the 2D ortho camera looks straight down -Z, so the ray origin's XY is the
	// world position under the cursor
	const Orkige::Vec3 world = camera->viewportPointToRay(nx, ny).getOrigin();
	const Orkige::EditorPaintGrid grid = core.resolvePaintGrid();
	const int col = Orkige::paintCellCoord(world.x, grid.originX, grid.cellSize);
	const int row = Orkige::paintCellCoord(world.y, grid.originY, grid.cellSize);
	const float centerX =
		Orkige::paintCellCenter(col, grid.originX, grid.cellSize);
	const float centerY =
		Orkige::paintCellCenter(row, grid.originY, grid.cellSize);

	// hovered-cell highlight: project the four corners and outline the cell
	const float half = grid.cellSize * 0.5f;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 corners[4];
	bool onScreen = true;
	const float cornerX[4] = { centerX - half, centerX + half,
		centerX + half, centerX - half };
	const float cornerY[4] = { centerY - half, centerY - half,
		centerY + half, centerY + half };
	for (int i = 0; i < 4 && onScreen; ++i)
	{
		Orkige::Real px = 0.0f;
		Orkige::Real py = 0.0f;
		if (!camera->projectPoint(Orkige::Vec3(cornerX[i], cornerY[i], 0.0f),
			px, py))
		{
			onScreen = false;
			break;
		}
		corners[i] = ImVec2(rectMin.x + px * rectSize.x,
			rectMin.y + py * rectSize.y);
	}
	if (onScreen)
	{
		drawList->AddQuad(corners[0], corners[1], corners[2], corners[3],
			IM_COL32(255, 220, 80, 220), 2.0f);
	}

	// erase on right-click or Alt+left; paint on plain left. A drag is one undo
	// step (the stroke's merge session); a repaint of the same cell mid-drag is
	// throttled by lastCol/lastRow.
	const bool eraseButton = ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
		(ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyAlt);
	const bool paintButton =
		ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyAlt;
	const bool pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
		ImGui::IsMouseClicked(ImGuiMouseButton_Right);

	if (eraseButton || paintButton)
	{
		if (!palette.strokeActive)
		{
			palette.strokeSession = core.beginMergeSession();
			palette.strokeActive = true;
			palette.lastCol = col;
			palette.lastRow = row;
		}
		else if (!pressed && col == palette.lastCol && row == palette.lastRow)
		{
			// same cell, still dragging - nothing new to do
			return true;
		}
		palette.lastCol = col;
		palette.lastRow = row;
		if (eraseButton)
		{
			core.erasePrefabAtCell(centerX, centerY, grid.cellSize,
				palette.strokeSession);
		}
		else
		{
			core.paintPrefabAtCell(paletteMakePaintDesc(palette),
				centerX, centerY, grid.cellSize, palette.strokeSession);
		}
	}
	else
	{
		palette.strokeActive = false;	// button released - next press = new stroke
	}
	return true;
}
