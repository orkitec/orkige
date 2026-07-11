// EditorScenePanel.cpp - the Scene viewport: the offscreen RTT, the editor
// grid, camera orbit/fly navigation, click-picking, the ImGuizmo transform
// gizmo and the Scene panel itself.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "ImGuiFacadeRenderer.h"
#include "ImGuiSDL3Input.h"

#include <ImGuizmo.h>

#include <engine_gocomponent/TransformComponent.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderWorld.h>

#include <algorithm>
#include <cmath>

// (re)size the scene RTT: first call creates it (camera + editor viewport
// state), later calls resize-by-recreate behind the facade - the ImGui
// overlay resolves texture ids per draw call, so the one frame that could
// still show the vanished old texture degrades gracefully
void createSceneRenderTexture(SceneRenderTarget& target, int width, int height)
{
	if (!target.texture)
	{
		target.texture = Orkige::RenderSystem::get()->createRenderTexture(
			"EditorSceneRT", static_cast<unsigned int>(width),
			static_cast<unsigned int>(height));
		target.texture->setCamera(target.camera);
		// dark neutral backdrop, in tune with the macOS-dark editor theme
		target.texture->setBackgroundColour(
			Orkige::Color(0.09f, 0.10f, 0.12f));
		target.texture->setShadowsEnabled(true);
		// no 2D overlays in the scene panel (DrawLayer2D never renders
		// into RTTs by contract anyway - this is belt+braces)
		target.texture->setOverlaysEnabled(false);
	}
	else
	{
		// recreates the backend texture, keeps camera + viewport state and
		// re-derives the camera aspect
		target.texture->resize(static_cast<unsigned int>(width),
			static_cast<unsigned int>(height));
	}
	target.width = width;
	target.height = height;
}

// place the scene camera on its orbit sphere around the orbit target
// (the position math lives in EditorCamera.h, shared with the fly mode)
void applyOrbitCamera(EditorState const& state,
	optr<Orkige::RenderNode> const& cameraNode)
{
	cameraNode->setPosition(Orkige::editorCameraPosition(state.camera));
	// Orientation is built EXPLICITLY from the same yaw/pitch that place the
	// camera - NOT via lookAt: Node::lookAt rotates by shortest arc from the
	// CURRENT orientation, and doing that every navigation frame accumulates
	// roll (jiggling the mouse in fly mode visibly tilted the horizon).
	// yaw about world Y, then pitch about local X; -pitch because positive
	// orbit pitch raises the camera, which must look DOWN at the target.
	cameraNode->setOrientation(
		Orkige::Quat(Orkige::Degree(state.camera.yawDeg),
			Orkige::Vec3::UNIT_Y) *
		Orkige::Quat(Orkige::Degree(-state.camera.pitchDeg),
			Orkige::Vec3::UNIT_X));
}

// 2D editor mode camera constants. The camera looks straight down -Z
// at the XY plane from a FIXED standoff (decoupled from the zoom) so sprites at
// any plausible Z stay inside the near/far range - the orbit "distance" drives
// only the orthographic half-extent (the 2D zoom). near/far are generous around
// the standoff: visible Z spans roughly [target.z - 1000, target.z + 999].
static const float EDITOR_2D_CAMERA_STANDOFF = 1000.0f;
static const float EDITOR_2D_NEAR = 1.0f;
static const float EDITOR_2D_FAR = 2000.0f;
// perspective clips restored when leaving 2D (match the historical editor
// camera near/far the boot block documents, 1 / 100000)
static const float EDITOR_PERSPECTIVE_NEAR = 1.0f;
static const float EDITOR_PERSPECTIVE_FAR = 100000.0f;

// 2D editor mode: point the editor's own camera straight down the -Z
// axis at the XY plane and switch it to orthographic. Identity orientation IS
// the look-down-(-Z) pose (Ogre's default camera direction), so the XY plane
// maps 1:1 to the screen (screen +X = world +X, screen +Y = world +Y). No
// yaw/pitch, no scene object involved - a pure view reconfiguration.
void apply2DCamera(EditorState const& state,
	optr<Orkige::RenderCamera> const& camera,
	optr<Orkige::RenderNode> const& cameraNode)
{
	cameraNode->setPosition(Orkige::Vec3(state.camera.target.x,
		state.camera.target.y,
		state.camera.target.z + EDITOR_2D_CAMERA_STANDOFF));
	cameraNode->setOrientation(Orkige::Quat::IDENTITY);
	// the orbit distance doubles as the ortho vertical half-extent (zoom)
	camera->setOrthographic(state.camera.distance,
		EDITOR_2D_NEAR, EDITOR_2D_FAR);
}

// The ground-plane reference grid, all facade: the line list becomes a mesh
// resource through RenderWorld::createLineListMesh (the cube-service
// pattern - shared unlit "VertexColour" look, works on every render flavor)
// and instantiates onto its own root child node; the View menu toggles
// visibility. Query flags 0 keep it invisible to the click-picking ray
// queries (facade queryRay masks against them). Only the scene RTT renders
// it - the window is UI-only (showUIOnlyWindow).
// The returned mesh handle must stay alive with the node (RAII).
optr<Orkige::MeshInstance> createEditorGrid(Orkige::RenderWorld* world,
	optr<Orkige::RenderNode> const& gridNode)
{
	const int halfLineCount = 10;		// lines each side of the axes
	const float spacing = 1.0f;			// one world unit per cell
	const float extent = halfLineCount * spacing;
	const Orkige::Color minorColour(0.32f, 0.32f, 0.32f);
	const Orkige::Color axisXColour(0.75f, 0.30f, 0.30f);	// X axis line
	const Orkige::Color axisZColour(0.30f, 0.45f, 0.85f);	// Z axis line

	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	auto addSegment = [&](Orkige::Vec3 const& from, Orkige::Vec3 const& to,
		Orkige::Color const& colour)
	{
		points.push_back(from);
		points.push_back(to);
		colours.push_back(colour);
		colours.push_back(colour);
	};
	for (int i = -halfLineCount; i <= halfLineCount; ++i)
	{
		const float d = i * spacing;
		// line parallel to the X axis (constant z); the z=0 one IS the X axis
		addSegment(Orkige::Vec3(-extent, 0.0f, d),
			Orkige::Vec3(extent, 0.0f, d),
			(i == 0) ? axisXColour : minorColour);
		// line parallel to the Z axis (constant x); the x=0 one IS the Z axis
		addSegment(Orkige::Vec3(d, 0.0f, -extent),
			Orkige::Vec3(d, 0.0f, extent),
			(i == 0) ? axisZColour : minorColour);
	}
	world->createLineListMesh("EditorGrid.mesh", points.data(),
		colours.data(), points.size());
	optr<Orkige::MeshInstance> grid =
		world->createMeshInstance("EditorGrid.mesh");
	if (grid)
	{
		grid->setCastShadows(false);
		grid->setQueryFlags(0); // never a picking hit
		grid->attachTo(gridNode);
	}
	return grid;
}

// F: frame the selected object - retarget the orbit to the object's world
// bounds centre and fit the orbit distance to its bounding radius
void frameSelectedObject(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera)
{
	if (!core.hasSelection())
	{
		return;
	}
	optr<Orkige::GameObject> gameObject = core.getGameObjectManager()
		.getGameObject(core.getSelectedObjectId()).lock();
	if (!gameObject ||
		!gameObject->hasComponent<Orkige::TransformComponent>())
	{
		return;
	}
	Orkige::TransformComponent* transform =
		gameObject->getComponentPtr<Orkige::TransformComponent>();
	Orkige::Vec3 center = transform->getWorldPosition();
	float radius = 1.0f;
	const Orkige::AABB box = transform->getWorldAABB();
	if (box.isFinite() && !box.isNull())
	{
		center = box.getCenter();
		radius = std::max(box.getHalfSize().length(), 0.25f);
	}
	state.camera.target = center;
	const float halfFov = std::min(
		camera->getFOVy().valueRadians() * 0.5f, 1.2f);
	state.camera.distance = std::clamp(
		radius / std::sin(halfFov) * 1.25f, 2.0f, 200.0f);
}

// double-click focus (select + frame) (Hierarchy entries; the Scene viewport's
// double-click runs its pick first and then frames the result): select the
// object AND frame it - the same orbit retarget/refit the F shortcut does.
// The edittest drives this exact function.
void focusObjectFromDoubleClick(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera, std::string const& id)
{
	core.selectObject(id);
	frameSelectedObject(state, core, camera);
}

// ModelComponent does not serialize material tweaks (yet), so re-apply the
// unlit vertex-colour render state to every model after a scene load
void applyUnlitFixToLoadedModels(Orkige::EditorCore& core)
{
	for (auto const& [id, gameObject] :
		core.getGameObjectManager().getGameObjects())
	{
		core.applyModelFixups(id);
	}
}

// viewport click-picking: cast a camera ray through the click point (facade
// RenderWorld::queryRay, AABB-level, nearest first) and select the nearest
// hit that belongs to a GameObject - a TransformComponent tags its node with
// itself as the user pointer, queryRay walks hits back up to the first tag.
// A Cmd/Ctrl click (additive) toggles the hit's selection-set membership
// instead of replacing the selection. AABB-level picking is right for the
// editor bootstrap; polygon-accurate picking is PhysicsWorld::castRay
// territory (against collision shapes) when the need arrives.
bool pickObjectAtCursor(Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera,
	float normalizedX, float normalizedY, bool additive)
{
	const Orkige::Ray3 ray =
		camera->viewportPointToRay(normalizedX, normalizedY);
	bool picked = false;
	for (Orkige::RenderWorld::RayQueryHit const& hit :
		Orkige::RenderSystem::get()->getWorld()->queryRay(ray))
	{
		if (!hit.userPointer)
		{
			continue; // not GameObject content (grid opts out via query flags)
		}
		// within the engine only TransformComponent tags scene nodes
		// (@see TransformComponent::getComponentFromNode)
		Orkige::GameObject* gameObject =
			static_cast<Orkige::TransformComponent*>(hit.userPointer)
				->getComponentOwner();
		if (gameObject)
		{
			if (additive)
			{
				core.toggleSelection(gameObject->getObjectID());
			}
			else
			{
				core.selectObject(gameObject->getObjectID());
			}
			picked = true;
			break;
		}
	}
	if (!picked && !additive)
	{
		// clicking empty space deselects
		core.clearSelection();
	}
	return picked;
}

// selfcheck helper: compute the viewport-normalized Scene-panel position of
// a GameObject from the RTT camera (facade projectPoint - the old
// worldToViewportNormalized moved behind RenderCamera) and run it through
// pickObjectAtCursor - the same function the Scene panel's mouse path calls
// (the panel image fills the panel content region, so panel-relative and
// viewport-normalized coordinates coincide). Returns false if the object is
// missing or behind the camera.
bool pickGameObjectThroughScenePanel(Orkige::EditorCore& core,
	Orkige::GameObjectManager& gameObjectManager,
	optr<Orkige::RenderCamera> const& camera, std::string const& id)
{
	optr<Orkige::GameObject> gameObject =
		gameObjectManager.getGameObject(id).lock();
	Orkige::Real normalizedX = 0.0f;
	Orkige::Real normalizedY = 0.0f;
	if (!gameObject || !camera->projectPoint(
		gameObject->getComponentPtr<Orkige::TransformComponent>()
			->getPosition(),
		normalizedX, normalizedY))
	{
		return false;
	}
	pickObjectAtCursor(core, camera, normalizedX, normalizedY);
	return true;
}

namespace
{

// The engine Mat4 (Ogre-layout math per RenderMath.h) stores row-major
// (m[row][col]); ImGuizmo expects the usual OpenGL-style column-major
// float16 - copying transposed converts between the two (both directions).
// The facade camera matrices (RenderCamera::getViewMatrix/
// getProjectionMatrix) return the same row-major Mat4 the raw camera did,
// so the transpose convention is unchanged - the gizmo/picking selfchecks
// cover it.
void matrixToImGuizmo(Orkige::Mat4 const& matrix, float* out16)
{
	for (int row = 0; row < 4; ++row)
	{
		for (int col = 0; col < 4; ++col)
		{
			out16[col * 4 + row] = matrix[row][col];
		}
	}
}

Orkige::Mat4 imGuizmoToMatrix(const float* in16)
{
	Orkige::Mat4 matrix;
	for (int row = 0; row < 4; ++row)
	{
		for (int col = 0; col < 4; ++col)
		{
			matrix[row][col] = in16[col * 4 + row];
		}
	}
	return matrix;
}

// The transform gizmo over the Scene panel image: ImGuizmo draws into the
// panel's drawlist (SetDrawlist/SetRect on the image screen rect) with the
// RTT camera's view/projection. A whole drag collapses into ONE undo command
// (merge session opened on drag start, closed on release). Returns true if
// the gizmo owns the mouse (hovered or dragging) - the click-to-pick path
// must stand down then.
bool drawSceneGizmo(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera, ImVec2 const& rectMin,
	ImVec2 const& rectSize, bool editor2D)
{
	const Orkige::EditorTool tool = core.getActiveTool();
	// the gizmo lives in WORLD space (a parented object's local transform is
	// relative to its parent); the undoable command stores LOCAL values
	Orkige::EditorTransform current;
	Orkige::EditorTransform currentLocal;
	// Select and Paint show no transform gizmo (Paint consumes clicks for grid
	// painting; the pick path is bypassed for both)
	if (tool == Orkige::EditorTool::Select ||
		tool == Orkige::EditorTool::Paint || !core.hasSelection() ||
		!core.getObjectWorldTransform(core.getSelectedObjectId(), current) ||
		!core.getObjectTransform(core.getSelectedObjectId(), currentLocal))
	{
		state.gizmoWasUsing = false;
		return false;
	}
	// ImGuizmo needs to know the projection kind: 2D mode renders through the
	// orthographic camera, 3D through the perspective one
	ImGuizmo::SetOrthographic(editor2D);
	ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
	ImGuizmo::SetRect(rectMin.x, rectMin.y, rectSize.x, rectSize.y);

	float view[16];
	float projection[16];
	float model[16];
	matrixToImGuizmo(camera->getViewMatrix(), view);
	matrixToImGuizmo(camera->getProjectionMatrix(), projection);
	Orkige::Mat4 modelMatrix;
	modelMatrix.makeTransform(current.position, current.scale,
		current.orientation);
	matrixToImGuizmo(modelMatrix, model);

	// In 2D mode the manipulation is constrained to the XY plane the
	// camera looks at: translate along X/Y only, rotate about the screen-facing
	// Z axis only, scale X/Y only. Belt-and-braces: even so, the world Z of the
	// result is clamped to its previous value on submit (below), so a finicky
	// ortho gizmo can never push the object off its plane.
	ImGuizmo::OPERATION operation = editor2D
		? (ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y)
		: ImGuizmo::TRANSLATE;
	// the editable snap steps from the toolbar popover (default to the
	// SNAP_* constants)
	float snapValues[3] = { core.getSnapTranslate(),
		core.getSnapTranslate(),
		core.getSnapTranslate() };
	if (tool == Orkige::EditorTool::Rotate)
	{
		operation = editor2D ? ImGuizmo::ROTATE_Z : ImGuizmo::ROTATE;
		snapValues[0] = snapValues[1] = snapValues[2] =
			core.getSnapRotateDegrees();
	}
	else if (tool == Orkige::EditorTool::Scale)
	{
		operation = editor2D
			? (ImGuizmo::SCALE_X | ImGuizmo::SCALE_Y)
			: ImGuizmo::SCALE;
		snapValues[0] = snapValues[1] = snapValues[2] =
			core.getSnapScale();
	}
	// scale is always object-local; translate/rotate follow the X toggle
	const ImGuizmo::MODE mode = (operation != ImGuizmo::SCALE &&
		core.getTransformSpace() == Orkige::EditorTransformSpace::World)
		? ImGuizmo::WORLD : ImGuizmo::LOCAL;
	// snap: toolbar toggle, or held Cmd/Ctrl while dragging
	ImGuiIO& io = ImGui::GetIO();
	const bool snapActive = core.isSnapEnabled() || io.KeySuper || io.KeyCtrl;

	const bool changed = ImGuizmo::Manipulate(view, projection, operation,
		mode, model, nullptr, snapActive ? snapValues : nullptr);
	if (ImGuizmo::IsUsing())
	{
		if (!state.gizmoWasUsing)
		{
			// drag start: everything until release merges into one command
			state.gizmoMergeSession = core.beginMergeSession();
			state.gizmoWasUsing = true;
		}
		if (changed)
		{
			Orkige::EditorTransform afterWorld;
			// gizmo output is affine (no shear) - decompose back to
			// position/scale/orientation (Affine3 extracts the 3x4 part)
			Orkige::Affine3(imGuizmoToMatrix(model)).decomposition(
				afterWorld.position, afterWorld.scale,
				afterWorld.orientation);
				// 2D mode: pin the object to its XY plane - keep the world Z it
				// had before the drag (the axis-constrained operations already
				// avoid Z; this guards against float drift on submit)
				if (editor2D)
				{
					afterWorld.position.z = current.position.z;
				}
			// world -> parent-relative local: the command stores what the
			// Inspector shows and the scene serializes
			Orkige::EditorTransform afterLocal;
			if (core.worldToLocalTransform(core.getSelectedObjectId(),
				afterWorld, afterLocal))
			{
				core.applyTransformChange(core.getSelectedObjectId(),
					currentLocal, afterLocal, state.gizmoMergeSession);
			}
		}
	}
	else if (state.gizmoWasUsing)
	{
		state.gizmoWasUsing = false; // drag ended - next drag = new undo step
	}
	return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}

} // namespace

// The Scene panel: displays the offscreen scene texture, records the size
// the RTT should have (applied with hysteresis in the frame loop) and hosts
// the in-panel interactions - the transform gizmo (input priority), left
// click picks (panel-relative mouse coords map 1:1 to viewport-normalized
// coords because the image always fills the content region), and the camera:
// right-HOLD = fly mode (true relative-mouse mouselook + WASD move, Q/E
// down/up, Shift = boost, scroll tunes the fly speed), Alt+left drag =
// classic orbit, middle-drag pans, scroll zooms. Fly mode captures the mouse
// via imguiInput.setRelativeMode (cursor hidden, raw relative counts drive
// the look, cursor restored on release).
void drawScenePanel(EditorState& state, Orkige::EditorCore& core,
	bool editMode, SceneRenderTarget& sceneTarget,
	optr<Orkige::RenderNode> const& cameraNode,
	ViewSettings& viewSettings, float contentScale,
	Orkige::ImGuiSDL3Input& imguiInput)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin("Scene", &viewSettings.showScenePanel);
	ImGui::PopStyleVar();
	state.scenePanelHovered = false;
	state.scenePanelFocused = open && ImGui::IsWindowFocused();
	if (open)
	{
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		state.scenePanelWidth = static_cast<int>(avail.x);
		state.scenePanelHeight = static_cast<int>(avail.y);
		if (sceneTarget.texture && avail.x >= 1.0f && avail.y >= 1.0f)
		{
			// the RTT binds by facade HANDLE (ImGuiFacadeRenderer registry;
			// DrawLayer2D re-resolves the current backend texture per draw,
			// so the id is stable across resizes on every render flavor)
			ImGui::Image(gImGuiRenderer->textureIdFor(sceneTarget.texture),
				avail);
			const ImVec2 rectMin = ImGui::GetItemRectMin();
			state.scenePanelHovered = ImGui::IsItemHovered();
			// Asset browser drop: a mesh/texture/prefab dragged from the
			// Assets panel onto the viewport instantiates at the origin
			// (ray/ground-plane placement is deferred - origin on the
			// XY plane is natural in 2D editor mode). Only while editing the
			// local scene (the panels show the remote scene during play).
			if (editMode)
			{
				handleAssetDropTarget(state, core);
			}
			// gizmo first: while it is hovered/dragged the click-pick and
			// the camera drags stand down (input priority). Editing the
			// local scene is pointless while the panels show the remote one.
			const bool gizmoOwnsMouse = editMode &&
				drawSceneGizmo(state, core, sceneTarget.camera, rectMin, avail,
					viewSettings.editor2D);
			// axis orientation gizmo (top-right corner): displays the camera
			// orientation and drives the orbit - ImGuizmo manipulates the
			// view matrix around a pivot orbitDistance away (the orbit
			// target), so the new camera pose decomposes straight back into
			// the orbit yaw/pitch. While it is hovered/dragged, picking and
			// the camera drags stand down like for the transform gizmo.
			// mutual exclusion: while a fly/orbit/pan drag is running, the
			// corner gizmo must NOT also write camera state (both paths
			// mutate the same yaw/pitch - running them simultaneously made
			// the view fight itself and "rotate weirdly")
			bool viewGizmoOwnsMouse = false;
			// the orbit corner gizmo is meaningless in 2D (no yaw/pitch) - the
			// 2D camera looks straight down -Z, so it is hidden there
			if (viewSettings.showViewGizmo && !viewSettings.editor2D &&
				!state.flyActive &&
				!state.orbitActive && !state.panActive)
			{
				// the corner gizmo and its inset are drawn straight into the
				// draw list in render-target pixels, so they must scale with the
				// content scale to keep a constant physical size on retina (the
				// themed chrome already does via ScaleAllSizes)
				const float viewGizmoSize = 96.0f * contentScale;
				const float viewGizmoInset = 8.0f * contentScale;
				if (avail.x > viewGizmoSize * 1.5f &&
					avail.y > viewGizmoSize * 1.5f)
				{
					ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
					float view[16];
					matrixToImGuizmo(sceneTarget.camera->getViewMatrix(),
						view);
					float viewBefore[16];
					std::memcpy(viewBefore, view, sizeof(view));
					ImGuizmo::ViewManipulate(view, state.camera.distance,
						ImVec2(rectMin.x + avail.x - viewGizmoSize - viewGizmoInset,
							rectMin.y + viewGizmoInset),
						ImVec2(viewGizmoSize, viewGizmoSize), 0x00000000);
					viewGizmoOwnsMouse = ImGuizmo::IsUsingViewManipulate() ||
						ImGuizmo::IsViewManipulateHovered();
					if (std::memcmp(viewBefore, view, sizeof(view)) != 0)
					{
						// decompose the manipulated view back into the orbit
						// spherical coordinates (distance stays fixed)
						const Orkige::Mat4 inverseView =
							imGuizmoToMatrix(view).inverse();
						const Orkige::Vec3 cameraPos(inverseView[0][3],
							inverseView[1][3], inverseView[2][3]);
						const Orkige::Vec3 offset =
							cameraPos - state.camera.target;
						const float distance = offset.length();
						if (distance > 1e-3f)
						{
							state.camera.pitchDeg = std::clamp(
								Orkige::Radian(std::asin(
									offset.y / distance)).valueDegrees(),
								-85.0f, 85.0f);
							state.camera.yawDeg = Orkige::Radian(std::atan2(
								offset.x, offset.z)).valueDegrees();
						}
					}
				}
			}
			ImGuiIO& io = ImGui::GetIO();
			// the 2D grid-paint tool consumes clicks for painting/erasing (the
			// pick path stands down); pan + scroll-zoom keep working
			bool paintOwnsMouse = false;
			if (state.scenePanelHovered && !gizmoOwnsMouse &&
				!viewGizmoOwnsMouse)
			{
				paintOwnsMouse = handleScenePaintInput(state, core,
					sceneTarget.camera, rectMin, avail, editMode, viewSettings);
			}
			if (state.scenePanelHovered && !gizmoOwnsMouse &&
				!viewGizmoOwnsMouse)
			{
				// Alt+left starts an orbit drag, a plain left click picks
				if (!paintOwnsMouse &&
					ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
				{
					// Cmd/Ctrl+click toggles selection-set membership
					pickObjectAtCursor(core, sceneTarget.camera,
						(io.MousePos.x - rectMin.x) / avail.x,
						(io.MousePos.y - rectMin.y) / avail.y,
						io.KeySuper || io.KeyCtrl);
					// double-click: the pick above selected the
					// hit - frame it too (a double-click on empty space just
					// cleared the selection; frameSelectedObject no-ops then)
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						frameSelectedObject(state, core, sceneTarget.camera);
					}
				}
				if (io.MouseWheel != 0.0f && !state.flyActive)
				{
					// scroll up zooms in (while flying the wheel tunes the
					// fly speed instead, below)
					state.camera.distance = std::clamp(state.camera.distance *
						std::pow(0.9f, io.MouseWheel * viewSettings.zoomSpeed),
						2.0f, 200.0f);
				}
				// the camera modes are mutually exclusive - a second button
				// pressed mid-drag must not start a competing mode that
				// would double-apply deltas onto the same yaw/pitch
				// 2D mode: orbit and fly are disabled - only pan and
				// scroll-zoom navigate the top-down orthographic view. Picking
				// (above) stays live.
				if (!viewSettings.editor2D &&
					ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
					!state.orbitActive && !state.panActive &&
					!state.flyActive)
				{
					// fly begins: capture the mouse (relative mode - cursor
					// hidden, look input arrives as raw xrel/yrel counts)
					state.flyActive = true;
					imguiInput.setRelativeMode(true);
				}
				if (!viewSettings.editor2D &&
					ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyAlt &&
					!state.flyActive && !state.panActive)
				{
					state.orbitActive = true;
				}
				if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
					!state.flyActive && !state.orbitActive)
				{
					state.panActive = true;
				}
			}
			// fly/orbit/pan keep going while their button is held, even when
			// the cursor leaves the panel mid-drag.
			// Mouse deltas come in TWO units here: fly mode reads the raw
			// relative-mode counts accumulated by ImGuiSDL3Input (1:1 with
			// physical mouse travel, NO retina/content scale applies), while
			// orbit/pan still use io.MouseDelta - ImGui's coordinate space =
			// render-target PIXELS (window points x backing-store factor),
			// so those divide by the content scale to get back to the
			// per-point sensitivities.
			if (state.flyActive)
			{
				// drain the relative motion every fly frame (even gated ones
				// below - a stale first-frame delta must not leak into the
				// second frame)
				float flyLookDeltaX = 0.0f;
				float flyLookDeltaY = 0.0f;
				imguiInput.consumeRelativeDelta(flyLookDeltaX, flyLookDeltaY);
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
				{
					// releasing the right button ends fly mode; the orbit
					// target is already "distance units ahead" (flyCameraStep
					// keeps it there), so orbit behavior stays sane - release
					// the mouse capture (restores the pre-fly cursor
					// position) and persist a scroll-tuned fly speed now
					state.flyActive = false;
					state.flyLookGate.update(false);
					imguiInput.setRelativeMode(false);
					viewSettings.save();
				}
				else
				{
					Orkige::FlyInput fly;
					// the hold's first frame may still carry a bogus delta
					// (absolute-motion backlog from before the capture, or a
					// platform-synthesized jump on entering relative mode) -
					// the gate swallows it (WASD movement is unaffected)
					if (state.flyLookGate.update(true))
					{
						fly.lookDeltaX = flyLookDeltaX;
						fly.lookDeltaY = flyLookDeltaY;
					}
					fly.moveForward = ImGui::IsKeyDown(ImGuiKey_W);
					fly.moveBack = ImGui::IsKeyDown(ImGuiKey_S);
					fly.moveLeft = ImGui::IsKeyDown(ImGuiKey_A);
					fly.moveRight = ImGui::IsKeyDown(ImGuiKey_D);
					fly.moveDown = ImGui::IsKeyDown(ImGuiKey_Q);
					fly.moveUp = ImGui::IsKeyDown(ImGuiKey_E);
					fly.boost = io.KeyShift;
					fly.speedScroll = io.MouseWheel;
					Orkige::flyCameraStep(state.camera, fly, io.DeltaTime,
						viewSettings.lookSpeed, viewSettings.flySpeed);
				}
			}
			if (state.orbitActive)
			{
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					state.orbitActive = false;
					state.orbitDragGate.update(false);
				}
				else if (state.orbitDragGate.update(true))
				{
					state.camera.yawDeg -= io.MouseDelta.x / contentScale *
						viewSettings.orbitSpeed;
					state.camera.pitchDeg = std::clamp(state.camera.pitchDeg +
						io.MouseDelta.y / contentScale *
							viewSettings.orbitSpeed,
						-85.0f, 85.0f);
				}
			}
			if (state.panActive)
			{
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				{
					state.panActive = false;
					state.panDragGate.update(false);
				}
				else if (state.panDragGate.update(true))
				{
					// slide the orbit target along the camera plane; the
					// factor scales with distance so a point of mouse travel
					// moves the scene about the same visual amount
					const float panScale = state.camera.distance * 0.003f;
					if (viewSettings.editor2D)
					{
						// 2D: the view is axis-aligned (screen right = world +X,
						// screen up = world +Y), so pan the target directly in
						// the XY plane - never through the node orientation,
						// which may still be the orbit pose on the transition
						// frame
						state.camera.target += Orkige::Vec3(
							-io.MouseDelta.x / contentScale * panScale,
							io.MouseDelta.y / contentScale * panScale, 0.0f);
					}
					else
					{
						state.camera.target += cameraNode->getOrientation() *
							Orkige::Vec3(
								-io.MouseDelta.x / contentScale * panScale,
								io.MouseDelta.y / contentScale * panScale, 0.0f);
					}
				}
			}
			if (viewSettings.editor2D)
			{
				apply2DCamera(state, sceneTarget.camera, cameraNode);
			}
			else
			{
				// restore the perspective projection when leaving 2D (the
				// camera is still orthographic on the 2D->3D transition frame)
				if (sceneTarget.camera->getProjectionType() ==
					Orkige::RenderCamera::PT_ORTHOGRAPHIC)
				{
					sceneTarget.camera->setPerspective(
						Orkige::Degree(viewSettings.fovDeg),
						EDITOR_PERSPECTIVE_NEAR, EDITOR_PERSPECTIVE_FAR);
				}
				applyOrbitCamera(state, cameraNode);
			}
		}
	}
	ImGui::End();
}
