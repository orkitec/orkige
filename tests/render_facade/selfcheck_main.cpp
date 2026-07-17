/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	selfcheck_main.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file selfcheck_main.cpp
//! @brief render_facade_selfcheck - THE backend conformance suite
//! @remarks This test IS the contract every render backend must pass
//! (Docs/render-abstraction.md, "Test matrix"): it exercises the whole
//! engine_render facade end to end in one headed run - startup + window,
//! world/node hierarchy, mesh instance, sprite quad, perspective + ortho
//! cameras, light, RTT with writeContentsToFile, ray queries, frame
//! stats and non-black screenshots. It is backend-agnostic BY
//! CONSTRUCTION: this TU includes ONLY engine_render facade headers (+
//! the SelfcheckBootstrap seam + std) - if a check cannot be written
//! against the facade, the facade is missing API, not this file a
//! backend include. Keep it that way; the A2 Ogre-Next backend must run
//! this file UNCHANGED (only bootstrap_next.cpp is added).

#include "SelfcheckBootstrap.h"

#include <engine_render/RenderPrerequisites.h>
#include <engine_render/RenderMath.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderWorld.h>
#include <engine_render/RenderNode.h>
#include <engine_render/MeshInstance.h>
#include <engine_render/SpriteQuad.h>
#include <engine_render/VectorMesh.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/RenderLight.h>
#include <engine_render/RenderTexture.h>
#include <engine_render/DrawLayer2D.h>

// the pure, renderer-free tessellator (orkige_core) that produces the vector
// shape's triangles - the ONE non-facade include, and deliberate: it proves
// the whole flat-colour-shape pipeline (tessellate -> facade VectorMesh ->
// render), and the parity gate compares the RTT it draws into on both flavors
#include <core_util/VectorTessellator.h>

// the capability register conformance leg (@see RenderCaps): a facade-level
// enum whose live per-backend bitset must match the committed per-backend
// snapshot the bootstrap seam exposes (SelfcheckBootstrap::expectedRenderCapSupport)
#include <engine_render/RenderCaps.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Orkige;

//! fail loudly and abort the run - the non-zero exit code IS the contract
#define SELFCHECK(condition, description) \
	do \
	{ \
		if(!(condition)) \
		{ \
			std::fprintf(stderr, "render_facade_selfcheck: FAILED - %s " \
				"(%s:%d: %s)\n", description, __FILE__, __LINE__, \
				#condition); \
			return 1; \
		} \
		std::printf("render_facade_selfcheck: ok - %s\n", description); \
	} while(false)

namespace
{
	bool nearlyEqual(Real a, Real b, Real epsilon = Real(0.001))
	{
		return std::abs(a - b) <= epsilon;
	}
	bool nearlyEqual(Vec3 const & a, Vec3 const & b, Real epsilon = Real(0.001))
	{
		return (a - b).length() <= epsilon;
	}
	//! render some frames, pumping the host loop between them
	bool renderFrames(RenderSystem* renderSystem, int count)
	{
		bool quitRequested = false;
		for(int each = 0; each < count; ++each)
		{
			SelfcheckBootstrap::pumpHostEvents(quitRequested);
			if(!renderSystem->renderOneFrame())
			{
				return false;
			}
		}
		return true;
	}

	//! Render up to maxFrames one at a time, saving the window and probing
	//! pixel (x,y) after each, until predicate(r,g,b) holds; returns whether
	//! it settled. A DrawLayer2D show/hide/drop settles within the backend's
	//! frame depth (the visibility-settling contract in DrawLayer2D.h), so a
	//! retained batch takes a few frames to appear or clear where the backend's
	//! vertex buffers are multi-buffered. Settle on the pixel rather than pin a
	//! frame count: a single-frame backend passes on the first iteration, a
	//! multi-buffered one within its depth - deterministic on every driver.
	template <typename Predicate>
	bool settleUntilPixel(RenderSystem* renderSystem, std::string const & shotPath,
		unsigned int x, unsigned int y, Predicate predicate, int maxFrames)
	{
		float r = 0, g = 0, b = 0;
		for(int each = 0; each < maxFrames; ++each)
		{
			if(!renderFrames(renderSystem, 1))
			{
				return false;
			}
			renderSystem->saveWindowContents(shotPath);
			if(SelfcheckBootstrap::readImagePixel(shotPath, x, y, r, g, b) &&
				predicate(r, g, b))
			{
				return true;
			}
		}
		return false;
	}
}

static int runChecks(RenderSystem* renderSystem, std::string const & outDir)
{
	//--- render system access ------------------------------------------
	SELFCHECK(renderSystem != NULL, "backend booted a render system");
	SELFCHECK(RenderSystem::get() == renderSystem,
		"RenderSystem::get() returns the booted system");
	RenderWorld* world = renderSystem->getWorld();
	SELFCHECK(world != NULL, "the render system carries a world");

	//--- resources through the facade ----------------------------------
	renderSystem->addResourceLocation(ORKIGE_SELFCHECK_MESH_DIR);
	renderSystem->addResourceLocation(ORKIGE_SELFCHECK_TEXTURE_DIR);
	renderSystem->initialiseResourceGroups();

	//--- ambient light --------------------------------------------------
	world->setAmbientLight(Color(0.9f, 0.85f, 0.8f));
	SELFCHECK(nearlyEqual(world->getAmbientLight().g, Real(0.85)),
		"ambient light round-trips");

	//--- node hierarchy + transforms ------------------------------------
	optr<RenderNode> root = world->getRootNode();
	SELFCHECK(root != NULL, "world has a root node");
	optr<RenderNode> parent = world->createNode("selfcheck.parent");
	SELFCHECK(parent != NULL, "createNode works");
	SELFCHECK(parent->getParent() == NULL,
		"a node under the world root reports a NULL parent");
	optr<RenderNode> child = parent->createChild("selfcheck.child");
	SELFCHECK(child != NULL, "createChild works");
	SELFCHECK(child->getParent() == parent, "child navigates to its parent");
	SELFCHECK(parent->numChildren() == 1 && parent->getChild(0) == child,
		"parent enumerates its child");
	parent->setPosition(Vec3(1, 2, 3));
	child->setPosition(Vec3(1, 0, 0));
	SELFCHECK(nearlyEqual(child->getPosition(), Vec3(1, 0, 0)),
		"local position round-trips");
	SELFCHECK(nearlyEqual(child->getWorldPosition(), Vec3(2, 2, 3)),
		"world position composes parent + local transform");
	parent->yaw(Degree(90), RenderNode::TS_LOCAL);
	SELFCHECK(nearlyEqual(child->getWorldPosition(), Vec3(1, 2, 2)),
		"parent yaw rotates the child's world position");
	parent->yaw(Degree(-90), RenderNode::TS_LOCAL);
	child->translate(Vec3(0, 1, 0), RenderNode::TS_PARENT);
	SELFCHECK(nearlyEqual(child->getPosition(), Vec3(1, 1, 0)),
		"translate moves in parent space");
	parent->setScale(Vec3(2, 2, 2));
	SELFCHECK(nearlyEqual(child->getWorldPosition(), Vec3(3, 4, 3)),
		"parent scale scales the child's offset");
	parent->setScale(Vec3(1, 1, 1));
	// re-parenting (local transform is kept, world follows the new parent)
	optr<RenderNode> foster = world->createNode("selfcheck.foster");
	foster->setPosition(Vec3(10, 0, 0));
	child->setParent(foster);
	SELFCHECK(child->getParent() == foster &&
		parent->numChildren() == 0 && foster->numChildren() == 1,
		"setParent rewires the facade graph");
	SELFCHECK(nearlyEqual(child->getWorldPosition(), Vec3(11, 1, 0)),
		"re-parented child follows the new parent's transform");
	child->setParent(parent);
	// orientation helpers (smoke + one value probe)
	parent->setFixedYawAxis(true, Vec3::UNIT_Y);
	parent->lookAt(Vec3(1, 2, -100), RenderNode::TS_WORLD);
	parent->setDirection(Vec3::NEGATIVE_UNIT_Z, RenderNode::TS_WORLD);
	parent->pitch(Degree(10));
	parent->roll(Degree(-10));
	parent->setOrientation(Quat::IDENTITY);

	//--- mesh instances --------------------------------------------------
	optr<RenderNode> meshNode = world->createNode("selfcheck.meshNode");
	optr<RenderNode> meshChild = meshNode->createChild("selfcheck.meshChild");
	int pickTag = 42;
	meshNode->setUserPointer(&pickTag);
	SELFCHECK(meshNode->getUserPointer() == &pickTag &&
		meshChild->getUserPointer() == NULL &&
		meshChild->findUserPointerUpwards() == &pickTag,
		"user pointers resolve locally and upwards");
	optr<MeshInstance> mesh = world->createMeshInstance("test_mesh.glb");
	SELFCHECK(mesh != NULL, "createMeshInstance loads test_mesh.glb");
	SELFCHECK(mesh->getMeshName() == "test_mesh.glb",
		"mesh instance remembers its mesh name");
	mesh->attachTo(meshChild);
	mesh->setVertexColourUnlit();
	mesh->setCastShadows(false);
	AABB localBounds = mesh->getLocalBounds();
	SELFCHECK(localBounds.isFinite() && !localBounds.isNull(),
		"mesh has finite local bounds");
	SELFCHECK(mesh->getNumSubMeshes() >= 1, "mesh has sub-meshes");
	// a textured mesh for the material introspection probe
	optr<RenderNode> platformNode = world->createNode("selfcheck.platform");
	platformNode->setPosition(Vec3(0, -2.5f, 0));
	optr<MeshInstance> platform =
		world->createMeshInstance("jumper_platform.glb");
	SELFCHECK(platform != NULL, "createMeshInstance loads a textured mesh");
	platform->attachTo(platformNode);
	SELFCHECK(platform->subMeshHasTexture(0),
		"textured mesh reports a texture on sub-mesh 0");
	// animation control surface (these meshes carry no animations - the
	// surface must behave, not crash; animated coverage is an honest TODO)
	StringVector animationNames = mesh->getAnimationNames();
	SELFCHECK(!mesh->hasAnimation("__selfcheck_no_such_animation__"),
		"hasAnimation is false for unknown names");
	SELFCHECK(mesh->getEnabledAnimations().empty(),
		"no animations are enabled initially");
	mesh->setAnimationEnabled("__selfcheck_no_such_animation__", true);
	mesh->setAnimationLoop("__selfcheck_no_such_animation__", true);
	mesh->addAnimationTime("__selfcheck_no_such_animation__", 0.1f);
	SELFCHECK(mesh->getAnimationLength("__selfcheck_no_such_animation__")
		== 0.0f, "animation calls on unknown names are safe no-ops");

	//--- world bounds -----------------------------------------------------
	AABB worldBounds = meshNode->getWorldBounds();
	SELFCHECK(worldBounds.isFinite() && !worldBounds.isNull(),
		"node world bounds cover the attached mesh");

	//--- perspective camera on the window --------------------------------
	optr<RenderCamera> camera = world->createCamera("selfcheck.camera");
	SELFCHECK(camera != NULL, "createCamera works");
	optr<RenderNode> cameraNode = world->createNode("selfcheck.cameraNode");
	cameraNode->setFixedYawAxis(true, Vec3::UNIT_Y);
	cameraNode->setPosition(Vec3(0, 2, 8));
	cameraNode->lookAt(Vec3::ZERO, RenderNode::TS_WORLD);
	camera->setPerspective(Degree(60), Real(0.1), Real(1000));
	camera->attachTo(cameraNode);
	SELFCHECK(camera->getProjectionType() == RenderCamera::PT_PERSPECTIVE,
		"projection type reports perspective");
	camera->setFOVy(Degree(55));
	SELFCHECK(nearlyEqual(camera->getFOVy().valueDegrees(), Real(55),
		Real(0.01)), "FOVy round-trips");
	renderSystem->showCameraOnWindow(camera);
	renderSystem->setWindowBackgroundColour(Color(0.1f, 0.12f, 0.3f));
	unsigned int windowWidth = 0, windowHeight = 0;
	renderSystem->getWindowSize(windowWidth, windowHeight);
	SELFCHECK(windowWidth > 0 && windowHeight > 0,
		"window reports a real drawable size");

	//--- light ------------------------------------------------------------
	optr<RenderLight> light = world->createLight();
	SELFCHECK(light != NULL, "createLight works");
	optr<RenderNode> lightNode = world->createNode("selfcheck.lightNode");
	lightNode->setPosition(Vec3(3, 6, 5));
	light->attachTo(lightNode);
	light->setType(RenderLight::LT_POINT);
	SELFCHECK(light->getType() == RenderLight::LT_POINT,
		"light type round-trips");
	light->setDiffuseColour(Color(1.0f, 0.95f, 0.9f));
	light->setSpecularColour(Color(0.2f, 0.2f, 0.2f));
	light->setRange(50);
	light->setCastShadows(false);

	//--- sprite quad + ortho camera + RTT ---------------------------------
	// a little 2D corner far away from the 3D content, rendered offscreen
	// through an ORTHO camera - black RTT background, so any non-black
	// pixel in the written file must be the sprite itself
	optr<SpriteQuad> sprite = world->createSpriteQuad("platform.png");
	SELFCHECK(sprite != NULL, "createSpriteQuad loads platform.png");
	SELFCHECK(sprite->getTextureName() == "platform.png",
		"sprite remembers its texture name");
	float texelWidth = 0, texelHeight = 0;
	sprite->getTextureSize(texelWidth, texelHeight);
	SELFCHECK(texelWidth > 0 && texelHeight > 0,
		"sprite reports the texel size of its texture");
	optr<RenderNode> spriteNode = world->createNode("selfcheck.spriteNode");
	spriteNode->setPosition(Vec3(100, 0, 0));
	sprite->attachTo(spriteNode);
	sprite->setSize(2.0f, 2.0f);
	sprite->setUVRect(0.0f, 0.0f, 1.0f, 1.0f);
	sprite->setTint(Color(1, 1, 1, 1));
	sprite->setFlip(true, false);
	sprite->setFlip(false, false);
	sprite->setZOrder(5);
	sprite->setVisible(true);

	//--- vector shape (untextured tessellated fill + feather) -------------
	// a concave blob tessellated headlessly (VectorTessellator, the .oshape
	// runtime producer), fed to the facade VectorMesh and placed in the ortho
	// camera's view at the RTT's top-left corner - clear of the sprite. Proves
	// createVectorMesh + concave tessellation + the shared untextured
	// vertex-colour datablock render, on BOTH flavors (the parity gate compares
	// this RTT screenshot).
	std::vector<VectorTessellator::Region> shapeRegions;
	{
		VectorTessellator::Region region;
		region.fill = VectorTessellator::Colour(0.2f, 0.8f, 0.3f, 1.0f);
		// a small concave arrow/blob (~0.5 world units), notched at the top
		region.outer.push_back(VectorTessellator::Point(-0.25f, -0.25f));
		region.outer.push_back(VectorTessellator::Point(0.25f, -0.25f));
		region.outer.push_back(VectorTessellator::Point(0.25f, 0.25f));
		region.outer.push_back(VectorTessellator::Point(0.0f, 0.05f));
		region.outer.push_back(VectorTessellator::Point(-0.25f, 0.25f));
		shapeRegions.push_back(region);
	}
	VectorTessellator::Mesh shapeMesh;
	VectorTessellator::build(shapeRegions, 0.01f, shapeMesh);
	SELFCHECK(shapeMesh.triangleCount() > 0,
		"the vector tessellator produced triangles");
	std::vector<VectorMesh::Vertex> shapeVertices;
	for(std::size_t each = 0; each < shapeMesh.positions.size(); ++each)
	{
		VectorMesh::Vertex vertex;
		vertex.position = Vec2(shapeMesh.positions[each].x,
			shapeMesh.positions[each].y);
		vertex.colour = Color(shapeMesh.colours[each].r,
			shapeMesh.colours[each].g, shapeMesh.colours[each].b,
			shapeMesh.colours[each].a);
		shapeVertices.push_back(vertex);
	}
	optr<VectorMesh> vectorShape = world->createVectorMesh();
	SELFCHECK(vectorShape != NULL, "createVectorMesh works");
	vectorShape->setMesh(shapeVertices.data(), shapeVertices.size(),
		shapeMesh.indices.data(), shapeMesh.indices.size());
	SELFCHECK(vectorShape->getTriangleCount() == shapeMesh.triangleCount(),
		"the facade mesh reports the triangle count it was filled with");
	optr<RenderNode> shapeNode = world->createNode("selfcheck.shapeNode");
	// top-left of the RTT ortho view (camera at x=100, half-width 3,
	// half-height 1.5) - well clear of the sprite at the centre
	shapeNode->setPosition(Vec3(97.8f, 1.0f, 0.1f));
	vectorShape->attachTo(shapeNode);
	vectorShape->setZOrder(6);
	vectorShape->setVisible(true);

	//--- textured mesh with a vertical-gradient texture (V-flip gate) -----
	// a glTF quad whose baseColor texture runs RED (top) -> BLUE (bottom);
	// glTF's UV origin is top-left, so an upright import puts the red end at
	// the quad's +Y (top) edge. Rendered vertex-colour unlit (no lighting
	// variance) into the SAME ortho RTT the parity gate compares, on the
	// right side clear of the sprite and vector shape. Two probes below
	// assert the absolute orientation within one flavor; the parity gate
	// catches any cross-flavor V-flip disagreement over the whole RTT.
	optr<RenderNode> gradientNode = world->createNode("selfcheck.gradient");
	gradientNode->setPosition(Vec3(101.8f, 0.0f, 0.1f));
	gradientNode->setScale(Vec3(1.2f, 1.2f, 1.0f));
	optr<MeshInstance> gradientMesh =
		world->createMeshInstance("uvcheck_mesh.glb");
	SELFCHECK(gradientMesh != NULL,
		"createMeshInstance loads the UV-check gradient mesh");
	SELFCHECK(gradientMesh->subMeshHasTexture(0),
		"the UV-check mesh carries its embedded gradient texture");
	gradientMesh->attachTo(gradientNode);
	gradientMesh->setVertexColourUnlit();	// show the texture unlit
	gradientMesh->setCastShadows(false);

	optr<RenderCamera> orthoCamera = world->createCamera("selfcheck.ortho");
	optr<RenderNode> orthoNode = world->createNode("selfcheck.orthoNode");
	orthoNode->setPosition(Vec3(100, 0, 5));
	orthoNode->lookAt(Vec3(100, 0, 0), RenderNode::TS_WORLD);
	orthoCamera->setOrthographic(Real(1.5), Real(0.1), Real(100));
	orthoCamera->attachTo(orthoNode);
	SELFCHECK(orthoCamera->getProjectionType() ==
		RenderCamera::PT_ORTHOGRAPHIC, "projection type reports ortho");

	optr<RenderTexture> renderTexture =
		renderSystem->createRenderTexture("selfcheck.rtt", 256, 128);
	SELFCHECK(renderTexture != NULL, "createRenderTexture works");
	renderTexture->setCamera(orthoCamera);
	renderTexture->setBackgroundColour(Color(0, 0, 0, 1));
	renderTexture->setOverlaysEnabled(false);
	renderTexture->setShadowsEnabled(false);
	SELFCHECK(renderTexture->getWidth() == 256 &&
		renderTexture->getHeight() == 128, "RTT reports its size");
	SELFCHECK(renderTexture->getNativeTextureId() != 0,
		"RTT exposes a native texture id");
	renderTexture->resize(320, 160);
	SELFCHECK(renderTexture->getWidth() == 320 &&
		renderTexture->getHeight() == 160, "RTT resize-by-recreate works");
	SELFCHECK(renderTexture->getNativeTextureId() != 0,
		"RTT still has a native texture id after resize");

	//--- frames ------------------------------------------------------------
	SELFCHECK(renderFrames(renderSystem, 30), "30 frames render");
	renderSystem->notifyWindowResized();	// resize plumbing smoke
	SELFCHECK(renderFrames(renderSystem, 5), "frames render after resize notify");

	//--- stats ---------------------------------------------------------------
	RenderSystem::FrameStats stats = renderSystem->getFrameStats();
	SELFCHECK(stats.triangleCount > 0,
		"frame stats report rendered triangles");
	std::printf("render_facade_selfcheck: stats - %.1f fps, %zu triangles, "
		"%zu batches\n", stats.lastFPS, stats.triangleCount, stats.batchCount);

	//--- screen <-> world ------------------------------------------------
	Real projectedX = -1, projectedY = -1;
	SELFCHECK(camera->projectPoint(Vec3::ZERO, projectedX, projectedY),
		"the look-at point projects");
	SELFCHECK(nearlyEqual(projectedX, Real(0.5), Real(0.02)) &&
		nearlyEqual(projectedY, Real(0.5), Real(0.02)),
		"the look-at point projects to the viewport center");
	Real behindX = 0, behindY = 0;
	SELFCHECK(!camera->projectPoint(Vec3(0, 2, 20), behindX, behindY),
		"points behind the camera refuse to project");
	// view matrix sanity: the camera's world position maps to the eye origin
	Vec3 eyeSpaceCameraPosition =
		camera->getViewMatrix() * cameraNode->getWorldPosition();
	SELFCHECK(nearlyEqual(eyeSpaceCameraPosition, Vec3(0, 0, 0), Real(0.01)),
		"view matrix maps the camera position to the eye origin");
	SELFCHECK(nearlyEqual(camera->getProjectionMatrix()[3][3], Real(0)),
		"perspective projection matrix has the expected form");

	//--- ray picking ------------------------------------------------------
	Ray3 centerRay = camera->viewportPointToRay(Real(0.5), Real(0.5));
	std::vector<RenderWorld::RayQueryHit> hits = world->queryRay(centerRay);
	bool meshWasHit = false;
	for(RenderWorld::RayQueryHit const & hit : hits)
	{
		if(hit.node == meshChild)
		{
			meshWasHit = true;
			SELFCHECK(hit.userPointer == &pickTag,
				"the hit resolves its user pointer through the parents");
			SELFCHECK(hit.distance > Real(0) && hit.distance < Real(20),
				"the hit distance is plausible");
			break;
		}
	}
	SELFCHECK(meshWasHit, "the center ray hits the mesh's node");
	// mask filtering: facade content carries QUERYFLAG_DEFAULT (1) - a
	// disjoint mask must not return it
	std::vector<RenderWorld::RayQueryHit> maskedHits =
		world->queryRay(centerRay, 0x2);
	bool meshHitDespiteMask = false;
	for(RenderWorld::RayQueryHit const & hit : maskedHits)
	{
		meshHitDespiteMask = meshHitDespiteMask || (hit.node == meshChild);
	}
	SELFCHECK(!meshHitDespiteMask, "query masks filter facade content");

	//--- screenshots (must not be black) -----------------------------------
	const std::string windowShot = outDir + "/selfcheck_window.png";
	const std::string rttShot = outDir + "/selfcheck_rtt.png";
	renderSystem->saveWindowContents(windowShot);
	renderTexture->writeContentsToFile(rttShot);
	SELFCHECK(SelfcheckBootstrap::imageHasNonBlackPixel(windowShot),
		"the window screenshot is not black");
	SELFCHECK(SelfcheckBootstrap::imageHasNonBlackPixel(rttShot),
		"the RTT capture is not black (the sprite rendered)");
	// the vector shape sits at the RTT's top-left corner (world (97.8,1.0) ->
	// ~pixel (43,27) in the 320x160 ortho view); its interior must read the
	// green fill against the black RTT background. A DOMINANCE check (green
	// clearly above red/blue), tolerant of the flavor's sRGB drift.
	{
		float shapeRed = 0, shapeGreen = 0, shapeBlue = 0;
		SELFCHECK(SelfcheckBootstrap::readImagePixel(rttShot, 43, 27,
			shapeRed, shapeGreen, shapeBlue), "the vector-shape probe decodes");
		SELFCHECK(shapeGreen > shapeRed + 0.2f && shapeGreen > shapeBlue + 0.2f,
			"the vector shape's green fill rendered into the RTT");
	}
	// the gradient quad (world ~(101.8,0), 1.2 units) covers RTT pixels
	// ~x[224..288] y[48..112] in the 320x160 ortho view. Its texture runs red
	// (top) -> blue (bottom); glTF's top-left UV origin means an upright import
	// puts red at the quad's TOP. ABSOLUTE orientation probe (one flavor, not
	// just cross-flavor agreement): the top interior must read red-dominant and
	// the bottom interior blue-dominant. A V-flip swaps them and fails here.
	{
		float topRed = 0, topGreen = 0, topBlue = 0;
		SELFCHECK(SelfcheckBootstrap::readImagePixel(rttShot, 256, 60,
			topRed, topGreen, topBlue), "the gradient top probe decodes");
		SELFCHECK(topRed > topBlue + 0.2f,
			"the gradient mesh renders upright: red end at the top (glTF "
			"top-left UV origin)");
		float bottomRed = 0, bottomGreen = 0, bottomBlue = 0;
		SELFCHECK(SelfcheckBootstrap::readImagePixel(rttShot, 256, 100,
			bottomRed, bottomGreen, bottomBlue),
			"the gradient bottom probe decodes");
		SELFCHECK(bottomBlue > bottomRed + 0.2f,
			"the gradient mesh renders upright: blue end at the bottom");
	}

	//--- 2D draw layers (the DrawLayer2D conformance pattern) --------------
	// a known pattern in the top-left corner, pixel-verified from a window
	// screenshot on EVERY backend: layer z-order, batch colours, exact
	// scissor clipping, alpha blending, textured batches, show/hide.
	// Colour probes are DOMINANCE checks: the Next flavor renders into an
	// sRGB swapchain (documented flavor difference), so absolute values
	// drift while hue relations hold.
	{
		auto makeQuad = [](Real left, Real top, Real right, Real bottom,
			Color const & colour, Real u0 = 0, Real v0 = 0,
			Real u1 = 1, Real v1 = 1)
			-> std::vector<DrawLayer2D::Vertex2D>
		{
			std::vector<DrawLayer2D::Vertex2D> quad;
			quad.push_back(DrawLayer2D::Vertex2D(left, top, u0, v0, colour));
			quad.push_back(DrawLayer2D::Vertex2D(right, top, u1, v0, colour));
			quad.push_back(DrawLayer2D::Vertex2D(right, bottom, u1, v1, colour));
			quad.push_back(DrawLayer2D::Vertex2D(left, top, u0, v0, colour));
			quad.push_back(DrawLayer2D::Vertex2D(right, bottom, u1, v1, colour));
			quad.push_back(DrawLayer2D::Vertex2D(left, bottom, u0, v1, colour));
			return quad;
		};
		// deliberately created in REVERSE z order: composition must follow
		// zOrder, not creation order
		optr<DrawLayer2D> topLayer = renderSystem->createDrawLayer2D(10);
		optr<DrawLayer2D> bottomLayer = renderSystem->createDrawLayer2D(0);
		optr<DrawLayer2D> dynamicLayer = renderSystem->createDrawLayer2D(5);
		optr<DrawLayer2D> hiddenLayer = renderSystem->createDrawLayer2D(20);
		SELFCHECK(topLayer && bottomLayer && dynamicLayer && hiddenLayer,
			"createDrawLayer2D works");
		SELFCHECK(topLayer->getZOrder() == 10 && bottomLayer->isVisible(),
			"draw layers report zOrder and visibility");

		// the whole pattern lives in the TOP strip (y < 220) of the
		// drawable: the 3D content (cube around the viewport centre,
		// platform below it) never reaches it on any drawable size
		// bottom: solid red; top: solid green overlapping it
		std::vector<DrawLayer2D::Vertex2D> redQuad =
			makeQuad(20, 20, 220, 220, Color(1, 0, 0, 1));
		bottomLayer->addTriangles("", redQuad.data(), redQuad.size());
		std::vector<DrawLayer2D::Vertex2D> greenQuad =
			makeQuad(120, 120, 320, 220, Color(0, 1, 0, 1));
		topLayer->addTriangles("", greenQuad.data(), greenQuad.size());
		// half-transparent white OVER the red quad: verifies alpha
		// blending AND within-layer batch order (white blends over red -
		// the pixel goes pink, not back to opaque red)
		std::vector<DrawLayer2D::Vertex2D> blendQuad =
			makeQuad(20, 20, 120, 120, Color(1, 1, 1, 0.5f));
		bottomLayer->addTriangles("", blendQuad.data(), blendQuad.size());
		// yellow, scissored to its left half (240..340 of 240..440) -
		// submitted INDEXED to cover the index path too
		{
			const DrawLayer2D::Vertex2D corners[4] = {
				DrawLayer2D::Vertex2D(240, 20, 0, 0, Color(1, 1, 0, 1)),
				DrawLayer2D::Vertex2D(440, 20, 1, 0, Color(1, 1, 0, 1)),
				DrawLayer2D::Vertex2D(440, 120, 1, 1, Color(1, 1, 0, 1)),
				DrawLayer2D::Vertex2D(240, 120, 0, 1, Color(1, 1, 0, 1)),
			};
			const unsigned short indices[6] = { 0, 1, 2, 0, 2, 3 };
			DrawLayer2D::ScissorRect scissor;
			scissor.left = 240;
			scissor.top = 20;
			scissor.width = 100;
			scissor.height = 100;
			bottomLayer->addTriangles("", corners, 4, indices, 6, &scissor);
		}
		// textured batch (resource-system binding, same texture the sprite
		// used)
		std::vector<DrawLayer2D::Vertex2D> texturedQuad =
			makeQuad(460, 20, 588, 148, Color(1, 1, 1, 1));
		bottomLayer->addTriangles("platform.png",
			texturedQuad.data(), texturedQuad.size());
		// magenta on the hidden layer - must NOT appear
		std::vector<DrawLayer2D::Vertex2D> magentaQuad =
			makeQuad(650, 20, 850, 120, Color(1, 0, 1, 1));
		hiddenLayer->addTriangles("", magentaQuad.data(), magentaQuad.size());
		hiddenLayer->setVisible(false);
		// a cyan sliver whose 2D pixel coordinates happen to cover the RTT
		// camera's world-frustum region (pixel space maps 1:1 onto world
		// x/-y): if a backend leaked 2D content into scene passes, the RTT
		// leak probe below would catch this quad
		std::vector<DrawLayer2D::Vertex2D> rttBaitQuad =
			makeQuad(95, 0, 105, 2, Color(0, 1, 1, 1));
		bottomLayer->addTriangles("", rttBaitQuad.data(), rttBaitQuad.size());

		SELFCHECK(renderFrames(renderSystem, 3), "frames render with 2D layers");
		const std::string patternShot = outDir + "/selfcheck_drawlayer2d.png";
		renderSystem->saveWindowContents(patternShot);

		float red = 0, green = 0, blue = 0;
		SELFCHECK(SelfcheckBootstrap::readImagePixel(patternShot, 170, 60,
			red, green, blue), "the 2D pattern screenshot decodes");
		SELFCHECK(red > green + 0.3f && red > blue + 0.3f,
			"2D pattern: the bottom layer's red quad rendered");
		SELFCHECK(SelfcheckBootstrap::readImagePixel(patternShot, 170, 170,
			red, green, blue), "overlap probe decodes");
		SELFCHECK(green > red + 0.3f && green > blue + 0.3f,
			"2D pattern: zOrder composites the top layer over the bottom one");
		SELFCHECK(SelfcheckBootstrap::readImagePixel(patternShot, 70, 70,
			red, green, blue), "alpha-blend probe decodes");
		SELFCHECK(red > 0.8f && green > 0.3f && green < 0.9f &&
			std::abs(green - blue) < 0.15f,
			"2D pattern: half-transparent white blends over the red batch (pink)");
		SELFCHECK(SelfcheckBootstrap::readImagePixel(patternShot, 260, 60,
			red, green, blue), "scissor-inside probe decodes");
		SELFCHECK(red > 0.5f && green > 0.5f && blue < red - 0.3f,
			"2D pattern: pixels inside the scissor rect draw (yellow)");
		float backgroundRed = 0, backgroundGreen = 0, backgroundBlue = 0;
		SELFCHECK(SelfcheckBootstrap::readImagePixel(patternShot, 920, 60,
			backgroundRed, backgroundGreen, backgroundBlue),
			"background reference probe decodes");
		SELFCHECK(SelfcheckBootstrap::readImagePixel(patternShot, 400, 60,
			red, green, blue), "scissor-outside probe decodes");
		SELFCHECK(std::abs(red - backgroundRed) < 0.1f &&
			std::abs(green - backgroundGreen) < 0.1f &&
			std::abs(blue - backgroundBlue) < 0.1f,
			"2D pattern: pixels outside the scissor rect stay background");
		SELFCHECK(SelfcheckBootstrap::readImagePixel(patternShot, 750, 70,
			red, green, blue), "hidden-layer probe decodes");
		SELFCHECK(!(red > 0.5f && blue > 0.5f && green < 0.3f),
			"2D pattern: the hidden layer does not render");
		// textured batch: at least one probe must differ from background
		{
			bool texturedRendered = false;
			const unsigned int probes[5][2] =
				{ {490, 50}, {520, 80}, {550, 110}, {570, 40}, {480, 130} };
			for(int each = 0; each < 5; ++each)
			{
				if(!SelfcheckBootstrap::readImagePixel(patternShot,
					probes[each][0], probes[each][1], red, green, blue))
				{
					continue;
				}
				if(std::abs(red - backgroundRed) > 0.15f ||
					std::abs(green - backgroundGreen) > 0.15f ||
					std::abs(blue - backgroundBlue) > 0.15f)
				{
					texturedRendered = true;
					break;
				}
			}
			SELFCHECK(texturedRendered,
				"2D pattern: the textured batch binds through the resource system");
		}
		// Live raw-RGBA upload: mirror the animation-preview handoff by replacing
		// ring entries every frame while submitting the PREVIOUS settled entry.
		// The final screenshot is taken while a new red upload is happening, but
		// must contain the preceding blue pose rather than a white fallback.
		{
			const String uploadNames[2] = {
				"selfcheck_dynamic_rgba_0", "selfcheck_dynamic_rgba_1"
			};
			std::vector<unsigned char> pixels(16u * 16u * 4u, 255u);
			std::vector<DrawLayer2D::Vertex2D> dynamicQuad =
				makeQuad(600, 140, 728, 212, Color(1, 1, 1, 1));
			for(int frame = 0; frame <= 8; ++frame)
			{
				const bool blueFrame = (frame & 1) != 0;
				for(size_t pixel = 0; pixel < pixels.size(); pixel += 4u)
				{
					pixels[pixel] = blueFrame ? 24u : 230u;
					pixels[pixel + 1u] = blueFrame ? 48u : 24u;
					pixels[pixel + 2u] = blueFrame ? 230u : 24u;
					pixels[pixel + 3u] = 255u;
				}
				const String & uploadName = uploadNames[frame & 1];
				SELFCHECK(renderSystem->createTexture2D(uploadName, pixels.data(),
					16u, 16u), "a live raw-RGBA texture uploads");
				dynamicLayer->clear();
				if(frame > 0)
				{
					dynamicLayer->addTriangles(uploadNames[(frame - 1) & 1],
						dynamicQuad.data(), dynamicQuad.size());
				}
				if(frame < 8)
				{
					SELFCHECK(renderFrames(renderSystem, 1),
						"a frame renders during raw-RGBA texture churn");
				}
			}
			// The newly replaced red entry intentionally gets no settling frame;
			// the normal render submits the blue entry uploaded and rendered on the
			// preceding frame. The screenshot then verifies the displayed handoff.
			SELFCHECK(renderFrames(renderSystem, 1),
				"the settled raw-RGBA ring entry renders during a new upload");
			const std::string dynamicShot =
				outDir + "/selfcheck_drawlayer2d_dynamic.png";
			renderSystem->saveWindowContents(dynamicShot);
			SELFCHECK(SelfcheckBootstrap::readImagePixel(dynamicShot, 664, 176,
				red, green, blue), "live raw-RGBA texture probe decodes");
			// The settled pose displays the PREVIOUS ring entry (blue): the
			// probe rings two alternating texture names, drawing the one that
			// was NOT overwritten this frame, so both backends show the settled
			// entry - next because it forbids sampling a texture in the frame it
			// was created, classic because the OTHER name still holds it.
			SELFCHECK(blue > red + 0.4f && blue > green + 0.4f,
				"2D pattern: live raw-RGBA handoff displays the settled pose");
			dynamicLayer->clear();
			renderSystem->destroyTexture2D(uploadNames[0]);
			renderSystem->destroyTexture2D(uploadNames[1]);
		}
		// Create-or-REPLACE under the same name, with NO frame rendered in
		// between: the shape a runtime font atlas takes when it bakes a glyph
		// on demand and re-uploads its page right after the boot bake. The
		// first upload's pixels can still be in flight then, and a backend that
		// only defers freeing the previous incarnation would refuse the second
		// create under that name (the name must be free the moment the replace
		// returns). Batches bound to the name must follow the new incarnation.
		{
			const String atlasName = "selfcheck_atlas_rebake";
			std::vector<DrawLayer2D::Vertex2D> atlasQuad =
				makeQuad(600, 140, 728, 212, Color(1, 1, 1, 1));
			auto fillPage = [](std::vector<unsigned char> & page,
				unsigned char r, unsigned char g, unsigned char b)
			{
				for(size_t pixel = 0; pixel < page.size(); pixel += 4u)
				{
					page[pixel] = r;
					page[pixel + 1u] = g;
					page[pixel + 2u] = b;
					page[pixel + 3u] = 255u;
				}
			};
			std::vector<unsigned char> page(32u * 32u * 4u, 255u);
			fillPage(page, 230u, 24u, 24u);
			SELFCHECK(renderSystem->createTexture2D(atlasName, page.data(),
				32u, 32u), "an atlas page uploads");
			// the replace, before a single frame has rendered
			fillPage(page, 24u, 230u, 24u);
			SELFCHECK(renderSystem->createTexture2D(atlasName, page.data(),
				32u, 32u),
				"the same atlas page re-uploads before a frame renders");
			// and once more at a DIFFERENT size (a page that outgrew itself)
			std::vector<unsigned char> grown(64u * 64u * 4u, 255u);
			fillPage(grown, 24u, 48u, 230u);
			SELFCHECK(renderSystem->createTexture2D(atlasName, grown.data(),
				64u, 64u), "the atlas page re-uploads at a new size");
			dynamicLayer->clear();
			dynamicLayer->addTriangles(atlasName, atlasQuad.data(),
				atlasQuad.size());
			SELFCHECK(renderFrames(renderSystem, 2),
				"frames render after an atlas re-upload");
			const std::string rebakeShot =
				outDir + "/selfcheck_drawlayer2d_rebake.png";
			renderSystem->saveWindowContents(rebakeShot);
			SELFCHECK(SelfcheckBootstrap::readImagePixel(rebakeShot, 664, 176,
				red, green, blue), "atlas re-upload probe decodes");
			SELFCHECK(blue > red + 0.4f && blue > green + 0.4f,
				"2D pattern: a re-uploaded atlas renders its NEWEST contents");
			dynamicLayer->clear();
			renderSystem->destroyTexture2D(atlasName);
		}
		// dynamic show/hide: unhide the magenta layer and re-verify. The shown
		// batch settles over a few frames on Vulkan (@see settleUntilPixel).
		hiddenLayer->setVisible(true);
		const std::string shownShot = outDir + "/selfcheck_drawlayer2d_shown.png";
		SELFCHECK(settleUntilPixel(renderSystem, shownShot, 750, 70,
			[](float r, float g, float b)
			{ return r > 0.5f && b > 0.5f && g < r - 0.3f; }, 6),
			"2D pattern: show() brings the layer back (magenta)");
		// the RTT must NOT contain the 2D layers (window-only contract):
		// the cyan bait quad above sits exactly in the RTT camera's world
		// frustum - the probe (below the sprite, beside the bait's world
		// spot) must stay the RTT's black background
		const std::string rttLeakShot = outDir + "/selfcheck_rtt_2d.png";
		renderTexture->writeContentsToFile(rttLeakShot);
		SELFCHECK(SelfcheckBootstrap::readImagePixel(rttLeakShot, 300, 150,
			red, green, blue), "RTT leak probe decodes");
		SELFCHECK(red + green + blue < 0.2f,
			"2D layers never leak into offscreen render textures");
		// resubmission: clear + resubmit keeps rendering (dirty-path smoke)
		bottomLayer->clear();
		bottomLayer->addTriangles("", redQuad.data(), redQuad.size());
		SELFCHECK(renderFrames(renderSystem, 2), "frames render after clear+resubmit");
		// RAII: dropping the handles removes the layers. A removed batch's last
		// draws stay in flight for a few frames on Vulkan, so settle on the red
		// quad clearing rather than pin a frame count (@see settleUntilPixel).
		topLayer.reset();
		bottomLayer.reset();
		dynamicLayer.reset();
		hiddenLayer.reset();
		const std::string removedShot = outDir + "/selfcheck_drawlayer2d_removed.png";
		SELFCHECK(settleUntilPixel(renderSystem, removedShot, 170, 60,
			[](float r, float g, float b)
			{ return !(r > g + 0.3f && r > b + 0.3f); }, 6),
			"2D pattern: dropped layers stop rendering");
	}

	//--- offscreen 2D composition (RenderTexture::createLayer) -------------
	// the per-target generalization of the DrawLayer2D contract: a whole 2D
	// layer composited INTO an offscreen RenderTexture at the target's own
	// pixel size (the editor GUI Preview stage). Absolute assertions within
	// this flavor; the case is Ogre-Next only (classic reports no offscreen
	// owned layers and the editor disables the GUI Preview tab there), so it is
	// skipped on a backend without offscreen 2D and never joins the
	// cross-flavor parity comparison.
	if(RenderSystem::get()->supports(RenderCaps::OffscreenOwnedLayers))
	{
		auto makeQuad = [](Real left, Real top, Real right, Real bottom,
			Color const & colour) -> std::vector<DrawLayer2D::Vertex2D>
		{
			std::vector<DrawLayer2D::Vertex2D> quad;
			quad.push_back(DrawLayer2D::Vertex2D(left, top, 0, 0, colour));
			quad.push_back(DrawLayer2D::Vertex2D(right, top, 1, 0, colour));
			quad.push_back(DrawLayer2D::Vertex2D(right, bottom, 1, 1, colour));
			quad.push_back(DrawLayer2D::Vertex2D(left, top, 0, 0, colour));
			quad.push_back(DrawLayer2D::Vertex2D(right, bottom, 1, 1, colour));
			quad.push_back(DrawLayer2D::Vertex2D(left, bottom, 0, 1, colour));
			return quad;
		};
		// a UI-only preview surface: no 3D camera, an opaque background, and
		// its own 2D layers (200x160 - a made-up "device" size). The same gui
		// stack renders into one of these in the editor.
		optr<RenderTexture> previewSurface =
			renderSystem->createRenderTexture("selfcheck.uisurface", 200, 160);
		SELFCHECK(previewSurface != NULL, "createRenderTexture (UI surface) works");
		previewSurface->setBackgroundColour(Color(0, 0, 0, 1));
		optr<DrawLayer2D> targetLayer = previewSurface->createLayer(0);
		SELFCHECK(targetLayer != NULL,
			"RenderTexture::createLayer returns a layer on this flavor");

		// a fresh WINDOW layer at the SAME pixel coords but a different colour:
		// isolation must keep the two surfaces from bleeding into each other
		optr<DrawLayer2D> windowLayer = renderSystem->createDrawLayer2D(5);
		std::vector<DrawLayer2D::Vertex2D> windowRed =
			makeQuad(20, 20, 120, 120, Color(1, 0, 0, 1));
		windowLayer->addTriangles("", windowRed.data(), windowRed.size());

		// the target's own content: a green field, a half-transparent white
		// over it (alpha), and a textured batch (resource-system binding)
		std::vector<DrawLayer2D::Vertex2D> targetGreen =
			makeQuad(20, 20, 120, 120, Color(0, 1, 0, 1));
		targetLayer->addTriangles("", targetGreen.data(), targetGreen.size());
		std::vector<DrawLayer2D::Vertex2D> targetBlend =
			makeQuad(20, 20, 70, 70, Color(1, 1, 1, 0.5f));
		targetLayer->addTriangles("", targetBlend.data(), targetBlend.size());
		std::vector<DrawLayer2D::Vertex2D> targetTex =
			makeQuad(130, 20, 190, 80, Color(1, 1, 1, 1));
		targetLayer->addTriangles("platform.png",
			targetTex.data(), targetTex.size());

		SELFCHECK(renderFrames(renderSystem, 3),
			"frames render with an offscreen 2D layer");
		const std::string surfaceShot = outDir + "/selfcheck_ui_surface.png";
		previewSurface->writeContentsToFile(surfaceShot);
		const std::string windowShot = outDir + "/selfcheck_ui_window.png";
		renderSystem->saveWindowContents(windowShot);

		float red = 0, green = 0, blue = 0;
		// the target composited its own green field
		SELFCHECK(SelfcheckBootstrap::readImagePixel(surfaceShot, 90, 100,
			red, green, blue), "UI surface green probe decodes");
		SELFCHECK(green > red + 0.3f && green > blue + 0.3f,
			"offscreen 2D: the target's own layer composited into it");
		// isolation A: the WINDOW's red layer did NOT bleed into the target
		SELFCHECK(SelfcheckBootstrap::readImagePixel(surfaceShot, 40, 100,
			red, green, blue), "UI surface isolation probe decodes");
		SELFCHECK(!(red > green + 0.3f),
			"offscreen 2D: window 2D layers never leak into the target");
		// alpha blending inside the target (white over green -> pale)
		SELFCHECK(SelfcheckBootstrap::readImagePixel(surfaceShot, 40, 40,
			red, green, blue), "UI surface alpha probe decodes");
		SELFCHECK(red > 0.3f && green > 0.6f && blue > 0.3f,
			"offscreen 2D: alpha blends inside the target (white over green)");
		// the target background stays its own clear colour where nothing drew
		SELFCHECK(SelfcheckBootstrap::readImagePixel(surfaceShot, 180, 150,
			red, green, blue), "UI surface background probe decodes");
		SELFCHECK(red + green + blue < 0.2f,
			"offscreen 2D: the target background is its clear colour");
		// the textured batch bound through the resource system
		{
			bool texturedRendered = false;
			const unsigned int probes[4][2] =
				{ {150, 40}, {170, 50}, {140, 60}, {180, 30} };
			for(int each = 0; each < 4; ++each)
			{
				if(!SelfcheckBootstrap::readImagePixel(surfaceShot,
					probes[each][0], probes[each][1], red, green, blue))
				{
					continue;
				}
				if(red + green + blue > 0.2f)
				{
					texturedRendered = true;
					break;
				}
			}
			SELFCHECK(texturedRendered,
				"offscreen 2D: a textured batch binds inside the target");
		}
		// isolation B: the window still shows its OWN red layer, not the
		// target's green (same pixel coords, different surface)
		SELFCHECK(SelfcheckBootstrap::readImagePixel(windowShot, 70, 70,
			red, green, blue), "window isolation probe decodes");
		SELFCHECK(red > green + 0.3f && red > blue + 0.3f,
			"offscreen 2D: the target's layer never leaks onto the window");

		// RAII: dropping the target layer then the target keeps frames rendering
		targetLayer.reset();
		SELFCHECK(renderFrames(renderSystem, 2),
			"frames render after the offscreen layer was dropped");
		previewSurface.reset();
		windowLayer.reset();
		SELFCHECK(renderFrames(renderSystem, 2),
			"frames render after the preview surface was dropped (RAII)");
	}

	//--- visibility affects rendering --------------------------------------
	const size_t trianglesBefore = renderSystem->getFrameStats().triangleCount;
	mesh->setVisible(false);
	SELFCHECK(renderFrames(renderSystem, 2), "frames render with hidden mesh");
	const size_t trianglesAfter = renderSystem->getFrameStats().triangleCount;
	SELFCHECK(trianglesAfter < trianglesBefore,
		"hiding the mesh drops the window triangle count");
	mesh->setVisible(true);
	meshNode->setVisible(false, true);
	meshNode->setVisible(true, true);

	//--- wireframe toggle (documented no-op on Filament) --------------------
	camera->setWireframe(true);
	SELFCHECK(renderFrames(renderSystem, 2), "frames render in wireframe");
	camera->setWireframe(false);

	//--- a lit scene renders differently from an unlit one -----------------
	// toggling the scene light (the two-colour hemisphere ambient this package
	// adds to the facade) between bright and black takes a lit mesh from visible
	// to BLACK - proving lighting reaches the shading, on BOTH flavors. Runs LAST
	// (after every parity-compared screenshot is written) so changing the light /
	// window background here disturbs nothing the WYSIWYG gate compares, and it
	// writes to its own files (selfcheck_light_*.png, not in the parity set).
	// (The dynamic point/spot/directional RenderLight the LightComponent drives
	// is exercised above and by the component tests; these demo meshes carry no
	// normals suited to per-light PBS shading, so the visible difference is
	// driven through the hemisphere ambient term.)
	{
		// a textured, LIT mesh (its imported material shades; with no light it
		// goes black). Placed far from the other content and rendered through the
		// WINDOW camera - the proven scene-lighting path (an offscreen "basic"
		// workspace carries no lighting)
		optr<RenderNode> litMeshNode = world->createNode("selfcheck.litMeshNode");
		litMeshNode->setPosition(Vec3(200, 0, 0));
		optr<MeshInstance> litMesh =
			world->createMeshInstance("jumper_platform.glb");
		SELFCHECK(litMesh != NULL, "the lit probe mesh loads");
		litMesh->attachTo(litMeshNode);
		litMesh->setCastShadows(false);
		AABB litBounds = litMeshNode->getWorldBounds();
		SELFCHECK(litBounds.isFinite() && !litBounds.isNull(),
			"the lit probe mesh has finite world bounds");
		const Vec3 litCentre = litBounds.getCenter();
		// the same relative camera geometry the main scene frames its lit
		// platform with, on a black window background so an unlit surface reads
		// black
		renderSystem->setWindowBackgroundColour(Color(0, 0, 0, 1));
		camera->setPerspective(Degree(55), Real(0.1), Real(1000));
		cameraNode->setPosition(litCentre + Vec3(0, 4.5f, 8));
		cameraNode->lookAt(litCentre, RenderNode::TS_WORLD);
		// a live dynamic RenderLight in the scene (the LightComponent's facade;
		// created + typed + placed through the facade in a real scene)
		optr<RenderLight> probeLight = world->createLight();
		optr<RenderNode> probeLightNode =
			world->createNode("selfcheck.probeLightNode");
		probeLightNode->setPosition(litCentre + Vec3(3, 8.5f, 5));
		probeLight->attachTo(probeLightNode);
		probeLight->setType(RenderLight::LT_POINT);
		probeLight->setDiffuseColour(Color(1.0f, 1.0f, 1.0f));
		probeLight->setRange(50);
		probeLight->setCastShadows(false);

		// the BRIGHTEST luminance over the WHOLE window (one decode via the
		// bootstrap helper - readImagePixel would re-decode per pixel): the lit
		// surface is the brightest thing against the black background; unlit, the
		// frame stays near-black
		unsigned int probeW = 0, probeH = 0;
		renderSystem->getWindowSize(probeW, probeH);
		SELFCHECK(probeW > 0 && probeH > 0, "the window reports a probe size");

		// LIT: a bright two-colour hemisphere ambient (the facade growth). Its
		// upper/lower colours round-trip through the facade getters.
		world->setAmbientHemisphere(Color(0.9f, 0.9f, 0.9f),
			Color(0.6f, 0.6f, 0.6f));
		SELFCHECK(nearlyEqual(world->getAmbientHemisphereUpper().r, Real(0.9)) &&
			nearlyEqual(world->getAmbientHemisphereLower().g, Real(0.6)),
			"hemisphere ambient round-trips through the facade");
		SELFCHECK(renderFrames(renderSystem, 3), "frames render with the light on");
		const std::string litOnShot = outDir + "/selfcheck_light_on.png";
		renderSystem->saveWindowContents(litOnShot);
		const float brightnessOn = SelfcheckBootstrap::imageMaxBrightness(litOnShot);

		// UNLIT: every scene light off - black ambient (the flat equal-hemisphere
		// case) AND the dynamic light dark (classic shades the platform from the
		// point light where next does not, so it must go off too for a
		// flavor-robust dark frame)
		world->setAmbientLight(Color(0.0f, 0.0f, 0.0f));
		SELFCHECK(nearlyEqual(world->getAmbientHemisphereUpper().g, Real(0)),
			"flat setAmbientLight is the equal-hemisphere case");
		probeLight->setDiffuseColour(Color(0.0f, 0.0f, 0.0f));
		probeLight->setSpecularColour(Color(0.0f, 0.0f, 0.0f));
		SELFCHECK(renderFrames(renderSystem, 3), "frames render with the light off");
		const std::string litOffShot = outDir + "/selfcheck_light_off.png";
		renderSystem->saveWindowContents(litOffShot);
		const float brightnessOff = SelfcheckBootstrap::imageMaxBrightness(litOffShot);

		std::printf("render_facade_selfcheck: lit probe - on %.3f, off %.3f\n",
			brightnessOn, brightnessOff);
		SELFCHECK(brightnessOff < 0.15f,
			"the surface is near-black with no scene light");
		SELFCHECK(brightnessOn > brightnessOff + 0.2f,
			"a lit scene renders visibly brighter than the unlit one");

		// RAII teardown of the probe content
		probeLight.reset();
		litMesh.reset();
		SELFCHECK(renderFrames(renderSystem, 2),
			"frames render after the lit probe was dropped (RAII teardown)");
	}

	//--- dynamic shadows (quality knob; a shadowless flavor answers honestly)
	// Runs after every parity-compared capture (like the lit-scene probe) and
	// writes only its own files. Two seams are exercised where the flavor
	// renders shadows: the per-light cast flag (RenderLight::setCastShadows -
	// what LightComponent.castsShadows drives) and the world quality knob
	// (RenderWorld::setShadowQuality - the `r.shadowQuality` cvar's target).
	if(RenderSystem::get()->supports(RenderCaps::DynamicShadows))
	{
		// a flat caster plate hovering over a wide slab, lit by a nearly
		// vertical shadow-casting directional sun on black ambient: the slab
		// point under the plate reads dark, open slab reads lit
		world->setShadowQuality(ShadowPreset::SQ_MEDIUM);
		SELFCHECK(world->getShadowQuality() == ShadowPreset::SQ_MEDIUM,
			"the shadow quality knob round-trips");
		world->setAmbientLight(Color(0, 0, 0));
		renderSystem->setWindowBackgroundColour(Color(0, 0, 0, 1));
		optr<RenderNode> slabNode = world->createNode("selfcheck.shadowSlab");
		slabNode->setPosition(Vec3(400, 0, 0));
		slabNode->setScale(Vec3(16, 1, 16));
		optr<MeshInstance> slab =
			world->createMeshInstance("jumper_platform.glb");
		SELFCHECK(slab != NULL, "the shadow slab mesh loads");
		slab->attachTo(slabNode);
		// the caster plate hovers OFF-centre and the open reference point
		// mirrors it about the slab centre: the imported cube's smoothed
		// corner normals shade a face as a radial pool peaking at its centre,
		// so only symmetric probe points share an unshadowed baseline
		optr<RenderNode> plateNode = world->createNode("selfcheck.shadowPlate");
		plateNode->setPosition(Vec3(402, 3, 0));
		plateNode->setScale(Vec3(4, 0.5f, 4));
		optr<MeshInstance> plate =
			world->createMeshInstance("jumper_platform.glb");
		SELFCHECK(plate != NULL, "the shadow caster mesh loads");
		plate->attachTo(plateNode);
		// the sun: direction comes from the node orientation (facade rule)
		optr<RenderLight> sun = world->createLight();
		optr<RenderNode> sunNode = world->createNode("selfcheck.sunNode");
		sunNode->setDirection(Vec3(0.05f, -1.0f, 0.05f), RenderNode::TS_WORLD);
		sun->attachTo(sunNode);
		sun->setType(RenderLight::LT_DIRECTIONAL);
		// PBS is energy-conserving (the direct term divides by pi) and the
		// slab albedo is a dark checker - an HDR sun colour keeps the lit
		// reading comfortably above the probe threshold. Specular stays
		// BLACK: a broad specular lobe would put a view-dependent hotspot on
		// the slab and corrupt the flat diffuse reading the probes compare.
		sun->setDiffuseColour(Color(4.0f, 4.0f, 4.0f));
		sun->setSpecularColour(Color(0.0f, 0.0f, 0.0f));
		sun->setCastShadows(true);	// what LightComponent.castsShadows drives
		// camera above the slab, both probe points in clear view. The NEAR
		// plane matters here: PSSM derives its split scheme from the camera
		// near/far, and a 0.1 near squeezes the crisp first cascade into the
		// first few units - a sane near plane puts the probe area into it
		camera->setPerspective(Degree(55), Real(4.0), Real(200));
		cameraNode->setPosition(Vec3(400, 9, 11));
		cameraNode->lookAt(Vec3(400, 0.5f, 0), RenderNode::TS_WORLD);
		SELFCHECK(renderFrames(renderSystem, 5),
			"frames render with a shadow-casting sun");

		// probe pixels via projection (top of the slab is y=+0.5): one point
		// under the plate's footprint, one on open slab 4 units clear of it
		const Vec3 shadowedPoint(402, 0.5f, 0);
		const Vec3 openPoint(398, 0.5f, 0);
		unsigned int shadowW = 0, shadowH = 0;
		renderSystem->getWindowSize(shadowW, shadowH);
		// the slab texture is a checkerboard (one check per world unit) - a
		// single pixel could land on a dark or a bright cell. Probe a small
		// 3x3 world-point spread around the target and take the BRIGHTEST
		// reading, so shadowed and open compare their matching bright cells.
		auto probeLuminance = [&](std::string const & imageFile,
			Vec3 const & worldPoint, float & outLuminance) -> bool
		{
			bool decoded = false;
			float brightest = 0.0f;
			for(int dx = -1; dx <= 1; ++dx)
			{
				for(int dz = -1; dz <= 1; ++dz)
				{
					const Vec3 samplePoint = worldPoint +
						Vec3(0.6f * dx, 0.0f, 0.6f * dz);
					Real nx = 0, ny = 0;
					if(!camera->projectPoint(samplePoint, nx, ny))
					{
						continue;
					}
					float red = 0, green = 0, blue = 0;
					if(!SelfcheckBootstrap::readImagePixel(imageFile,
						static_cast<unsigned int>(nx * (shadowW - 1)),
						static_cast<unsigned int>(ny * (shadowH - 1)),
						red, green, blue))
					{
						continue;
					}
					decoded = true;
					brightest = std::max(brightest, (red + green + blue) / 3.0f);
				}
			}
			outLuminance = brightest;
			return decoded;
		};

		const std::string shadowOnShot = outDir + "/selfcheck_shadow_on.png";
		renderSystem->saveWindowContents(shadowOnShot);
		float shadowedLum = 0, openLum = 0;
		SELFCHECK(probeLuminance(shadowOnShot, shadowedPoint, shadowedLum),
			"the shadowed probe point projects and decodes");
		SELFCHECK(probeLuminance(shadowOnShot, openPoint, openLum),
			"the open probe point projects and decodes");
		std::printf("render_facade_selfcheck: shadow probe - shadowed %.3f, "
			"open %.3f\n", shadowedLum, openLum);
		SELFCHECK(openLum > 0.15f,
			"the sunlit slab reads visibly lit (directional light shades)");
		SELFCHECK(openLum > shadowedLum + 0.15f,
			"the region under the caster reads darker (the shadow rendered)");

		// seam 1: the light stops casting - the shadow disappears (this is
		// the exact toggle LightComponent.castsShadows performs)
		sun->setCastShadows(false);
		SELFCHECK(renderFrames(renderSystem, 3),
			"frames render after the caster flag went off");
		const std::string casterOffShot =
			outDir + "/selfcheck_shadow_caster_off.png";
		renderSystem->saveWindowContents(casterOffShot);
		float shadowedNoCaster = 0, openNoCaster = 0;
		SELFCHECK(probeLuminance(casterOffShot, shadowedPoint, shadowedNoCaster)
			&& probeLuminance(casterOffShot, openPoint, openNoCaster),
			"the caster-off probes decode");
		SELFCHECK(std::abs(shadowedNoCaster - openNoCaster) < 0.1f,
			"castShadows(false) restores the unshadowed baseline");

		// seam 2: the caster is back on but the world knob is OFF - still no
		// shadow (the r.shadowQuality=off path)
		sun->setCastShadows(true);
		world->setShadowQuality(ShadowPreset::SQ_OFF);
		SELFCHECK(world->getShadowQuality() == ShadowPreset::SQ_OFF,
			"the shadow quality knob turns off");
		SELFCHECK(renderFrames(renderSystem, 3),
			"frames render after the quality knob went off");
		const std::string knobOffShot = outDir + "/selfcheck_shadow_off.png";
		renderSystem->saveWindowContents(knobOffShot);
		float shadowedKnobOff = 0, openKnobOff = 0;
		SELFCHECK(probeLuminance(knobOffShot, shadowedPoint, shadowedKnobOff)
			&& probeLuminance(knobOffShot, openPoint, openKnobOff),
			"the knob-off probes decode");
		SELFCHECK(std::abs(shadowedKnobOff - openKnobOff) < 0.1f,
			"quality off matches the unshadowed baseline");

		// restore the default and tear the probe content down
		world->setShadowQuality(ShadowPreset::SQ_MEDIUM);
		sun.reset();
		slab.reset();
		plate.reset();
		SELFCHECK(renderFrames(renderSystem, 2),
			"frames render after the shadow probe was dropped (RAII teardown)");
	}
	else
	{
		// the honest path of a shadowless flavor: the knob is ACCEPTED
		// (round-trips, so scenes tuned on a shadow-capable flavor keep their
		// setting), a caster flag is harmless, frames keep rendering, and the
		// backend says so in EXACTLY ONE log line
		world->setShadowQuality(ShadowPreset::SQ_HIGH);
		SELFCHECK(world->getShadowQuality() == ShadowPreset::SQ_HIGH,
			"the shadow quality knob round-trips on a shadowless flavor");
		optr<RenderLight> hopefulSun = world->createLight();
		optr<RenderNode> hopefulNode = world->createNode("selfcheck.hopeful");
		hopefulSun->attachTo(hopefulNode);
		hopefulSun->setType(RenderLight::LT_DIRECTIONAL);
		hopefulSun->setCastShadows(true);
		SELFCHECK(renderFrames(renderSystem, 3),
			"frames render after a shadow request on a shadowless flavor");
		world->setShadowQuality(ShadowPreset::SQ_MEDIUM);	// a second knob move
		// exactly one honest log line for BOTH knob moves (the backend log
		// file is the boot's log - see main below)
		{
			std::ifstream logFile(outDir + "/render_facade_selfcheck.log");
			SELFCHECK(logFile.good(), "the backend log file opens");
			std::stringstream buffered;
			buffered << logFile.rdbuf();
			const std::string logText = buffered.str();
			const std::string marker =
				"dynamic shadows are not supported on this render backend";
			std::size_t occurrences = 0;
			for(std::size_t at = logText.find(marker); at != std::string::npos;
				at = logText.find(marker, at + marker.size()))
			{
				++occurrences;
			}
			SELFCHECK(occurrences == 1,
				"the unsupported-shadows log line appears exactly once");
		}
		hopefulSun.reset();
		SELFCHECK(renderFrames(renderSystem, 2),
			"frames render after the shadow request was dropped");
	}

	//--- RAII teardown of content while frames keep rendering ---------------
	sprite.reset();
	platform.reset();
	vectorShape.reset();
	gradientMesh.reset();
	SELFCHECK(renderFrames(renderSystem, 2),
		"frames render after content handles were dropped (RAII teardown)");

	//--- sky / fog atmosphere ----------------------------------------------
	// Two seams on BOTH flavors: enabling the atmosphere changes the
	// background pixels (a real sky dome where the SkyDome capability is
	// present, the flat sky clear colour otherwise) and thickening the fog changes a distant
	// object's reading. Runs after the parity-compared captures on an emptied
	// scene, writing only its own files.
	{
		unsigned int atmoW = 0, atmoH = 0;
		renderSystem->getWindowSize(atmoW, atmoH);
		// average a small pixel block's luminance from a saved shot (robust to
		// a single pixel landing on a texture check / dither)
		auto blockLuminance = [&](std::string const & imageFile,
			unsigned int px, unsigned int py, float & outLuminance) -> bool
		{
			float sum = 0.0f;
			int samples = 0;
			for(int dx = -2; dx <= 2; ++dx)
			{
				for(int dy = -2; dy <= 2; ++dy)
				{
					const int sx = static_cast<int>(px) + dx * 2;
					const int sy = static_cast<int>(py) + dy * 2;
					if(sx < 0 || sy < 0 || sx >= static_cast<int>(atmoW) ||
						sy >= static_cast<int>(atmoH))
					{
						continue;
					}
					float red = 0, green = 0, blue = 0;
					if(!SelfcheckBootstrap::readImagePixel(imageFile,
						static_cast<unsigned int>(sx),
						static_cast<unsigned int>(sy), red, green, blue))
					{
						continue;
					}
					sum += (red + green + blue) / 3.0f;
					++samples;
				}
			}
			if(samples == 0)
			{
				return false;
			}
			outLuminance = sum / static_cast<float>(samples);
			return true;
		};

		// point the camera at empty space so the upper frame is pure sky
		camera->setPerspective(Degree(55), Real(1.0), Real(500));
		cameraNode->setPosition(Vec3(0, 2, 0));
		cameraNode->lookAt(Vec3(0, 4, -50), RenderNode::TS_WORLD);
		const unsigned int skyX = atmoW / 2u;
		const unsigned int skyY = static_cast<unsigned int>(atmoH * 0.18f);

		// OFF baseline: a black sky, no dome, no fog
		AtmosphereDesc offDesc;	// disabled by default
		offDesc.skyRed = offDesc.skyGreen = offDesc.skyBlue = 0.0f;
		world->setAtmosphere(offDesc);
		SELFCHECK(!world->getAtmosphere().enabled,
			"the atmosphere desc round-trips (disabled)");
		SELFCHECK(renderFrames(renderSystem, 3),
			"frames render with the atmosphere off");
		const std::string skyOffShot = outDir + "/selfcheck_atmosphere_off.png";
		renderSystem->saveWindowContents(skyOffShot);
		float skyOffLum = 0;
		SELFCHECK(blockLuminance(skyOffShot, skyX, skyY, skyOffLum),
			"the atmosphere-off sky probe decodes");

		// ON: a bright daytime sky (a dome on next, the flat blue clear on classic)
		AtmosphereDesc onDesc =
			AtmospherePreset::forSky(AtmospherePreset::SKY_DAY);
		world->setAtmosphere(onDesc);
		SELFCHECK(world->getAtmosphere().enabled,
			"the atmosphere desc round-trips (enabled)");
		SELFCHECK(renderFrames(renderSystem, 3),
			"frames render with the atmosphere on");
		const std::string skyOnShot = outDir + "/selfcheck_atmosphere_on.png";
		renderSystem->saveWindowContents(skyOnShot);
		float skyOnLum = 0;
		SELFCHECK(blockLuminance(skyOnShot, skyX, skyY, skyOnLum),
			"the atmosphere-on sky probe decodes");
		std::printf("render_facade_selfcheck: atmosphere sky - off %.3f, "
			"on %.3f\n", skyOffLum, skyOnLum);
		SELFCHECK(skyOnLum > skyOffLum + 0.1f,
			"enabling the atmosphere brightens the sky background");

		// capability honesty: a shadowless-of-sky flavor says so ONCE
		if(RenderSystem::get()->supports(RenderCaps::SkyDome))
		{
			SELFCHECK(true,
				"this flavor renders an atmospheric sky dome (RenderCaps::SkyDome)");
		}
		else
		{
			std::ifstream logFile(outDir + "/render_facade_selfcheck.log");
			SELFCHECK(logFile.good(), "the backend log file opens (sky)");
			std::stringstream buffered;
			buffered << logFile.rdbuf();
			const std::string logText = buffered.str();
			const std::string marker =
				"atmospheric sky dome is not supported";
			std::size_t occurrences = 0;
			for(std::size_t at = logText.find(marker); at != std::string::npos;
				at = logText.find(marker, at + marker.size()))
			{
				++occurrences;
			}
			SELFCHECK(occurrences == 1,
				"the unsupported-sky-dome log line appears exactly once");
		}

		// NOT-CLIPPED exposure: the atmosphere drives its linked sun's power for
		// an HDR pipeline, but THIS pipeline has no tonemapper - the native sun
		// power (Math::PI) clips a mid-albedo surface to pure white. A mid-grey,
		// colour-only PBS surface (the terrain case: a horizontal slab under a
		// near-vertical daytime sun) must render NEITHER white (clipped) NOR
		// black (unlit) - the regression guard for the un-tonemapped sun-drive
		// scale (AtmosphereDesc::sunPower). Gated on the sun-EXPOSURE linkage
		// (next only), NOT the SkyDome capability (both flavors render a dome now):
		// classic's dome reads the sun direction but never drives the light's
		// power, so there is no exposure to clip.
		if(RenderSystem::get()->supports(RenderCaps::SunExposureLinkage))
		{
			// a mid-grey, colour-only PBS material (no maps, so the reading is
			// the pure albedo * lighting - the exposure the sun drive controls)
			RenderMaterialDesc greyDesc;
			greyDesc.albedo = Color(0.5f, 0.5f, 0.5f, 1.0f);	// mid-grey
			greyDesc.metalness = 0.0f;
			greyDesc.roughness = 1.0f;
			SELFCHECK(renderSystem->createMaterial("selfcheck.midGrey", greyDesc),
				"the mid-grey exposure probe material builds");
			// a wide horizontal slab (the terrain analogy: a flat lit surface
			// under a near-vertical daytime sun) taking the mid-grey material -
			// jumper_platform's top is a flat surface the shadow probe above
			// proved the directional sun shades. The atmosphere OVERRIDES the
			// sun's colour/power via its linkage, so the reading is entirely the
			// atmosphere's exposure drive.
			optr<RenderNode> greyNode =
				world->createNode("selfcheck.midGreyNode");
			greyNode->setPosition(Vec3(600, 0, 0));
			greyNode->setScale(Vec3(16, 1, 16));
			optr<MeshInstance> greySlab =
				world->createMeshInstance("jumper_platform.glb");
			SELFCHECK(greySlab != NULL, "the exposure probe mesh loads");
			greySlab->attachTo(greyNode);
			greySlab->setCastShadows(false);
			SELFCHECK(greySlab->setMaterial("selfcheck.midGrey"),
				"the exposure probe mesh takes the mid-grey material");
			// the sun: a near-vertical daytime directional light (the FIRST
			// directional light, so the atmosphere links + drives it). The
			// node's direction is the LIGHT-TRAVEL direction (down onto the
			// slab top for a noon sun) - toward-the-sun is its negation,
			// which the atmosphere derives itself (same rule as the classic
			// dome's sun glow).
			optr<RenderNode> exposureSunNode =
				world->createNode("selfcheck.exposureSun");
			exposureSunNode->setDirection(Vec3(-0.15f, -1.0f, -0.1f),
				RenderNode::TS_WORLD);
			optr<RenderLight> exposureSun = world->createLight();
			exposureSun->attachTo(exposureSunNode);
			exposureSun->setType(RenderLight::LT_DIRECTIONAL);
			exposureSun->setSpecularColour(Color(0, 0, 0));	// flat diffuse read
			world->setAtmosphere(
				AtmospherePreset::forSky(AtmospherePreset::SKY_DAY));
			// camera above the slab, looking down at the sunlit top
			camera->setPerspective(Degree(55), Real(1.0), Real(500));
			cameraNode->setPosition(Vec3(600, 9, 6));
			cameraNode->lookAt(Vec3(600, 0.5f, 0), RenderNode::TS_WORLD);
			SELFCHECK(renderFrames(renderSystem, 4),
				"frames render with the sunlit exposure slab");
			const std::string exposureShot =
				outDir + "/selfcheck_atmosphere_exposure.png";
			renderSystem->saveWindowContents(exposureShot);
			// scan a spread of slab-top world points and take the BRIGHTEST
			// reading: the sun-facing part of the slab is the surface that would
			// clip to white at the native sun power (the slab's normal-mapped
			// checker + the angled sun leave some cells facing away, so a single
			// centre probe is unreliable - the max over the top is the exposure
			// the fix must keep below clipping)
			float slabLum = 0;
			bool slabDecoded = false;
			// a 3x3 spread is enough to catch a sun-facing cell, and each
			// probe re-decodes the whole screenshot - a wider scan blows the
			// test timeout on a large window
			for(int gx = -1; gx <= 1; ++gx)
			{
				for(int gz = -1; gz <= 1; ++gz)
				{
					const Vec3 slabPoint(600.0f + gx * 5.0f, 0.5f, gz * 5.0f);
					Real nx = 0, ny = 0;
					if(!camera->projectPoint(slabPoint, nx, ny))
					{
						continue;
					}
					float blockLum = 0;
					if(!blockLuminance(exposureShot,
						static_cast<unsigned int>(nx * (atmoW - 1)),
						static_cast<unsigned int>(ny * (atmoH - 1)), blockLum))
					{
						continue;
					}
					slabDecoded = true;
					slabLum = std::max(slabLum, blockLum);
				}
			}
			SELFCHECK(slabDecoded, "the exposure probe decodes");
			std::printf("render_facade_selfcheck: exposure probe - "
				"sunlit slab (brightest) %.3f (want > 0.05 and < 0.95)\n", slabLum);
			SELFCHECK(slabLum > 0.05f,
				"the slab is lit by the atmosphere sun (not black)");
			SELFCHECK(slabLum < 0.95f,
				"the sunlit slab does NOT clip to white (the sun drive is capped)");

			// RESTORE-EXACTLY: while enabled the atmosphere OWNS the linked
			// sun's colour (the shared day/night curve drives it near-white,
			// on BOTH flavors now); disabling must hand the AUTHORED colour
			// back untouched - the recover-then-reapply rule. A pure-red
			// authored sun proves the round-trip in pixels: driven, the slab
			// reads near-neutral; restored, it reads strongly red again.
			auto slabChannels = [&](std::string const & imageFile,
				float & outRed, float & outGreen) -> bool
			{
				float bestRed = 0, bestGreen = 0;
				bool decoded = false;
				for(int gx = -1; gx <= 1; ++gx)
				{
					for(int gz = -1; gz <= 1; ++gz)
					{
						const Vec3 slabPoint(600.0f + gx * 5.0f, 0.5f,
							gz * 5.0f);
						Real nx = 0, ny = 0;
						if(!camera->projectPoint(slabPoint, nx, ny))
						{
							continue;
						}
						float red = 0, green = 0, blue = 0;
						if(!SelfcheckBootstrap::readImagePixel(imageFile,
							static_cast<unsigned int>(nx * (atmoW - 1)),
							static_cast<unsigned int>(ny * (atmoH - 1)),
							red, green, blue))
						{
							continue;
						}
						decoded = true;
						if(red + green > bestRed + bestGreen)
						{
							bestRed = red;
							bestGreen = green;
						}
					}
				}
				outRed = bestRed;
				outGreen = bestGreen;
				return decoded;
			};
			exposureSun->setDiffuseColour(Color(3.0f, 0.0f, 0.0f));
			// re-apply: the atmosphere re-takes the sun and stomps the red
			// with its own driven colour (authored values wait in the snapshot)
			world->setAtmosphere(
				AtmospherePreset::forSky(AtmospherePreset::SKY_DAY));
			SELFCHECK(renderFrames(renderSystem, 3),
				"frames render with the re-driven sun");
			const std::string drivenShot =
				outDir + "/selfcheck_atmosphere_driven.png";
			renderSystem->saveWindowContents(drivenShot);
			float drivenRed = 0, drivenGreen = 0;
			SELFCHECK(slabChannels(drivenShot, drivenRed, drivenGreen),
				"the driven-sun probe decodes");
			world->setAtmosphere(AtmosphereDesc());	// disable -> restore
			SELFCHECK(renderFrames(renderSystem, 3),
				"frames render after the atmosphere released the sun");
			const std::string restoredShot =
				outDir + "/selfcheck_atmosphere_restored.png";
			renderSystem->saveWindowContents(restoredShot);
			float restoredRed = 0, restoredGreen = 0;
			SELFCHECK(slabChannels(restoredShot, restoredRed, restoredGreen),
				"the restored-sun probe decodes");
			std::printf("render_facade_selfcheck: sun restore - driven r %.3f "
				"g %.3f, restored r %.3f g %.3f\n", drivenRed, drivenGreen,
				restoredRed, restoredGreen);
			// driven: the curve's near-white sun keeps green close to red;
			// restored: the authored red sun leaves green far behind
			SELFCHECK(drivenGreen > drivenRed * 0.5f,
				"the atmosphere-driven sun overrides the authored red");
			SELFCHECK(restoredRed > 0.1f,
				"the restored authored sun lights the slab");
			SELFCHECK(restoredGreen < restoredRed * 0.5f,
				"disabling the atmosphere restores the authored sun colour");

			// tear the exposure probe content down (the fog section rebuilds its
			// own sun + object below)
			exposureSun.reset();
			greySlab.reset();
			SELFCHECK(renderFrames(renderSystem, 2),
				"frames render after the exposure probe was dropped");
		}

		// fog: a distant SUNLIT surface's reading shifts as fog thickens - the
		// exposure probe's recipe (mid-grey slab top under a near-vertical,
		// atmosphere-driven sun) at fog distance, so the clear reading is
		// bright and the fogged one converges to the ray's dark in-scatter
		// colour: a large, driver-robust delta (a dark object barely moves -
		// fog swaps its lighting for an equally dark colour). The atmosphere
		// stays ENABLED across both captures so only fogDensity varies.
		world->setAmbientLight(Color(0.2f, 0.2f, 0.2f));
		optr<RenderNode> fogSunNode = world->createNode("selfcheck.atmoSun");
		fogSunNode->setDirection(Vec3(0.15f, 1.0f, 0.1f), RenderNode::TS_WORLD);
		optr<RenderLight> fogSun = world->createLight();
		fogSun->attachTo(fogSunNode);
		fogSun->setType(RenderLight::LT_DIRECTIONAL);
		fogSun->setSpecularColour(Color(0.0f, 0.0f, 0.0f));
		const Vec3 fogPoint(0, 0, -90);
		optr<RenderNode> fogObjNode = world->createNode("selfcheck.fogObject");
		fogObjNode->setPosition(fogPoint);
		fogObjNode->setScale(Vec3(16, 1, 16));
		optr<MeshInstance> fogObj =
			world->createMeshInstance("jumper_platform.glb");
		SELFCHECK(fogObj != NULL, "the distant fog object loads");
		fogObj->attachTo(fogObjNode);
		fogObj->setCastShadows(false);
		// the fog leg's own mid-grey (the exposure probe's material only
		// exists where the sky dome does - this leg runs on BOTH flavors)
		RenderMaterialDesc fogGreyDesc;
		fogGreyDesc.albedo = Color(0.5f, 0.5f, 0.5f, 1.0f);
		fogGreyDesc.metalness = 0.0f;
		fogGreyDesc.roughness = 1.0f;
		SELFCHECK(renderSystem->createMaterial("selfcheck.fogGrey", fogGreyDesc),
			"the fog probe material builds");
		SELFCHECK(fogObj->setMaterial("selfcheck.fogGrey"),
			"the fog probe takes the mid-grey material");
		camera->setPerspective(Degree(55), Real(1.0), Real(500));
		cameraNode->setPosition(Vec3(0, 40, 0));
		cameraNode->lookAt(fogPoint, RenderNode::TS_WORLD);

		AtmosphereDesc clearDesc =
			AtmospherePreset::forSky(AtmospherePreset::SKY_DAY);
		clearDesc.fogDensity = 0.0f;
		world->setAtmosphere(clearDesc);
		SELFCHECK(renderFrames(renderSystem, 3),
			"frames render with the atmosphere, no fog");
		const std::string fogOffShot = outDir + "/selfcheck_fog_off.png";
		renderSystem->saveWindowContents(fogOffShot);

		AtmosphereDesc foggyDesc = clearDesc;
		foggyDesc.fogDensity = 1.0f;
		world->setAtmosphere(foggyDesc);
		SELFCHECK(renderFrames(renderSystem, 3),
			"frames render with heavy fog");
		const std::string fogOnShot = outDir + "/selfcheck_fog_on.png";
		renderSystem->saveWindowContents(fogOnShot);

		Real fogNdcX = 0, fogNdcY = 0;
		SELFCHECK(camera->projectPoint(fogPoint, fogNdcX, fogNdcY),
			"the distant fog object projects");
		const unsigned int fogObjX =
			static_cast<unsigned int>(fogNdcX * (atmoW - 1));
		const unsigned int fogObjY =
			static_cast<unsigned int>(fogNdcY * (atmoH - 1));
		float objClearLum = 0, objFoggyLum = 0;
		SELFCHECK(blockLuminance(fogOffShot, fogObjX, fogObjY, objClearLum) &&
			blockLuminance(fogOnShot, fogObjX, fogObjY, objFoggyLum),
			"the distant fog object probes decode");
		std::printf("render_facade_selfcheck: fog object - clear %.3f, "
			"foggy %.3f\n", objClearLum, objFoggyLum);
		// a relative test: fog measurably shifts the object's reading (its
		// absolute luminance is atmosphere-driven, so compare against it - a
		// >20%% shift is unambiguous fog, robust across flavors/lighting)
		const float fogDelta = std::abs(objFoggyLum - objClearLum);
		SELFCHECK(fogDelta > 0.2f * std::max(objClearLum, 0.03f),
			"fog changes the distant object's contrast");

		// restore the neutral state + tear the atmosphere probe content down
		world->setAtmosphere(AtmosphereDesc());	// disabled
		fogSun.reset();
		fogObj.reset();
		SELFCHECK(renderFrames(renderSystem, 2),
			"frames render after the atmosphere probe was dropped (RAII teardown)");
	}

	// --- capability register conformance (@see RenderCaps) -----------------
	// The X-macro enum (RenderCaps.h) and the live per-backend bitset must agree
	// with the committed per-backend snapshot the bootstrap exposes
	// (RenderCapsExpected*.inc). Every enum identity must be covered by the
	// snapshot, and its live supports() must equal the snapshot value; a boot
	// fill that drifts from the snapshot - or an enum identity the snapshot
	// forgot - fails HERE, on the flavor it diverges on (this leg runs per-flavor
	// with a live backend). The snapshot is also the doc matrix's column source,
	// so this same file keeps the docs honest too.
	{
		bool allCovered = true;
		bool allValuesMatch = true;
		bool allNamesRoundTrip = true;
		for(int i = 0; i < static_cast<int>(RenderCaps::Count); ++i)
		{
			const RenderCaps cap = static_cast<RenderCaps>(i);
			const char* capName = renderCapName(cap);

			// the name round-trips through parse (the Lua/MCP name path)
			if(parseRenderCap(capName) != cap)
			{
				std::fprintf(stderr, "render_facade_selfcheck: RenderCaps name "
					"'%s' does not round-trip through parseRenderCap\n", capName);
				allNamesRoundTrip = false;
			}

			bool known = false;
			const bool expected =
				SelfcheckBootstrap::expectedRenderCapSupport(cap, known);
			if(!known)
			{
				std::fprintf(stderr, "render_facade_selfcheck: RenderCaps '%s' is "
					"missing from this backend's expected snapshot\n", capName);
				allCovered = false;
				continue;
			}
			const bool live = renderSystem->supports(cap);
			std::printf("render_facade_selfcheck: cap %s - snapshot %d, live %d\n",
				capName, expected ? 1 : 0, live ? 1 : 0);
			if(expected != live)
			{
				allValuesMatch = false;
			}
		}
		SELFCHECK(allNamesRoundTrip,
			"every RenderCaps name round-trips through parseRenderCap");
		SELFCHECK(allCovered,
			"the backend snapshot covers every RenderCaps identity");
		SELFCHECK(allValuesMatch,
			"the live backend's supports() matches its committed snapshot");

		// the unknown-name path the Lua engine:supports / MCP name lookup relies
		// on: a name that is not a capability is an honest miss, never a hit
		SELFCHECK(parseRenderCap("definitelyNotACapability") == RenderCaps::Count,
			"parseRenderCap returns Count for an unknown name");
	}

	std::printf("render_facade_selfcheck: all checks passed\n");
	return 0;
}

int main(int, char**)
{
	// screenshot output directory: ctest points this at the build tree;
	// interactive runs fall back to the system temp directory
	std::string outDir;
	if(const char* outEnv = std::getenv("ORKIGE_SELFCHECK_OUT"))
	{
		outDir = outEnv;
	}
	else
	{
		outDir = (std::filesystem::temp_directory_path() /
			"render_facade_selfcheck").string();
	}
	std::error_code errorCode;
	std::filesystem::create_directories(outDir, errorCode);

	RenderSystem* renderSystem = SelfcheckBootstrap::boot(960, 540,
		outDir + "/render_facade_selfcheck.log");
	if(!renderSystem)
	{
		std::fprintf(stderr,
			"render_facade_selfcheck: FAILED - backend boot\n");
		return 1;
	}
	// dimensions sidecar for the render_backend_parity driver: it cross-checks
	// that both flavors report the SAME logical (points) window AND the SAME
	// drawable (pixel) surface for the same request - a genuine flavor density
	// disagreement (e.g. one flavor ignoring the backing scale) is caught here,
	// not resized away.
	{
		unsigned int logicalWidth = 0, logicalHeight = 0;
		SelfcheckBootstrap::getLogicalWindowSize(logicalWidth, logicalHeight);
		unsigned int pixelWidth = 0, pixelHeight = 0;
		renderSystem->getWindowSize(pixelWidth, pixelHeight);
		std::FILE* dims = std::fopen((outDir + "/dimensions.txt").c_str(), "w");
		if(dims)
		{
			std::fprintf(dims, "logical %u %u\npixel %u %u\n",
				logicalWidth, logicalHeight, pixelWidth, pixelHeight);
			std::fclose(dims);
		}
	}

	// all facade handles live and die inside runChecks - they must be
	// released before the backend shuts down
	const int result = runChecks(renderSystem, outDir);
	SelfcheckBootstrap::shutdown();
	return result;
}
