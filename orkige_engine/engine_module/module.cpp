/********************************************************************
	created:	Wednesday 2010/09/08 at 17:03
	filename: 	module.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
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
#include "engine_gocomponent/LightComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/WaterComponent.h"
#include "engine_gocomponent/DecalComponent.h"
#include "engine_gocomponent/SpriteComponent.h"
#include "engine_gocomponent/VectorShapeComponent.h"
#include "engine_gocomponent/VectorAnimationComponent.h"
#include "engine_gocomponent/SpriteAnimationComponent.h"
#include "engine_gocomponent/ParticleComponent.h"
#include "engine_gocomponent/AnimationComponent.h"
#include "engine_gocomponent/BoneAttachComponent.h"
#include "engine_gocomponent/RigidBodyComponent.h"
#include "engine_gocomponent/ScriptComponent.h"
#include "engine_physic/PhysicsWorld.h"
#include "engine_input/InputManager.h"
#include "engine_input/InputActionMap.h"
#include "engine_sound/SoundManager.h"
#include "engine_sound/MusicStream.h"
#include "engine_gui/IGuiObject.h"
#include "engine_gui/GuiManager.h"
#include "engine_gui/GuiWidget.h"
#include "engine_gui/GuiWidgetHandle.h"
#include "engine_gui/GuiLayerHandle.h"
#include "engine_gui/GuiFactory.h"
#include "engine_gui/GuiLabel.h"
#include "engine_gui/GuiButton.h"
#include "engine_gui/GuiCheckBox.h"
#include "engine_gui/GuiDecorWidget.h"
#include "engine_gui/GuiProgressBar.h"
#include "engine_gui/GuiScrollView.h"
#include "engine_gui/GuiSelectMenu.h"
#include "engine_gui/GuiTextEntry.h"
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
	OEXPORT(WaterComponent)
	OEXPORT(DecalComponent)
	OEXPORT(SpriteComponent)
	OEXPORT(VectorShapeComponent)
	OEXPORT(VectorAnimationComponent)
	OEXPORT(SpriteAnimationComponent)
	OEXPORT(ParticleComponent)
	OEXPORT(AnimationComponent)
	OEXPORT(BoneAttachComponent)
	OEXPORT(CameraComponent)
	OEXPORT(LightComponent)
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

	// widget visibility rides on the shared per-z UiLayer, reached via
	// widget:getLayer(). Lua holds a WEAK GuiLayerHandle (not the raw UiLayer): a
	// layer is SCREEN-scoped and dies with its view (an .oui hot-reload or a
	// preview teardown destroys the screen mid-session), so each method locks the
	// view for the call and raises "layer handle is dead" once the screen is gone
	// rather than dangling. @see engine_gui/GuiLayerHandle.h
	OSIMPLEEXPORT(Orkige::MetaLuaDetail::GuiLayerHandle,GuiLayer)
		OFUNC_CUSTOM(show, [](Orkige::MetaLuaDetail::GuiLayerHandle & h)
			{ Orkige::MetaLuaDetail::lockLayerHandle(h)->show(); })
		OFUNC_CUSTOM(hide, [](Orkige::MetaLuaDetail::GuiLayerHandle & h)
			{ Orkige::MetaLuaDetail::lockLayerHandle(h)->hide(); })
		OFUNC_CUSTOM(isVisible, [](Orkige::MetaLuaDetail::GuiLayerHandle & h)
			{ return Orkige::MetaLuaDetail::lockLayerHandle(h)->isVisible(); })
		OFUNC_CUSTOM(setVisible, [](Orkige::MetaLuaDetail::GuiLayerHandle & h, bool visible)
			{ Orkige::MetaLuaDetail::lockLayerHandle(h)->setVisible(visible); })
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

	// Weak Lua widget handles (option C). A finder or a factory create* hands Lua
	// a WidgetHandle: a WEAK proxy over the base GuiWidget. Every method locks the
	// object for the call (a dead handle raises an honest, pcall-catchable script
	// error naming the kind + id); a base method calls straight through (a virtual
	// override - e.g. GuiTextEntry::setPosition - is reached by the vtable); a leaf
	// method dynamic_casts the locked base to its leaf (a wrong-leaf call on a LIVE
	// widget of another type errors distinctly rather than pretending death); a
	// COLLISION name shared by several leaves lists its candidates and tries each
	// (see OWEAKHANDLE_LEAFMETHOD2/3 + the maintenance rule in Meta_Lua.h). ONE
	// handle carries the whole widget surface, so any handle can call any method
	// gated by the live type - findWidget(id):setText(...) works when the object
	// really is a label; the typed finders stay acquisition-time filters (nil on
	// the wrong kind). setParent takes another handle as a PARAMETER, locked inside
	// the wrapper (this is what dissolved the interim setParent adapter).
	OWEAKHANDLE_BEGIN(Orkige::GuiWidget, "WidgetHandle", "widget handle", "widget")
		// --- GuiWidget base surface (calls straight through; vtable reaches leaf overrides) ---
		OWEAKHANDLE_BASEMETHOD(setPosition)
		OWEAKHANDLE_BASEMETHOD(setSize)
		OWEAKHANDLE_BASEMETHOD(getSize)
		OWEAKHANDLE_BASEMETHOD(getPosition)
		OWEAKHANDLE_BASEMETHOD(centerHorizontal)
		OWEAKHANDLE_BASEMETHOD(setEnabled)
		OWEAKHANDLE_BASEMETHOD(isEnabled)
		// getLayer locks the widget, then hands back the view-keyed weak
		// GuiLayerHandle (a screen-scoped layer must not dangle behind a cached
		// handle) - @see engine_gui/GuiLayerHandle.h
		OWEAKHANDLE_CUSTOM(getLayer, [](Orkige::MetaLuaDetail::LuaWeakHandle<Orkige::GuiWidget> & h)
			{ return Orkige::makeLayerHandle(*Orkige::MetaLuaDetail::lockHandle(h, "widget handle")); })
		OWEAKHANDLE_PARENTMETHOD(setParent)					// widget-valued parameter
		OWEAKHANDLE_BASEMETHOD(setAnchors)
		OWEAKHANDLE_BASEMETHOD(setAnchorPreset)
		OWEAKHANDLE_BASEMETHOD(setPivot)
		OWEAKHANDLE_BASEMETHOD(setOffsets)
		OWEAKHANDLE_BASEMETHOD(setAnchoredPosition)
		OWEAKHANDLE_BASEMETHOD(setSizeDelta)
		OWEAKHANDLE_BASEMETHOD(setUseSafeArea)
		OWEAKHANDLE_BASEMETHOD(setLayoutGroup)
		OWEAKHANDLE_BASEMETHOD(setGroupPadding)
		OWEAKHANDLE_BASEMETHOD(setGroupSpacing)
		OWEAKHANDLE_BASEMETHOD(setChildAlignment)
		OWEAKHANDLE_BASEMETHOD(setChildForceExpand)
		OWEAKHANDLE_BASEMETHOD(setGridCellSize)
		OWEAKHANDLE_BASEMETHOD(setGridConstraint)
		OWEAKHANDLE_BASEMETHOD(setContentSizeFit)
		OWEAKHANDLE_BASEMETHOD(setRenderScale)
		OWEAKHANDLE_BASEMETHOD(getRenderScaleX)
		OWEAKHANDLE_BASEMETHOD(getRenderScaleY)
		OWEAKHANDLE_BASEMETHOD(setRenderRotation)
		OWEAKHANDLE_BASEMETHOD(getRenderRotation)
		OWEAKHANDLE_BASEMETHOD(setGroupAlpha)
		OWEAKHANDLE_BASEMETHOD(getGroupAlpha)
		OWEAKHANDLE_BASEMETHOD(getEffectiveAlpha)
		OWEAKHANDLE_BASEMETHOD(setAlphaBlocksInput)
		OWEAKHANDLE_BASEMETHOD(setTransition)
		// --- collision sets (one Lua name, several distinct leaves) ---
		OWEAKHANDLE_LEAFMETHOD2(setText, Orkige::GuiLabel, Orkige::GuiTextEntry)
		OWEAKHANDLE_LEAFMETHOD2(setAlpha, Orkige::GuiLabel, Orkige::GuiDecorWidget)
		OWEAKHANDLE_LEAFMETHOD2(setNineSlice, Orkige::GuiButton, Orkige::GuiDecorWidget)
		OWEAKHANDLE_LEAFMETHOD2(setTiled, Orkige::GuiButton, Orkige::GuiDecorWidget)
		OWEAKHANDLE_LEAFMETHOD3(getCaption, Orkige::GuiButton, Orkige::GuiProgressBar, Orkige::GuiSelectMenu)
		OWEAKHANDLE_LEAFMETHOD3(setCaption, Orkige::GuiButton, Orkige::GuiProgressBar, Orkige::GuiSelectMenu)
		OWEAKHANDLE_LEAFMETHOD2(setItems, Orkige::GuiSelectMenu, Orkige::GuiDropDown)
		OWEAKHANDLE_LEAFMETHOD2(setItemsString, Orkige::GuiSelectMenu, Orkige::GuiDropDown)
		OWEAKHANDLE_LEAFMETHOD2(getSelectedItem, Orkige::GuiSelectMenu, Orkige::GuiDropDown)
		// --- single-leaf methods ---
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiLabel, setAlignment)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiButton, getState)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiButton, wasClicked)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiButton, setPressFeedback)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiCheckBox, isChecked)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiCheckBox, setChecked)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiCheckBox, toggle)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiDecorWidget, setSprite)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiDecorWidget, setColour)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiDropDown, getSelectedIndex)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiDropDown, selectIndex)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiDropDown, isMenuOpen)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiProgressBar, setProgress)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiProgressBar, addProgress)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiProgressBar, getProgress)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiScrollView, setScroll)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiScrollView, getScroll)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiScrollView, getMaxScroll)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiSelectMenu, getSelectedItemIndex)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiSelectMenu, selectItemIndex)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiSelectMenu, selectItem)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiTextEntry, getText)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiTextEntry, setPlaceholder)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiTextEntry, setMaxLength)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiTextEntry, getMaxLength)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiTextEntry, isFocused)
		OWEAKHANDLE_LEAFMETHOD(Orkige::GuiTextEntry, wasSubmitted)
	OWEAKHANDLE_END

	// FactoryHandle: gui:getFactory() hands Lua a WEAK handle over the GuiFactory
	// (own its own currency - not a widget). Its create* lock the factory then hand
	// back the new widget as a WidgetHandle (the manager owns the widget; Lua holds
	// only weak handles). The owning GuiFactory usertype (the GuiFactory() ctor)
	// exposes the same create* through OFUNCWEAK, so both factory-acquisition paths
	// behave identically.
	OWEAKHANDLE_BEGIN(Orkige::GuiFactory, "FactoryHandle", "factory handle", "factory")
		OWEAKHANDLE_HANDLEMETHOD(createLabel)
		OWEAKHANDLE_HANDLEMETHOD(createButton)
		OWEAKHANDLE_HANDLEMETHOD(createProgressBar)
		OWEAKHANDLE_HANDLEMETHOD(createCheckBox)
		OWEAKHANDLE_HANDLEMETHOD(createSlider)
		OWEAKHANDLE_HANDLEMETHOD(createSelectMenu)
		OWEAKHANDLE_HANDLEMETHOD(createDecorWidget)
		OWEAKHANDLE_HANDLEMETHOD(createTextEntry)
		OWEAKHANDLE_HANDLEMETHOD(createScrollView)
		OWEAKHANDLE_HANDLEMETHOD(createDropDown)
	OWEAKHANDLE_END

	// ToggleGroupHandle: create/getToggleGroup hand Lua a WEAK handle over the
	// GuiToggleGroup (the single-selection radio group). addMember takes a checkbox
	// as a WidgetHandle PARAMETER, narrowed to GuiCheckBox inside the wrapper.
	OWEAKHANDLE_BEGIN(Orkige::GuiToggleGroup, "ToggleGroupHandle", "toggle group handle", "toggle group")
		OWEAKHANDLE_WIDGETPARAM(addMember, Orkige::GuiCheckBox)
		OWEAKHANDLE_BASEMETHOD(getSelected)
		OWEAKHANDLE_BASEMETHOD(setSelected)
		OWEAKHANDLE_BASEMETHOD(setAllowNone)
		OWEAKHANDLE_BASEMETHOD(getMemberCount)
		OWEAKHANDLE_BASEMETHOD(pollChanged)
	OWEAKHANDLE_END

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
	// objects with).
	// Ownership (by design): createChild / getParent stay OWNING by
	// design - RenderNode IS an optr-shared owning handle (RenderNode.h: "the
	// handle OWNS the backend node: destroying the last optr detaches+destroys
	// it"; the graph mirrors children with woptr, createChild transfers sole
	// ownership). A script legitimately HOLDS the node it places content on;
	// handing a weak handle would destroy the node the moment the Lua temporary
	// dropped. This is the sanctioned "by-design ownership the script holds".
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
		// getNode stays OWNING: it hands back the node the camera is attached to
		// (an optr the camera also holds) - the script drives the camera by
		// holding/moving this node, the same by-design RenderNode ownership.
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
	// C++-only (Engine wraps the window size as getWindowWidth/Height).
	// getWorld stays RAW: RenderWorld is scene-lifetime facade infrastructure the
	// RenderSystem owns by value (not shared_ptr-managed, so no weak_ptr source
	// exists) and it strictly outlives any script instance (scripts retire with
	// the scene). It is engine plumbing a script reaches per call, not a per-
	// object resource that can vanish under a live script - no weak flip applies.
	OSIMPLEEXPORT(Orkige::RenderSystem,RenderSystem)
		OFUNC(getWindowCamera)
		OFUNC(getWorld)
		OFUNC(saveWindowContents)
	OSIMPLEEXPORT_END

	// the scene world: node factory + root access for scripts.
	// getRootNode / createNode stay OWNING - the RenderNode ownership contract
	// (see the RenderNode block above): createNode transfers sole ownership, and
	// the script is meant to hold the nodes it builds a scene subtree from.
	OSIMPLEEXPORT(Orkige::RenderWorld,RenderWorld)
		OFUNC(getRootNode)
		// (name) - empty string = backend-generated name
		OFUNC(createNode)
	OSIMPLEEXPORT_END
ORKIGE_MODULE_END
