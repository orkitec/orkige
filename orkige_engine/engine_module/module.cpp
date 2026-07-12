/********************************************************************
	created:	Wednesday 2010/09/08 at 17:03
	filename: 	module.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

// Only the modules already ported to OGRE 14 are registered here.
// Script behavior lives in the sol2-based ScriptComponent.
#include "engine_graphic/Engine.h"
#ifdef ORKIGE_RENDER_CLASSIC
// classic-only export: the overlay-based ingame console (Ogre Overlay +
// Rectangle2D - a classic zone; a cross-backend console would be rebuilt
// on gui/DrawLayer2D when wanted). gui itself is flavor-neutral -
// engine:hasUISystem() is true on BOTH
// flavors and the Gui usertypes below register unconditionally.
#include "engine_graphic/IngameConsole.h"
#endif
#include "engine_gocomponent/SoundComponent.h"
#include "engine_gocomponent/CameraComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/SpriteComponent.h"
#include "engine_gocomponent/VectorShapeComponent.h"
#include "engine_gocomponent/SpriteAnimationComponent.h"
#include "engine_gocomponent/ParticleComponent.h"
#include "engine_gocomponent/AnimationComponent.h"
#include "engine_gocomponent/RigidBodyComponent.h"
#include "engine_gocomponent/ScriptComponent.h"
#include "engine_physic/PhysicsWorld.h"
#include "engine_input/InputManager.h"
#include "engine_input/InputActionMap.h"
#include "engine_sound/SoundManager.h"
#include "engine_sound/MusicStream.h"
#include "engine_gui/IGuiObject.h"
#include "engine_gui/GuiManager.h"
#include "engine_gui/GuiToggleGroup.h"
#include "engine_gui/GuiDropDown.h"
#include "engine_gui/GuiToast.h"
#include "engine_gui/GuiModalScrim.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderNode.h"
#include "engine_render/RenderCamera.h"

using namespace Orkige;

ORKIGE_MODULE(orkige_engine)
	OEXPORT(TransformComponent)
	OEXPORT(ModelComponent)
	OEXPORT(SpriteComponent)
	OEXPORT(VectorShapeComponent)
	OEXPORT(SpriteAnimationComponent)
	OEXPORT(ParticleComponent)
	OEXPORT(AnimationComponent)
	OEXPORT(CameraComponent)
	OEXPORT(SoundComponent)
	OEXPORT(RigidBodyComponent)
	OEXPORT(ScriptComponent)
	OEXPORTMAP(StringGameObjectMap,Orkige::String,optr<Orkige::GameObject>)

	OEXPORT(Engine)
	OEXPORT(FrameEventData)
#ifdef ORKIGE_RENDER_CLASSIC
	OEXPORT(IngameConsole)
#endif

	OEXPORT(KeyEventData)
	OEXPORT(MouseEventData)
	OEXPORT(AccelerationEventData)
	OEXPORT(TouchEventData)
	OEXPORT(GestureEventData)
	OEXPORT(InputManager)

	// the action-mapping layer on top of InputManager. Like GuiFactory it
	// is not an OOBJECT, so its Lua face lives here - registered under the
	// game-facing name "InputActions" (decoupled from the C++ class name).
	// Scripts: local actions = InputActions.getSingleton();
	//          actions:pressed("jump"); local m = actions:value2("move")
	OSIMPLEEXPORT(Orkige::InputActionMap,InputActions)
		OSINGLETON()
		OFUNC(down)			// down("name")   - held this frame (digital)
		OFUNC(pressed)		// pressed("name") - went down this frame
		OFUNC(released)		// released("name") - went up this frame
		OFUNC(value)		// value("name")   - analog1D value
		OFUNC(value2)		// value2("name")  - analog2D value (Vector2)
		OFUNC(hasAction)	// hasAction("name")
	OSIMPLEEXPORT_END

	OEXPORT(PhysicsWorld)
	OEXPORT(SoundManager)
	OEXPORT(SoundSource)
	OEXPORT(MusicStream)

	// PhysicsWorld::castRayHit answers with this plain value type
	OSIMPLEEXPORT(Orkige::PhysicsWorld::RayHit,RayHit)
		OCONSTVAR(hit)
		OCONSTVAR(position)
		OCONSTVAR(bodyId)
	OSIMPLEEXPORT_END

	// GuiFactory is not an OOBJECT (it derives Ogre::ConfigFile), so its
	// Lua face lives here: scripts construct one, hand it to GuiManager
	// and create the widgets the game needs. The create* functions return
	// woptr - OFUNCWEAK hands Lua the locked optr. sol2 carries no C++
	// default arguments across the binding: Lua passes every parameter.
	OSIMPLEEXPORT(Orkige::GuiFactory,GuiFactory)
		OCONSTRUCTOR0()
		// (id, glyphIndex, text, position, atlas, z, scaled)
		OFUNCWEAK(createLabel)
		// (id, sprite, glyphIndex, text, position, textAlignment, size,
		//  atlas, z, nostate, blinkState) - sprite name is the base of the
		//  _over/_down/_disabled state sprites; blinkState 0 = none
		OFUNCWEAK(createButton)
		// (id, sprite, glyphIndex, text, position, textAlignment, size,
		//  atlas, z) - sprite is the frame, "<sprite>_bar" the fill
		OFUNCWEAK(createProgressBar)
		// (id, sprite, glyphIndex, text, position, textAlignment, size,
		//  atlas, z, useCheckbox) - settings toggle; poll isChecked()
		OFUNCWEAK(createCheckBox)
		// (id, buttonId, sprite, glyphIndex, text, position, textAlignment,
		//  size, atlas, z) - settings slider; poll getSelectedItemIndex(),
		//  set options with setItems({...})
		OFUNCWEAK(createSlider)
		// (id, buttonId, sprite, glyphIndex, text, position, textAlignment,
		//  size, atlas, z) - option cycler; same value API as the slider
		OFUNCWEAK(createSelectMenu)
		// (id, sprite, position, size, atlas, z) - a sprite panel OR, with an
		// empty/"none" sprite, a SOLID fill: the dimmed pause/menu backdrop
		// (tint + fade it via setColour/setAlpha on a top z layer)
		OFUNCWEAK(createDecorWidget)
		// (id, sprite, glyphIndex, placeholder, position, size, atlas, z,
		//  maxLength) - single-line text field; poll getText()/wasSubmitted(),
		//  set with setText()/setPlaceholder()/setMaxLength() (maxLength 0 =
		//  unlimited). Focus follows a tap; the mobile keyboard rises on focus.
		OFUNCWEAK(createTextEntry)
		// (id, position, size, atlas, z) - a scroll viewport: parent content
		// widgets (authored at the SAME z) under it; a taller child scrolls by
		// drag / wheel, clipped to the viewport
		OFUNCWEAK(createScrollView)
		// (id, sprite, glyphIndex, text, position, textAlignment, size, atlas,
		//  z) - a button that drops a scrollable option list on tap; set the
		//  options with :setItems({...}), poll :getSelectedIndex()
		OFUNCWEAK(createDropDown)
		// (path) - load a declarative .oui layout at runtime (widgets, anchors,
		// groups, nine-slice, scroll, modals, toggle groups); the file the MCP
		// write_project_file authors
		OFUNC(loadLayout)
	OSIMPLEEXPORT_END

	// the single-selection (radio) group over checkboxes: created via
	// gui:createToggleGroup(id), members added with :addMember(checkbox);
	// scripts poll :getSelected() / :pollChanged()
	OSIMPLEEXPORT(Orkige::GuiToggleGroup,GuiToggleGroup)
		OFUNC(addMember)
		OFUNC(getSelected)
		OFUNC(setSelected)
		OFUNC(setAllowNone)
		OFUNC(getMemberCount)
		OFUNC(pollChanged)
	OSIMPLEEXPORT_END

	// safe-area insets (notch / rounded corners / home indicator) in PIXELS:
	// engine:getSafeAreaInsets() answers this value type; scripts read the
	// fields to anchor HUD/menus inside the drawable box (all zero on desktop)
	OSIMPLEEXPORT(Orkige::SafeAreaInsets,SafeAreaInsets)
		OVAR(mLeft)
		OVAR(mTop)
		OVAR(mRight)
		OVAR(mBottom)
	OSIMPLEEXPORT_END

	// widget visibility rides on the shared per-z UiLayer
	OSIMPLEEXPORT(Orkige::UiLayer,GuiLayer)
		OFUNC(show)
		OFUNC(hide)
		OFUNC(isVisible)
		OFUNC(setVisible)
	OSIMPLEEXPORT_END

	OEXPORT(IGuiObject)
	OEXPORT(GuiWidget)
	OEXPORT(GuiView)
	OEXPORT(GuiTextbox)
	OEXPORT(GuiTextEntry)
	OEXPORT(GuiSlider)
	OEXPORT(GuiSelectMenu)
	OEXPORT(GuiProgressBar)
	OEXPORT(GuiManager)
	OEXPORT(GuiLabel)
	OEXPORT(GuiDragDropButton)
	OEXPORT(GuiDecorWidget)
	OEXPORT(GuiCheckBox)
	OEXPORT(GuiButtonBlink)
	OEXPORT(GuiButton)
	// the dropdown (a Button that opens a scrollable option list on a modal),
	// the passive toast, and the modal scrim (a DecorWidget that consumes input
	// for the layers below it)
	OEXPORT(GuiDropDown)
	OEXPORT(GuiToast)
	OEXPORT(GuiModalScrim)
	// the scroll viewport: setScroll(y)/getScroll()/getMaxScroll() plus the
	// GuiWidget layout setters (setParent/setAnchorPreset/...)
	OEXPORT(GuiScrollView)
	OEXPORT(DragEventData)

	// the math value types scripts actually compute with (the engine math
	// vocabulary - Ogre spellings per the RenderMath.h alias decision);
	// construction is Vector3(x,y,z) / Quaternion(w,x,y,z), members are
	// plain fields. CLASSIC: x/y/z (and cross/dot) physically live on
	// Ogre's VectorBase - the base must be registered for sol2 to resolve
	// them (see OSIMPLEEXPORT_BASED); the typedef exists because macro
	// arguments cannot carry the comma. NEXT: Ogre-Next's vectors have no
	// VectorBase split - everything sits on the type itself.
#ifdef ORKIGE_RENDER_CLASSIC
	using OgreVector3Base [[maybe_unused]] = Ogre::VectorBase<3, Ogre::Real>;
	OSIMPLEEXPORT_BASED(Ogre::Vector3,OgreVector3Base,Vector3)
#else
	OSIMPLEEXPORT(Ogre::Vector3,Vector3)
#endif
		OCONSTRUCTOR3(float,float,float)
		OVAR(x)
		OVAR(y)
		OVAR(z)
		OFUNC(length)
		OFUNC(distance)
		OFUNC(squaredDistance)
		OFUNC(dotProduct)
		OFUNC(crossProduct)
		OFUNC(normalisedCopy)
	OSIMPLEEXPORT_END

	// the 2D sibling for UI work (positions/sizes in screen pixels); like
	// Vector3, x/y live on the VectorBase on classic only
#ifdef ORKIGE_RENDER_CLASSIC
	using OgreVector2Base [[maybe_unused]] = Ogre::VectorBase<2, Ogre::Real>;
	OSIMPLEEXPORT_BASED(Ogre::Vector2,OgreVector2Base,Vector2)
#else
	OSIMPLEEXPORT(Ogre::Vector2,Vector2)
#endif
		OCONSTRUCTOR2(float,float)
		OVAR(x)
		OVAR(y)
	OSIMPLEEXPORT_END

	OSIMPLEEXPORT(Ogre::Quaternion,Quaternion)
		OCONSTRUCTOR4(float,float,float,float)
		OVAR(w)
		OVAR(x)
		OVAR(y)
		OVAR(z)
	OSIMPLEEXPORT_END

	// --- the engine_render facade surface (Docs/render-abstraction
	// .md): the classic Ogre scene usertypes (SceneNode/SceneManager/
	// Viewport/Camera) are gone - scripts drive the renderer through the
	// backend-free facade handles below (optr = shared_ptr, which sol2
	// binds natively). No default arguments cross the binding: Lua passes
	// every parameter (node:lookAt(target, RenderNode.TransformSpace
	// .TS_WORLD, Vector3(0, 0, -1))). yaw/pitch/roll and the Degree-taking
	// projection calls stay unregistered until a Radian/Degree usertype
	// exists - scripts rotate via setOrientation/lookAt/setDirection.

	// the transform-hierarchy node handle (what scripts place cameras and
	// objects with)
	OSIMPLEEXPORT(Orkige::RenderNode,RenderNode)
		OFUNCIR(getPosition)
		OFUNC(setPosition)
		OFUNCIR(getOrientation)
		OFUNC(setOrientation)
		OFUNCIR(getScale)
		OFUNC(setScale)
		OFUNC(getWorldPosition)
		OFUNC(getWorldOrientation)
		// (delta, TransformSpace)
		OFUNC(translate)
		// (target, TransformSpace, localDirection)
		OFUNC(lookAt)
		// (direction, TransformSpace, localDirection)
		OFUNC(setDirection)
		// (useFixed, fixedAxis)
		OFUNC(setFixedYawAxis)
		// (visible, cascade)
		OFUNC(setVisible)
		OFUNC(createChild)
		OFUNC(getParent)
		OFUNC(numChildren)
		OENUM_START(TransformSpace)
			OENUM_VALUE(TS_LOCAL)
			OENUM_VALUE(TS_PARENT)
			OENUM_VALUE(TS_WORLD)
		OENUM_END
	OSIMPLEEXPORT_END

	// the camera handle: scripts fetch the window camera from the Engine
	// singleton (engine:getCamera()) and move it via getNode()
	OSIMPLEEXPORT(Orkige::RenderCamera,RenderCamera)
		OFUNC(getNode)
		// (verticalHalfExtent, nearClip, farClip) - Engine's
		// setCameraOrthographic wraps this preserving the clips
		OFUNC(setOrthographic)
		OFUNC(getProjectionType)
		OFUNC(getNearClip)
		OFUNC(getFarClip)
		OFUNC(setAspectRatio)
		OFUNC(setWireframe)
		OENUM_START(ProjectionType)
			OENUM_VALUE(PT_PERSPECTIVE)
			OENUM_VALUE(PT_ORTHOGRAPHIC)
		OENUM_END
	OSIMPLEEXPORT_END

	// the renderer entry point (engine:getRenderSystem()) - the services a
	// script can meaningfully reach; sizes/stats with out-parameters stay
	// C++-only (Engine wraps the window size as getWindowWidth/Height)
	OSIMPLEEXPORT(Orkige::RenderSystem,RenderSystem)
		OFUNC(getWindowCamera)
		OFUNC(getWorld)
		OFUNC(saveWindowContents)
	OSIMPLEEXPORT_END

	// the scene world: node factory + root access for scripts
	OSIMPLEEXPORT(Orkige::RenderWorld,RenderWorld)
		OFUNC(getRootNode)
		// (name) - empty string = backend-generated name
		OFUNC(createNode)
	OSIMPLEEXPORT_END
ORKIGE_MODULE_END
