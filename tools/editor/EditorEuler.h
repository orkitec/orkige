// EditorEuler - pure quaternion <-> Euler-angle conversion for the Inspector.
//
// The Inspector shows a rotation quaternion as human-readable Euler angles (X/Y/Z
// in DEGREES) but the reflected/serialized/MCP value stays the quaternion. These
// helpers convert between the canonical quaternion component order (w, x, y, z -
// what PropertyValue::toString emits) and Euler X/Y/Z degrees.
//
// EULER ORDER: intrinsic Y-X-Z (yaw about Y, then pitch about X, then roll about
// Z) - the same family the engine's own math uses (the editor camera rig composes
// Quat(yaw,+Y) * Quat(pitch,+X); ScriptComponent reads/builds roll about +Z). So
// eulerDegreesToQuat composes q = Ry(Y) * Rx(X) * Rz(Z). The two directions are
// exact inverses (round-trip identity within epsilon - see EditorEulerTests).
//
// PLAIN-FLOAT interface on purpose: no Ogre/RenderMath types leak into the widget
// layer that calls this; the .cpp uses the engine math internally. Unit-testable
// headlessly from tests/editor_core.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

namespace Orkige
{
	//! @brief quaternion (w, x, y, z) -> Euler angles (X, Y, Z) in DEGREES,
	//! intrinsic Y-X-Z order. At gimbal lock (X = +-90) the Z angle folds into Y
	//! (Z reported 0) - the reconstructed quaternion is still identical.
	//! @param quatWXYZ input quaternion, [w, x, y, z] (need not be normalised)
	//! @param outEulerXYZ output Euler degrees, [X, Y, Z]
	void quatToEulerDegrees(const float quatWXYZ[4], float outEulerXYZ[3]);

	//! @brief Euler angles (X, Y, Z) in DEGREES -> quaternion (w, x, y, z),
	//! composing q = Ry(Y) * Rx(X) * Rz(Z) (intrinsic Y-X-Z). The result is
	//! normalised. Inverse of quatToEulerDegrees.
	//! @param eulerXYZ input Euler degrees, [X, Y, Z]
	//! @param outQuatWXYZ output quaternion, [w, x, y, z]
	void eulerDegreesToQuat(const float eulerXYZ[3], float outQuatWXYZ[4]);
}
