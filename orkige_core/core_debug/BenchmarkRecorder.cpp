/**************************************************************
	created:	2026/07/12 at 12:00
	filename: 	BenchmarkRecorder.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debug/BenchmarkRecorder.h"

#include "core_debug/MemoryManager.h"
#include "core_debug/MemorySampler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Orkige
{
	IMPL_OSINGLETON(BenchmarkRecorder);

	namespace
	{
		//! append a JSON string literal (quoted, escaped) - scene/project names
		//! and device strings may hold quotes or control characters
		void appendJsonString(String & out, String const & value)
		{
			out += '"';
			for (char const c : value)
			{
				switch (c)
				{
				case '"':	out += "\\\"";	break;
				case '\\':	out += "\\\\";	break;
				case '\b':	out += "\\b";	break;
				case '\f':	out += "\\f";	break;
				case '\n':	out += "\\n";	break;
				case '\r':	out += "\\r";	break;
				case '\t':	out += "\\t";	break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char buffer[8];
						std::snprintf(buffer, sizeof(buffer), "\\u%04x",
							static_cast<unsigned>(static_cast<unsigned char>(c)));
						out += buffer;
					}
					else
					{
						out += c;	// UTF-8 bytes pass through
					}
					break;
				}
			}
			out += '"';
		}
		//---------------------------------------------------------
		//! a JSON number with a fixed 3-decimal form (compact, stable across
		//! platforms - the artifact is diffed and compared across runs)
		String jsonNumber(double value)
		{
			char buffer[48];
			std::snprintf(buffer, sizeof(buffer), "%.3f", value);
			return String(buffer);
		}
		//---------------------------------------------------------
		String jsonInt(long long value)
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%lld", value);
			return String(buffer);
		}
	}

	//=========================================================
	// BenchmarkSceneStats - the pure aggregator
	//=========================================================
	void BenchmarkSceneStats::reset()
	{
		this->mFrameMs.clear();
		this->mAllocSum = 0.0;
		this->mAllocPeak = 0;
		this->mRssPeak = 0;
		this->mTrisSum = 0.0;
		this->mBatchesSum = 0.0;
		this->mTexMemSum = 0.0;
		this->mPhases.clear();
	}
	//---------------------------------------------------------
	void BenchmarkSceneStats::addFrame(BenchmarkFrameSample const & sample)
	{
		this->mFrameMs.push_back(sample.frameMs);
		this->mAllocSum += static_cast<double>(sample.allocCount);
		this->mAllocPeak = std::max(this->mAllocPeak, sample.allocCount);
		this->mRssPeak = std::max(this->mRssPeak, sample.rssBytes);
		this->mTrisSum += static_cast<double>(sample.triangles);
		this->mBatchesSum += static_cast<double>(sample.batches);
		this->mTexMemSum += static_cast<double>(sample.texMemMB);
		for (std::pair<String, double> const & phase : sample.phasesMs)
		{
			std::pair<double, std::size_t> & accum = this->mPhases[phase.first];
			accum.first += phase.second;
			accum.second += 1;
		}
	}
	//---------------------------------------------------------
	double BenchmarkSceneStats::totalSeconds() const
	{
		double sum = 0.0;
		for (double ms : this->mFrameMs)
		{
			sum += ms;
		}
		return sum / 1000.0;
	}
	//---------------------------------------------------------
	double BenchmarkSceneStats::minFrameMs() const
	{
		if (this->mFrameMs.empty())
		{
			return 0.0;
		}
		return *std::min_element(this->mFrameMs.begin(), this->mFrameMs.end());
	}
	//---------------------------------------------------------
	double BenchmarkSceneStats::maxFrameMs() const
	{
		if (this->mFrameMs.empty())
		{
			return 0.0;
		}
		return *std::max_element(this->mFrameMs.begin(), this->mFrameMs.end());
	}
	//---------------------------------------------------------
	double BenchmarkSceneStats::avgFrameMs() const
	{
		if (this->mFrameMs.empty())
		{
			return 0.0;
		}
		double sum = 0.0;
		for (double ms : this->mFrameMs)
		{
			sum += ms;
		}
		return sum / static_cast<double>(this->mFrameMs.size());
	}
	//---------------------------------------------------------
	double BenchmarkSceneStats::percentileFrameMs(double pct) const
	{
		if (this->mFrameMs.empty())
		{
			return 0.0;
		}
		std::vector<double> sorted = this->mFrameMs;
		std::sort(sorted.begin(), sorted.end());
		const std::size_t n = sorted.size();
		// integer nearest-rank: index = floor(p * n / 100), clamped. Integer
		// math keeps the result a real observed sample with no FP fragility.
		long p = std::lround(pct);
		if (p < 0)
		{
			p = 0;
		}
		if (p > 100)
		{
			p = 100;
		}
		std::size_t index = (static_cast<std::size_t>(p) * n) / 100;
		if (index >= n)
		{
			index = n - 1;
		}
		return sorted[index];
	}
	//---------------------------------------------------------
	double BenchmarkSceneStats::avgFps() const
	{
		const double average = this->avgFrameMs();
		return average > 0.0 ? 1000.0 / average : 0.0;
	}
	//---------------------------------------------------------
	double BenchmarkSceneStats::allocPerFrameAvg() const
	{
		if (this->mFrameMs.empty())
		{
			return 0.0;
		}
		return this->mAllocSum / static_cast<double>(this->mFrameMs.size());
	}
	//---------------------------------------------------------
	std::size_t BenchmarkSceneStats::allocPeakFrame() const
	{
		return this->mAllocPeak;
	}
	//---------------------------------------------------------
	std::size_t BenchmarkSceneStats::rssPeakBytes() const
	{
		return this->mRssPeak;
	}
	//---------------------------------------------------------
	double BenchmarkSceneStats::trianglesAvg() const
	{
		if (this->mFrameMs.empty())
		{
			return 0.0;
		}
		return this->mTrisSum / static_cast<double>(this->mFrameMs.size());
	}
	//---------------------------------------------------------
	double BenchmarkSceneStats::batchesAvg() const
	{
		if (this->mFrameMs.empty())
		{
			return 0.0;
		}
		return this->mBatchesSum / static_cast<double>(this->mFrameMs.size());
	}
	//---------------------------------------------------------
	double BenchmarkSceneStats::texMemMBAvg() const
	{
		if (this->mFrameMs.empty())
		{
			return 0.0;
		}
		return this->mTexMemSum / static_cast<double>(this->mFrameMs.size());
	}
	//---------------------------------------------------------
	std::vector<std::pair<String, double>>
		BenchmarkSceneStats::phaseMeansMs() const
	{
		// std::map iterates in sorted key order, so the result is deterministic
		std::vector<std::pair<String, double>> out;
		out.reserve(this->mPhases.size());
		for (std::map<String, std::pair<double, std::size_t>>::value_type const &
			entry : this->mPhases)
		{
			const double mean = entry.second.second > 0
				? entry.second.first / static_cast<double>(entry.second.second)
				: 0.0;
			out.emplace_back(entry.first, mean);
		}
		return out;
	}

	//=========================================================
	// BenchmarkRecorder - the file-writing lifecycle
	//=========================================================
	BenchmarkRecorder::BenchmarkRecorder()
	{
	}
	//---------------------------------------------------------
	BenchmarkRecorder::~BenchmarkRecorder()
	{
		// a run that is torn down without an explicit finish() is aborted: still
		// close the open scene and cap the file so partial results parse (a
		// clean run already wrote its summary, so finish() no-ops here)
		if (this->mStream.is_open())
		{
			this->finish(true);
			this->mStream.flush();
			this->mStream.close();
		}
	}
	//---------------------------------------------------------
	void BenchmarkRecorder::setMeta(BenchmarkMeta const & meta)
	{
		this->mMeta = meta;
	}
	//---------------------------------------------------------
	void BenchmarkRecorder::setFile(String const & path)
	{
		if (this->mStream.is_open())
		{
			this->mStream.close();
		}
		this->mFile = path;
		this->mMetaWritten = false;
		this->mFinished = false;
		this->mSceneOpen = false;
		this->mScenesWritten = 0;
		this->mTotalSeconds = 0.0;
		this->mScene.reset();
		if (path.empty())
		{
			return;
		}
		// a stamped file name per run - no rotation needed (each run is fresh)
		this->mStream.open(this->mFile.c_str(), std::ios::binary | std::ios::trunc);
	}
	//---------------------------------------------------------
	void BenchmarkRecorder::writeLine(String const & line)
	{
		if (!this->mStream.is_open())
		{
			return;
		}
		this->mStream.write(line.data(),
			static_cast<std::streamsize>(line.size()));
		this->mStream.put('\n');
		this->mStream.flush();	// on disk before we return: a crash keeps this line
	}
	//---------------------------------------------------------
	void BenchmarkRecorder::writeMetaLineIfNeeded()
	{
		if (this->mMetaWritten || !this->mStream.is_open())
		{
			return;
		}
		String line = "{\"type\":\"meta\",\"schema\":1";
		if (!this->mMeta.utc.empty())
		{
			line += ",\"utc\":";	appendJsonString(line, this->mMeta.utc);
		}
		line += ",\"engineSha\":";	appendJsonString(line, this->mMeta.engineSha);
		line += ",\"flavor\":";		appendJsonString(line, this->mMeta.flavor);
		line += ",\"renderSystem\":";
		appendJsonString(line, this->mMeta.renderSystem);
		line += ",\"build\":";		appendJsonString(line, this->mMeta.build);
		line += ",\"platform\":";	appendJsonString(line, this->mMeta.platform);
		line += ",\"device\":{\"model\":";
		appendJsonString(line, this->mMeta.deviceModel);
		line += ",\"os\":";			appendJsonString(line, this->mMeta.deviceOs);
		line += ",\"gpu\":";		appendJsonString(line, this->mMeta.deviceGpu);
		line += "}";
		line += ",\"scenario\":";	appendJsonString(line, this->mMeta.scenario);
		line += ",\"project\":";	appendJsonString(line, this->mMeta.project);
		line += "}";
		this->writeLine(line);
		this->mMetaWritten = true;
	}
	//---------------------------------------------------------
	void BenchmarkRecorder::beginScene(String const & name)
	{
		if (!this->isArmed())
		{
			return;
		}
		this->writeMetaLineIfNeeded();
		// close any open scene first so begin composes with a level switch: an
		// explicit Lua begin renames/restarts the current aggregation
		if (this->mSceneOpen)
		{
			this->endScene();
		}
		this->mScene.reset();
		this->mSceneName = name;
		this->mSceneOpen = true;
	}
	//---------------------------------------------------------
	void BenchmarkRecorder::endScene()
	{
		if (!this->isArmed() || !this->mSceneOpen)
		{
			return;
		}
		this->mSceneOpen = false;
		// an empty scene (no frames sampled) is not worth a record
		if (this->mScene.frames() == 0)
		{
			return;
		}
		this->writeSceneLine(this->mSceneName, this->mScene);
		this->mTotalSeconds += this->mScene.totalSeconds();
		++this->mScenesWritten;
	}
	//---------------------------------------------------------
	void BenchmarkRecorder::writeSceneLine(String const & name,
		BenchmarkSceneStats const & stats)
	{
		String line = "{\"type\":\"scene\",\"name\":";
		appendJsonString(line, name);
		line += ",\"seconds\":" + jsonNumber(stats.totalSeconds());
		line += ",\"frames\":" +
			jsonInt(static_cast<long long>(stats.frames()));
		line += ",\"fpsAvg\":" + jsonNumber(stats.avgFps());
		line += ",\"frameMs\":{\"min\":" + jsonNumber(stats.minFrameMs());
		line += ",\"avg\":" + jsonNumber(stats.avgFrameMs());
		line += ",\"p50\":" + jsonNumber(stats.percentileFrameMs(50.0));
		line += ",\"p95\":" + jsonNumber(stats.percentileFrameMs(95.0));
		line += ",\"p99\":" + jsonNumber(stats.percentileFrameMs(99.0));
		line += ",\"max\":" + jsonNumber(stats.maxFrameMs());
		line += "}";
		// per-phase means (input/scripts/physics/render/...), sorted by name
		line += ",\"subsystemsMs\":{";
		std::vector<std::pair<String, double>> const phases = stats.phaseMeansMs();
		for (std::size_t i = 0; i < phases.size(); ++i)
		{
			if (i > 0)
			{
				line += ',';
			}
			appendJsonString(line, phases[i].first);
			line += ':' + jsonNumber(phases[i].second);
		}
		line += "}";
		line += ",\"allocs\":{\"perFrameAvg\":" +
			jsonNumber(stats.allocPerFrameAvg());
		line += ",\"peakFrame\":" +
			jsonInt(static_cast<long long>(stats.allocPeakFrame()));
		line += ",\"rssPeakBytes\":" +
			jsonInt(static_cast<long long>(stats.rssPeakBytes()));
		line += "}";
		line += ",\"gpu\":{\"trisAvg\":" + jsonNumber(stats.trianglesAvg());
		line += ",\"batchesAvg\":" + jsonNumber(stats.batchesAvg());
		line += ",\"texMemMB\":" + jsonNumber(stats.texMemMBAvg());
		line += "}}";
		this->writeLine(line);
	}
	//---------------------------------------------------------
	void BenchmarkRecorder::addFrame(BenchmarkFrameSample const & sample)
	{
		if (!this->isArmed() || !this->mSceneOpen)
		{
			return;
		}
		this->mScene.addFrame(sample);
	}
	//---------------------------------------------------------
	void BenchmarkRecorder::sampleFrame(unsigned int triangles,
		unsigned int batches, float texMemMB)
	{
		if (!this->isArmed() || !this->mSceneOpen)
		{
			return;
		}
		BenchmarkFrameSample sample;
		sample.frameMs = ProfileManager::lastFrameMilliseconds();
		sample.allocCount = MemoryManager::lastFrameTotal();
		sample.rssBytes = MemorySampler::residentBytes();
		sample.triangles = triangles;
		sample.batches = batches;
		sample.texMemMB = texMemMB;
		// the canonical tick phases are the depth-0 profiler scopes; the reusable
		// scratch keeps the steady-state sampler allocation-light
		ProfileManager::snapshot(this->mSnapshotScratch);
		for (ProfileManager::SnapshotNode const & row : this->mSnapshotScratch)
		{
			if (row.depth == 0 && row.name != nullptr)
			{
				sample.phasesMs.emplace_back(String(row.name), row.milliseconds);
			}
		}
		this->mScene.addFrame(sample);
	}
	//---------------------------------------------------------
	void BenchmarkRecorder::finish(bool aborted)
	{
		if (!this->isArmed() || this->mFinished)
		{
			return;
		}
		this->mFinished = true;
		if (this->mSceneOpen)
		{
			this->endScene();
		}
		String line = "{\"type\":\"summary\",\"scenes\":" +
			jsonInt(static_cast<long long>(this->mScenesWritten));
		line += ",\"totalSeconds\":" + jsonNumber(this->mTotalSeconds);
		line += ",\"aborted\":";
		line += aborted ? "true" : "false";
		line += "}";
		this->writeLine(line);
	}
}
