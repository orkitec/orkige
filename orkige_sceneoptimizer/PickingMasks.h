/**************************************************************
	created:	2010/10/29
	filename: 	PickingMasks.h
	author:		philipp.engelhard
	purpose:	Mask enum for picking is defined here
	copyright:	(c) 2010 kunst-stoff
***************************************************************/
#ifndef __CC_PickingMasks_h__
#define __CC_PickingMasks_h__


namespace PPR
{
	enum PickingMasks
	{
		PICK_DEFAULT	= 1<<0,
		PICK_JUNCTION	= 1<<1,
		PICK_SLOT		= 1<<2,
		PICK_CAR		= 1<<3,
		PICK_SURFACE	= 1<<4,
		PICK_PLAYER		= 1<<5,
	};
}

#endif //__CC_PickingMasks_h__