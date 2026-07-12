/********************************************************************
	created:	Tuesday 2010/10/26 at 18:24
	filename: 	GuiManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __GuiManager_h__26_10_2010__18_24_45__
#define __GuiManager_h__26_10_2010__18_24_45__

#include "engine_gui/UiRenderer.h"
#include <core_module/OrkigePrerequisites.h>
#include "engine_gui/GuiFactory.h"
#include <core_event/EventHandler.h>
#include "engine_gui/GuiView.h"
#include <core_util/ModalStack.h>
#include <core_util/ToastQueue.h>
#include <core_util/UiTransition.h>
#include <core_util/ScreenStack.h>
#include <core_util/SafeArea.h>
#include <core_tween/TweenManager.h>
#include "engine_render/RenderTexture.h"

#include <OgreResourceGroupManager.h>	// the resource-group default arguments

#include <map>
#include <vector>
#include <functional>

namespace Orkige
{
	class GuiTextEntry;
	class FontAtlas;
	class GuiToggleGroup;
	class GuiToast;
	class FrameEventData;

	class ORKIGE_ENGINE_DLL GuiManager : public Singleton<GuiManager>, public Interface, public EventHandler
	{
		OOBJECT(GuiManager, Interface);
		DECL_OSINGLETON(GuiManager);
		//--- Types -------------------------------------------------
	public:
		typedef std::map<String, optr<GuiView> > GuiViewMap;
		typedef std::list<optr<GuiView> > GuiViewList;
		typedef std::map<String, optr<GuiWidget> > GuiWidgetMap;
		typedef std::list<optr<GuiWidget> > GuiWidgetList;
		//! @brief flattened on-screen layout of one widget for remote
		//! inspection (the MSG_UI_LAYOUT debug readback): id + pixel rect +
		//! effective visibility (its layer's visible flag)
		struct WidgetLayout
		{
			String	id;
			float	left = 0.0f;
			float	top = 0.0f;
			float	width = 0.0f;
			float	height = 0.0f;
			bool	visible = true;
			bool	enabled = true;		//!< interactive (false = dimmed/inert)
			bool	modal = false;		//!< part of an active modal (scrim/dialog)
		};
		//! @brief which rect a parentless layout widget resolves against
		enum RootSpace
		{
			RS_FullWindow = 0,	//!< the whole window (0,0,W,H)
			RS_SafeArea			//!< the window minus the notch/home-bar insets
		};
		//! @brief an offscreen preview surface: a whole gui composited into a
		//! RenderTexture at a SIMULATED device context (resolution + safe-area
		//! insets + content scale) instead of the live window. The editor GUI
		//! Preview stage builds a GuiManager with one of these; games/the
		//! running HUD leave it default (a null target = the normal window
		//! path). @see the GUI Preview tab / the preview_ui MCP verb.
		struct PreviewSurface
		{
			optr<RenderTexture>	target;			//!< the offscreen surface the views composite into
			unsigned int		width = 0;		//!< simulated device width (pixels)
			unsigned int		height = 0;		//!< simulated device height (pixels)
			SafeAreaInsets		safeArea;		//!< simulated notch/home-bar insets
			float				contentScale = 1.0f;	//!< simulated display density (1/2/3x)
		};
		//! @brief a resolved confirm/alert dialog's outcome (poll-once via
		//! getDialogResult); DR_YES doubles as the alert's OK
		enum DialogResult
		{
			DR_NONE = 0,	//!< still open / no result yet
			DR_YES = 1,		//!< the affirmative button (Yes / OK)
			DR_NO = 2		//!< the negative button (No / Cancel)
		};
		//! @brief the animatable widget channels. One running tween per (widget,
		//! channel): starting a new one on the same channel REPLACES the old
		//! (last-wins retarget). @see tweenWidget
		enum WidgetTweenChannel
		{
			WTC_Alpha = 0,	//!< group alpha (cascades to the subtree)
			WTC_Scale,		//!< scale about the widget centre (2 channels x,y)
			WTC_Rotation,	//!< Z rotation about the centre (degrees)
			WTC_Position,	//!< anchoredPosition (layout) or absolute position
			WTC_Size,		//!< sizeDelta (layout) or absolute size
			WTC_Color,		//!< decor tint r,g,b,a (decor widgets only)
			WTC_COUNT
		};
		//! @brief builds a screen's widgets (a code-authored screen; the .oui path
		//! is the declarative alternative). Called each time the screen becomes
		//! current - the widgets it creates ARE the screen and are torn down when
		//! the screen leaves the top of the stack (the .oui/builder is the source
		//! of truth; transient widget state is not preserved across navigation).
		typedef std::function<void()> ScreenBuilder;
		//! @brief consulted when the Android back button / Escape is pressed with a
		//! non-empty screen stack and no modal up. Returning true CONSUMES the back
		//! (the current screen handled it itself); returning false lets the router
		//! run its default (pop when more than the root screen remains).
		typedef std::function<bool()> ScreenBackInterceptor;
	protected:
		//! @brief a registered screen: EITHER a declarative `.oui` layout path OR a
		//! code builder (exactly one is set). @see registerScreen / registerScreenBuilder
		struct ScreenDef
		{
			String			ouiPath;	//!< the `.oui` to loadLayout (empty if a builder)
			ScreenBuilder	builder;	//!< the code builder (null if a .oui path)
		};
		//! @brief one active modal: its two z layers plus the widget ids to
		//! destroy on dismissal, and (for a confirm/alert) its result buttons
		struct ModalRecord
		{
			ModalStack::Entry		layers;			//!< scrim + content z layers
			std::vector<String>		widgetIds;		//!< scrim + dialog widgets
			bool					isDialog = false;	//!< a confirm/alert dialog
			String					yesButtonId;	//!< affirmative (or OK) button id
			String					noButtonId;		//!< negative button id ("" for alert)
			bool					lightDismiss = false;	//!< Escape / outside-tap closes it
		};
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		//! loaded atlases by name (the views' screens reference them)
		std::map<String, optr<UiAtlas> > atlases;
		//! runtime-baked (TTF/SVG) atlases by name: they OWN the UiAtlas the
		//! screen references, so they must outlive their view; flushed once a
		//! frame so lazily-baked glyphs reach the GPU
		std::map<String, optr<FontAtlas> > fontAtlases;
		optr<GuiFactory> factory;
		GuiViewMap views;
		GuiWidgetMap widgets;
		GuiWidgetList sortedWidgets;
		optr<GuiDecorWidget> cursor;
		optr<GuiTextbox> stats;
		optr<GuiTextbox> statsValues;
		unsigned short statsMarkupColorIndex;
		String defaultAtlas;
		bool cancelInputUpdate;
        bool scaleStats;
		//! the single focused text-entry field, or NULL (@see focusTextEntry)
		GuiTextEntry* focusedTextEntry;
		//! the text field focused BELOW the modal stack, blurred while a modal is
		//! up and refocused when the last modal closes (focus transfer)
		GuiTextEntry* modalSavedFocus;
		//! set true while a cursor press claimed a text field (tap-away blur)
		bool textEntryFocusClaimed;
		//! @brief the rect-anchor layout state (@see the resolve pass in
		//! onFrameStarted). The default policy leaves layoutScale at 1, so a
		//! game that never opts in is unscaled and unaffected.
		LayoutScalePolicy layoutPolicy;
		RootSpace layoutRootSpace;
		//! a layout property changed since the last resolve (or the window
		//! resized): re-run the resolve pass on the next frame, else skip it
		bool layoutDirty;
		unsigned int lastLayoutWidth, lastLayoutHeight;
		//! @brief the modal-dialog stack + z allocation (pure, @see ModalStack)
		ModalStack modalStack;
		//! active modals by id (scrim/dialog widgets + teardown bookkeeping)
		std::map<String, ModalRecord> modalRecords;
		//! modals asked to close: drained at the next frame boundary so we never
		//! destroy widgets while the dispatch loop is still iterating them
		std::vector<String> pendingDismiss;
		//! poll-once dialog outcomes by modal id (@see getDialogResult)
		std::map<String, int> dialogResults;
		//! auto-id serial for showModal/showConfirm/showAlert
		unsigned int modalSerial;
		//! single-selection groups by id (owned so they outlive a script frame)
		std::map<String, optr<GuiToggleGroup> > toggleGroups;
		//! the single active toast + the pure queue that sequences/times toasts
		optr<GuiToast> toast;
		ToastQueue toastQueue;
		//! actions to run at the next frame boundary - the safe place to create
		//! or destroy widgets (never mid input-dispatch). @see runDeferred
		std::vector<std::function<void()> > deferredActions;
		//! a group alpha changed (or a subtree parent did): re-run the cascade
		//! pass next frame so every widget's effective alpha reaches its elements
		bool groupAlphaDirty;
		//! the one running tween per (widget id, channel) - a new tween on the
		//! same channel cancels the old (last-wins retarget). Cleared per widget
		//! when it is destroyed (auto-kill). @see tweenWidget
		std::map<String, std::map<int, TweenManager::TweenId> > widgetTweens;
		//--- the screen router (@see pushScreen / reconcileScreens) ---
		//! the pure LIFO stack of screen names (the navigation intent). Widget
		//! lifecycles follow it asynchronously through transitions.
		ScreenStack screenStack;
		//! registered screens by name (a .oui path or a code builder)
		std::map<String, ScreenDef> screenDefs;
		//! the name of the screen whose widgets are currently live ("" = none).
		//! Mid-transition this may lag screenStack.current() by one exit animation.
		String materializedScreen;
		//! the ids of the currently materialized screen's widgets (diffed at
		//! build time), torn down when the screen leaves the top
		std::vector<String> screenWidgetIds;
		//! true while the materialized screen is playing its exit transition; it is
		//! torn down once its transition tweens finish (@see anyScreenExitTweenActive)
		bool screenExiting;
		//! the current screen's back hook: consulted before the default pop (@see
		//! ScreenBackInterceptor). Cleared whenever a new screen is materialized.
		ScreenBackInterceptor screenBackInterceptor;
		//--- offscreen preview surface (the editor GUI Preview stage) ---
		//! true when this manager composites into previewTarget at a simulated
		//! device context instead of the live window (@see PreviewSurface)
		bool previewActive;
		//! the offscreen target the views' layers composite into (null = window)
		optr<RenderTexture> previewTarget;
		//! the simulated device context used for layout while previewActive
		unsigned int previewWidth, previewHeight;
		SafeAreaInsets previewSafeArea;
		//--- Methods -----------------------------------------------
	public:
		GuiManager(optr<GuiFactory> _factory, String const & defaultAtlas = "gui_default", String const & group = Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, PreviewSurface const * previewSurface = NULL);
		virtual ~GuiManager();
		//! @brief is this manager rendering into an offscreen preview surface
		//! (the editor GUI Preview stage) rather than the live window?
		bool hasPreviewSurface() const { return this->previewActive; }
		//! enable key and mouse events
		void enableInputEvents();
		//! disable key and mouse events
		void disableInputEvents();
		//! Displays Cursor
		void showCursor(String const & atlas, String const & sprite);
		//! hide cursor
		void hideCursor();
		//! is cursor visible?
		bool isCursorVisible();
		//! Updates cursor position based on unbuffered mouse state. This is necessary because if the gui manager has been cut off 
		//! from mouse events for a time, the cursor position will be out of date.
		void refreshCursor();
		//! get widget creation factory
		inline woptr<GuiFactory> getFactory();
		//! create screen with given atlas asserts if there is already a screen with that atlas
		woptr<GuiView> createView(String const & atlas, String const & group = Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		//! free resources from given view
		void destroyView(String const & atlas);
		//! free resources from given view and all widgets using its view/atlas
		void destroyViewWithWidgets(String const & atlas);
		//! free all views
		void destroyAllViews(bool keepDefaultAtlas = true);
		//! get screen with given atlas or NULL
		inline woptr<GuiView> getView(String const & atlas);
		//! get o create view with given atlas
		inline woptr<GuiView> getCreateView(String const & atlas, String const & group = Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		//! checks if screen with given id exists
		inline bool hasView(String const & atlas);
		//! hides all views
		void hideAllViews();
		//! inhieds all views
		void showAllViews();
		//! add given widget
		bool addWidget(optr<GuiWidget> widget);
		//! destroy given widget
		bool destroyWidget(String const & id);
		//! destroy given widget
		void destroyAllWidgets();
		//! check if widget with given id already exists
		inline bool widgetExists(String const & id);
		//! get widget with given id
		inline woptr<GuiWidget> getWidget(String const & id);
		//! @brief look a widget up by id for SCRIPTS - the bridge that lets Lua
		//! wire behavior (setters, tweens, transitions) onto a screen authored in
		//! a `.oui`. Returns empty (nil in Lua) when absent, unlike getWidget which
		//! asserts. The two authoring modes compose through this lookup.
		woptr<GuiWidget> findWidget(String const & id);
		//! get widget of given type with given id
		template<typename WidgetType>
		inline woptr<WidgetType> getWidgetAs(String const & id);
		//! get default texture atlas
		inline String const & getDefaultAtlas();
		//! show frame stats
		void showStats(uint glyphIndex = 9, Ogre::Vector2 const & pos = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, unsigned short markupColorIndex = 0, bool scaleStats = false);
		//! clear worst and best fps and more
		void resetStats();
		//! update statistic
		void updateStats();
		//! hide frame stats
		void hideStats();
		//! reorder the view rendering by their z value
		void reorderViews();

		//! exchanges the texture of an atlas
		void replaceAtlasTexture(String const & atlas, String const & texture);

		//! cancel current input dispatch
		void cancelCurrentInputUpdate();

		//! @brief run @p action at the next frame boundary. The only safe place
		//! to add/remove widgets from inside an input handler (the dispatch loop
		//! is iterating the widget list). Used by widgets that open/close a
		//! popup on a tap (@see GuiDropDown).
		void runDeferred(std::function<void()> const & action);

		//--- modal dialogs ------------------------------------------
		//! @brief raise a modal: a full-window consuming scrim on a fresh layer
		//! above everything, so nothing below it reacts. Author the dialog's own
		//! widgets at getModalContentZ(id) and register them with
		//! registerModalWidget so dismissing tears them down. @param id the modal
		//! id (empty -> an auto id, returned); @param lightDismiss a tap on the
		//! scrim closes it (an outside-tap sheet/dropdown). @return the modal id.
		String showModal(String const & id, bool lightDismiss = false);
		//! @brief the z layer the modal's dialog widgets must sit on (one above
		//! its scrim). 0 when no such modal exists.
		uint getModalContentZ(String const & id) const;
		//! @brief register a widget as part of a modal so dismissModal destroys it
		void registerModalWidget(String const & modalId, String const & widgetId);
		//! @brief request a modal be dismissed (deferred to the next frame
		//! boundary). @return true when the modal was active.
		bool dismissModal(String const & id);
		//! @brief dismiss the topmost modal (the Escape / Android-back action)
		void dismissTopModal();
		//! @brief dismiss every active modal
		void dismissAllModals();
		//! is any modal currently up?
		bool isModalActive() const { return !this->modalStack.entries.empty(); }
		//! the topmost modal's id, or "" when none is up
		String getTopModalId() const { return this->modalStack.topId(); }
		//! number of stacked modals
		int getModalCount() const { return static_cast<int>(this->modalStack.size()); }

		//! @brief build and raise a confirm dialog (panel + message + two
		//! buttons) as a modal. Poll the outcome with getDialogResult(id). Text
		//! with a leading '@' is looked up in the StringTable. @return modal id.
		String showConfirm(String const & title, String const & message,
			String const & yesText = "Yes", String const & noText = "No");
		//! @brief build and raise a one-button alert dialog as a modal. Poll with
		//! getDialogResult(id) (DR_YES on OK). @return modal id.
		String showAlert(String const & title, String const & message,
			String const & okText = "OK");
		//! @brief poll-and-consume a dialog's result: DR_YES / DR_NO once the
		//! user chose, else DR_NONE. Clears the latch after returning a choice.
		int getDialogResult(String const & modalId);

		//--- toggle groups ------------------------------------------
		//! @brief create a single-selection group; add checkboxes to it with
		//! group:addMember(checkbox). Owned by the manager (outlives a frame).
		woptr<GuiToggleGroup> createToggleGroup(String const & id);
		//! @brief the group with the given id, or empty
		woptr<GuiToggleGroup> getToggleGroup(String const & id);
		//! @brief destroy a toggle group (its member checkboxes keep existing)
		void destroyToggleGroup(String const & id);

		//--- toasts -------------------------------------------------
		//! @brief queue a timed notification. It slides no widgets and takes no
		//! input; shown one at a time (queued if several), fading in and out.
		void showToast(String const & text, float seconds = 2.5f);
		//! @brief is a toast currently on screen? (its widget exists and its
		//! layer is visible) - the readback a selfcheck asserts against
		bool isToastVisible() const;

		//! @brief give a text-entry field input focus (NULL clears focus): blurs
		//! the previously focused field and opens/closes the InputManager
		//! text-input session so exactly ONE field is focused at a time.
		void focusTextEntry(GuiTextEntry* entry);
		//! the focused text-entry field, or NULL
		GuiTextEntry* getFocusedTextEntry() const { return this->focusedTextEntry; }
		//! @brief a focused field being destroyed clears the focus + input session
		//! (the widget calls this from its destructor)
		void notifyTextEntryDestroyed(GuiTextEntry* entry);

		//--- rect-anchor layout (opt-in; @see GuiWidget) ---
		//! @brief set the design resolution + match factor the layout scale is
		//! derived from (match 0 = match width, 1 = match height). Zero design
		//! extents disable reference scaling (layout stays 1:1).
		void setDesignResolution(float designWidth, float designHeight,
			float matchWidthHeight = 0.0f);
		//! @brief how the two axis ratios combine (0 match, 1 shrink/shortest-
		//! side, 2 expand/longest-side; @see LayoutScalePolicy::MatchMode)
		void setLayoutMatchMode(int mode);
		//! @brief which rect parentless layout widgets resolve against
		//! ("FullWindow" or "SafeArea"); case-insensitive
		void setRootSpace(String const & space);
		//! @brief the current layout scale (design units -> window pixels) for
		//! the live window size (1 when no design resolution is set)
		float getLayoutScale() const;
		//! @brief mark the layout tree dirty so the next frame re-resolves it
		//! (widgets call this from their layout setters)
		void markLayoutDirty();
		//! @brief mark the cascade-alpha tree dirty so the next frame re-applies
		//! every widget's effective alpha to its elements (a group alpha changed)
		void markGroupAlphaDirty();

		//--- widget animation (@see WidgetTweenChannel) ---
		//! @brief animate a widget property to a target over a duration, through
		//! the shared TweenManager. Starting a tween on a channel already running
		//! for that widget REPLACES it (last-wins retarget); the tween auto-kills
		//! when the widget is destroyed. The apply re-fetches the widget by id each
		//! step (lifetime-safe). @param widgetId the target; @param channel which
		//! property; @param toValues the target value(s) (channel count per channel
		//! - Scale/Position/Size take 2, Color 4, others 1); @param duration
		//! seconds; @param ease curve (NULL = linear); @param delay seconds before
		//! the first step; @param onComplete fires once at the end. @return the
		//! tween handle (isActive/cancel; poll for completion), or 0 if the widget
		//! is gone / no TweenManager (the editor).
		TweenManager::TweenId tweenWidget(String const & widgetId, int channel,
			float const * toValues, float duration, Ease::Function ease,
			float delay = 0.0f,
			TweenManager::CompleteFunction const & onComplete =
				TweenManager::CompleteFunction());
		//! @brief cancel the running tween on one channel of a widget (if any)
		void cancelWidgetTween(String const & widgetId, int channel);
		//! @brief cancel every running tween on a widget (auto-kill on destroy)
		void cancelWidgetTweens(String const & widgetId);
		//! @brief play a widget's enter (show) or exit (hide) transition, resolved
		//! from its transition spec (@see GuiWidget::setTransition). show restores
		//! the widget and animates it in; hide animates it out then parks it at
		//! effective-invisible. A widget with no transition snaps. @return true
		//! when a transition (or snap) was applied (widget exists).
		bool playWidgetTransition(String const & widgetId, bool entering);

		//--- screen router (whole-screen navigation; @see ScreenStack) ---
		//! @brief register a screen backed by a declarative `.oui` layout, under a
		//! name push/replace refer to. Re-registering a name replaces its def.
		void registerScreen(String const & name, String const & ouiPath);
		//! @brief register a screen built by code (a Lua builder / a C++ callback);
		//! the builder runs each time the screen becomes current.
		void registerScreenBuilder(String const & name, ScreenBuilder const & builder);
		//! @brief is a screen registered under this name
		bool hasScreen(String const & name) const;
		//! @brief push a screen on top of the stack: the current screen plays its
		//! exit transition and is torn down, then the new screen is built and plays
		//! its enter transition. An unregistered name ending in ".oui" is taken as a
		//! layout path (a convenience for `push("screens/title.oui")`).
		void pushScreen(String const & name);
		//! @brief replace the top screen without growing the stack (a sideways
		//! navigation); an empty stack makes this a push.
		void replaceScreen(String const & name);
		//! @brief pop the top screen and return to the one beneath it. @return the
		//! popped name ("" when the stack was empty).
		String popScreen();
		//! @brief the current (top) screen's name, or "" when none is up
		String currentScreen() const { return this->screenStack.current(); }
		//! @brief the number of screens on the stack
		int screenDepth() const { return static_cast<int>(this->screenStack.size()); }
		//! @brief the whole bottom-to-top screen path (the agent readback)
		std::vector<String> screenPath() const { return this->screenStack.path(); }
		//! @brief pop every screen (and tear the current one down)
		void clearScreens();
		//! @brief set the current screen's back hook (@see ScreenBackInterceptor);
		//! pass an empty function to clear it. Reset automatically on navigation.
		void setScreenBackInterceptor(ScreenBackInterceptor const & interceptor);
		//! @brief route an Android-back / Escape press through the screen stack:
		//! the back hook first, then the default pop (only above the root screen).
		//! @return true when the stack CONSUMED the back (so it is not passed on).
		bool handleScreenBack();

		//--- message-bus mirrors (core_script/ScriptEventBus) --------------
		//! @brief emit a semantic gui event onto the script message bus at
		//! input-dispatch time - the multi-consumer complement to widget polling
		//! (which is untouched: several scripts can subscribe, while the poll
		//! idiom stays single-consumer). The widget id (and the value/text where
		//! one applies) rides the payload; delivered in the same frame's script
		//! phase. A no-op shape when nobody subscribes (the common case). The
		//! widgets and the screen/dialog/toast routers call these.
		void emitGuiClicked(String const & widgetId);				//!< gui.clicked {id}
		void emitGuiToggled(String const & widgetId, bool state);	//!< gui.toggled {id, state}
		void emitGuiSubmitted(String const & widgetId,
			String const & text);									//!< gui.submitted {id, text}
		void emitGuiValueChanged(String const & widgetId,
			double value);											//!< gui.valueChanged {id, value}
		void emitGuiDialogResult(String const & modalId, int result);	//!< gui.dialogResult {id, result}
		void emitGuiScreenPushed(String const & name);			//!< gui.screenPushed {name}
		void emitGuiScreenPopped(String const & name);			//!< gui.screenPopped {name}
		void emitGuiToastShown(String const & text);				//!< gui.toastShown {text}

		//--- performance-contract probes (the "1 draw per screen per atlas,
		//--- dirty-tracked" promise; @see the demo_gui_matrix perf assertions) ---
		//! @brief total draw submissions across every visible screen this frame
		//! (1 per atlas + 1 per scissored scroll region). @see UiScreen::getLastBatchCount
		size_t getLastBatchCount() const;
		//! @brief total monotonic batch-resubmit count across screens; read deltas
		//! to assert dirty-tracking (0 on a static frame, +1 per content change /
		//! per active-animation frame). @see UiScreen::getRebuildCount
		size_t getRebuildCount() const;
		//! @brief total monotonic element GEOMETRY-rebuild count across screens.
		//! A post-pass transform/alpha animation resubmits but rebuilds NO geometry,
		//! so this stays flat while it runs (the transform-only design proof).
		//! @see UiScreen::getGeometryRebuildCount
		size_t getGeometryRebuildCount() const;
		//! @brief total retained scratch-buffer capacity across screens (stable
		//! across identical rebuilt frames = no per-frame reallocation)
		size_t getScratchCapacity() const;
		//! @brief run one full gui frame pipeline off the event bus, so a selfcheck
		//! can TIME it (the perf-budget probe). @param delta seconds since last frame
		void profileTick(float delta);

		//! returns true if given point is over any widget
		bool isPointOverWidget(Ogre::Vector2 const & point);
		//! @brief snapshot every widget's id, pixel rect and visibility - the
		//! source of the MSG_UI_LAYOUT readback an agent asserts "HUD inside the
		//! safe box" against. Order follows the widget map (stable by id).
		std::vector<WidgetLayout> getWidgetLayouts();
	protected:
		//! @brief the layout surface size: the preview surface when previewActive,
		//! else the live window (RenderSystem::getWindowSize). ALL layout code
		//! reads this, so the preview stage lays out at the simulated device size.
		void surfaceSize(unsigned int & width, unsigned int & height) const;
		//! @brief the safe-area insets in effect: the simulated insets when
		//! previewActive, else the engine's live insets. Returns zero insets when
		//! neither is available.
		SafeAreaInsets surfaceSafeArea() const;
		//! @brief the per-frame gui pipeline (deferred actions, widget ticks, modal
		//! drain, toast, layout + cascade-alpha resolve, screen rebuild + submit).
		//! Shared by the FrameStarted handler and profileTick (the perf probe).
		void tickFrame(FrameEventData & frameData);
		//! Process frame events. Updates frame statistics widget set and deletes all widgets queued for destruction.
		bool onFrameStarted(Orkige::Event const & event);
		//! Process frame events. Updates frame statistics widget set and deletes all widgets queued for destruction.
		bool onFrameRenderingQueued(Orkige::Event const & event);
		//! after changing a game state (with its possible loading times) reset the worst and best fps
		bool onGameStateChanged(Orkige::Event const & event);

		//! @brief resolve every opted-in layout widget to absolute pixels and
		//! push the result into it. Builds a transient LayoutItem forest from the
		//! widget hierarchy, runs the pure two-pass resolver (bottom-up preferred
		//! sizes for groups/content-fit, then top-down rect assignment), and
		//! writes the results back. Parentless widgets resolve against the
		//! (full-window or safe-area) root rect. Runs only when the layout went
		//! dirty or the window resized.
		void resolveLayouts();

		//! @brief re-apply every widget's effective (cascaded) alpha to its visual
		//! elements. Runs only when a group alpha changed (groupAlphaDirty), so a
		//! static UI pays nothing. O(n * layout depth) over the widgets.
		void resolveGroupAlpha();

		//! @brief drive the materialized screen toward screenStack.current(): tear
		//! the old screen down once its exit transition has finished, then build
		//! the new top. Called once per frame and right after each navigation so a
		//! no-transition flow settles immediately. One transition is in flight at
		//! a time; teardown keys off the exit tweens completing, so it is exact at
		//! any frame rate (and instant when a screen has no transition).
		void reconcileScreens();
		//! @brief build @p name's widgets (its builder or its .oui) and play each
		//! one's enter transition; the created ids become screenWidgetIds
		void materializeScreen(String const & name);
		//! @brief start the current screen's exit: play each widget's exit
		//! transition, then wait for those tweens (else tear down now if they snap)
		void beginScreenExit();
		//! @brief destroy the exiting screen's widgets and clear the materialized state
		void finishScreenExit();
		//! @brief is any of the exiting screen's widgets still running a transition
		//! tween (the "exit still animating" test that gates teardown)
		bool anyScreenExitTweenActive() const;
		//! @brief tear down the modals queued for dismissal (frame-boundary safe)
		void drainModalDismissals();
		//! @brief poll a live dialog's buttons; a click latches the result and
		//! requests the dialog be dismissed
		void pollDialogButtons();
		//! @brief drive the toast queue by @p delta and refresh the toast widget
		void updateToast(float delta);
		//! @brief assemble a confirm (two-button) or alert (one-button) dialog
		//! on a fresh modal layer and register it. @return the modal id.
		String buildDialogModal(String const & id, String const & title,
			String const & message, String const & yesText,
			String const & noText, bool twoButtons);

		//! process committed text input (routed to the focused text-entry field)
		bool onTextInput(Orkige::Event const & event);
		//! process key pressed events
		bool onKeyPressed(Orkige::Event const & event);
		//! process key released events
		bool onKeyReleased(Orkige::Event const & event);

		//! Processes mouse button down events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onMousePressed(Orkige::Event const & event);
		//! Processes mouse button up events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onMouseReleased(Orkige::Event const & event);
		//! Updates cursor position. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onMouseMoved(Orkige::Event const & event);

		//! Processes touch down events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onTouchPressed(Orkige::Event const & event);
		//! Processes touch up events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onTouchReleased(Orkige::Event const & event);
		//! Processes touch move events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onTouchMoved(Orkige::Event const & event);

	private:
	};
	//---------------------------------------------------------------
	inline woptr<GuiFactory> GuiManager::getFactory()
	{
		return this->factory;
	}
	//---------------------------------------------------------------
	inline woptr<GuiView> GuiManager::getView(String const & atlas)
	{
		GuiViewMap::iterator it = this->views.find(atlas);
		if(it != this->views.end())
		{
			return it->second;
		}
		return oNULL(GuiView);
	}
	//---------------------------------------------------------------
	inline bool GuiManager::hasView(String const & atlas)
	{
		bool screenExists = this->getView(atlas).lock() != NULL;
		return screenExists;
	}
	//---------------------------------------------------------------
	inline woptr<GuiView> GuiManager::getCreateView(String const & atlas, String const & group)
	{
		if(atlas == StringUtil::BLANK)
		{
			return this->getView(this->defaultAtlas);
		}

		if(this->hasView(atlas))
		{
			return this->getView(atlas);
		}

		return this->createView(atlas, group);
	}
	//---------------------------------------------------------------
	inline bool GuiManager::widgetExists(String const & id)
	{
		if(this->widgets.find(id) != this->widgets.end())
		{
			return true;
		}
		return false;
	}
	//---------------------------------------------------------------
	inline woptr<GuiWidget> GuiManager::getWidget(String const & id)
	{
		GuiWidgetMap::iterator it = this->widgets.find(id);
		oAssertDesc(it != this->widgets.end(), "Could not find Widget: " << id << "!");
		return it->second;
	}
	//---------------------------------------------------------------
	template<typename WidgetType>
	inline woptr<WidgetType> GuiManager::getWidgetAs(String const & id)
	{
		woptr<GuiWidget> weak_widget = this->getWidget(id);
		optr<GuiWidget> widget = weak_widget.lock();
		oAssert(widget);
		oAssertDesc(widget->getTypeInfo() == WidgetType::getClassTypeInfo(), "Widget: " << id << " is of type: " << widget->getTypeInfo().getName() << " and not of type: " << WidgetType::getClassTypeInfo().getName() << " !");
		optr<WidgetType> casted_widget = std::static_pointer_cast<WidgetType>(widget);
		oAssert(casted_widget);
		return casted_widget;
	}
	//---------------------------------------------------------------
	inline String const & GuiManager::getDefaultAtlas()
	{
		return this->defaultAtlas;
	}
}

#endif //__GuiManager_h__26_10_2010__18_24_45__