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
	//! @brief a vertical list: a scroll viewport whose content is a vertical
	//! layout group that grows to fit its items, so a list taller than the
	//! viewport scrolls by drag / wheel exactly like any scroll content. Pure
	//! composition over the pieces already shipped - a GuiScrollView (this class
	//! IS one) plus an inner content container that is a vertical layout group
	//! with content-size-fit, plus one label widget per item. addItem/removeItem/
	//! clear manage the item widgets and re-flow the group through the existing
	//! layout resolver, so nothing here is backend-specific (both flavors by
	//! construction). This is a v1 convenience: it materialises one widget per
	//! item (no virtualization / recycling) - fine for the option/level/inventory
	//! lists a mobile game shows a screenful of. Create it via
	//! GuiFactory::createListView (which builds the content container) or the
	//! `.oui` `[ListView]` section.
	class ORKIGE_ENGINE_DLL GuiListView : public GuiScrollView
	{
		OOBJECT(GuiListView, GuiScrollView);
	public:
		GuiListView(String const & id, Ogre::Vector2 const & position,
			Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~GuiListView();

		//! @brief the factory calls this once after registration to build the inner
		//! vertical-group content container (a widget of its own, parented to the
		//! list). @param fontIndex the glyph/font used for item labels.
		void initContent(uint fontIndex);

		//! @brief append a text row; returns the new item widget's id (empty if the
		//! content container is not ready). Each item is a label the vertical group
		//! arranges, so the list re-flows and its scroll extent updates.
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
		//! the inner content container's widget id (the vertical group)
		String const & getContentId() const { return this->contentId; }

	private:
		String					contentId;		//!< the inner vertical-group container id
		String					viewAtlas;		//!< atlas for item widgets (matches the list)
		uint					itemFont;		//!< glyph index for item labels
		uint					contentZ;		//!< z of the content/clip layer
		unsigned int			itemSerial;		//!< monotone id source for item widgets
		std::vector<String>		itemIds;		//!< item widget ids in list order
	};
}

#endif //__GuiListView_h__26_7_2026__12_30_00__
