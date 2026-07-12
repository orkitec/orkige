/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	ProfileManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "core_debug/ProfileManager.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace Orkige
{
	namespace ProfileManager
	{
		namespace
		{
			using Clock = std::chrono::steady_clock;

			//! one scope in a thread's call tree; allocated on the scope's
			//! first visit, reused for the thread's lifetime (steady state
			//! opens/closes scopes without ever allocating)
			struct Node
			{
				const char *	mName;			//!< static string (pointer-compared first)
				Node *			mParent;
				Node *			mFirstChild;
				Node *			mLastChild;		//!< children append in creation order
				Node *			mSibling;
				int				mRecursion;		//!< direct recursion depth (time counted once)
				Clock::time_point	mStart;		//!< outermost entry of the running call
				std::uint64_t	mFrameNs;		//!< accumulating, the running frame
				unsigned int	mFrameCalls;
				std::uint64_t	mLastNs;		//!< the last completed frame
				unsigned int	mLastCalls;
				std::uint64_t	mMaxNs;			//!< worst completed frame since reset

				explicit Node(const char * name, Node * parent)
					: mName(name), mParent(parent), mFirstChild(NULL),
					mLastChild(NULL), mSibling(NULL), mRecursion(0),
					mFrameNs(0), mFrameCalls(0), mLastNs(0), mLastCalls(0),
					mMaxNs(0)
				{
				}
			};

			struct ThreadState;

			//! the registry of live thread trees; function-local static so
			//! any initialization order works
			struct Registry
			{
				std::mutex					mMutex;
				std::vector<ThreadState *>	mThreads;	//!< insertion order (no meaning beyond stability)
				int							mNextLabel = 0;
			};
			Registry & registry()
			{
				static Registry sRegistry;
				return sRegistry;
			}

			//! per-thread tree + cursor; registers itself on the thread's
			//! first scope, unregisters (and frees its nodes) at thread exit,
			//! so a snapshot only ever sees live threads' data
			struct ThreadState
			{
				Node	mRoot;
				Node *	mCurrent;
				char	mLabel[24];

				ThreadState() : mRoot(NULL, NULL), mCurrent(&mRoot)
				{
					Registry & reg = registry();
					std::lock_guard<std::mutex> lock(reg.mMutex);
					// every thread gets a neutral label; which tree is "the
					// frame's own" is decided by WHO CALLS snapshot(), never
					// by registration order - a worker's first scope may
					// legally run before the loop thread's
					std::snprintf(mLabel, sizeof(mLabel), "thread-%d",
						++reg.mNextLabel);
					reg.mThreads.push_back(this);
				}

				~ThreadState()
				{
					Registry & reg = registry();
					std::lock_guard<std::mutex> lock(reg.mMutex);
					for (std::size_t i = 0; i < reg.mThreads.size(); ++i)
					{
						if (reg.mThreads[i] == this)
						{
							reg.mThreads.erase(reg.mThreads.begin() + i);
							break;
						}
					}
					freeChildren(&mRoot);
				}

				static void freeChildren(Node * node)
				{
					Node * child = node->mFirstChild;
					while (child != NULL)
					{
						Node * next = child->mSibling;
						freeChildren(child);
						delete child;
						child = next;
					}
					node->mFirstChild = NULL;
					node->mLastChild = NULL;
				}
			};

			thread_local ThreadState tState;

#if defined(NDEBUG)
			std::atomic<bool> sEnabled(false);
#else
			std::atomic<bool> sEnabled(true);
#endif
			//! frame duration is measured whether or not scopes are enabled
			Clock::time_point	sLastFrameEnd;
			double				sLastFrameMs = 0.0;
			unsigned long		sFrames = 0;

			//! find (pointer compare first, text compare second) or create
			//! the child scope; the only allocation in the profiler
			Node * findOrAddChild(Node * parent, const char * name)
			{
				for (Node * child = parent->mFirstChild; child != NULL;
					child = child->mSibling)
				{
					if (child->mName == name ||
						std::strcmp(child->mName, name) == 0)
					{
						return child;
					}
				}
				Node * node = new Node(name, parent);
				if (parent->mLastChild != NULL)
				{
					parent->mLastChild->mSibling = node;
				}
				else
				{
					parent->mFirstChild = node;
				}
				parent->mLastChild = node;
				return node;
			}

			void foldFrame(Node * node)
			{
				for (Node * child = node->mFirstChild; child != NULL;
					child = child->mSibling)
				{
					child->mLastNs = child->mFrameNs;
					child->mLastCalls = child->mFrameCalls;
					if (child->mLastNs > child->mMaxNs)
					{
						child->mMaxNs = child->mLastNs;
					}
					child->mFrameNs = 0;
					child->mFrameCalls = 0;
					foldFrame(child);
				}
			}

			void resetNode(Node * node)
			{
				for (Node * child = node->mFirstChild; child != NULL;
					child = child->mSibling)
				{
					resetNode(child);
				}
				node->mRecursion = 0;
				node->mFrameNs = 0;
				node->mFrameCalls = 0;
				node->mLastNs = 0;
				node->mLastCalls = 0;
				node->mMaxNs = 0;
			}

			double toMilliseconds(std::uint64_t nanoseconds)
			{
				return static_cast<double>(nanoseconds) / 1000000.0;
			}

			void appendSnapshot(Node * node, int depth,
				std::vector<SnapshotNode> & out)
			{
				for (Node * child = node->mFirstChild; child != NULL;
					child = child->mSibling)
				{
					SnapshotNode entry;
					entry.name = child->mName;
					entry.depth = depth;
					entry.calls = child->mLastCalls;
					entry.milliseconds = toMilliseconds(child->mLastNs);
					entry.maxMilliseconds = toMilliseconds(child->mMaxNs);
					out.push_back(entry);
					appendSnapshot(child, depth + 1, out);
				}
			}

			//! did this thread record anything in the last completed frame?
			bool hasLastFrameData(Node * node)
			{
				for (Node * child = node->mFirstChild; child != NULL;
					child = child->mSibling)
				{
					if (child->mLastCalls > 0 || hasLastFrameData(child))
					{
						return true;
					}
				}
				return false;
			}
		}
		//---------------------------------------------------------
		void setEnabled(bool enabled)
		{
			sEnabled.store(enabled, std::memory_order_relaxed);
		}
		//---------------------------------------------------------
		bool isEnabled()
		{
			return sEnabled.load(std::memory_order_relaxed);
		}
		//---------------------------------------------------------
		bool beginScope(const char * name)
		{
			if (!sEnabled.load(std::memory_order_relaxed))
			{
				return false;
			}
			ThreadState & state = tState;
			Node * current = state.mCurrent;
			// direct recursion re-enters the OPEN node instead of descending
			const bool sameAsCurrent = current != &state.mRoot &&
				(current->mName == name ||
					std::strcmp(current->mName, name) == 0);
			if (!sameAsCurrent)
			{
				current = findOrAddChild(current, name);
				state.mCurrent = current;
			}
			++current->mFrameCalls;
			if (current->mRecursion++ == 0)
			{
				current->mStart = Clock::now();
			}
			return true;
		}
		//---------------------------------------------------------
		void endScope()
		{
			ThreadState & state = tState;
			Node * current = state.mCurrent;
			if (current == &state.mRoot)
			{
				return;	// unbalanced close - ignore rather than corrupt
			}
			if (--current->mRecursion == 0)
			{
				current->mFrameNs += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						Clock::now() - current->mStart).count());
				state.mCurrent = current->mParent;
			}
		}
		//---------------------------------------------------------
		void endFrame()
		{
			const Clock::time_point now = Clock::now();
			if (sFrames > 0)
			{
				sLastFrameMs = std::chrono::duration_cast<
					std::chrono::duration<double, std::milli>>(
						now - sLastFrameEnd).count();
			}
			sLastFrameEnd = now;
			++sFrames;
			if (!sEnabled.load(std::memory_order_relaxed))
			{
				return;
			}
			Registry & reg = registry();
			std::lock_guard<std::mutex> lock(reg.mMutex);
			for (ThreadState * state : reg.mThreads)
			{
				foldFrame(&state->mRoot);
			}
		}
		//---------------------------------------------------------
		double lastFrameMilliseconds()
		{
			return sLastFrameMs;
		}
		//---------------------------------------------------------
		unsigned long framesSampled()
		{
			return sFrames;
		}
		//---------------------------------------------------------
		void snapshot(std::vector<SnapshotNode> & out)
		{
			out.clear();
			// the caller IS the frame's own thread (the loop calls
			// snapshot); touching tState here also registers a
			// never-profiled caller so the identity below always resolves
			ThreadState * const callerState = &tState;
			Registry & reg = registry();
			std::lock_guard<std::mutex> lock(reg.mMutex);
			// the calling thread's tree first, its scopes at depth 0
			appendSnapshot(&callerState->mRoot, 0, out);
			// other threads under a labeled row so their scopes stay
			// distinguishable from the frame's own phases
			for (ThreadState * state : reg.mThreads)
			{
				if (state == callerState ||
					!hasLastFrameData(&state->mRoot))
				{
					continue;
				}
				SnapshotNode row;
				row.name = state->mLabel;
				row.depth = 0;
				row.calls = 0;
				row.milliseconds = 0.0;
				row.maxMilliseconds = 0.0;
				out.push_back(row);
				appendSnapshot(&state->mRoot, 1, out);
			}
		}
		//---------------------------------------------------------
		void reset()
		{
			Registry & reg = registry();
			std::lock_guard<std::mutex> lock(reg.mMutex);
			for (ThreadState * state : reg.mThreads)
			{
				resetNode(&state->mRoot);
				state->mCurrent = &state->mRoot;
			}
			sLastFrameMs = 0.0;
			sFrames = 0;
		}
	}
}
