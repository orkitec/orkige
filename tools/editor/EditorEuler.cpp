// EditorEuler - see header. Intrinsic Y-X-Z quaternion<->Euler conversion.
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorEuler.h"

#include <engine_render/RenderMath.h>

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace
	{
		constexpr float RAD2DEG = 57.2957795130823209f;
	}

	//---------------------------------------------------------
	void quatToEulerDegrees(const float q[4], float out[3])
	{
		// normalise a copy so the extraction is stable
		Quat quat(q[0], q[1], q[2], q[3]); // (w, x, y, z)
		quat.normalise();
		const float w = quat.w;
		const float x = quat.x;
		const float y = quat.y;
		const float z = quat.z;

		// Intrinsic Y-X-Z: R = Ry(Y) * Rx(X) * Rz(Z). Extract from the rotation
		// matrix elements written in quaternion components:
		//   sin(X) = 2(wx - yz)                       (the X pitch)
		//   Y = atan2(2(xz + wy), 1 - 2(x^2 + y^2))   (the Y yaw)
		//   Z = atan2(2(xy + wz), 1 - 2(x^2 + z^2))   (the Z roll)
		float sinX = 2.0f * (w * x - y * z);
		sinX = std::clamp(sinX, -1.0f, 1.0f);
		const float X = std::asin(sinX);
		float Y;
		float Z;
		if (std::abs(sinX) > 0.99999f)
		{
			// gimbal lock (X ~= +-90): only Y +- Z is determined; pin Z = 0 and
			// fold the coupled angle into Y so the round-trip quat is preserved
			const float r01 = 2.0f * (x * y - w * z);
			const float r00 = 1.0f - 2.0f * (y * y + z * z);
			Y = std::atan2(sinX > 0.0f ? r01 : -r01, r00);
			Z = 0.0f;
		}
		else
		{
			Y = std::atan2(2.0f * (x * z + w * y),
				1.0f - 2.0f * (x * x + y * y));
			Z = std::atan2(2.0f * (x * y + w * z),
				1.0f - 2.0f * (x * x + z * z));
		}
		out[0] = X * RAD2DEG;
		out[1] = Y * RAD2DEG;
		out[2] = Z * RAD2DEG;
	}

	//---------------------------------------------------------
	void eulerDegreesToQuat(const float e[3], float out[4])
	{
		// compose in the SAME order the extraction assumes: Ry(Y) * Rx(X) * Rz(Z)
		Quat q = Quat(Degree(e[1]), Vec3::UNIT_Y) *
			Quat(Degree(e[0]), Vec3::UNIT_X) *
			Quat(Degree(e[2]), Vec3::UNIT_Z);
		q.normalise();
		out[0] = q.w;
		out[1] = q.x;
		out[2] = q.y;
		out[3] = q.z;
	}
}
