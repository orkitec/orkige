/**************************************************************
	created:	2026/07/10 at 14:00
	filename: 	TraceWriter.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __TraceWriter_h__10_7_2026__14_00_00__
#define __TraceWriter_h__10_7_2026__14_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <utility>
#include <vector>

namespace Orkige
{
	//! @brief the serializer behind the player-side flight recorder: turns
	//! per-frame world SAMPLES and interleaved EVENTS into a bounded JSON-lines
	//! (.jsonl) trace - the temporal-evidence artifact an AI agent reads back
	//! (agents parse text, not pixels).
	//! @remarks Pure data + string work, no engine/renderer types: the player
	//! extracts the object state (positions from the transform, velocity from a
	//! rigid body, view containment from the facade camera) and hands it here.
	//! One JSON object per line so a reader can parse incrementally and a
	//! truncated tail still yields whole lines. The buffer is capped (default
	//! ~2MB); once full, further lines are dropped and save() appends a single
	//! honest truncation marker line. Sample object lists are capped per sample
	//! (a "capped" count records the real total). Numbers print compact
	//! (%.6g) so the trace stays small and legible.
	class ORKIGE_CORE_DLL TraceWriter
	{
	public:
		//! one sampled object. pos is world-space; vel rides only when a rigid
		//! body exists (hasVelocity); visible is -1 (omit - no cheap camera
		//! containment) or 0/1
		struct ObjectSample
		{
			String	id;
			String	name;
			float	pos[3] = { 0.0f, 0.0f, 0.0f };
			bool	hasVelocity = false;
			float	vel[3] = { 0.0f, 0.0f, 0.0f };
			bool	active = true;
			int		visible = -1;
		};

		//! default byte cap for the whole trace (a bound on memory + file size)
		static const size_t DEFAULT_MAX_BYTES;
		//! default per-sample object cap (honest "capped" marker beyond it)
		static const size_t DEFAULT_MAX_OBJECTS;

		TraceWriter(size_t maxBytes = DEFAULT_MAX_BYTES,
			size_t maxObjectsPerSample = DEFAULT_MAX_OBJECTS);

		//! @brief append a SAMPLE line:
		//! {"t","frame","dt","mem"?,"objects":[...]} plus a "capped" total when
		//! the object list overflows the per-sample cap. dt is the last frame's
		//! delta seconds (agents assert on it). memRss is the process resident
		//! set size in bytes at the sample - written as "mem" when >= 0, omitted
		//! otherwise (a platform without a memory query); agents assert "no
		//! unbounded growth" off it.
		void addSample(double t, unsigned long frame, double dt,
			std::vector<ObjectSample> const & objects,
			long long memRss = -1);

		//! @brief append an EVENT line: {"t","frame","event", <fields...>}. The
		//! fields are string key/value pairs (e.g. both object names for a
		//! contact, the message for a warning/script error).
		void addEvent(double t, unsigned long frame, String const & event,
			std::vector<std::pair<String, String>> const & fields);

		//! has the byte cap been hit (subsequent lines were dropped)
		bool truncated() const { return this->mTruncated; }
		//! current trace size in bytes
		size_t byteCount() const { return this->mBuffer.size(); }
		bool empty() const { return this->mBuffer.empty(); }
		//! the accumulated .jsonl text (no trailing truncation marker) - tests
		String const & buffer() const { return this->mBuffer; }

		//! @brief write the trace to a file, appending the truncation marker
		//! line when the cap was hit. false on a write failure.
		bool save(String const & path) const;

	private:
		//! append one ready line (+newline) if it fits the byte budget, else
		//! flag truncated and drop it (a margin is reserved for the marker)
		bool appendLine(String const & line);

		size_t	mMaxBytes;
		size_t	mMaxObjects;
		String	mBuffer;
		bool	mTruncated;
	};
}

#endif //__TraceWriter_h__10_7_2026__14_00_00__
