/**************************************************************
	created:	2026/07/24 at 12:00
	filename: 	DevicePreset.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __DevicePreset_h__24_7_2026__12_00_00__
#define __DevicePreset_h__24_7_2026__12_00_00__

//! @file DevicePreset.h
//! @brief pure device-target policy for the Game Preview panel: the one place
//! that maps a device preset to a concrete simulated surface - pixel resolution,
//! display density (content scale), safe-area insets AND the procedurally-drawn
//! device FRAME geometry (bezel, rounded-corner mask, notch / punch-hole / pill
//! cutout, home button, home indicator). The presets carry REAL device names
//! (factual nominative data, like the engine's iOS/Android targets) with real
//! logical resolutions / scales / safe-areas; the drawn frame is our OWN generic
//! silhouette, never third-party device art. The Game Preview panel draws the
//! frame off this geometry and the preview_game MCP verb resolves a preset by
//! NAME through the same table (the IblPreset.h / ShadowPreset.h pattern).
//! Renderer- and editor-independent so it unit tests headlessly.

#include "core_util/String.h"
#include "core_util/SafeArea.h"

#include <algorithm>

namespace Orkige
{
	namespace DevicePreset
	{
		//! the screen-edge intrusion a device frame draws OVER the preview
		enum Cutout
		{
			CUT_NONE = 0,	//!< a clean screen (tablet / home-button phone)
			CUT_NOTCH,		//!< a notch band CONNECTED to the top (portrait) / side (landscape) bezel
			CUT_PUNCHHOLE,	//!< a small round front-camera hole near the top
			CUT_PILL		//!< a DETACHED rounded pill inset below the top edge (a "dynamic island")
		};

		//! @brief the procedural device-frame proportions for a preset. All
		//! fractions are relative to the SCREEN dimensions so the drawn frame
		//! stays proportionate at any panel size. framed=false leaves a preset
		//! frameless (Free / Custom).
		struct FrameSpec
		{
			bool	framed;					//!< draw a device frame at all
			float	bezelFraction;			//!< uniform SIDE bezel thickness / screen min-dimension
			float	foreheadFraction;		//!< EXTRA top bezel / screen min (home-button phones)
			float	chinFraction;			//!< EXTRA bottom bezel / screen min (home-button phones)
			float	cornerRadiusFraction;	//!< screen corner radius / screen min-dimension
			Cutout	cutout;					//!< the screen intrusion kind
			//! notch: main = band WIDTH/screenW (portrait) or HEIGHT/screenH
			//! (landscape); punch-hole: main = DIAMETER / screen min-dimension;
			//! pill: main = WIDTH / screenW
			float	cutoutMainFraction;
			//! notch / pill band depth / screen min-dimension (unused for punch-hole)
			float	cutoutBandFraction;
			bool	homeButton;				//!< draw a physical home-button ring in the chin
			//! home-indicator bar length / screen WIDTH (0 = no indicator)
			float	indicatorLengthFraction;
			//! home-indicator bar thickness / screen min-dimension
			float	indicatorThicknessFraction;
		};

		//! the coarse preset choice (the canonical order the dropdown shows)
		enum Kind
		{
			DP_FREE = 0,				//!< panel-sized: the RTT follows the panel content region
			DP_IPHONE_SE,				//!< iPhone SE (16:9, home button)
			DP_IPHONE_NOTCH,			//!< iPhone 13/14 class (19.5:9, notch)
			DP_IPHONE_NOTCH_LANDSCAPE,	//!< iPhone 13/14 class rotated (side notch)
			DP_IPHONE_ISLAND,			//!< iPhone 15/16 class (19.5:9, Dynamic Island)
			DP_ANDROID_COMPACT,			//!< Pixel-class compact (20:9, punch-hole)
			DP_ANDROID_LARGE,			//!< Pixel Pro-class (19.5:9, punch-hole)
			DP_IPAD,					//!< iPad (4:3)
			DP_IPAD_PRO_11,				//!< iPad Pro 11" (~1.43:1)
			DP_IPAD_PRO_129,			//!< iPad Pro 12.9" (4:3)
			DP_IPAD_LANDSCAPE,			//!< iPad Pro 12.9" landscape (4:3 wide)
			DP_GALAXY_FOLD_COVER,		//!< Galaxy Fold-class folded cover (~23:9 tall)
			DP_GALAXY_FOLD_MAIN,		//!< Galaxy Fold-class unfolded (~6:5 near-square)
			DP_GALAXY_FLIP,				//!< Galaxy Flip-class (22:9)
			DP_CUSTOM,					//!< caller-supplied width x height + insets
			DP_COUNT
		};

		//! @brief one concrete preset behind a Kind
		struct Preset
		{
			char const *	label;			//!< the dropdown / MCP name (real device name allowed)
			char const *	token;			//!< the stable machine token (MCP argument)
			unsigned int	width;			//!< simulated surface width in pixels (0 for FREE)
			unsigned int	height;			//!< simulated surface height in pixels (0 for FREE)
			float			contentScale;	//!< simulated display density (1/2/3x)
			SafeAreaInsets	insets;			//!< real safe-area insets (pixels)
			bool			panelSized;		//!< FREE: the panel content region drives the size
			bool			custom;			//!< CUSTOM: the caller supplies width/height/insets
			FrameSpec		frame;			//!< the procedural device-frame proportions
		};

		//! @brief pixels for a safe inset (small helper so the table reads clean)
		inline SafeAreaInsets makeInsets(unsigned int l, unsigned int t,
			unsigned int r, unsigned int b)
		{
			SafeAreaInsets insets;
			insets.mLeft = l;
			insets.mTop = t;
			insets.mRight = r;
			insets.mBottom = b;
			return insets;
		}

		//! shared frame proportion presets (product-render silhouettes)
		//                             framed bezel  fore   chin   corner cutout        main   band   home  indLen indThick
		inline FrameSpec noFrame()      { return { false, 0.0f,  0.0f,  0.0f,  0.0f,  CUT_NONE,      0.0f,  0.0f,  false, 0.0f,  0.0f  }; }
		inline FrameSpec homeButton()   { return { true,  0.028f,0.115f,0.150f,0.040f,CUT_NONE,      0.0f,  0.0f,  true,  0.0f,  0.0f  }; }
		inline FrameSpec notchFrame()   { return { true,  0.024f,0.0f,  0.0f,  0.090f,CUT_NOTCH,     0.33f, 0.060f,false, 0.32f, 0.012f}; }
		inline FrameSpec islandFrame()  { return { true,  0.024f,0.0f,  0.0f,  0.090f,CUT_PILL,      0.30f, 0.052f,false, 0.32f, 0.012f}; }
		inline FrameSpec punchFrame()   { return { true,  0.022f,0.0f,  0.0f,  0.075f,CUT_PUNCHHOLE, 0.045f,0.0f,  false, 0.30f, 0.012f}; }
		inline FrameSpec tabletFrame()  { return { true,  0.045f,0.0f,  0.0f,  0.032f,CUT_NONE,      0.0f,  0.0f,  false, 0.20f, 0.009f}; }

		//! @brief the concrete preset for a Kind. Out-of-range yields FREE.
		//! @remarks Real device names + real logical resolutions / content
		//! scales / safe-areas; the drawn frame is our own generic silhouette.
		inline Preset const & forKind(Kind kind)
		{
			static const Preset presets[DP_COUNT] = {
				//  label                                     token                     width height scale insets                            panel  custom frame
				{ "Free (panel size)",                        "free",                   0,    0,     1.0f, makeInsets(0,   0,   0,   0),  true,  false, noFrame()     },
				{ "iPhone SE (750x1334)",                     "iphone_se",              750,  1334,  2.0f, makeInsets(0,   40,  0,   0),  false, false, homeButton()  },
				{ "iPhone 14 (1170x2532)",                    "iphone_notch",           1170, 2532,  3.0f, makeInsets(0,   141, 0,   102), false, false, notchFrame()  },
				{ "iPhone 14 landscape (2532x1170)",          "iphone_notch_landscape", 2532, 1170,  3.0f, makeInsets(141, 0,   141, 63),  false, false, notchFrame()  },
				{ "iPhone 15 (1179x2556)",                    "iphone_island",          1179, 2556,  3.0f, makeInsets(0,   177, 0,   102), false, false, islandFrame() },
				{ "Pixel 8 (1080x2400)",                      "android_compact",        1080, 2400,  2.625f, makeInsets(0, 66,  0,   48),  false, false, punchFrame()  },
				{ "Pixel 8 Pro (1440x3120)",                  "android_large",          1440, 3120,  3.5f, makeInsets(0,   90,  0,   63),  false, false, punchFrame()  },
				{ "iPad (1536x2048)",                         "ipad",                   1536, 2048,  2.0f, makeInsets(0,   48,  0,   40),  false, false, tabletFrame() },
				{ "iPad Pro 11\" (1668x2388)",                "ipad_pro_11",            1668, 2388,  2.0f, makeInsets(0,   48,  0,   48),  false, false, tabletFrame() },
				{ "iPad Pro 12.9\" (2048x2732)",              "ipad_pro_129",           2048, 2732,  2.0f, makeInsets(0,   48,  0,   48),  false, false, tabletFrame() },
				{ "iPad Pro 12.9\" landscape (2732x2048)",    "ipad_landscape",         2732, 2048,  2.0f, makeInsets(0,   24,  0,   40),  false, false, tabletFrame() },
				{ "Galaxy Fold cover (904x2316)",             "fold_cover",             904,  2316,  2.6f, makeInsets(0,   48,  0,   48),  false, false, punchFrame()  },
				{ "Galaxy Fold unfolded (1812x2176)",         "fold_main",              1812, 2176,  2.6f, makeInsets(0,   54,  0,   48),  false, false, punchFrame()  },
				{ "Galaxy Flip (1080x2640)",                  "galaxy_flip",            1080, 2640,  2.6f, makeInsets(0,   66,  0,   48),  false, false, punchFrame()  },
				{ "Custom",                                   "custom",                 1080, 1920,  2.0f, makeInsets(0,   0,   0,   0),  false, true,  noFrame()     },
			};
			const int index = (kind >= 0 && kind < DP_COUNT)
				? static_cast<int>(kind) : static_cast<int>(DP_FREE);
			return presets[index];
		}

		//! the number of presets (the dropdown length)
		inline int count() { return static_cast<int>(DP_COUNT); }

		//! @brief the stable machine token for a Kind (the MCP `preset` argument)
		inline char const * tokenFor(Kind kind) { return forKind(kind).token; }

		//! @brief parse a preset token (case-insensitive) back to its Kind.
		//! @return false (outKind untouched) on an unknown token.
		inline bool parseToken(String const & text, Kind & outKind)
		{
			String lowered;
			lowered.reserve(text.size());
			for(char each : text)
			{
				lowered.push_back(each >= 'A' && each <= 'Z'
					? static_cast<char>(each - 'A' + 'a') : each);
			}
			for(int i = 0; i < DP_COUNT; ++i)
			{
				if(lowered == forKind(static_cast<Kind>(i)).token)
				{
					outKind = static_cast<Kind>(i);
					return true;
				}
			}
			return false;
		}

		//! @brief the concrete, drawable device-frame geometry (all pixels, in
		//! the SAME space as the screen rect fed to deriveFrame).
		struct FrameGeometry
		{
			bool	framed = false;
			//--- the outer bezel (a rounded rect enclosing the screen) ---
			float	bezelX = 0, bezelY = 0, bezelW = 0, bezelH = 0;
			float	bezelRadius = 0;
			//--- the screen opening (echoes the input screen rect) ---
			float	screenX = 0, screenY = 0, screenW = 0, screenH = 0;
			float	screenRadius = 0;	//!< the corner-mask radius
			//--- the screen intrusion (occludes the image) ---
			Cutout	cutout = CUT_NONE;
			float	cutoutX = 0, cutoutY = 0, cutoutW = 0, cutoutH = 0;
			float	cutoutRadius = 0;	//!< pill/punch-hole = half-size; notch far-edge rounding
			//! notch orientation: true = connects to the TOP bezel (portrait),
			//! false = connects to a SIDE bezel (landscape). Only meaningful for
			//! CUT_NOTCH (the panel squares the bezel edge, rounds the far edge).
			bool	notchOnTop = true;
			//--- the physical home button (home-button phones) ---
			bool	hasHomeButton = false;
			float	homeButtonX = 0, homeButtonY = 0;	//!< centre
			float	homeButtonRadius = 0;
			//--- the home indicator (a rounded bar over the image) ---
			bool	hasIndicator = false;
			float	indicatorX = 0, indicatorY = 0, indicatorW = 0, indicatorH = 0;
			float	indicatorRadius = 0;
		};

		//! @brief derive the drawable frame geometry from a preset and the pixel
		//! rect the preview screen occupies. Pure layout math (no renderer): the
		//! bezel wraps the screen (with an optional forehead/chin for home-button
		//! phones), the corners round, and the cutout/button/indicator are placed
		//! by orientation. A frameless preset returns framed=false.
		//! @param preset the chosen preset (its FrameSpec drives the proportions)
		//! @param screenX,screenY,screenW,screenH where the preview image draws
		inline FrameGeometry deriveFrame(Preset const & preset,
			float screenX, float screenY, float screenW, float screenH)
		{
			FrameGeometry geometry;
			geometry.screenX = screenX;
			geometry.screenY = screenY;
			geometry.screenW = screenW;
			geometry.screenH = screenH;
			geometry.cutout = CUT_NONE;
			if(!preset.frame.framed || screenW <= 0.0f || screenH <= 0.0f)
			{
				return geometry;	// frameless (echoes the screen rect)
			}
			const FrameSpec & spec = preset.frame;
			const float screenMin = std::min(screenW, screenH);
			const bool portrait = screenH >= screenW;

			geometry.framed = true;
			const float sideBezel = spec.bezelFraction * screenMin;
			const float topBezel =
				(spec.bezelFraction + spec.foreheadFraction) * screenMin;
			const float bottomBezel =
				(spec.bezelFraction + spec.chinFraction) * screenMin;
			geometry.bezelX = screenX - sideBezel;
			geometry.bezelY = screenY - topBezel;
			geometry.bezelW = screenW + 2.0f * sideBezel;
			geometry.bezelH = screenH + topBezel + bottomBezel;
			geometry.screenRadius = spec.cornerRadiusFraction * screenMin;
			geometry.bezelRadius = geometry.screenRadius + sideBezel;

			// the cutout (occludes the image exactly where hardware would)
			if(spec.cutout == CUT_NOTCH)
			{
				geometry.cutout = CUT_NOTCH;
				const float band = spec.cutoutBandFraction * screenMin;
				if(portrait)
				{
					const float width = spec.cutoutMainFraction * screenW;
					geometry.cutoutW = width;
					geometry.cutoutH = band;
					geometry.cutoutX = screenX + 0.5f * (screenW - width);
					geometry.cutoutY = screenY;		// flush with the top edge
					geometry.notchOnTop = true;
				}
				else
				{
					const float height = spec.cutoutMainFraction * screenH;
					geometry.cutoutW = band;
					geometry.cutoutH = height;
					geometry.cutoutX = screenX;		// flush with the left edge
					geometry.cutoutY = screenY + 0.5f * (screenH - height);
					geometry.notchOnTop = false;
				}
				geometry.cutoutRadius = band * 0.55f;
			}
			else if(spec.cutout == CUT_PILL)
			{
				// a DETACHED rounded pill inset below the top edge (dynamic island)
				geometry.cutout = CUT_PILL;
				const float width = spec.cutoutMainFraction * screenW;
				const float band = spec.cutoutBandFraction * screenMin;
				geometry.cutoutW = width;
				geometry.cutoutH = band;
				geometry.cutoutX = screenX + 0.5f * (screenW - width);
				geometry.cutoutY = screenY + band * 0.55f;	// a gap below the top
				geometry.cutoutRadius = band * 0.5f;
			}
			else if(spec.cutout == CUT_PUNCHHOLE)
			{
				geometry.cutout = CUT_PUNCHHOLE;
				const float diameter = spec.cutoutMainFraction * screenMin;
				geometry.cutoutW = diameter;
				geometry.cutoutH = diameter;
				geometry.cutoutX = screenX + 0.5f * (screenW - diameter);
				geometry.cutoutY = screenY + diameter * 0.7f;
				geometry.cutoutRadius = diameter * 0.5f;
			}

			// the physical home button, centred in the chin (home-button phones)
			if(spec.homeButton && bottomBezel > 0.0f)
			{
				geometry.hasHomeButton = true;
				geometry.homeButtonRadius = std::min(bottomBezel * 0.42f,
					screenMin * 0.06f);
				geometry.homeButtonX = screenX + 0.5f * screenW;
				geometry.homeButtonY = screenY + screenH + bottomBezel * 0.5f;
			}

			// the home indicator bar (bottom centre, over the image); its LENGTH
			// is a fraction of the screen WIDTH so it stays ~1/3 across
			if(spec.indicatorLengthFraction > 0.0f)
			{
				geometry.hasIndicator = true;
				const float thick = spec.indicatorThicknessFraction * screenMin;
				const float length = spec.indicatorLengthFraction * screenW;
				geometry.indicatorW = length;
				geometry.indicatorH = thick;
				geometry.indicatorX = screenX + 0.5f * (screenW - length);
				geometry.indicatorY = screenY + screenH - thick * 2.4f;
				geometry.indicatorRadius = thick * 0.5f;
			}
			return geometry;
		}

		//! @brief the bottom-right picture-in-picture inset rect the Scene panel
		//! draws the selected-camera preview into. Pure layout math (no renderer):
		//! given the Scene image rect (panelW x panelH pixels) and the preview
		//! target aspect (width/height), fit a box whose WIDTH is `widthFraction`
		//! of the panel width, preserve the target aspect, pin it to the bottom-
		//! right corner inset by `margin` pixels. Clamped to stay on-screen.
		inline void insetRect(float panelW, float panelH, float targetAspect,
			float widthFraction, float margin,
			float & outX, float & outY, float & outW, float & outH)
		{
			const float safeAspect = targetAspect > 1.0e-4f ? targetAspect : 1.0f;
			const float fraction = std::clamp(widthFraction, 0.05f, 1.0f);
			float boxW = panelW * fraction;
			float boxH = boxW / safeAspect;
			const float maxH = std::max(panelH - 2.0f * margin, 1.0f);
			if(boxH > maxH)
			{
				boxH = maxH;
				boxW = boxH * safeAspect;
			}
			const float maxW = std::max(panelW - 2.0f * margin, 1.0f);
			if(boxW > maxW)
			{
				boxW = maxW;
				boxH = boxW / safeAspect;
			}
			outW = boxW;
			outH = boxH;
			outX = panelW - boxW - margin;
			outY = panelH - boxH - margin;
			if(outX < 0.0f) { outX = 0.0f; }
			if(outY < 0.0f) { outY = 0.0f; }
		}
	}
}

#endif //__DevicePreset_h__24_7_2026__12_00_00__
