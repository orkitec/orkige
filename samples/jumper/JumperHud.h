/********************************************************************
	created:	Wednesday 2026/07/08 at 10:05
	filename: 	JumperHud.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __JumperHud_h__8_7_2026__10_05_00__
#define __JumperHud_h__8_7_2026__10_05_00__

#include <engine_gui/GuiManager.h>
#include <engine_gui/GuiLabel.h>
#include <engine_gui/GuiProgressBar.h>
#include <engine_render/RenderMath.h>
#include <core_util/optr.h>
#include <core_util/StringUtil.h>

#include <algorithm>
#include <cmath>

namespace Orkige
{
	//! @brief the jumper's in-game HUD, built on the engine's own UI system
	//! (engine_gui / UiRenderer): boots GuiManager with the generated
	//! gui_default atlas (Util/make_gui_atlas.py ->
	//! gui_default.{ogui,png}) and owns four widgets - a title splash,
	//! the persistent controls hint, the hidden win banner and a
	//! distance-to-goal progress bar. Widget groups live on their own
	//! z-layers (UiLayers are shared per z), so the title/banner can
	//! be shown/hidden without touching the persistent widgets.
	//! @remarks shared between the C++ jumper SAMPLE (samples/jumper, atlas
	//! from its media/ dir in the default resource group) and the jumper
	//! NATIVE PROJECT MODULE (projects/jumper-native, atlas from the
	//! project's assets/ in the "OrkigeProject" group) - one source of
	//! truth, same pattern as JumperLogic.h; the atlas resource group is
	//! the only difference and comes in through the constructor.
	class JumperHud
	{
	public:
		static constexpr float TITLE_SECONDS = 3.0f;		//!< title splash time
		static constexpr float WIN_BANNER_SECONDS = 2.0f;	//!< "YOU WIN!" time
		static constexpr uint FONT_HUD = 9;		//!< 10x14 px glyphs
		static constexpr uint FONT_TITLE = 24;	//!< 20x28 px glyphs
		// z-layers: 12 persistent HUD, 13 title splash, 14 win banner
		static constexpr uint Z_HUD = 12;
		static constexpr uint Z_TITLE = 13;
		static constexpr uint Z_WIN = 14;

		JumperHud(int screenWidth, int screenHeight,
			String const & atlas = "gui_default",
			// gui renders on BOTH render flavors (Docs/render-abstraction.md);
			// this stays flavor-neutral game code. "General" is the engine's
			// default resource group (the atlas media the C++ sample registers
			// there); the native project module passes the "OrkigeProject"
			// group instead
			String const & resourceGroup = "General")
		{
			// GuiManager loads "<atlas>.ogui" (+ texture) from the given
			// resource group and creates the UI screen for the main window
			mFactory = onew(new GuiFactory());
			mManager = onew(new GuiManager(mFactory, atlas, resourceGroup));

			optr<GuiFactory> factory = mFactory;
			// whole-pixel positions only - Caption asserts on subpixel coords
			mTitle = factory->createLabel("HudTitle", FONT_TITLE, "ORKIGE JUMPER",
				Vec2(0.0f, std::floor(screenHeight * 0.22f)),
				StringUtil::BLANK, Z_TITLE, false);
			mTitle.lock()->centerHorizontal();

			mHint = factory->createLabel("HudHint", FONT_HUD,
				"WASD move - SPACE jump",
				Vec2(0.0f, static_cast<float>(screenHeight - 34)),
				StringUtil::BLANK, Z_HUD, false);
			mHint.lock()->centerHorizontal();

			mWinBanner = factory->createLabel("HudWinBanner", FONT_TITLE,
				"YOU WIN!", Vec2(0.0f, std::floor(screenHeight * 0.35f)),
				StringUtil::BLANK, Z_WIN, false);
			mWinBanner.lock()->centerHorizontal();
			mWinBanner.lock()->getLayer()->hide();	// hidden until the goal

			// distance-to-goal indicator, top-left ("progressbar" frame sprite +
			// the "progressbar_bar" fill sprite GuiProgressBar hardcodes)
			mProgress = factory->createProgressBar("HudProgress", "progressbar",
				FONT_HUD, "", Vec2(16.0f, 16.0f),
				GuiLabel::LA_CENTER, Vec2(192.0f, 20.0f),
				StringUtil::BLANK, Z_HUD);
			mProgress.lock()->setProgress(0.0f);
		}

		//! per-frame: timed title/banner hiding + progress bar value (0..1)
		void update(float deltaTime, float progressToGoal)
		{
			if (mTitleTimer > 0.0f)
			{
				mTitleTimer -= deltaTime;
				if (mTitleTimer <= 0.0f)
				{
					mTitle.lock()->getLayer()->hide();
				}
			}
			if (mWinTimer > 0.0f)
			{
				mWinTimer -= deltaTime;
				if (mWinTimer <= 0.0f)
				{
					mWinBanner.lock()->getLayer()->hide();
				}
			}
			mProgress.lock()->setProgress(
				std::clamp(progressToGoal, 0.0f, 1.0f) * 100.0f);
		}

		//! the goal was reached - flash the win banner for WIN_BANNER_SECONDS
		void showWinBanner()
		{
			mWinBanner.lock()->getLayer()->show();
			mWinTimer = WIN_BANNER_SECONDS;
		}

		// state queries (used by the selfchecks)
		bool isTitleVisible() const { return mTitle.lock()->getLayer()->isVisible(); }
		bool isWinBannerVisible() const { return mWinBanner.lock()->getLayer()->isVisible(); }
		float getProgress() const { return mProgress.lock()->getProgress(); }
		bool widgetsExist() const
		{
			return mManager->widgetExists("HudTitle") &&
				mManager->widgetExists("HudHint") &&
				mManager->widgetExists("HudWinBanner") &&
				mManager->widgetExists("HudProgress");
		}

	private:
		optr<GuiFactory>		mFactory;
		optr<GuiManager>		mManager;
		woptr<GuiLabel>			mTitle;
		woptr<GuiLabel>			mHint;
		woptr<GuiLabel>			mWinBanner;
		woptr<GuiProgressBar>	mProgress;
		float						mTitleTimer = TITLE_SECONDS;
		float						mWinTimer = 0.0f;
	};
}

#endif //__JumperHud_h__8_7_2026__10_05_00__
