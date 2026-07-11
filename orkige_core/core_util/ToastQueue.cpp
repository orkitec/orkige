/********************************************************************
	created:	Saturday 2026/07/11 at 18:00
	filename: 	ToastQueue.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the pure timed-notification queue (@see ToastQueue.h)
*********************************************************************/

#include "core_util/ToastQueue.h"

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	void ToastQueue::enqueue(String const & text, float lifetime, float fade)
	{
		Item item;
		item.text = text;
		item.lifetime = lifetime > 0.0f ? lifetime : 0.0f;
		// the two ramps must fit inside the lifetime; cap each at half of it
		item.fade = std::max(0.0f, std::min(fade, item.lifetime * 0.5f));
		item.elapsed = 0.0f;
		this->mItems.push_back(item);
	}
	//---------------------------------------------------------
	void ToastQueue::update(float dt)
	{
		if(dt < 0.0f)
		{
			return;
		}
		float remaining = dt;
		while(remaining > 0.0f && !this->mItems.empty())
		{
			Item & front = this->mItems.front();
			const float left = front.lifetime - front.elapsed;
			if(remaining < left)
			{
				front.elapsed += remaining;
				remaining = 0.0f;
			}
			else
			{
				// this toast expires; carry the leftover time into the next
				remaining -= left;
				this->mItems.pop_front();
			}
		}
	}
	//---------------------------------------------------------
	void ToastQueue::clear()
	{
		this->mItems.clear();
	}
	//---------------------------------------------------------
	String ToastQueue::activeText() const
	{
		if(this->mItems.empty())
		{
			return String();
		}
		return this->mItems.front().text;
	}
	//---------------------------------------------------------
	float ToastQueue::activeAlpha() const
	{
		if(this->mItems.empty())
		{
			return 0.0f;
		}
		Item const & front = this->mItems.front();
		if(front.fade <= 0.0f)
		{
			return 1.0f;	// no ramp: fully opaque for the whole lifetime
		}
		const float t = front.elapsed;
		if(t < front.fade)
		{
			return t / front.fade;					// fade in
		}
		const float fadeOutStart = front.lifetime - front.fade;
		if(t > fadeOutStart)
		{
			const float k = (front.lifetime - t) / front.fade;	// fade out
			return k < 0.0f ? 0.0f : k;
		}
		return 1.0f;								// hold
	}
	//---------------------------------------------------------
	std::size_t ToastQueue::pending() const
	{
		return this->mItems.empty() ? 0 : this->mItems.size() - 1;
	}
}
