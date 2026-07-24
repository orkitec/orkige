// EditorTilePalettePanel.cpp - the Tile Palette panel and the 2D grid-paint
// input. The palette arms a project prefab (click a row) and stamps paint
// options (open edges + tags); the Scene panel then paints/erases prefab
// instances snapped to the grid in 2D editor mode. All authoring rides the
// EditorCore paint seams (paintTileAtCell/eraseTileAtCell) and the generic
// prefab/reflection machinery - the editor knows nothing game-specific.
// Split out of main.cpp's panel set (see EditorApp.h).
#include "EditorApp.h"
#include "EditorTabMenu.h"
#include "EditorTheme.h"

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

	//! @brief probe a prefab FILE for a representative preview ref: the first
	//! SpriteComponent "texture" (else the first VectorShapeComponent "shape")
	//! any prefab object carries, read straight from the .oprefab through the
	//! PrefabSerializer without instantiating - no live scene/undo state is
	//! touched. Returns "" for a pure-logic prefab with no drawable (the paint
	//! tool then shows only the outline).
	std::string probePrefabPreviewRef(std::string const& prefabPath)
	{
		return Orkige::PrefabSerializer::readComponentPropertyRef(prefabPath,
			{ { "SpriteComponent", "texture" },
			  { "VectorShapeComponent", "shape" } });
	}

	//! @brief resolve a bare asset ref (texture/shape file name) to an absolute
	//! path, through the project's AssetDatabase - the same id-tracked lookup the
	//! asset browser thumbnails use, so a duplicate basename resolves the SAME
	//! way the engine's resource system does (idForFileName is deterministic on a
	//! clash) rather than to whatever a raw filesystem walk hit first. "" when no
	//! project is open, the ref is untracked, or the file is missing.
	std::string resolvePreviewImagePath(Orkige::Project const& project,
		std::string const& ref)
	{
		if (ref.empty() || !project.isLoaded())
		{
			return std::string();
		}
		optr<Orkige::AssetDatabase> const& database = project.getAssetDatabase();
		if (!database)
		{
			return std::string();
		}
		const Orkige::String assetId = database->idForFileName(ref);
		if (assetId.empty())
		{
			return std::string();
		}
		const Orkige::String relativePath = database->pathForId(assetId);
		if (relativePath.empty())
		{
			return std::string();
		}
		const fs::path absolute =
			fs::path(database->getRootDirectory()) / relativePath;
		std::error_code ec;
		if (!fs::exists(absolute, ec))
		{
			return std::string();
		}
		return absolute.string();
	}
}

void disarmPaintTileOnIntent(EditorState& state, Orkige::EditorCore& core)
{
	if (!state.tilePalette.armedAssetPath.empty())
	{
		paletteArmAsset(state, core, std::string());
	}
}

bool paletteArmAsset(EditorState& state, Orkige::EditorCore& core,
	std::string const& absolutePath)
{
	// paint places ROOT-level grid objects - incompatible with the prefab
	// stage's single-root contract (disarming stays allowed: opening a stage
	// disarms through this same seam)
	if (!absolutePath.empty() && prefabEditBlocks(state, "Paint"))
	{
		return false;
	}
	TilePaletteState& palette = state.tilePalette;
	// disarm: "" clears the armed asset and restores the translate tool
	if (absolutePath.empty())
	{
		palette.armedAssetPath.clear();
		palette.armedAssetRef.clear();
		palette.armedAssetId.clear();
		palette.armedKind = AssetKind::Prefab;
		palette.previewImageRef.clear();
		palette.previewImagePath.clear();
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

	// only three kinds are paintable: a prefab (instantiated) or a bare texture/
	// .oshape (painted as a bare sprite/shape tile - no prefab file)
	const AssetKind kind = classifyAsset(absolutePath);
	if (kind != AssetKind::Prefab && kind != AssetKind::Texture &&
		kind != AssetKind::VectorShape)
	{
		oDebugWarn("editor.tiles", 0, "cannot arm '" << absolutePath <<
			"' - not a paintable asset (prefab / texture / shape)");
		return false;
	}

	Orkige::StringVector locals;
	Orkige::StringVector rootComponents;
	if (kind == AssetKind::Prefab &&
		!Orkige::PrefabSerializer::listPrefabInfo(absolutePath, locals,
			rootComponents))
	{
		oDebugWarn("editor.tiles", 0, "cannot arm '" << absolutePath <<
			"' - not a readable prefab file");
		return false;
	}

	// derive the project-relative reference + stable asset id (the same wiring
	// the Asset browser's instantiate uses), so a painted tile re-resolves its
	// source across renames/moves
	palette.armedAssetPath = absolutePath;
	palette.armedKind = kind;
	palette.armedAssetRef.clear();
	palette.armedAssetId.clear();
	if (state.project.isLoaded())
	{
		palette.armedAssetRef =
			state.project.makeProjectRelative(absolutePath);
		if (optr<Orkige::AssetDatabase> const& database =
			state.project.getAssetDatabase())
		{
			palette.armedAssetId =
				database->idForPath(palette.armedAssetRef);
		}
	}
	palette.prefabLocalIds = locals;
	palette.strokeActive = false;

	if (kind == AssetKind::Prefab)
	{
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
		// ghost preview: probe the prefab for a representative texture/shape and
		// resolve it to an absolute path the thumbnail machinery loads (empty for
		// a pure-logic prefab - the paint tool then falls back to the outline)
		palette.previewImageRef = probePrefabPreviewRef(absolutePath);
		palette.previewImagePath =
			resolvePreviewImagePath(state.project, palette.previewImageRef);
	}
	else
	{
		// a bare texture/shape: no prefab structure, and its OWN thumbnail is the
		// ghost (the browser thumbnail machinery loads a texture directly and
		// CPU-rasterizes an .oshape)
		palette.hasEdgeWalls = false;
		palette.rootHasTileComponent = false;
		palette.previewImageRef =
			fs::path(absolutePath).filename().string();
		palette.previewImagePath = absolutePath;
	}

	core.setActiveTool(Orkige::EditorTool::Paint);
	return true;
}

Orkige::EditorPaintDesc paletteMakePaintDesc(TilePaletteState const& palette)
{
	Orkige::EditorPaintDesc desc;
	if (palette.armedKind != AssetKind::Prefab)
	{
		// a BARE-asset tile: a sprite (texture) or shape (.oshape) object, sized
		// to the grid cell, carrying a TileComponent that stamps the source id
		desc.kind = palette.armedKind == AssetKind::VectorShape
			? Orkige::PaintTileKind::ShapeTile
			: Orkige::PaintTileKind::SpriteTile;
		desc.assetName = fs::path(palette.armedAssetPath).filename().string();
		desc.assetRef = palette.armedAssetRef;
		desc.assetId = palette.armedAssetId;
		desc.tags = parsePaintTags(palette.paintTags);
		return desc;
	}

	desc.kind = Orkige::PaintTileKind::Prefab;
	desc.prefabFilePath = palette.armedAssetPath;
	desc.prefabRef = palette.armedAssetRef;
	desc.prefabAssetId = palette.armedAssetId;

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

ImTextureID paletteTileThumbnail(EditorState& state,
	AssetBrowserItem const& item)
{
	// a prefab shows its probed primary visual, a bare texture/shape shows
	// itself; "" (a pure-logic prefab) => 0, a generic tile
	const std::string thumbPath = item.kind == AssetKind::Prefab
		? resolvePreviewImagePath(state.project,
			probePrefabPreviewRef(item.absolutePath))
		: item.absolutePath;
	return thumbPath.empty() ? 0 : assetThumbnailFor(state, thumbPath);
}

void drawTilePalettePanel(EditorState& state, Orkige::EditorCore& core,
	bool* visible)
{
	const bool paletteShown = ImGui::Begin("Tile Palette###TilePalette", visible);
	OrkigeEditor::editorPanelTabMenu(visible);
	if (!paletteShown)
	{
		ImGui::End();
		return;
	}
	TilePaletteState& palette = state.tilePalette;
	palette.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

	if (!state.project.isLoaded())
	{
		ImGui::TextDisabled("Open a project to paint its tiles.");
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

	if (!state.tilePalette.armedAssetPath.empty() &&
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

	// the paintable tiles: one THUMBNAIL per project prefab, texture and .oshape.
	// A prefab instantiates its subtree; a bare texture/shape paints a bare tile.
	// Textures/shapes show their own thumbnail (the asset-browser cache); a
	// prefab shows its primary visual (its sprite texture or .oshape) through the
	// same cache, or a generic prefab tile when it has no cheap drawable.
	const unsigned int paintableMask =
		(1u << static_cast<unsigned>(AssetKind::Prefab)) |
		(1u << static_cast<unsigned>(AssetKind::Texture)) |
		(1u << static_cast<unsigned>(AssetKind::VectorShape));
	const std::vector<AssetBrowserItem> paintables =
		searchAssets(state.project, state.project.getAssetsDirectory(), "",
			true, paintableMask);
	if (paintables.empty())
	{
		ImGui::TextDisabled("No prefabs, textures or shapes in assets/.");
	}
	else if (palette.armedAssetPath.empty())
	{
		// mode-gating hint: the Paint tool (B) stays inert until a tile is armed
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
			"Select a tile to arm the Paint tool (B).");
	}

	// the tile grid sits on a recessed region ground (a step darker than the
	// panel) so the browsing area separates from the controls/hint strip above
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Orkige::editorRegionBackground());
	ImGui::BeginChild("##paintgrid", ImVec2(0.0f, 0.0f), false);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImGuiStyle& style = ImGui::GetStyle();
	const float tileSize = ImGui::GetFontSize() * 3.5f;	// DPI-aware
	const float step = tileSize + style.ItemSpacing.x;
	const float avail = ImGui::GetContentRegionAvail().x;
	const int perRow = std::max(1,
		static_cast<int>((avail + style.ItemSpacing.x) / step));
	int col = 0;
	for (AssetBrowserItem const& item : paintables)
	{
		ImGui::PushID(item.relativePath.c_str());
		const bool armed = palette.armedAssetPath == item.absolutePath;
		// the tile's thumbnail (prefab primary visual / the bare texture-shape
		// itself; 0 => a generic tile drawn below)
		const ImTextureID thumb = paletteTileThumbnail(state, item);

		const ImVec2 tileMin = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::Selectable("##tile", armed,
			ImGuiSelectableFlags_None, ImVec2(tileSize, tileSize));
		const ImVec2 tileMax(tileMin.x + tileSize, tileMin.y + tileSize);
		if (thumb)
		{
			drawList->AddImageRounded(thumb, tileMin, tileMax,
				ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, 4.0f);
		}
		else
		{
			// generic tile: a rounded backing + a short label, so a pure-logic
			// prefab (no drawable) still reads as a distinct tile
			drawList->AddRectFilled(tileMin, tileMax,
				IM_COL32(58, 62, 70, 255), 4.0f);
			const char* glyph =
				item.kind == AssetKind::Prefab ? "prefab" : "?";
			const ImVec2 gsz = ImGui::CalcTextSize(glyph);
			drawList->AddText(
				ImVec2(tileMin.x + (tileSize - gsz.x) * 0.5f,
					tileMin.y + (tileSize - gsz.y) * 0.5f),
				IM_COL32(200, 205, 215, 255), glyph);
		}
		// selection highlight: a bright border framing the armed tile
		if (armed)
		{
			drawList->AddRect(tileMin, tileMax, IM_COL32(255, 210, 70, 255),
				4.0f, 0, 2.5f);
		}
		if (clicked)
		{
			// clicking the armed tile again disarms it
			paletteArmAsset(state, core, armed ? std::string()
				: item.absolutePath);
		}
		// a palette tile is ALSO a drag source: dropping it on the 2D grid paints
		// the drop cell (the Scene panel's handleSceneDropTarget), the same
		// ORKIGE_ASSET payload the Asset browser emits (its own kind)
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			AssetDragDropPayload payload;
			payload.kind = item.kind;
			SDL_strlcpy(payload.path, item.absolutePath.c_str(),
				sizeof(payload.path));
			ImGui::SetDragDropPayload(ASSET_DND_PAYLOAD, &payload,
				sizeof(payload));
			ImGui::TextUnformatted(
				fs::path(item.absolutePath).stem().string().c_str());
			ImGui::EndDragDropSource();
		}
		ImGui::SetItemTooltip("%s%s",
			fs::path(item.absolutePath).stem().string().c_str(),
			item.kind == AssetKind::Prefab ? " (prefab)" : "");
		if (++col < perRow)
		{
			ImGui::SameLine();
		}
		else
		{
			col = 0;
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();	// region ground

	if (!palette.armedAssetPath.empty())
	{
		ImGui::Separator();
		const bool bareTile = palette.armedKind != AssetKind::Prefab;
		ImGui::Text("Armed: %s%s",
			fs::path(palette.armedAssetPath).stem().string().c_str(),
			bareTile ? "" : " (prefab)");
		if (bareTile)
		{
			// bare tiles all share the one texture/shape asset: edit the art and
			// every painted tile updates (no per-tile prefab to re-apply)
			ImGui::TextDisabled("Bare tile - all painted tiles share this asset.");
		}
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
			paletteArmAsset(state, core, std::string());
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
		!palette.armedAssetPath.empty();
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
		// ghost preview: the armed prefab's texture/shape thumbnail, drawn in the
		// hovered cell at ~50% alpha under the outline (corners run bottom-left,
		// bottom-right, top-right, top-left in world XY; the image's top row maps
		// to the cell's +Y edge). Falls back to the outline alone when the prefab
		// has no drawable preview (previewImagePath empty / not yet cached).
		if (!palette.previewImagePath.empty())
		{
			const ImTextureID ghost =
				assetThumbnailFor(state, palette.previewImagePath);
			if (ghost)
			{
				drawList->AddImageQuad(ghost, corners[3], corners[2],
					corners[1], corners[0], ImVec2(0.0f, 0.0f),
					ImVec2(1.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec2(0.0f, 1.0f),
					IM_COL32(255, 255, 255, 128));
			}
		}
		drawList->AddQuad(corners[0], corners[1], corners[2], corners[3],
			IM_COL32(255, 220, 80, 220), 2.0f);
	}

	// mode banner: while armed, paint consumes every viewport click - say so
	// on screen and name the exits
	const std::string bannerText = "Painting '" +
		fs::path(palette.armedAssetPath).stem().string() +
		"'   left: paint   right/Alt: erase   Esc: stop";
	const ImVec2 bannerPos(rectMin.x + 10.0f, rectMin.y + 8.0f);
	const ImVec2 bannerSize = ImGui::CalcTextSize(bannerText.c_str());
	drawList->AddRectFilled(
		ImVec2(bannerPos.x - 6.0f, bannerPos.y - 4.0f),
		ImVec2(bannerPos.x + bannerSize.x + 6.0f,
			bannerPos.y + bannerSize.y + 4.0f),
		IM_COL32(20, 20, 20, 200), 4.0f);
	drawList->AddText(bannerPos, IM_COL32(255, 220, 80, 255),
		bannerText.c_str());

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
			core.eraseTileAtCell(centerX, centerY, grid.cellSize,
				palette.strokeSession);
		}
		else
		{
			core.paintTileAtCell(paletteMakePaintDesc(palette),
				centerX, centerY, grid.cellSize, palette.strokeSession);
		}
	}
	else
	{
		palette.strokeActive = false;	// button released - next press = new stroke
	}
	return true;
}
