/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalSession.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// EditorTerminalSession.cpp - the pure Terminal-panel bookkeeping: title
// cleaning, agent classification, tab-label composition and post-close active
// index. UI-free and library-free so it links into orkige_editor_core and is
// unit-tested headlessly (EditorTerminalSessionTests).
#include "EditorTerminalSession.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace OrkigeEditor
{
	namespace
	{
		//! the recognised terminal-agent CLI names (lower-case). A cleaned name
		//! whose leading run matches one of these by prefix draws the robot
		//! glyph. These name PROGRAMS the user runs - never a product referenced
		//! in UI text; the label the user sees is always runtime session data.
		const char* const kAgentNames[] = {
			"claude", "codex", "opencode", "aider", "gemini"
		};

		std::string toLower(std::string const& in)
		{
			std::string out;
			out.reserve(in.size());
			for (char c : in)
			{
				out.push_back(static_cast<char>(
					std::tolower(static_cast<unsigned char>(c))));
			}
			return out;
		}

		bool isSpace(char c)
		{
			return std::isspace(static_cast<unsigned char>(c)) != 0;
		}

		//! the basename of a (possibly trailing-slash) path
		std::string baseName(std::string const& path)
		{
			std::string p = path;
			while (!p.empty() && p.back() == '/')
			{
				p.pop_back();
			}
			const std::size_t slash = p.find_last_of('/');
			return slash == std::string::npos ? p : p.substr(slash + 1);
		}
	}

	std::string terminalCleanTitle(std::string const& raw)
	{
		std::size_t b = 0;
		std::size_t e = raw.size();
		while (b < e && isSpace(raw[b]))
		{
			++b;
		}
		while (e > b && isSpace(raw[e - 1]))
		{
			--e;
		}
		std::string s = raw.substr(b, e - b);
		if (s.empty())
		{
			return s;
		}
		// the leading whitespace-delimited token
		const std::size_t sp = s.find_first_of(" \t");
		const std::string first = (sp == std::string::npos) ? s : s.substr(0, sp);
		// a path or path-prefixed command line -> the leading app word only
		const bool looksPath =
			first.find('/') != std::string::npos ||
			(!first.empty() && first[0] == '~');
		if (looksPath)
		{
			const std::string base = baseName(first);
			return base.empty() ? first : base;
		}
		return s;
	}

	TerminalGlyphClass classifyTerminalApp(std::string const& name)
	{
		return terminalAgentOf(name) == TerminalAgent::None
			? TerminalGlyphClass::Terminal : TerminalGlyphClass::Agent;
	}

	TerminalAgent terminalAgentOf(std::string const& name)
	{
		const std::string lower = toLower(name);
		const int count = static_cast<int>(
			sizeof(kAgentNames) / sizeof(kAgentNames[0]));
		for (int i = 0; i < count; ++i)
		{
			const std::string a(kAgentNames[i]);
			if (lower.size() >= a.size() && lower.compare(0, a.size(), a) == 0)
			{
				return static_cast<TerminalAgent>(i);
			}
		}
		return TerminalAgent::None;
	}

	std::uint32_t terminalGlyphCodepoint(TerminalGlyphClass glyphClass)
	{
		// ICON_FA_ROBOT (U+f544) / ICON_FA_TERMINAL (U+f120) - both live in
		// EditorTheme.cpp's ICON_GLYPH_RANGES so the atlas bakes them.
		return glyphClass == TerminalGlyphClass::Agent ? 0xf544u : 0xf120u;
	}

	TerminalTabLabel terminalTabLabel(std::string const& vtTitle,
		std::string const& processName, int index1Based)
	{
		const std::string title = terminalCleanTitle(vtTitle);
		const std::string proc = terminalCleanTitle(processName);

		TerminalTabLabel out;
		// which specific agent (title first, then process) drives the badge; a
		// recognised agent from EITHER signal marks the tab
		out.agent = terminalAgentOf(title);
		if (out.agent == TerminalAgent::None)
		{
			out.agent = terminalAgentOf(proc);
		}
		if (out.agent != TerminalAgent::None)
		{
			out.glyph = TerminalGlyphClass::Agent;
		}

		if (!title.empty())
		{
			out.text = title;
		}
		else if (!proc.empty())
		{
			out.text = proc;
		}
		else
		{
			out.text = "Terminal " + std::to_string(index1Based);
		}
		return out;
	}

	TerminalGridPoint terminalCellAtPoint(float px, float py, float originX,
		float originY, float cellW, float cellH, int cols, int totalLines)
	{
		TerminalGridPoint out;
		if (cellH > 0.0f)
		{
			out.line = static_cast<int>(std::floor((py - originY) / cellH));
		}
		if (cellW > 0.0f)
		{
			out.col = static_cast<int>(std::floor((px - originX) / cellW));
		}
		const int lineHi = (totalLines > 0) ? totalLines - 1 : 0;
		out.line = std::max(0, std::min(out.line, lineHi));
		// cols is a valid stop: a selection END is exclusive of the cell it names
		out.col = std::max(0, std::min(out.col, std::max(0, cols)));
		return out;
	}

	int terminalIndexAfterClose(int count, int closedIndex, int activeIndex)
	{
		const int newCount = count - 1;
		if (newCount <= 0)
		{
			return -1;
		}
		int active = activeIndex;
		if (active > closedIndex)
		{
			--active;	// everything past the removed slot slides down one
		}
		else if (active == closedIndex)
		{
			// the active tab itself went: keep the same slot (the next tab
			// slid into it) or fall back to the new last tab
			active = std::min(closedIndex, newCount - 1);
		}
		// active < closedIndex is unchanged
		return std::max(0, std::min(active, newCount - 1));
	}

	// ------------------------------------------------------------------------
	// Agent badge glyphs.
	//
	// DOCTRINE: the marks below are GENERATED procedurally (parametric strokes,
	// never traced third-party artwork) and render EXCLUSIVELY to identify the
	// third-party program running in a terminal session - nominative
	// identification, the OS dock-icon precedent, owner-directed 2026-07-28.
	// They must never appear elsewhere in the product or docs, and they are not
	// product logos. A future runtime vendor-icon discovery may overwrite these
	// pixels into the same atlas rect with no call-site change; this generator
	// is the one seam.
	// ------------------------------------------------------------------------
	namespace
	{
		//! straight-alpha RGBA pixel, alpha-composited onto the buffer (src-over)
		void blendPixel(std::vector<unsigned char>& buf, int size, int x, int y,
			unsigned char r, unsigned char g, unsigned char b, float a)
		{
			if (x < 0 || y < 0 || x >= size || y >= size || a <= 0.0f)
			{
				return;
			}
			if (a > 1.0f)
			{
				a = 1.0f;
			}
			const std::size_t i =
				(static_cast<std::size_t>(y) * size + x) * 4;
			const float da = buf[i + 3] / 255.0f;
			const float outA = a + da * (1.0f - a);
			auto mix = [&](unsigned char dst, unsigned char src) -> unsigned char
			{
				const float s = src / 255.0f;
				const float d = dst / 255.0f;
				const float o = (outA > 0.0f)
					? (s * a + d * da * (1.0f - a)) / outA : 0.0f;
				return static_cast<unsigned char>(
					std::max(0.0f, std::min(1.0f, o)) * 255.0f + 0.5f);
			};
			buf[i + 0] = mix(buf[i + 0], r);
			buf[i + 1] = mix(buf[i + 1], g);
			buf[i + 2] = mix(buf[i + 2], b);
			buf[i + 3] = static_cast<unsigned char>(
				std::max(0.0f, std::min(1.0f, outA)) * 255.0f + 0.5f);
		}

		//! distance from point p to segment a-b (all in normalised units)
		float distToSegment(float px, float py, float ax, float ay,
			float bx, float by)
		{
			const float dx = bx - ax;
			const float dy = by - ay;
			const float len2 = dx * dx + dy * dy;
			float t = 0.0f;
			if (len2 > 1e-6f)
			{
				t = ((px - ax) * dx + (py - ay) * dy) / len2;
				t = std::max(0.0f, std::min(1.0f, t));
			}
			const float cx = ax + t * dx;
			const float cy = ay + t * dy;
			const float ex = px - cx;
			const float ey = py - cy;
			return std::sqrt(ex * ex + ey * ey);
		}

		//! signed distance to a rounded rectangle (half-extents hx,hy, corner
		//! radius rad), centre-origin - negative inside
		float sdRoundRect(float px, float py, float hx, float hy, float rad)
		{
			const float qx = std::fabs(px) - hx + rad;
			const float qy = std::fabs(py) - hy + rad;
			const float ax = std::max(qx, 0.0f);
			const float ay = std::max(qy, 0.0f);
			return std::sqrt(ax * ax + ay * ay) +
				std::min(std::max(qx, qy), 0.0f) - rad;
		}

		//! 5x7 monogram letterforms (only the initials the agents need)
		const char* letterRows(char c)
		{
			switch (c)
			{
			case 'O': return "01110" "10001" "10001" "10001" "10001" "10001" "01110";
			case 'A': return "01110" "10001" "10001" "11111" "10001" "10001" "10001";
			case 'G': return "01110" "10001" "10000" "10111" "10001" "10011" "01111";
			default:  return "00000" "00100" "01110" "11111" "01110" "00100" "00000";
			}
		}

		//! the initial letter each agent's monogram carries
		char monogramLetter(TerminalAgent agent)
		{
			switch (agent)
			{
			case TerminalAgent::Opencode:	return 'O';
			case TerminalAgent::Aider:		return 'A';
			case TerminalAgent::Gemini:		return 'G';
			default:						return '?';
			}
		}

		//! 4x supersampled coverage of a normalised-coordinate predicate at pixel
		//! (x,y). `inside(nx,ny)` takes centre-origin coords in [-1,1].
		template <typename Fn>
		float coverage(int x, int y, int size, Fn inside)
		{
			const float half = size * 0.5f;
			int hits = 0;
			for (int sy = 0; sy < 2; ++sy)
			{
				for (int sx = 0; sx < 2; ++sx)
				{
					const float fx = x + 0.25f + sx * 0.5f;
					const float fy = y + 0.25f + sy * 0.5f;
					const float nx = (fx - half) / half;
					const float ny = (fy - half) / half;
					if (inside(nx, ny))
					{
						++hits;
					}
				}
			}
			return hits * 0.25f;
		}

		//! CLAUDE: the coral radiating-asterisk mark - 8 rounded spokes, thicker
		//! at the hub, tapering to the rim (parametric strokes)
		void renderClaudeStarburst(std::vector<unsigned char>& buf, int size)
		{
			const TerminalBadgeTint tint = terminalAgentTint(TerminalAgent::Claude);
			const int spokes = terminalAgentBadgeStrokeCount(TerminalAgent::Claude);
			const float radius = 0.92f;
			const float hubHalf = 0.16f;		//!< half-width at the hub
			const float tipHalf = 0.055f;		//!< half-width at the rim
			for (int y = 0; y < size; ++y)
			{
				for (int x = 0; x < size; ++x)
				{
					const float cov = coverage(x, y, size,
						[&](float nx, float ny) -> bool
						{
							for (int s = 0; s < spokes; ++s)
							{
								const float a =
									(6.2831853f * s) / static_cast<float>(spokes);
								const float tx = std::cos(a) * radius;
								const float ty = std::sin(a) * radius;
								// project onto the spoke to taper the half-width
								const float len2 = tx * tx + ty * ty;
								float t = (nx * tx + ny * ty) / len2;
								t = std::max(0.0f, std::min(1.0f, t));
								const float halfW =
									hubHalf + (tipHalf - hubHalf) * t;
								if (distToSegment(nx, ny, 0.0f, 0.0f, tx, ty)
									<= halfW)
								{
									return true;
								}
							}
							return false;
						});
					blendPixel(buf, size, x, y, tint.r, tint.g, tint.b, cov);
				}
			}
		}

		//! CODEX: the monochrome interlocking-ring mark - six elongated
		//! rounded-rect loop OUTLINES rotated 60 degrees apart around the hub, a
		//! recognisable parametric approximation of the knotted ring (not a
		//! pixel-faithful trace)
		void renderCodexRing(std::vector<unsigned char>& buf, int size)
		{
			const TerminalBadgeTint tint = terminalAgentTint(TerminalAgent::Codex);
			const int loops = terminalAgentBadgeStrokeCount(TerminalAgent::Codex);
			const float loopHx = 0.30f;		//!< loop half-length (radial)
			const float loopHy = 0.15f;		//!< loop half-width
			const float loopRad = 0.13f;	//!< corner radius
			const float stroke = 0.052f;	//!< outline half-thickness
			const float offset = 0.34f;		//!< loop centre distance from the hub
			for (int y = 0; y < size; ++y)
			{
				for (int x = 0; x < size; ++x)
				{
					const float cov = coverage(x, y, size,
						[&](float nx, float ny) -> bool
						{
							for (int s = 0; s < loops; ++s)
							{
								const float a =
									(6.2831853f * s) / static_cast<float>(loops);
								const float ca = std::cos(a);
								const float sa = std::sin(a);
								// translate to the loop centre, rotate into its
								// local frame (long axis pointing radially out)
								const float px = nx - ca * offset;
								const float py = ny - sa * offset;
								const float lx = px * ca + py * sa;
								const float ly = -px * sa + py * ca;
								const float d = sdRoundRect(lx, ly,
									loopHx, loopHy, loopRad);
								if (std::fabs(d) <= stroke)
								{
									return true;
								}
							}
							return false;
						});
					blendPixel(buf, size, x, y, tint.r, tint.g, tint.b, cov);
				}
			}
		}

		//! the letter-monogram badge: a signature-tinted rounded square carrying
		//! the program's initial (opencode/aider/gemini and the generic fallback)
		void renderMonogram(std::vector<unsigned char>& buf, int size,
			TerminalAgent agent)
		{
			const TerminalBadgeTint tint = terminalAgentTint(agent);
			const char letter = monogramLetter(agent);
			const char* rows = letterRows(letter);
			// the rounded-square field
			for (int y = 0; y < size; ++y)
			{
				for (int x = 0; x < size; ++x)
				{
					const float cov = coverage(x, y, size,
						[&](float nx, float ny) -> bool
						{
							return sdRoundRect(nx, ny, 0.86f, 0.86f, 0.5f) <= 0.0f;
						});
					blendPixel(buf, size, x, y, tint.r, tint.g, tint.b, cov);
				}
			}
			// the near-white initial, centred, ~0.6 of the field
			const float glyphExtent = size * 0.30f;		//!< half-size in px
			const float cellW = (glyphExtent * 2.0f) / 5.0f;
			const float cellH = (glyphExtent * 2.0f) / 7.0f;
			const float left = size * 0.5f - glyphExtent;
			const float top = size * 0.5f - glyphExtent;
			for (int gy = 0; gy < 7; ++gy)
			{
				for (int gx = 0; gx < 5; ++gx)
				{
					if (rows[gy * 5 + gx] != '1')
					{
						continue;
					}
					const int x0 = static_cast<int>(left + gx * cellW);
					const int x1 = static_cast<int>(left + (gx + 1) * cellW);
					const int y0 = static_cast<int>(top + gy * cellH);
					const int y1 = static_cast<int>(top + (gy + 1) * cellH);
					for (int y = y0; y <= y1; ++y)
					{
						for (int x = x0; x <= x1; ++x)
						{
							blendPixel(buf, size, x, y, 245, 245, 245, 1.0f);
						}
					}
				}
			}
		}
	}

	std::uint32_t terminalAgentBadgeCodepoint(TerminalAgent agent)
	{
		if (agent == TerminalAgent::None ||
			agent == TerminalAgent::Count)
		{
			return 0;
		}
		// U+E000 (Private Use Area) + the agent's ordinal
		return 0xE000u + static_cast<std::uint32_t>(agent);
	}

	TerminalBadgeTint terminalAgentTint(TerminalAgent agent)
	{
		// TASTE-FLAG (owner's eye): the signature tints. Claude = coral; Codex =
		// near-white (a monochrome mark on the dark tab); the rest are distinct
		// hues for the monograms.
		switch (agent)
		{
		case TerminalAgent::Claude:		return { 217, 119, 87 };	// coral
		case TerminalAgent::Codex:		return { 236, 236, 236 };	// near-white
		case TerminalAgent::Opencode:	return { 40, 158, 138 };	// teal
		case TerminalAgent::Aider:		return { 150, 111, 214 };	// violet
		case TerminalAgent::Gemini:		return { 70, 120, 220 };	// blue
		case TerminalAgent::Generic:	return { 140, 144, 150 };	// slate
		default:						return { 140, 144, 150 };
		}
	}

	int terminalAgentBadgeStrokeCount(TerminalAgent agent)
	{
		switch (agent)
		{
		case TerminalAgent::Claude:	return 8;	// radiating spokes
		case TerminalAgent::Codex:	return 6;	// interlocking loops
		default:					return 0;	// letter monograms
		}
	}

	std::vector<unsigned char> terminalAgentBadgePixels(TerminalAgent agent,
		int size)
	{
		if (size <= 0)
		{
			return {};
		}
		std::vector<unsigned char> buf(
			static_cast<std::size_t>(size) * size * 4, 0);
		switch (agent)
		{
		case TerminalAgent::Claude:	renderClaudeStarburst(buf, size); break;
		case TerminalAgent::Codex:	renderCodexRing(buf, size); break;
		default:					renderMonogram(buf, size, agent); break;
		}
		return buf;
	}
}
