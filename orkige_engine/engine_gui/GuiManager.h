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
		//! @brief a resolved confirm/alert dialog's outcome (poll-once via
		//! getDialogResult); DR_YES doubles as the alert's OK
		enum DialogResult
		{
			DR_NONE = 0,	//!< still open / no result yet
			DR_YES = 1,		//!< the affirmative button (Yes / OK)
			DR_NO = 2		//!< the negative button (No / Cancel)
		};
	protected:
		//! @brief one active modal: its two z layers plus the widget ids to
		//! destroy on dismissal, and (for a confirm/alert) its result buttons
		struct ModalRecord
		{
			ModalStack::Entry		layers;			//!< scrim + content z layers
			std::vector<String>		widgetIds;		//!< scrim + dialog widgets
			bool					isDialog = false;	//!< a confirm/alert dialog
			String					yesButtonId;	//!< affirmative (or OK) button id
			String					noButtonId;		//!< negative button id ("" for alert)
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
		//--- Methods -----------------------------------------------
	public:
		GuiManager(optr<GuiFactory> _factory, String const & defaultAtlas = "gui_default", String const & group = Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		virtual ~GuiManager();
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

		//! returns true if given point is over any widget
		bool isPointOverWidget(Ogre::Vector2 const & point);
		//! @brief snapshot every widget's id, pixel rect and visibility - the
		//! source of the MSG_UI_LAYOUT readback an agent asserts "HUD inside the
		//! safe box" against. Order follows the widget map (stable by id).
		std::vector<WidgetLayout> getWidgetLayouts();
	protected:
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