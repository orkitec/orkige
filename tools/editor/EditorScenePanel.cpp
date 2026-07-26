// EditorScenePanel.cpp - the Scene viewport: the offscreen RTT, the editor
// grid, camera orbit/fly navigation, click-picking, the ImGuizmo transform
// gizmo and the Scene panel itself.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "EditorTabMenu.h"
#include "ImGuiFacadeRenderer.h"
#include "ImGuiSDL3Input.h"

#include <ImGuizmo.h>

#include "EditorCameraGizmo.h"
#include "EditorOverlayGeometry.h"
#include "EditorViewModes.h"
#include "GamePreviewStage.h"
#include "IconsFontAwesome6.h"

#include <core_util/DevicePreset.h>
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/CameraComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_gocomponent/VectorShapeComponent.h>
#include <core_util/ShapeCollider.h>
#include <core_util/VectorShapeAsset.h>
#include <engine_render/MeshInstance.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderTexture.h>
#include <engine_render/RenderWorld.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>
#include <cstring>

namespace OrkigeEditor
{
	//! the Scene panel's selected-camera inset last-draw seam
	//! (@see CameraInsetDebug)
	CameraInsetDebug& cameraInsetDebug()
	{
		static CameraInsetDebug debug;
		return debug;
	}
}

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
// marquee (rubber-band) select: pixels the cursor must travel from the press
// point before a left-drag on empty space becomes a band select rather than a
// plain click (content-scaled by the caller)
static const float MARQUEE_DRAG_THRESHOLD = 4.0f;

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
		// editor-only: the grid shows in the Scene RTT but is masked OUT of the
		// Game Preview RTT (which renders the game, not the editor chrome)
		grid->setVisibilityFlags(OrkigeEditor::EDITOR_ONLY_VISIBILITY);
		grid->attachTo(gridNode);
	}
	return grid;
}

// The camera frustum gizmo: the SELECTED object that carries a CameraComponent
// draws its view volume in the Scene panel. Facade line-mesh, exactly like the
// grid (shared unlit "VertexColour" look, both render flavors), but on a
// persistent ROOT node the panel re-poses to the camera object's world pose
// every frame (so it follows moves/orbit without polluting the object's own
// AABB or picking). The geometry (pure, unit-tested EditorCameraGizmo builder)
// is rebuilt only when the projection SIGNATURE changes - selecting a different
// camera, editing a projection property, or a viewport-aspect change - never on
// a plain object move. createLineListMesh is idempotent per name, so a rebuild
// takes a fresh (counter-suffixed) name; the previous mesh instance is dropped
// (RAII detaches it). Hidden while nothing camera-shaped is selected and during
// play (editMode false).
namespace Orkige
{
	namespace
	{
		struct CameraFrustumGizmo
		{
			optr<RenderNode>		node;			//!< persistent root, re-posed per frame
			optr<MeshInstance>		instance;		//!< current line mesh (RAII)
			std::string				signature;		//!< last-built shape signature
			std::string				objectId;		//!< tracked camera object ("" = none)
			std::size_t				vertexCount = 0;//!< vertices last uploaded (seam)
			unsigned int			meshCounter = 0;//!< unique mesh-name generator
		};
		CameraFrustumGizmo& cameraFrustumGizmo()
		{
			static CameraFrustumGizmo gizmo;
			return gizmo;
		}
	}
	//---------------------------------------------------------
	String const& editorSceneCameraGizmoObjectId()
	{
		static String value;
		value = cameraFrustumGizmo().objectId;
		return value;
	}
	//---------------------------------------------------------
	std::size_t editorSceneCameraGizmoVertexCount()
	{
		return cameraFrustumGizmo().vertexCount;
	}
	//---------------------------------------------------------
	void editorSceneCameraGizmoRelease()
	{
		CameraFrustumGizmo& gizmo = cameraFrustumGizmo();
		// drop the mesh instance before its node, both before the backend goes
		gizmo.instance.reset();
		gizmo.node.reset();
		gizmo.objectId.clear();
		gizmo.signature.clear();
		gizmo.vertexCount = 0;
	}
}

// drive the frustum gizmo for the current selection. editMode false (playing)
// or no camera-carrying selection hides it. aspect is the Scene viewport aspect
// (width/height) the perspective/ortho width is derived from.
void updateCameraFrustumGizmo(Orkige::EditorCore& core, bool editMode,
	float aspect)
{
	Orkige::CameraFrustumGizmo& gizmo = Orkige::cameraFrustumGizmo();
	auto hide = [&]()
	{
		if (gizmo.node)
		{
			gizmo.node->setVisible(false);
		}
		gizmo.objectId.clear();
		gizmo.vertexCount = 0;
	};

	// resolve the selected camera-carrying object (primary selection)
	optr<Orkige::GameObject> gameObject;
	if (editMode && core.hasSelection())
	{
		gameObject = core.getGameObjectManager()
			.getGameObject(core.getSelectedObjectId()).lock();
	}
	if (!gameObject || !gameObject->hasComponent<Orkige::CameraComponent>() ||
		!gameObject->hasComponent<Orkige::TransformComponent>())
	{
		hide();
		return;
	}
	Orkige::CameraComponent* cameraComponent =
		gameObject->getComponentPtr<Orkige::CameraComponent>();
	Orkige::TransformComponent* transform =
		gameObject->getComponentPtr<Orkige::TransformComponent>();

	Orkige::RenderWorld* world = Orkige::RenderSystem::get()->getWorld();
	if (!world)
	{
		hide();
		return;
	}
	// lazily create the persistent root node the mesh rides on
	if (!gizmo.node)
	{
		gizmo.node = world->createNode("EditorCameraFrustumNode");
	}

	// gather the reflected projection params + build the shape signature
	Orkige::CameraFrustumParams params;
	params.projectionMode =
		static_cast<int>(cameraComponent->getProjectionMode());
	params.orthoSize = cameraComponent->getOrthoSize();
	params.fitMode = static_cast<int>(cameraComponent->getFitMode());
	params.designWidth = cameraComponent->getDesignWidth();
	params.designHeight = cameraComponent->getDesignHeight();
	// quantise the aspect so a sub-pixel resize does not churn the mesh
	const float quantAspect = std::round(aspect * 100.0f) / 100.0f;
	std::string signature = core.getSelectedObjectId() + "|" +
		std::to_string(params.projectionMode) + "|" +
		std::to_string(params.orthoSize) + "|" +
		std::to_string(params.fitMode) + "|" +
		std::to_string(params.designWidth) + "|" +
		std::to_string(params.designHeight) + "|" +
		std::to_string(quantAspect);

	if (signature != gizmo.signature || !gizmo.instance)
	{
		std::vector<Orkige::Vec3> pointsVec;
		std::vector<Orkige::Color> coloursVec;
		Orkige::buildCameraFrustumLines(params, quantAspect, pointsVec,
			coloursVec);
		++gizmo.meshCounter;
		const std::string meshName = "EditorCameraFrustum_" +
			std::to_string(gizmo.meshCounter) + ".mesh";
		world->createLineListMesh(meshName, pointsVec.data(),
			coloursVec.data(), pointsVec.size());
		// drop the old instance first (RAII detaches it), then attach the new
		gizmo.instance.reset();
		gizmo.instance = world->createMeshInstance(meshName);
		if (gizmo.instance)
		{
			gizmo.instance->setCastShadows(false);
			gizmo.instance->setQueryFlags(0);	// never a picking hit
			// editor-only: masked OUT of the Game Preview RTT (the grid too)
			gizmo.instance->setVisibilityFlags(
				OrkigeEditor::EDITOR_ONLY_VISIBILITY);
			gizmo.instance->attachTo(gizmo.node);
		}
		gizmo.signature = signature;
		gizmo.vertexCount = pointsVec.size();
	}

	// re-pose the root node onto the camera object's world transform (the mesh
	// is authored in the camera's local space: looks down -Z from the origin)
	gizmo.node->setPosition(transform->getWorldPosition());
	gizmo.node->setOrientation(transform->getWorldOrientation());
	gizmo.node->setVisible(true);
	gizmo.objectId = core.getSelectedObjectId();
}

// The Scene panel's display-option overlays (Colliders, Bounding Boxes, all-
// Camera Frames): each toggled from the toolbar Display dropdown, each a facade
// line-mesh built EXACTLY like the reference grid and the frustum gizmo (shared
// unlit "VertexColour" look, both flavors, editor-only visibility bit so the Game
// Preview RTT never shows them). One COMBINED world-space mesh per category rides
// a persistent identity root node; the pure builders (EditorOverlayGeometry.h /
// EditorCameraGizmo.h) fill the vertices. Rebuilt only when a signature (toggle +
// per-object pose/shape/aspect) changes - a plain view orbit never re-uploads.
namespace Orkige
{
	namespace
	{
		//! one combined line-mesh overlay: a persistent root node, the current
		//! mesh instance (RAII), the last-built signature and a unique mesh-name
		//! counter (createLineListMesh is idempotent per name, so each rebuild
		//! takes a fresh counter-suffixed name and drops the old instance).
		struct OverlayMesh
		{
			optr<RenderNode>	node;
			optr<MeshInstance>	instance;
			std::string			signature;
			std::string			meshName;		//!< the uploaded mesh ("" = none) - destroyed on rebuild
			std::size_t			vertexCount = 0;
			unsigned int		meshCounter = 0;
		};
		struct SceneOverlays
		{
			OverlayMesh	collider;
			OverlayMesh	boundingBox;
			OverlayMesh	cameraFrames;
		};
		SceneOverlays& sceneOverlays()
		{
			static SceneOverlays overlays;
			return overlays;
		}

		//! append the world-space position of a body/camera-local point
		Vec3 overlayLocalToWorld(Vec3 const& worldPosition,
			Quat const& worldOrientation, Vec3 const& localPoint)
		{
			return worldPosition + worldOrientation * localPoint;
		}

		//! @brief resolve the shape-local collider CONTOURS (closed XY loops, z=0)
		//! of an ST_SHAPE rigid body for the overlay: the sibling
		//! VectorShapeComponent's live rest regions when it names the same shape
		//! (or the body has no explicit override), otherwise the explicit
		//! `.oshape` read through the render facade. Empty when nothing resolves.
		std::vector<std::vector<Vec3> > resolveShapeColliderContours(
			GameObject& gameObject, RigidBodyComponent& body)
		{
			std::vector<std::vector<Vec3> > out;
			std::vector<VectorTessellator::Region> parsed;
			std::vector<VectorTessellator::Region> const* regions = nullptr;
			VectorShapeComponent* sibling =
				gameObject.hasComponent<VectorShapeComponent>() ?
				gameObject.getComponentPtr<VectorShapeComponent>() : nullptr;
			String const& assetName = body.getShapeAsset();
			if (sibling && sibling->hasShape() && (assetName.empty() ||
				assetName == sibling->getShapeName()))
			{
				regions = &sibling->getRegions();
			}
			else if (!assetName.empty())
			{
				String text;
				if (RenderSystem::get()->readResourceText(assetName, text) &&
					VectorShapeAsset::parse(text, parsed))
				{
					regions = &parsed;
				}
			}
			if (!regions)
			{
				return out;
			}
			std::vector<std::vector<ShapeCollider::Point> > contours;
			ShapeCollider::extractContours(*regions, contours);
			for (std::vector<ShapeCollider::Point> const& contour : contours)
			{
				std::vector<Vec3> loop;
				loop.reserve(contour.size());
				for (ShapeCollider::Point const& p : contour)
				{
					loop.push_back(Vec3(p.x, p.y, 0.0f));
				}
				out.push_back(std::move(loop));
			}
			return out;
		}

		//! append a quantised float to a signature string (1e-3 granularity, so a
		//! sub-milli jitter never churns the mesh)
		void overlaySigNum(std::string& signature, float value)
		{
			signature += std::to_string(
				static_cast<long long>(std::lround(value * 1000.0f)));
			signature += ',';
		}

		//! (re)build one overlay mesh from freshly gathered geometry: skip the GPU
		//! upload when the signature is unchanged, drop the instance + hide the node
		//! when there is nothing to draw. Mirrors the frustum gizmo's rebuild path.
		void applyOverlayMesh(OverlayMesh& mesh, RenderWorld* world,
			char const* namePrefix, std::string const& signature,
			std::vector<Vec3> const& points, std::vector<Color> const& colours)
		{
			if (!mesh.node)
			{
				mesh.node = world->createNode(
					std::string(namePrefix) + "Node");
			}
			if (signature == mesh.signature && (mesh.instance || points.empty()))
			{
				mesh.node->setVisible(!points.empty());
				return;
			}
			mesh.instance.reset();	// RAII detaches the previous mesh instance
			// drop the previous mesh RESOURCE (create takes a fresh name every
			// rebuild - without this every overlay rebuild leaks a GPU mesh, and
			// a gizmo drag with an overlay armed rebuilds every frame)
			if (!mesh.meshName.empty())
			{
				world->destroyLineListMesh(mesh.meshName);
				mesh.meshName.clear();
			}
			if (!points.empty())
			{
				++mesh.meshCounter;
				const std::string meshName = std::string(namePrefix) + "_" +
					std::to_string(mesh.meshCounter) + ".mesh";
				world->createLineListMesh(meshName, points.data(),
					colours.data(), points.size());
				mesh.instance = world->createMeshInstance(meshName);
				if (mesh.instance)
				{
					mesh.instance->setCastShadows(false);
					mesh.instance->setQueryFlags(0);	// never a picking hit
					// editor-only: masked OUT of the Game Preview RTT (grid too)
					mesh.instance->setVisibilityFlags(
						OrkigeEditor::EDITOR_ONLY_VISIBILITY);
					mesh.instance->attachTo(mesh.node);
				}
				mesh.meshName = meshName;
			}
			mesh.node->setVisible(!points.empty());
			mesh.signature = signature;
			mesh.vertexCount = points.size();
		}
	}
	//---------------------------------------------------------
	std::size_t editorSceneColliderOverlayVertexCount()
	{
		return sceneOverlays().collider.vertexCount;
	}
	//---------------------------------------------------------
	String const& editorSceneColliderOverlayMeshName()
	{
		static String value;
		value = sceneOverlays().collider.meshName;
		return value;
	}
	//---------------------------------------------------------
	std::size_t editorSceneBoundingBoxOverlayVertexCount()
	{
		return sceneOverlays().boundingBox.vertexCount;
	}
	//---------------------------------------------------------
	std::size_t editorSceneCameraFramesOverlayVertexCount()
	{
		return sceneOverlays().cameraFrames.vertexCount;
	}
	//---------------------------------------------------------
	void editorSceneOverlaysRelease()
	{
		SceneOverlays& overlays = sceneOverlays();
		RenderWorld* world = RenderSystem::get() ?
			RenderSystem::get()->getWorld() : nullptr;
		OverlayMesh* meshes[] = { &overlays.collider, &overlays.boundingBox,
			&overlays.cameraFrames };
		for (OverlayMesh* mesh : meshes)
		{
			// instance first, then the mesh resource (no live Item left on it),
			// then the node - all before the backend goes (process-lifetime state)
			mesh->instance.reset();
			if (world && !mesh->meshName.empty())
			{
				world->destroyLineListMesh(mesh->meshName);
			}
			mesh->meshName.clear();
			mesh->node.reset();
			mesh->signature.clear();
			mesh->vertexCount = 0;
		}
	}
}

// drive the display-option overlays for the current scene. All three hide (empty
// mesh) while playing (editMode false); otherwise each armed toggle gathers its
// world-space geometry and (re)builds its combined mesh. panelAspect is the Scene
// viewport aspect the frustums derive their width from; deviceAspect is the Game
// Preview preset aspect (<= 0 = panel-sized/no preset -> no design-aspect rect).
void updateSceneOverlays(Orkige::EditorCore& core, bool editMode,
	float panelAspect, float deviceAspect)
{
	using namespace Orkige;
	SceneOverlays& overlays = sceneOverlays();
	ViewSettings const* view = gViewSettings;
	RenderWorld* world = RenderSystem::get()->getWorld();
	const bool anyOn = view != nullptr && editMode &&
		(view->showColliders || view->showBoundingBoxes ||
			view->showAllCameraFrames);
	if (!world || !anyOn)
	{
		// hide every overlay with an empty rebuild (keeps the seams truthful)
		const std::vector<Vec3> noPoints;
		const std::vector<Color> noColours;
		if (world)
		{
			applyOverlayMesh(overlays.collider, world, "EditorColliderOverlay",
				"off", noPoints, noColours);
			applyOverlayMesh(overlays.boundingBox, world,
				"EditorBoundingBoxOverlay", "off", noPoints, noColours);
			applyOverlayMesh(overlays.cameraFrames, world,
				"EditorCameraFramesOverlay", "off", noPoints, noColours);
		}
		else
		{
			overlays.collider.vertexCount = 0;
			overlays.boundingBox.vertexCount = 0;
			overlays.cameraFrames.vertexCount = 0;
		}
		return;
	}

	const float quantPanel = std::round(panelAspect * 100.0f) / 100.0f;
	const float quantDevice = std::round(deviceAspect * 100.0f) / 100.0f;
	GameObjectManager& manager = core.getGameObjectManager();

	std::vector<Vec3> colliderPts, boxPts, framePts;
	std::vector<Color> colliderCols, boxCols, frameCols;
	std::string colliderSig = "on|", boxSig = "on|", frameSig = "on|";
	overlaySigNum(frameSig, quantPanel);
	overlaySigNum(frameSig, quantDevice);

	for (auto const& entry : manager.getGameObjects())
	{
		optr<GameObject> const& gameObject = entry.second;
		if (!gameObject ||
			!gameObject->hasComponent<TransformComponent>())
		{
			continue;
		}
		TransformComponent* transform =
			gameObject->getComponentPtr<TransformComponent>();
		const Vec3 worldPos = transform->getWorldPosition();
		const Quat worldOrient = transform->getWorldOrientation();

		// (1) colliders: the RigidBodyComponent shape at the body world pose
		if (view->showColliders &&
			gameObject->hasComponent<RigidBodyComponent>())
		{
			RigidBodyComponent* body =
				gameObject->getComponentPtr<RigidBodyComponent>();
			PhysicsWorld::BodyDesc const& desc = body->getBodyDesc();
			colliderSig += entry.first;
			colliderSig += '#';
			colliderSig += std::to_string(static_cast<int>(desc.shapeType));
			if (desc.shapeType == PhysicsWorld::ST_SHAPE)
			{
				// draw the ACTUAL `.oshape` outline the collider is built from.
				// The editor has no created body (no baked geometry), so resolve
				// the contours from the sibling VectorShapeComponent's live regions
				// (or the explicit shapeAsset file) exactly as the runtime does.
				std::vector<std::vector<Vec3>> contours =
					resolveShapeColliderContours(*gameObject, *body);
				appendColliderShapeOutline(contours, worldPos, worldOrient,
					editorColliderColour(), colliderPts, colliderCols);
				// the asset name AND every contour coordinate ride the signature,
				// so a re-cooked shape (same id, changed geometry) rebuilds
				colliderSig += '|';
				colliderSig += body->getShapeAsset();
				for (std::vector<Vec3> const& contour : contours)
				{
					overlaySigNum(colliderSig,
						static_cast<float>(contour.size()));
					for (Vec3 const& p : contour)
					{
						overlaySigNum(colliderSig, p.x);
						overlaySigNum(colliderSig, p.y);
					}
				}
			}
			else
			{
				appendColliderOutline(static_cast<int>(desc.shapeType), worldPos,
					worldOrient, desc.halfExtents, desc.radius, desc.halfHeight,
					editorColliderCircleSegments(), editorColliderColour(),
					colliderPts, colliderCols);
			}
			overlaySigNum(colliderSig, worldPos.x);
			overlaySigNum(colliderSig, worldPos.y);
			overlaySigNum(colliderSig, worldPos.z);
			overlaySigNum(colliderSig, worldOrient.x);
			overlaySigNum(colliderSig, worldOrient.y);
			overlaySigNum(colliderSig, worldOrient.z);
			overlaySigNum(colliderSig, worldOrient.w);
			overlaySigNum(colliderSig, desc.halfExtents.x);
			overlaySigNum(colliderSig, desc.halfExtents.y);
			overlaySigNum(colliderSig, desc.halfExtents.z);
			overlaySigNum(colliderSig, desc.radius);
			overlaySigNum(colliderSig, desc.halfHeight);
		}

		// (2) bounding boxes: the renderable's world AABB (facade node bounds)
		if (view->showBoundingBoxes &&
			gameObject->hasComponent<ModelComponent>())
		{
			optr<RenderNode> const& node =
				gameObject->getComponentPtr<ModelComponent>()->getNode();
			if (node)
			{
				const AABB bounds = node->getWorldBounds();
				if (!bounds.isNull() && !bounds.isInfinite())
				{
					const Vec3 minCorner = bounds.getMinimum();
					const Vec3 maxCorner = bounds.getMaximum();
					appendOverlayAabb(minCorner, maxCorner,
						editorBoundingBoxColour(), boxPts, boxCols);
					boxSig += entry.first;
					overlaySigNum(boxSig, minCorner.x);
					overlaySigNum(boxSig, minCorner.y);
					overlaySigNum(boxSig, minCorner.z);
					overlaySigNum(boxSig, maxCorner.x);
					overlaySigNum(boxSig, maxCorner.y);
					overlaySigNum(boxSig, maxCorner.z);
				}
			}
		}

		// (3) all-camera frames: every camera's frustum (panel aspect) + the
		// design-aspect rect (device aspect) when a Game Preview preset is active
		if (view->showAllCameraFrames &&
			gameObject->hasComponent<CameraComponent>())
		{
			CameraComponent* camera =
				gameObject->getComponentPtr<CameraComponent>();
			CameraFrustumParams params;
			params.projectionMode =
				static_cast<int>(camera->getProjectionMode());
			params.orthoSize = camera->getOrthoSize();
			params.fitMode = static_cast<int>(camera->getFitMode());
			params.designWidth = camera->getDesignWidth();
			params.designHeight = camera->getDesignHeight();

			std::vector<Vec3> localPts;
			std::vector<Color> localCols;
			buildCameraFrustumLines(params, quantPanel, localPts, localCols);
			for (std::size_t i = 0; i < localPts.size(); ++i)
			{
				framePts.push_back(overlayLocalToWorld(worldPos, worldOrient,
					localPts[i]));
				frameCols.push_back(localCols[i]);
			}
			if (quantDevice > 1.0e-4f)
			{
				buildCameraAspectFrame(params, quantDevice, localPts, localCols);
				for (std::size_t i = 0; i < localPts.size(); ++i)
				{
					framePts.push_back(overlayLocalToWorld(worldPos,
						worldOrient, localPts[i]));
					frameCols.push_back(localCols[i]);
				}
			}
			frameSig += entry.first;
			frameSig += '#';
			frameSig += std::to_string(params.projectionMode);
			overlaySigNum(frameSig, params.orthoSize);
			frameSig += std::to_string(params.fitMode);
			overlaySigNum(frameSig, params.designWidth);
			overlaySigNum(frameSig, params.designHeight);
			overlaySigNum(frameSig, worldPos.x);
			overlaySigNum(frameSig, worldPos.y);
			overlaySigNum(frameSig, worldPos.z);
			overlaySigNum(frameSig, worldOrient.x);
			overlaySigNum(frameSig, worldOrient.y);
			overlaySigNum(frameSig, worldOrient.z);
			overlaySigNum(frameSig, worldOrient.w);
		}
	}

	// off toggles keep an empty (hidden) mesh so their seams read 0
	applyOverlayMesh(overlays.collider, world, "EditorColliderOverlay",
		view->showColliders ? colliderSig : "off", colliderPts, colliderCols);
	applyOverlayMesh(overlays.boundingBox, world, "EditorBoundingBoxOverlay",
		view->showBoundingBoxes ? boxSig : "off", boxPts, boxCols);
	applyOverlayMesh(overlays.cameraFrames, world, "EditorCameraFramesOverlay",
		view->showAllCameraFrames ? frameSig : "off", framePts, frameCols);
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

// non-mutating variant of the pick ray-cast: does the cursor ray hit ANY
// GameObject? The marquee uses it at press time to tell an on-object click
// (which selects that object) from an empty-space click (which begins a
// rubber-band). Mirrors pickObjectAtCursor's hit walk without touching the
// selection.
static bool rayHitsGameObject(Orkige::RenderCamera const& camera,
	float normalizedX, float normalizedY)
{
	const Orkige::Ray3 ray = camera.viewportPointToRay(normalizedX, normalizedY);
	for (Orkige::RenderWorld::RayQueryHit const& hit :
		Orkige::RenderSystem::get()->getWorld()->queryRay(ray))
	{
		if (!hit.userPointer)
		{
			continue;
		}
		if (static_cast<Orkige::TransformComponent*>(hit.userPointer)
				->getComponentOwner())
		{
			return true;
		}
	}
	return false;
}

// project a GameObject's world bounds to a screen-space rectangle inside the
// Scene image (render-target pixels). Uses the object's world AABB when it is
// finite, falling back to its world position as a zero-size point rect (an
// object with no renderable bounds is still band-selectable by its origin).
// Returns false only when nothing projects in front of the camera.
static bool projectObjectScreenRect(Orkige::TransformComponent* transform,
	Orkige::RenderCamera const& camera, ImVec2 const& rectMin,
	ImVec2 const& rectSize, Orkige::ScreenRect& out)
{
	std::vector<Orkige::Vec3> samples;
	const Orkige::AABB box = transform->getWorldAABB();
	if (box.isFinite() && !box.isNull())
	{
		const Orkige::Vec3 mn = box.getMinimum();
		const Orkige::Vec3 mx = box.getMaximum();
		samples.reserve(8);
		for (int corner = 0; corner < 8; ++corner)
		{
			samples.push_back(Orkige::Vec3((corner & 1) ? mx.x : mn.x,
				(corner & 2) ? mx.y : mn.y, (corner & 4) ? mx.z : mn.z));
		}
	}
	else
	{
		samples.push_back(transform->getWorldPosition());
	}

	bool any = false;
	float minX = 0.0f;
	float minY = 0.0f;
	float maxX = 0.0f;
	float maxY = 0.0f;
	for (Orkige::Vec3 const& point : samples)
	{
		Orkige::Real nx = 0.0f;
		Orkige::Real ny = 0.0f;
		if (!camera.projectPoint(point, nx, ny))
		{
			continue;	// behind the camera - skip this sample
		}
		const float sx = rectMin.x + nx * rectSize.x;
		const float sy = rectMin.y + ny * rectSize.y;
		if (!any)
		{
			minX = maxX = sx;
			minY = maxY = sy;
			any = true;
		}
		else
		{
			minX = std::min(minX, sx);
			minY = std::min(minY, sy);
			maxX = std::max(maxX, sx);
			maxY = std::max(maxY, sy);
		}
	}
	if (!any)
	{
		return false;
	}
	out = Orkige::ScreenRect{ minX, minY, maxX, maxY };
	return true;
}

Orkige::StringVector objectsInMarquee(Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera, ImVec2 const& rectMin,
	ImVec2 const& rectSize, Orkige::ScreenRect const& marquee)
{
	Orkige::StringVector hits;
	if (rectSize.x < 1.0f || rectSize.y < 1.0f)
	{
		return hits;
	}
	for (auto const& [id, gameObject] :
		core.getGameObjectManager().getGameObjects())
	{
		if (!gameObject ||
			!gameObject->hasComponent<Orkige::TransformComponent>())
		{
			continue;
		}
		// band-select ROOT objects only (like most DCC tools): a prefab
		// instance is picked as a whole, never its internal children
		if (!gameObject->getParentId().empty())
		{
			continue;
		}
		Orkige::ScreenRect bounds;
		if (projectObjectScreenRect(
				gameObject->getComponentPtr<Orkige::TransformComponent>(),
				*camera, rectMin, rectSize, bounds) &&
			Orkige::screenRectsIntersect(bounds, marquee))
		{
			hits.push_back(id);
		}
	}
	return hits;
}

void applyMarqueeSelection(Orkige::EditorCore& core,
	Orkige::StringVector const& hits, bool extend)
{
	if (!extend)
	{
		core.clearSelection();
	}
	for (Orkige::String const& id : hits)
	{
		core.addToSelection(id);
	}
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
	// Select, Paint and Hand show no transform gizmo (Paint consumes clicks for
	// grid painting; Hand pans the camera; the pick path is bypassed for all)
	if (tool == Orkige::EditorTool::Select ||
		tool == Orkige::EditorTool::Paint ||
		tool == Orkige::EditorTool::Hand || !core.hasSelection() ||
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
	Orkige::ImGuiSDL3Input& imguiInput, Orkige::GameObjectManager& world,
	OrkigeEditor::GamePreviewStage& cameraInset)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin("Scene", &viewSettings.showScenePanel);
	OrkigeEditor::editorPanelTabMenu(&viewSettings.showScenePanel);
	ImGui::PopStyleVar();
	state.scenePanelHovered = false;
	state.scenePanelFocused = open && ImGui::IsWindowFocused();
	// the Scene tab is the visible/active tab this frame (drives the render
	// invariant + lighting decision - @see OrkigeEditor::chooseGameViewRenderer)
	state.scenePanelVisibleThisFrame = open;
	if (state.scenePanelFocused)
	{
		state.lastFocusedGameView = OrkigeEditor::GameViewRenderer::Scene;
	}
	if (open && isPrefabEditActive(state))
	{
		// prefab edit breadcrumb: "<scene> ▸ <prefab>" - clicking the scene
		// segment closes the stage (confirm modal when dirty), Save Prefab
		// mirrors Cmd/Ctrl+S. Drawn as a strip above the viewport image so
		// the mode is unmissable while every tool keeps working below.
		PrefabEditContext const& context = state.prefabEditStack.back();
		std::string sceneLabel = context.stashedScenePath.empty()
			? std::string("untitled")
			: std::filesystem::path(context.stashedScenePath)
				.filename().string();
		std::string prefabLabel = context.prefabRef.empty()
			? std::filesystem::path(context.prefabPath).filename().string()
			: context.prefabRef;
		ImGui::Spacing();
		ImGui::SameLine(0.0f, 6.0f);
		if (ImGui::SmallButton((sceneLabel + "##PrefabBreadcrumbScene")
			.c_str()))
		{
			requestClosePrefabEdit(state, core);
		}
		ImGui::SetItemTooltip("back to the scene (closes the prefab)");
		ImGui::SameLine();
		ImGui::TextUnformatted("\xE2\x96\xB8");	// breadcrumb separator
		ImGui::SameLine();
		ImGui::Text("%s%s", prefabLabel.c_str(),
			core.isSceneDirty() ? " *" : "");
		ImGui::SameLine();
		if (ImGui::SmallButton("Save Prefab"))
		{
			savePrefabEdit(state, core);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Close"))
		{
			requestClosePrefabEdit(state, core);
		}
		ImGui::Spacing();
	}
	if (open && gPlaySession != nullptr && gPlaySession->isActive() &&
		gPlaySession->mirrorDocument)
	{
		// mirror-document banner: a mid-play scene switch swapped the Scene
		// view to a view-only load of the RUNNING scene - say so unmissably
		// (the authored document returns on Stop; editing stays routed to the
		// remote panels like in every play session)
		ImGui::Spacing();
		ImGui::SameLine(0.0f, 6.0f);
		ImGui::Text("Viewing running scene: %s",
			gPlaySession->mirrorSceneName.c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("(view only - the authored scene returns on Stop)");
		ImGui::Spacing();
	}
	// the play-mode contract: the Scene view either MIRRORS the running game
	// or goes dark with one honest line - never a silently stale scene. This
	// is the dark half: the game switched to a scene the editor could not
	// load for viewing, so instead of showing the frozen authored scene the
	// viewport blanks until Stop (or the next switch the editor CAN follow).
	const bool mirrorUnavailable = open && gPlaySession != nullptr &&
		gPlaySession->isActive() && gPlaySession->mirrorSwapFailed;
	if (mirrorUnavailable)
	{
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const char* const notice = "Scene preview not available during play";
		const ImVec2 textSize = ImGui::CalcTextSize(notice);
		ImGui::SetCursorPos(ImVec2(
			ImGui::GetCursorPosX() + (avail.x - textSize.x) * 0.5f,
			ImGui::GetCursorPosY() + (avail.y - textSize.y) * 0.5f));
		ImGui::TextDisabled("%s", notice);
		state.scenePanelHovered = false;
	}
	if (open && !mirrorUnavailable)
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
			// render invariant: when the Game Preview is the frame's renderer (both
			// game views up, Preview focused) the Scene RTT was FROZEN this frame -
			// dim the frozen image + say so (@see chooseGameViewRenderer)
			if (state.gameViewRenderer ==
				OrkigeEditor::GameViewRenderer::Preview)
			{
				ImDrawList* frozenDraw = ImGui::GetWindowDrawList();
				const ImVec2 frozenMax(rectMin.x + avail.x, rectMin.y + avail.y);
				frozenDraw->AddRectFilled(rectMin, frozenMax,
					IM_COL32(8, 10, 14, 150));
				char const* frozenNote = "Paused while Game Preview is active";
				const ImVec2 frozenSize = ImGui::CalcTextSize(frozenNote);
				frozenDraw->AddText(ImVec2(
					(rectMin.x + frozenMax.x - frozenSize.x) * 0.5f,
					(rectMin.y + frozenMax.y - frozenSize.y) * 0.5f),
					ImGui::GetColorU32(ImGuiCol_Text, 0.85f), frozenNote);
			}
			// record the image's screen rect (render-target pixels) so the
			// selfchecks can synthesise mouse events at known viewport positions
			state.sceneImageMin = rectMin;
			state.sceneImageSize = avail;
			// Asset browser drop: a mesh/texture/prefab dragged from the
			// Assets panel onto the viewport instantiates at the origin
			// (ray/ground-plane placement is deferred - origin on the
			// XY plane is natural in 2D editor mode). Only while editing the
			// local scene (the panels show the remote scene during play).
			if (editMode)
			{
				handleSceneDropTarget(state, core, sceneTarget.camera,
					rectMin, avail, viewSettings.editor2D);
			}
			// remember the current view-camera world pose so "Create Camera"
			// spawns the new camera object right where the author is looking
			if (editMode && cameraNode)
			{
				core.setViewCameraPose(cameraNode->getWorldPosition(),
					cameraNode->getWorldOrientation());
			}
			// the frustum gizmo for a selected camera object (facade line mesh;
			// hidden when nothing camera-shaped is selected / during play)
			const float panelAspect =
				avail.y > 0.0f ? avail.x / avail.y : 1.0f;
			updateCameraFrustumGizmo(core, editMode, panelAspect);
			// the Scene display-option overlays (Colliders / Bounding Boxes /
			// all-Camera Frames from the toolbar Display dropdown). The design-
			// aspect rect follows the Game Preview panel's device preset.
			float deviceAspect = 0.0f;
			if (viewSettings.showGamePreviewPanel)
			{
				Orkige::DevicePreset::Preset const& preset =
					Orkige::DevicePreset::forKind(
						static_cast<Orkige::DevicePreset::Kind>(
							viewSettings.gamePreviewPreset));
				if (preset.width > 0 && preset.height > 0)
				{
					deviceAspect = static_cast<float>(preset.width) /
						static_cast<float>(preset.height);
				}
			}
			updateSceneOverlays(core, editMode, panelAspect, deviceAspect);
			// Display dropdown: a compact overlay button in the viewport's top-
			// left corner (its own Scene-panel toolbar) that houses the per-view
			// display toggles, matching the main toolbar's snap-popover style.
			// Persisted in orkige_editor_view.ini like the other view flags.
			bool displayMenuOwnsMouse = false;
			{
				const float menuInset = 8.0f * contentScale;
				ImGui::SetCursorScreenPos(ImVec2(rectMin.x + menuInset,
					rectMin.y + menuInset));
				ImGui::PushID("##SceneDisplayMenu");
				// the eye glyph names the dropdown (matching the toolbar's icon
				// buttons); the tooltip spells it out for discoverability
				if (ImGui::Button(ICON_FA_EYE))
				{
					ImGui::OpenPopup("##SceneDisplayOptions");
				}
				ImGui::SetItemTooltip("Display options");
				displayMenuOwnsMouse = ImGui::IsItemHovered();
				if (ImGui::BeginPopup("##SceneDisplayOptions"))
				{
					displayMenuOwnsMouse = true;
					ImGui::TextDisabled("Overlays");
					bool changed = false;
					changed |= ImGui::Checkbox("Grid", &viewSettings.showGrid);
					changed |= ImGui::Checkbox("Colliders",
						&viewSettings.showColliders);
					changed |= ImGui::Checkbox("Bounding Boxes",
						&viewSettings.showBoundingBoxes);
					changed |= ImGui::Checkbox("Camera Frames",
						&viewSettings.showAllCameraFrames);
					// the scene atmosphere's visible sky in the Scene RTT only
					// (default off - the Game Preview / inset / Play always show
					// it). Objects stay lit + fogged like the game either way.
					changed |= ImGui::Checkbox("Sky", &viewSettings.showSky);
					ImGui::SetItemTooltip("Show the scene sky (dome / cubemap) in "
						"the Scene view.\nThe Game Preview and Play always show "
						"it. Lighting and fog are unaffected.");

					// View Mode (Shaded / Wireframe / Shaded+Wireframe): a
					// per-target look on the Scene RTT only. Each mode is greyed
					// per flavor by its render capability (@see EditorViewModes /
					// RenderCaps::SceneWireframeView + SceneWireframeOverlayView),
					// with the reason as a tooltip.
					ImGui::Separator();
					ImGui::TextDisabled("View Mode");
					const bool wireCap = Orkige::RenderSystem::get()->supports(
						Orkige::RenderCaps::SceneWireframeView);
					const bool wireOverlayCap =
						Orkige::RenderSystem::get()->supports(
							Orkige::RenderCaps::SceneWireframeOverlayView);
					const bool unlitCap = Orkige::RenderSystem::get()->supports(
						Orkige::RenderCaps::SceneUnlitView);
					struct ModeRow { Orkige::RenderViewMode mode; char const* label; };
					const ModeRow modeRows[] = {
						{ Orkige::RenderViewMode::Shaded, "Shaded" },
						{ Orkige::RenderViewMode::Wireframe, "Wireframe" },
						{ Orkige::RenderViewMode::ShadedWireframe,
							"Shaded + Wireframe" },
					};
					for (ModeRow const& row : modeRows)
					{
						const OrkigeEditor::SceneViewModeInfo info =
							OrkigeEditor::sceneViewModeInfo(row.mode, wireCap,
								wireOverlayCap);
						const int modeInt = static_cast<int>(row.mode);
						ImGui::BeginDisabled(!info.available);
						if (ImGui::RadioButton(row.label,
							viewSettings.sceneViewMode == modeInt) &&
							info.available)
						{
							viewSettings.sceneViewMode = modeInt;
							changed = true;
						}
						ImGui::EndDisabled();
						// a greyed mode still reports its reason on hover
						// (AllowWhenDisabled - a plain SetItemTooltip skips
						// disabled items)
						if (!info.available && !info.reason.empty() &&
							ImGui::IsItemHovered(
								ImGuiHoveredFlags_AllowWhenDisabled))
						{
							ImGui::SetTooltip("%s", info.reason.c_str());
						}
					}

					// Lighting (lit / unlit): the Scene RTT renders flat albedo +
					// ambient when off. Greyed on a flavor without a per-view
					// lighting override (@see RenderCaps::SceneUnlitView).
					ImGui::Separator();
					const OrkigeEditor::SceneViewModeInfo litInfo =
						OrkigeEditor::sceneLightingToggleInfo(unlitCap);
					ImGui::BeginDisabled(!litInfo.available);
					if (ImGui::Checkbox("Lighting",
						&viewSettings.sceneLightingEnabled) && litInfo.available)
					{
						changed = true;
					}
					ImGui::EndDisabled();
					if (litInfo.available)
					{
						ImGui::SetItemTooltip("Light the Scene view with the "
							"scene's lights.\nOff = flat albedo + ambient for "
							"inspecting materials.\nApplies only when the Game "
							"Preview is not also visible; the Game Preview and "
							"Play always stay lit.");
					}
					else if (!litInfo.reason.empty() && ImGui::IsItemHovered(
						ImGuiHoveredFlags_AllowWhenDisabled))
					{
						ImGui::SetTooltip("%s", litInfo.reason.c_str());
					}

					if (changed)
					{
						viewSettings.save();
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			// selected-camera picture-in-picture inset: when the selection
			// carries a CameraComponent, show ITS view live in a bottom-right
			// corner box while the author moves/rotates it. The same preview
			// RTT + camera-copy path as the Game Preview panel, but pointed at
			// the SELECTED camera (the panel tracks the active one), at the
			// panel aspect, scene-only (no overlay, no guides, no frame). It
			// disappears on deselect; hidden during play.
			OrkigeEditor::CameraInsetDebug& insetDbg =
				OrkigeEditor::cameraInsetDebug();
			insetDbg = OrkigeEditor::CameraInsetDebug();
			if (editMode && core.hasSelection())
			{
				optr<Orkige::GameObject> selected =
					world.getGameObject(core.getSelectedObjectId()).lock();
				if (selected &&
					selected->hasComponent<Orkige::CameraComponent>() &&
					selected->hasComponent<Orkige::TransformComponent>())
				{
					float insetX = 0.0f, insetY = 0.0f;
					float insetW = 0.0f, insetH = 0.0f;
					Orkige::DevicePreset::insetRect(avail.x, avail.y,
						panelAspect, 0.25f, 10.0f * contentScale,
						insetX, insetY, insetW, insetH);
					OrkigeEditor::GuiPreviewContext insetCtx;
					insetCtx.width = static_cast<unsigned int>(
						std::max(16.0f, insetW));
					insetCtx.height = static_cast<unsigned int>(
						std::max(16.0f, insetH));
					insetCtx.contentScale = 1.0f;
					cameraInset.setContext(insetCtx);
					cameraInset.update(world, core.getSelectedObjectId(),
						false, 0.0f);
					if (cameraInset.hasCamera() && cameraInset.getTarget())
					{
						ImDrawList* insetDraw = ImGui::GetWindowDrawList();
						const ImVec2 a(rectMin.x + insetX, rectMin.y + insetY);
						const ImVec2 b(a.x + insetW, a.y + insetH);
						// a subtle dark backing so the framed image reads on
						// any scene content
						insetDraw->AddRectFilled(
							ImVec2(a.x - 2.0f, a.y - 2.0f),
							ImVec2(b.x + 2.0f, b.y + 2.0f),
							IM_COL32(0, 0, 0, 200), 3.0f);
						insetDraw->AddImage(gImGuiRenderer->textureIdFor(
							cameraInset.getTarget()), a, b);
							insetDbg.drew = true;
							insetDbg.trackedCameraId =
								cameraInset.getTrackedCameraId();
						insetDraw->AddRect(a, b, IM_COL32(120, 175, 255, 220),
							3.0f, 0, 1.5f);
					}
				}
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
				!viewGizmoOwnsMouse && !displayMenuOwnsMouse)
			{
				paintOwnsMouse = handleScenePaintInput(state, core,
					sceneTarget.camera, rectMin, avail, editMode, viewSettings);
			}
			// Hand tool (or a held Space) turns left-drag into a grab-the-world
			// camera pan; picking/marquee stand down while it is engaged, and
			// the cursor reads as a hand over the viewport
			const bool handMode =
				(core.getActiveTool() == Orkige::EditorTool::Hand) ||
				ImGui::IsKeyDown(ImGuiKey_Space);
			if (handMode && state.scenePanelHovered)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			}
			if (state.scenePanelHovered && !gizmoOwnsMouse &&
				!viewGizmoOwnsMouse && !displayMenuOwnsMouse)
			{
				// Alt+left starts an orbit drag, a plain left click picks
				if (!paintOwnsMouse && !handMode &&
					ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
				{
					// a scene click that reaches here is NOT a paint stroke
					// (paint owns the mouse in paint mode) - so clicking the
					// scene to pick or marquee leaves paint mode
					disarmPaintTileOnIntent(state, core);
					const float nx = (io.MousePos.x - rectMin.x) / avail.x;
					const float ny = (io.MousePos.y - rectMin.y) / avail.y;
					// marquee tools (Select/Translate): a press on EMPTY space
					// arms a rubber-band box select; a press ON an object falls
					// through to the pick below (a Translate gizmo already owns
					// the mouse before we get here when the object is hovered).
					// Screen-space band test, so it works under both projections.
					const Orkige::EditorTool tool = core.getActiveTool();
					const bool marqueeTool =
						(tool == Orkige::EditorTool::Select ||
							tool == Orkige::EditorTool::Translate);
					if (marqueeTool &&
						!rayHitsGameObject(*sceneTarget.camera, nx, ny))
					{
						state.marqueePending = true;
						state.marqueeActive = false;
						state.marqueeExtend =
							io.KeySuper || io.KeyCtrl || io.KeyShift;
						state.marqueeStart = io.MousePos;
						state.marqueeCurrent = io.MousePos;
					}
					else
					{
						// Cmd/Ctrl+click toggles selection-set membership
						pickObjectAtCursor(core, sceneTarget.camera, nx, ny,
							io.KeySuper || io.KeyCtrl);
						// double-click: the pick above selected the hit - frame
						// it too (a double-click on empty space just cleared the
						// selection; frameSelectedObject no-ops then)
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						{
							frameSelectedObject(state, core, sceneTarget.camera);
						}
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
				// Hand tool / Space: left-drag pans (Alt+left still orbits)
				if (handMode &&
					ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyAlt &&
					!state.flyActive && !state.orbitActive &&
					!state.panActive && !state.handPanActive)
				{
					state.handPanActive = true;
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
			// slide the orbit target along the camera plane; the factor scales
			// with distance so a point of mouse travel moves the scene about the
			// same visual amount. Shared by the middle-drag pan and the Hand-tool
			// / Space left-drag pan.
			auto applyPanDelta = [&]()
			{
				const float panScale = state.camera.distance * 0.003f;
				if (viewSettings.editor2D)
				{
					// 2D: the view is axis-aligned (screen right = world +X,
					// screen up = world +Y), so pan the target directly in the
					// XY plane - never through the node orientation, which may
					// still be the orbit pose on the transition frame
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
			};
			if (state.panActive)
			{
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				{
					state.panActive = false;
					state.panDragGate.update(false);
				}
				else if (state.panDragGate.update(true))
				{
					applyPanDelta();
				}
			}
			if (state.handPanActive)
			{
				// grab-the-world pan on the LEFT button while the Hand tool /
				// Space engages it; ends when the button (or the mode) releases
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					state.handPanActive = false;
					state.handPanDragGate.update(false);
				}
				else if (state.handPanDragGate.update(true))
				{
					applyPanDelta();
				}
			}
			// marquee (rubber-band) box select: continues while the left
			// button is held, even if the cursor leaves the panel mid-drag
			// (like the camera drags above). A press that never travels past
			// the drag threshold falls back to the plain empty-space deselect.
			if (state.marqueePending)
			{
				state.marqueeCurrent = io.MousePos;
				if (!state.marqueeActive && Orkige::marqueeIsDrag(
						state.marqueeStart.x, state.marqueeStart.y,
						state.marqueeCurrent.x, state.marqueeCurrent.y,
						MARQUEE_DRAG_THRESHOLD * contentScale))
				{
					state.marqueeActive = true;
				}
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					if (state.marqueeActive)
					{
						const Orkige::ScreenRect box =
							Orkige::screenRectFromCorners(
								state.marqueeStart.x, state.marqueeStart.y,
								state.marqueeCurrent.x, state.marqueeCurrent.y);
						applyMarqueeSelection(core, objectsInMarquee(core,
							sceneTarget.camera, rectMin, avail, box),
							state.marqueeExtend);
					}
					else if (!state.marqueeExtend)
					{
						// empty-space click with no drag: clear the selection
						// (what the plain pick used to do on empty space)
						core.clearSelection();
					}
					state.marqueePending = false;
					state.marqueeActive = false;
				}
				else if (state.marqueeActive)
				{
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					const ImVec2 a(
						std::min(state.marqueeStart.x, state.marqueeCurrent.x),
						std::min(state.marqueeStart.y, state.marqueeCurrent.y));
					const ImVec2 b(
						std::max(state.marqueeStart.x, state.marqueeCurrent.x),
						std::max(state.marqueeStart.y, state.marqueeCurrent.y));
					drawList->AddRectFilled(a, b, IM_COL32(90, 150, 240, 40));
					drawList->AddRect(a, b, IM_COL32(120, 175, 255, 220));
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
