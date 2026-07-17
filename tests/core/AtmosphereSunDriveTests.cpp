/**************************************************************
	created:	2026/07/17 at 06:30
	filename: 	AtmosphereSunDriveTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless sun-exposure linkage policy: the pure day/night curve
	(core_util/AtmosphereSunDrive.h) both flavors read - the sun colour
	normalizes, the exposure knobs act monotonically, day outshines night,
	and the classic calibration tracks the linear drive monotonically. The
	rendered proof (the classic backend actually drives its light/ambient
	and restores them exactly) is the render_facade_selfcheck atmosphere leg.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <core_util/AtmosphereSunDrive.h>

#include <algorithm>
#include <cmath>

using namespace Orkige;
using AtmosphereSunDrive::Drive;

namespace
{
	//! the drive for a named look at a sun elevation (toward-the-sun y)
	Drive driveFor(AtmospherePreset::Sky sky, float elevation)
	{
		const AtmosphereDesc desc = AtmospherePreset::forSky(sky);
		const float horizontal =
			std::sqrt(std::max(0.0f, 1.0f - elevation * elevation));
		return AtmosphereSunDrive::compute(desc, 0.0f, elevation, horizontal);
	}
	float maxChannel(float r, float g, float b)
	{
		return std::max({ r, g, b });
	}
}

TEST_CASE("AtmosphereSunDrive: the sun colour is normalized and finite",
	"[atmosphere][sundrive]")
{
	// every named look, from straight overhead to well below the horizon -
	// the curve must stay finite (the native model divides by the sun
	// elevation) and the colour normalized (the power knob carries magnitude)
	const AtmospherePreset::Sky skies[] = { AtmospherePreset::SKY_DAY,
		AtmospherePreset::SKY_SUNSET, AtmospherePreset::SKY_NIGHT };
	for(AtmospherePreset::Sky sky : skies)
	{
		for(float elevation = -1.0f; elevation <= 1.0f; elevation += 0.125f)
		{
			const Drive drive = driveFor(sky, elevation);
			CHECK(std::isfinite(drive.sunRed));
			CHECK(std::isfinite(drive.sunGreen));
			CHECK(std::isfinite(drive.sunBlue));
			CHECK(drive.sunRed >= 0.0f);
			CHECK(drive.sunGreen >= 0.0f);
			CHECK(drive.sunBlue >= 0.0f);
			CHECK(maxChannel(drive.sunRed, drive.sunGreen, drive.sunBlue) ==
				Catch::Approx(1.0f).margin(1e-3f));
			CHECK(std::isfinite(drive.classicAmbientRed));
			CHECK(std::isfinite(drive.classicAmbientGreen));
			CHECK(std::isfinite(drive.classicAmbientBlue));
		}
	}
}

TEST_CASE("AtmosphereSunDrive: the power knobs pass through monotonically",
	"[atmosphere][sundrive]")
{
	// sunPower is the exposure knob on both flavors: next consumes it as the
	// linked light's linear power, classic through the calibrated scale -
	// both must grow with it
	AtmosphereDesc desc = AtmospherePreset::forSky(AtmospherePreset::SKY_DAY);
	desc.sunPower = 0.35f;
	const Drive dim = AtmosphereSunDrive::compute(desc, 0.0f, 0.7f, 0.7f);
	desc.sunPower = 1.6f;
	const Drive bright = AtmosphereSunDrive::compute(desc, 0.0f, 0.7f, 0.7f);
	CHECK(bright.nextSunPower > dim.nextSunPower);
	CHECK(bright.classicSunScale > dim.classicSunScale);

	// ambientPower scales the hemisphere fill on both flavors
	desc.ambientPower = 0.5f;
	const Drive lowFill = AtmosphereSunDrive::compute(desc, 0.0f, 0.7f, 0.7f);
	desc.ambientPower = 2.0f;
	const Drive highFill = AtmosphereSunDrive::compute(desc, 0.0f, 0.7f, 0.7f);
	CHECK(highFill.nextUpperGreen > lowFill.nextUpperGreen);
	CHECK(highFill.classicAmbientGreen > lowFill.classicAmbientGreen);
	// the linear ratio is exact (the knob is a plain multiplier)
	CHECK(highFill.nextUpperGreen ==
		Catch::Approx(lowFill.nextUpperGreen * 4.0f).epsilon(1e-3f));
}

TEST_CASE("AtmosphereSunDrive: day fills brighter than night",
	"[atmosphere][sundrive]")
{
	// the day look under a high sun vs the night look under a low moon: the
	// ambient fill must order day > night on BOTH flavors' drive values (the
	// lumens-vignette readability contract)
	const Drive day = driveFor(AtmospherePreset::SKY_DAY, 0.9f);
	const Drive night = driveFor(AtmospherePreset::SKY_NIGHT, 0.3f);
	CHECK(day.nextUpperGreen > night.nextUpperGreen);
	CHECK(day.classicAmbientGreen > night.classicAmbientGreen);
	// and the day exposure outranks the night moon
	CHECK(day.nextSunPower > night.nextSunPower);
	CHECK(day.classicSunScale > night.classicSunScale);
}

TEST_CASE("AtmosphereSunDrive: the classic calibration tracks the linear drive",
	"[atmosphere][sundrive]")
{
	// classicLevel is the mid-grey-reference mapping of a linear level into
	// the classic gamma-space pipeline: zero at zero, strictly increasing -
	// so classic never reorders what next renders
	float previous = -1.0f;
	for(float level = 0.0f; level <= 4.0f; level += 0.25f)
	{
		const float mapped = AtmosphereSunDrive::Detail::classicLevel(level);
		CHECK(std::isfinite(mapped));
		CHECK(mapped > previous);
		previous = mapped;
	}
	CHECK(AtmosphereSunDrive::Detail::classicLevel(0.0f) == 0.0f);
	// the calibration point: a mid-grey slab under the day exposure reads
	// comparably on both flavors (0.5 * classicScale vs the encoded PBS
	// response sqrt(0.5/pi * power))
	const Drive day = driveFor(AtmospherePreset::SKY_DAY, 0.99f);
	const float classicReading = 0.5f * day.classicSunScale;
	const float nextReading =
		std::sqrt(0.5f / 3.14159f * day.nextSunPower);
	CHECK(classicReading ==
		Catch::Approx(nextReading).epsilon(0.05f));
}
