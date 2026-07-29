/********************************************************************
	created:	Saturday 2026/07/26 at 12:30
	filename: 	GuiListView.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiListView_h__26_7_2026__12_30_00__
#define __GuiListView_h__26_7_2026__12_30_00__

#include "engine_gui/GuiScrollView.h"

#include <vector>

namespace Orkige
{
	//! @brief a vertical list: a scroll viewport whose content is taller than
	//! the viewport and scrolls by drag / wheel exactly like any scroll content.
	//! Pure composition over the pieces already shipped - a GuiScrollView (this
	//! class IS one) plus an inner content container plus one label widget per
	//! visible item. addItem/removeItem/clear manage the items and re-flow the
	//! content through the existing layout resolver, so nothing here is
	//! backend-specific (both flavors by construction). Create it via
	//! GuiFactory::createListView (which builds the content container) or the
	//! `.oui` `[ListView]` section.
	//!
	//! TWO CONTENT MODES, one item API:
	//! - the DEFAULT flowed list keeps the content container a vertical layout
	//!   group with content-size-fit, so rows may be any height and the group
	//!   arranges them. Every item is a live widget - fine for the option /
	//!   level / inventory lists a mobile game shows a screenful of.
	//! - VIRTUALIZED (@see setVirtualized) trades that flexibility for a flat
	//!   cost on big lists: rows must share ONE uniform height (@see
	//!   setItemHeight) and only the rows the viewport shows - plus one row of
	//!   overscan above and below - exist as widgets. The window is decided by
	//!   the pure core_util `virtualWindow` and the rows are placed at their
	//!   virtual offsets through the SAME layout resolver, so scrolling, the
	//!   scroll extent and hit-testing behave exactly as in the flowed list.
	//!   A 1000-row list then costs a dozen widgets instead of a thousand.
	//!
	//! Item ids are stable per item in BOTH modes (addItem returns one and
	//! getItemId keeps returning it), so the public API is source-compatible.
	//! The one honest difference: in virtualized mode an id only RESOLVES to a
	//! live widget while its row is inside the materialised window - the rows
	//! outside it are data, not widgets. Wire per-row behaviour to the item
	//! INDEX (getItemId round-trips it), never to a cached widget handle.
	class ORKIGE_ENGINE_DLL GuiListView : public GuiScrollView
	{
		OOBJECT(GuiListView, GuiScrollView);
	public:
		//! rows kept alive above AND below the visible band (@see virtualWindow)
		static const int VIRTUAL_OVERSCAN;

		GuiListView(String const & id, Ogre::Vector2 const & position,
			Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~GuiListView();

		//! @brief the factory calls this once after registration to build the inner
		//! content container (a widget of its own, parented to the list).
		//! @param fontIndex the glyph/font used for item labels.
		void initContent(uint fontIndex);

		//! @brief append a text row; returns the new item's stable id (empty if
		//! the content container is not ready). In the flowed list the row is a
		//! label the vertical group arranges; in a virtualized list the row
		//! becomes a widget only once it scrolls into the window.
		String addItem(String const & text);
		//! @brief remove an item by the id addItem returned. @return true if found.
		bool removeItem(String const & itemId);
		//! @brief remove every item.
		void clear();
		//! number of items currently in the list
		int getItemCount() const { return static_cast<int>(this->itemIds.size()); }
		//! the id of item @p index (empty when out of range) - the row a game wires
		//! a tap/handler to (find it via the widget id)
		String getItemId(int index) const;
		//! the text of item @p index (empty when out of range)
		String getItemText(int index) const;
		//! the inner content container's widget id (the vertical group)
		String const & getContentId() const { return this->contentId; }

		//! @brief materialise only the rows the viewport shows (@see the class
		//! remarks). Requires a uniform row height; pair it with setItemHeight.
		void setVirtualized(bool enable);
		//! @brief is the list virtualized?
		inline bool isVirtualized() const { return this->virtualized; }
		//! @brief the uniform row height in DESIGN units (gap included) a
		//! virtualized list places its rows on. Ignored by the flowed list.
		void setItemHeight(float height);
		inline float getItemHeight() const { return this->itemHeight; }
		//! @brief how many rows currently exist as widgets - the virtualization
		//! bound a test asserts (equals getItemCount() in the flowed list)
		int getMaterializedCount() const;
		//! @brief the first materialised row index (0 in the flowed list)
		int getFirstMaterializedIndex() const { return this->materialized.first; }

		virtual void onLayoutResolved(Ogre::Vector2 const & viewportSize,
			Ogre::Vector2 const & contentExtent);
		virtual bool onFrameStarted(FrameEventData const & data);

	private:
		//! create the row widget for @p index and place it at its virtual offset
		void materializeItem(int index);
		//! destroy the row widget of @p index (a no-op when it does not exist)
		void releaseItem(int index);
		//! recompute the visible window and add/remove exactly the rows that
		//! entered/left it (a no-op when the window did not move)
		void updateWindow();
		//! drop every materialised row (a model change / a mode flip)
		void releaseAll();
		//! push the virtual content height into the content container so the
		//! scroll extent covers every row, materialised or not
		void applyContentExtent();
		//! configure the content container for the current mode (group vs. the
		//! plain container the virtual rows anchor inside)
		void applyContentMode();

		String					contentId;		//!< the inner content container id
		String					viewAtlas;		//!< atlas for item widgets (matches the list)
		uint					itemFont;		//!< glyph index for item labels
		uint					contentZ;		//!< z of the content/clip layer
		unsigned int			itemSerial;		//!< monotone id source for item widgets
		std::vector<String>		itemIds;		//!< item ids in list order (stable)
		std::vector<String>		itemTexts;		//!< item texts in list order (the model)
		bool					virtualized;	//!< only materialise the visible window
		float					itemHeight;		//!< uniform row height, design units
		ListWindow				materialized;	//!< the rows that exist as widgets
	};
}

#endif //__GuiListView_h__26_7_2026__12_30_00__
