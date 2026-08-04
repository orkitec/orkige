/**************************************************************
	created:	2026/08/03 at 09:00
	filename: 	InputDevices.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __InputDevices_h__3_8_2026__09_00_00__
#define __InputDevices_h__3_8_2026__09_00_00__

#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	//! @brief the phase ONE tracked finger is in for the current frame.
	//! @remarks A finger is reported for exactly one frame in TP_BEGAN, for
	//! every frame it stays down in TP_MOVED (whether it moved or not) and for
	//! exactly one frame in TP_ENDED - after which its slot is free again. A
	//! tap that goes down and up inside a single frame is therefore still seen
	//! as TP_BEGAN and then TP_ENDED, never swallowed.
	enum TouchPhase
	{
		TP_NONE = 0,	//!< nothing is tracked at this index
		TP_BEGAN,		//!< the finger went down this frame
		TP_MOVED,		//!< the finger is held (this frame is not its first)
		TP_ENDED		//!< the finger came up (or was cancelled) this frame
	};

	//! @brief one tracked finger as the once-per-frame input snapshot reports
	//! it. Positions are WINDOW PIXELS of the game's drawable - the space the
	//! gui hit-tests widgets in and the injected-input grammar spells.
	struct TouchPoint
	{
		int			id = -1;			//!< stable while the finger is down
		float		x = 0.0f;			//!< window pixels
		float		y = 0.0f;			//!< window pixels
		float		deltaX = 0.0f;		//!< movement since the previous frame
		float		deltaY = 0.0f;		//!< movement since the previous frame
		TouchPhase	phase = TP_NONE;
	};

	//! @brief the CONTROLLER vocabulary: the standard-layout gamepad every
	//! platform's driver maps its physical pad onto.
	//! @remarks Names are POSITIONAL (south/east/west/north), not lettered:
	//! the same physical bottom button is "A" on one vendor's pad and "B" on
	//! another's, so a binding authored by position survives the swap. The
	//! engine keeps its own enum rather than the backend's so InputAction and
	//! the action file stay free of the windowing library.
	namespace Gamepad
	{
		//! a face/shoulder/dpad button of the standard layout
		enum Button
		{
			GB_SOUTH = 0,		//!< bottom face button (the "jump" button)
			GB_EAST,			//!< right face button
			GB_WEST,			//!< left face button
			GB_NORTH,			//!< top face button
			GB_BACK,			//!< back / select / view
			GB_GUIDE,			//!< the system button
			GB_START,			//!< start / menu
			GB_LEFTSTICK,		//!< left stick pressed in
			GB_RIGHTSTICK,		//!< right stick pressed in
			GB_LEFTSHOULDER,	//!< left bumper
			GB_RIGHTSHOULDER,	//!< right bumper
			GB_DPAD_UP,
			GB_DPAD_DOWN,
			GB_DPAD_LEFT,
			GB_DPAD_RIGHT,
			GB_COUNT			//!< number of buttons (array bound, not a button)
		};

		//! an analog axis of the standard layout. Sticks read -1..+1 with
		//! +x right and +y DOWN (the screen convention the key axes already
		//! use: W/UP is negative y); triggers read 0..+1.
		enum Axis
		{
			GA_LEFTX = 0,
			GA_LEFTY,
			GA_RIGHTX,
			GA_RIGHTY,
			GA_LEFTTRIGGER,		//!< 0..1
			GA_RIGHTTRIGGER,	//!< 0..1
			GA_COUNT			//!< number of axes (array bound, not an axis)
		};
	}
	//---------------------------------------------------------
}

#endif //__InputDevices_h__3_8_2026__09_00_00__
