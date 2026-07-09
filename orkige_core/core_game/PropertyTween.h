/**************************************************************
	created:	2026/07/09 at 18:00
	filename: 	PropertyTween.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PropertyTween_h__9_7_2026__18_00_00__
#define __PropertyTween_h__9_7_2026__18_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"
#include "core_base/PropertyValue.h"
#include "core_tween/TweenManager.h"
#include "core_tween/EaseLibrary.h"

namespace Orkige
{
	/** \addtogroup Game
	*  @{ */

	//! @brief the DECLARATIVE property-path tween: a tween that interpolates a
	//! REFLECTED property by path
	//! (objectId, componentType, propertyName) instead of through a hand-written
	//! closure. It resolves the property's PropertyDesc through the ONE registry
	//! (getComponentSchema) at bind time, lowers the current and target values to
	//! the TweenManager's float channels, and each tick writes the interpolated
	//! value back through the reflected setter - so a Transform position, a Sprite
	//! tint or any numeric reflected field animates by NAME with no bespoke tween
	//! function. Reap + teardown follow the ordinary TweenManager rules (keyed by
	//! object id). Only the NUMERIC-interpolatable kinds are accepted - Float,
	//! Int, Vec3, Color; every other kind (Bool/String/Enum/Quat/AssetRef/
	//! ObjectRef) is rejected with an honest error (Quat has no easy channel
	//! interpolation - it would need slerp the linear channel model cannot do).
	//! This is core (scripting-free); the Lua `tween.property` binding is a thin
	//! face on top (engine_gocomponent/ScriptComponent).
	class ORKIGE_CORE_DLL PropertyTween
	{
		//--- Methods -----------------------------------------
	public:
		//! @brief is this property kind numeric-interpolatable (tween.property
		//! accepts it)? True for Float/Int/Vec3/Color, false for everything else.
		static bool isInterpolatable(PropertyKind kind);
		//! @brief the number of float channels an interpolatable kind occupies
		//! (Float/Int -> 1, Vec3 -> 3, Color -> 4); 0 when not interpolatable.
		static int channelCount(PropertyKind kind);
		//! @brief lower a value to float channels; returns the channel count
		//! (0 when the value's kind is not interpolatable, channels untouched).
		static int toChannels(PropertyValue const & value, float * outChannels);
		//! @brief rebuild a PropertyValue of the given kind from float channels
		//! (the inverse of toChannels; Int rounds). An un-interpolatable kind
		//! yields a default value of that kind.
		static PropertyValue fromChannels(PropertyKind kind,
			float const * channels, int count);

		//! @brief start a property-path tween against the singletons
		//! (GameObjectManager + TweenManager). Resolves the property, reads its
		//! current value as the tween start, parses targetText into a value of the
		//! property's kind as the tween end, and animates it over duration with
		//! ease; onComplete fires once on arrival. Returns the TweenId, or 0 with
		//! *outError set (and nothing started) when there is no runtime ticking
		//! tweens, the object/component/property is missing, the property is
		//! read-only, its kind is not interpolatable, or targetText does not parse.
		//! The tween is keyed by objectId, so it reaps when the object dies and
		//! clears on the scene-teardown hook like every other tween.
		static TweenManager::TweenId start(String const & objectId,
			String const & componentType, String const & propertyName,
			String const & targetText, float duration, Ease::Function ease,
			TweenManager::CompleteFunction const & onComplete, float delay,
			String * outError);
	};

	/** @} End of "addtogroup Game"*/
}

#endif //__PropertyTween_h__9_7_2026__18_00_00__
