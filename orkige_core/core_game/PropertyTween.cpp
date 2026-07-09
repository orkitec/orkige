/**************************************************************
	created:	2026/07/09 at 18:00
	filename: 	PropertyTween.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/PropertyTween.h"
#include "core_game/GameObjectManager.h"
#include "core_game/GameObject.h"
#include "core_game/GameObjectComponent.h"
#include "core_base/PropertySchema.h"

#include <cmath>

namespace Orkige
{
	namespace
	{
		//! resolve a live component by (objectId, componentType); NULL when the
		//! object or component is gone - the shared bind-time + per-tick lookup
		GameObjectComponent* resolveComponent(String const & objectId,
			String const & componentType)
		{
			if(GameObjectManager::getSingletonPtr() == NULL)
			{
				return NULL;
			}
			optr<GameObject> gameObject =
				GameObjectManager::getSingleton().getGameObject(objectId).lock();
			if(!gameObject)
			{
				return NULL;
			}
			const TypeInfo type(componentType);
			if(!gameObject->hasComponent(type))
			{
				return NULL;
			}
			return gameObject->getComponentPtr(type);
		}
	}
	//---------------------------------------------------------
	bool PropertyTween::isInterpolatable(PropertyKind kind)
	{
		return PropertyTween::channelCount(kind) > 0;
	}
	//---------------------------------------------------------
	int PropertyTween::channelCount(PropertyKind kind)
	{
		switch(kind)
		{
		case PropertyKind::Float:	return 1;
		case PropertyKind::Int:		return 1;
		case PropertyKind::Vec3:	return 3;
		case PropertyKind::Color:	return 4;
		default:					return 0;	// Bool/String/Enum/Quat/refs
		}
	}
	//---------------------------------------------------------
	int PropertyTween::toChannels(PropertyValue const & value, float * outChannels)
	{
		switch(value.kind())
		{
		case PropertyKind::Float:
		case PropertyKind::Int:
			outChannels[0] = static_cast<float>(value.asFloat());
			return 1;
		case PropertyKind::Vec3:
		{
			PropVec3 const vec = value.asVec3();
			outChannels[0] = vec.x;
			outChannels[1] = vec.y;
			outChannels[2] = vec.z;
			return 3;
		}
		case PropertyKind::Color:
		{
			PropColor const color = value.asColor();
			outChannels[0] = color.r;
			outChannels[1] = color.g;
			outChannels[2] = color.b;
			outChannels[3] = color.a;
			return 4;
		}
		default:
			return 0;
		}
	}
	//---------------------------------------------------------
	PropertyValue PropertyTween::fromChannels(PropertyKind kind,
		float const * channels, int count)
	{
		switch(kind)
		{
		case PropertyKind::Float:
			return PropertyValue::makeFloat(count > 0 ? channels[0] : 0.0f);
		case PropertyKind::Int:
			return PropertyValue::makeInt(count > 0 ?
				static_cast<long long>(std::lround(channels[0])) : 0);
		case PropertyKind::Vec3:
		{
			PropVec3 vec;
			if(count >= 3) { vec.x = channels[0]; vec.y = channels[1]; vec.z = channels[2]; }
			return PropertyValue::makeVec3(vec);
		}
		case PropertyKind::Color:
		{
			PropColor color;
			if(count >= 4) { color.r = channels[0]; color.g = channels[1]; color.b = channels[2]; color.a = channels[3]; }
			return PropertyValue::makeColor(color);
		}
		default:
			return PropertyValue::makeFloat(0.0);
		}
	}
	//---------------------------------------------------------
	TweenManager::TweenId PropertyTween::start(String const & objectId,
		String const & componentType, String const & propertyName,
		String const & targetText, float duration, Ease::Function ease,
		TweenManager::CompleteFunction const & onComplete, float delay,
		String * outError)
	{
		auto fail = [outError](String const & message) -> TweenManager::TweenId
		{
			if(outError)
			{
				*outError = message;
			}
			return 0;
		};
		TweenManager* tweens = TweenManager::getSingletonPtr();
		if(!tweens)
		{
			return fail("no TweenManager - this runtime does not tick tweens "
				"(editor edit mode?)");
		}
		GameObjectComponent* component = resolveComponent(objectId, componentType);
		if(!component)
		{
			return fail("no '" + componentType + "' on object '" + objectId + "'");
		}
		// resolve the property through the ONE registry (static UNION dynamic)
		const PropertySchema schema = getComponentSchema(*component);
		PropertyDesc const * desc = schema.find(propertyName);
		if(!desc)
		{
			return fail("'" + componentType + "' has no property '" +
				propertyName + "' on object '" + objectId + "'");
		}
		if(desc->isReadOnly())
		{
			return fail("property '" + propertyName + "' on '" + componentType +
				"' is read-only");
		}
		if(!PropertyTween::isInterpolatable(desc->kind))
		{
			return fail("property '" + propertyName + "' on '" + componentType +
				"' is not numeric-interpolatable (only Float/Int/Vec3/Color tween)");
		}
		const PropertyKind kind = desc->kind;
		// the tween START is the property's current value
		const PropertyValue current = desc->get(static_cast<void const *>(component));
		float fromChannels[TweenManager::MAX_CHANNELS] = { 0.0f };
		const int channelCount = PropertyTween::toChannels(current, fromChannels);
		// the tween END is targetText parsed into a value of the property's kind
		PropertyValue target = current;		// keeps the kind
		String parseError;
		if(!target.fromString(targetText, &parseError))
		{
			return fail("target '" + targetText + "' does not parse for property '"
				+ propertyName + "': " + parseError);
		}
		float toChannels[TweenManager::MAX_CHANNELS] = { 0.0f };
		PropertyTween::toChannels(target, toChannels);
		// each tick: re-resolve the property by path (never capture the
		// component - it dangles when the object dies; the id-reap retires the
		// tween) and write the interpolated value through the reflected setter
		TweenManager::UpdateFunction apply =
			[objectId, componentType, propertyName, kind]
			(float const * values, int count) -> bool
		{
			GameObjectComponent* target = resolveComponent(objectId, componentType);
			if(!target)
			{
				return true;	// gone this frame; the id-reap cleans up
			}
			const PropertySchema schema = getComponentSchema(*target);
			PropertyDesc const * desc = schema.find(propertyName);
			if(!desc || desc->isReadOnly())
			{
				return true;	// the property vanished/locked; no-op
			}
			desc->set(static_cast<void *>(target),
				PropertyTween::fromChannels(kind, values, count));
			return true;
		};
		return tweens->startTween(fromChannels, toChannels, channelCount,
			duration, ease, apply, onComplete, delay, objectId);
	}
	//---------------------------------------------------------
}
