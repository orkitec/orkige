/********************************************************************
	created:	Monday 2010/08/30 at 14:05
	filename: 	KeyEventData.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_input/KeyEventData.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(KeyEventData)
		OVAR(text)
		OVAR(key)
		// the gameplay-relevant KeyCode constants for scripts, reachable as
		// KeyEventData.KeyCode.KC_* (Lua convention: local KC =
		// KeyEventData.KeyCode); the exotic rest of the OIS-era table joins
		// when a script actually needs it
		OENUM_START(KeyCode)
			OENUM_VALUE(KC_A)
			OENUM_VALUE(KC_B)
			OENUM_VALUE(KC_C)
			OENUM_VALUE(KC_D)
			OENUM_VALUE(KC_E)
			OENUM_VALUE(KC_F)
			OENUM_VALUE(KC_G)
			OENUM_VALUE(KC_H)
			OENUM_VALUE(KC_I)
			OENUM_VALUE(KC_J)
			OENUM_VALUE(KC_K)
			OENUM_VALUE(KC_L)
			OENUM_VALUE(KC_M)
			OENUM_VALUE(KC_N)
			OENUM_VALUE(KC_O)
			OENUM_VALUE(KC_P)
			OENUM_VALUE(KC_Q)
			OENUM_VALUE(KC_R)
			OENUM_VALUE(KC_S)
			OENUM_VALUE(KC_T)
			OENUM_VALUE(KC_U)
			OENUM_VALUE(KC_V)
			OENUM_VALUE(KC_W)
			OENUM_VALUE(KC_X)
			OENUM_VALUE(KC_Y)
			OENUM_VALUE(KC_Z)
			OENUM_VALUE(KC_0)
			OENUM_VALUE(KC_1)
			OENUM_VALUE(KC_2)
			OENUM_VALUE(KC_3)
			OENUM_VALUE(KC_4)
			OENUM_VALUE(KC_5)
			OENUM_VALUE(KC_6)
			OENUM_VALUE(KC_7)
			OENUM_VALUE(KC_8)
			OENUM_VALUE(KC_9)
			OENUM_VALUE(KC_LEFT)
			OENUM_VALUE(KC_RIGHT)
			OENUM_VALUE(KC_UP)
			OENUM_VALUE(KC_DOWN)
			OENUM_VALUE(KC_SPACE)
			OENUM_VALUE(KC_ESCAPE)
			OENUM_VALUE(KC_RETURN)
			OENUM_VALUE(KC_TAB)
			OENUM_VALUE(KC_LSHIFT)
			OENUM_VALUE(KC_RSHIFT)
			OENUM_VALUE(KC_LCONTROL)
			OENUM_VALUE(KC_RCONTROL)
		OENUM_END
	OOBJECT_END
}
