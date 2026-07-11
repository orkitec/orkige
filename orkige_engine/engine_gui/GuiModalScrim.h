/********************************************************************
	created:	Saturday 2026/07/11 at 18:30
	filename: 	GuiModalScrim.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiModalScrim_h__11_7_2026__18_30_00__
#define __GuiModalScrim_h__11_7_2026__18_30_00__

#include "engine_gui/GuiDecorWidget.h"

namespace Orkige
{
	//! @brief the input-consuming backdrop of a modal dialog: a full-window
	//! solid fill that, on every cursor event, calls
	//! GuiManager::cancelCurrentInputUpdate() so nothing on a LOWER z layer
	//! reacts. The dialog's own widgets sit one layer ABOVE the scrim, so the
	//! highest-z-first dispatch reaches them before the scrim cancels the rest.
	//! With `lightDismiss` a tap on the scrim itself also requests the modal be
	//! dismissed (an outside-tap-to-close dropdown / sheet); a plain modal keeps
	//! the dialog up until a button dismisses it. The scrim never destroys
	//! anything synchronously - it requests a deferred dismissal so the manager
	//! tears the modal down at the next frame boundary (never mid-dispatch).
	class ORKIGE_ENGINE_DLL GuiModalScrim : public GuiDecorWidget
	{
		OOBJECT(GuiModalScrim, GuiDecorWidget);
		//--- Variables ---------------------------------------------
	protected:
		String	modalId;		//!< the modal this scrim belongs to
		bool	lightDismiss;	//!< a tap on the scrim requests dismissal
		//--- Methods -----------------------------------------------
	public:
		//! @param id widget id; @param modalId the owning modal's id; @param z
		//! the scrim's layer; @param tint the backdrop colour (typically a
		//! semi-transparent black); @param lightDismiss tap-to-close
		GuiModalScrim(String const & id, String const & modalId,
			String const & atlas, uint z, Color const & tint, bool lightDismiss);
		virtual ~GuiModalScrim();

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);
	protected:
	private:
	};
}

#endif //__GuiModalScrim_h__11_7_2026__18_30_00__
