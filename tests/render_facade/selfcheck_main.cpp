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

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
		optr<DrawLayer2D> hiddenLayer = renderSystem->createDrawLayer2D(20);
		SELFCHECK(topLayer && bottomLayer && hiddenLayer,
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
		// dynamic show/hide: unhide the magenta layer and re-verify
		hiddenLayer->setVisible(true);
		SELFCHECK(renderFrames(renderSystem, 2), "frames render after show()");
		const std::string shownShot = outDir + "/selfcheck_drawlayer2d_shown.png";
		renderSystem->saveWindowContents(shownShot);
		SELFCHECK(SelfcheckBootstrap::readImagePixel(shownShot, 750, 70,
			red, green, blue), "shown-layer probe decodes");
		SELFCHECK(red > 0.5f && blue > 0.5f && green < red - 0.3f,
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
		// RAII: dropping the handles removes the layers
		topLayer.reset();
		bottomLayer.reset();
		hiddenLayer.reset();
		SELFCHECK(renderFrames(renderSystem, 2),
			"frames render after 2D layers were dropped (RAII teardown)");
		const std::string removedShot = outDir + "/selfcheck_drawlayer2d_removed.png";
		renderSystem->saveWindowContents(removedShot);
		SELFCHECK(SelfcheckBootstrap::readImagePixel(removedShot, 170, 60,
			red, green, blue), "post-teardown probe decodes");
		SELFCHECK(!(red > green + 0.3f && red > blue + 0.3f),
			"2D pattern: dropped layers stop rendering");
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

	//--- RAII teardown of content while frames keep rendering ---------------
	sprite.reset();
	platform.reset();
	vectorShape.reset();
	SELFCHECK(renderFrames(renderSystem, 2),
		"frames render after content handles were dropped (RAII teardown)");

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
