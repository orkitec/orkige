/********************************************************************
	created:	Thursday 2026/07/30 at 09:00
	filename: 	MeshBuilder.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "core_util/MeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Orkige
{
	namespace
	{
		//! two-pi in float, the angle sweep every ring generator walks
		const float TWO_PI = 6.283185307179586f;
		//! degrees -> radians
		float toRadians(float degrees)
		{
			return degrees * 0.017453292519943295f;
		}
		//! a stable weld key: the quantised position plus the source index, so
		//! grouping coincident vertices is deterministic down to ties
		struct WeldKey
		{
			long long	qx;
			long long	qy;
			long long	qz;
			std::size_t	index;
		};
		bool weldKeyLess(WeldKey const & a, WeldKey const & b)
		{
			if(a.qx != b.qx) return a.qx < b.qx;
			if(a.qy != b.qy) return a.qy < b.qy;
			if(a.qz != b.qz) return a.qz < b.qz;
			return a.index < b.index;
		}
		bool weldKeySamePosition(WeldKey const & a, WeldKey const & b)
		{
			return a.qx == b.qx && a.qy == b.qy && a.qz == b.qz;
		}
		long long quantise(float value, float tolerance)
		{
			const double scaled = static_cast<double>(value) /
				static_cast<double>(tolerance);
			// llround of a non-finite value is undefined - the validate() gate
			// keeps those out, but clamp defensively so a weld can never trap
			if(!(scaled > -9.0e15 && scaled < 9.0e15))
			{
				return 0;
			}
			return static_cast<long long>(scaled < 0.0 ? scaled - 0.5
				: scaled + 0.5);
		}
	}
	//---------------------------------------------------------
	MeshBuilder::Vec3f MeshBuilder::Bounds::size() const
	{
		if(!this->valid)
		{
			return Vec3f();
		}
		return Vec3f(this->maximum.x - this->minimum.x,
			this->maximum.y - this->minimum.y,
			this->maximum.z - this->minimum.z);
	}
	//---------------------------------------------------------
	MeshBuilder::Vec3f MeshBuilder::Bounds::centre() const
	{
		if(!this->valid)
		{
			return Vec3f();
		}
		return Vec3f((this->maximum.x + this->minimum.x) * 0.5f,
			(this->maximum.y + this->minimum.y) * 0.5f,
			(this->maximum.z + this->minimum.z) * 0.5f);
	}
	//---------------------------------------------------------
	void MeshBuilder::Bounds::include(Vec3f const & point)
	{
		if(!this->valid)
		{
			this->minimum = point;
			this->maximum = point;
			this->valid = true;
			return;
		}
		this->minimum.x = std::min(this->minimum.x, point.x);
		this->minimum.y = std::min(this->minimum.y, point.y);
		this->minimum.z = std::min(this->minimum.z, point.z);
		this->maximum.x = std::max(this->maximum.x, point.x);
		this->maximum.y = std::max(this->maximum.y, point.y);
		this->maximum.z = std::max(this->maximum.z, point.z);
	}
	//---------------------------------------------------------
	void MeshBuilder::Mesh::clear()
	{
		this->vertices.clear();
		this->indices.clear();
		this->sections.clear();
	}
	//---------------------------------------------------------
	MeshBuilder::Bounds MeshBuilder::Mesh::computeBounds() const
	{
		Bounds bounds;
		for(std::size_t each = 0; each < this->vertices.size(); ++each)
		{
			bounds.include(this->vertices[each].position);
		}
		return bounds;
	}
	//---------------------------------------------------------
	//--- Xform ----------------------------------------------------
	//---------------------------------------------------------
	MeshBuilder::Xform::Xform()
	{
		for(int row = 0; row < 3; ++row)
		{
			for(int column = 0; column < 4; ++column)
			{
				this->m[row][column] = (row == column) ? 1.0f : 0.0f;
			}
		}
	}
	//---------------------------------------------------------
	MeshBuilder::Xform MeshBuilder::Xform::fromTRS(Vec3f const & translation,
		Vec3f const & rotationDegrees, Vec3f const & scale)
	{
		const float yaw = toRadians(rotationDegrees.y);
		const float pitch = toRadians(rotationDegrees.x);
		const float roll = toRadians(rotationDegrees.z);
		const float sy = std::sin(yaw), cy = std::cos(yaw);
		const float sp = std::sin(pitch), cp = std::cos(pitch);
		const float sr = std::sin(roll), cr = std::cos(roll);
		// R = Ry(yaw) * Rx(pitch) * Rz(roll)
		float rotation[3][3];
		rotation[0][0] = cy * cr + sy * sp * sr;
		rotation[0][1] = -cy * sr + sy * sp * cr;
		rotation[0][2] = sy * cp;
		rotation[1][0] = cp * sr;
		rotation[1][1] = cp * cr;
		rotation[1][2] = -sp;
		rotation[2][0] = -sy * cr + cy * sp * sr;
		rotation[2][1] = sy * sr + cy * sp * cr;
		rotation[2][2] = cy * cp;
		const float axisScale[3] = { scale.x, scale.y, scale.z };
		Xform out;
		for(int row = 0; row < 3; ++row)
		{
			for(int column = 0; column < 3; ++column)
			{
				out.m[row][column] = rotation[row][column] *
					axisScale[column];
			}
		}
		out.m[0][3] = translation.x;
		out.m[1][3] = translation.y;
		out.m[2][3] = translation.z;
		return out;
	}
	//---------------------------------------------------------
	MeshBuilder::Xform MeshBuilder::Xform::then(Xform const & outer) const
	{
		Xform out;
		for(int row = 0; row < 3; ++row)
		{
			for(int column = 0; column < 3; ++column)
			{
				out.m[row][column] =
					outer.m[row][0] * this->m[0][column] +
					outer.m[row][1] * this->m[1][column] +
					outer.m[row][2] * this->m[2][column];
			}
			out.m[row][3] =
				outer.m[row][0] * this->m[0][3] +
				outer.m[row][1] * this->m[1][3] +
				outer.m[row][2] * this->m[2][3] + outer.m[row][3];
		}
		return out;
	}
	//---------------------------------------------------------
	MeshBuilder::Vec3f MeshBuilder::Xform::transformPoint(
		Vec3f const & point) const
	{
		return Vec3f(
			this->m[0][0] * point.x + this->m[0][1] * point.y +
				this->m[0][2] * point.z + this->m[0][3],
			this->m[1][0] * point.x + this->m[1][1] * point.y +
				this->m[1][2] * point.z + this->m[1][3],
			this->m[2][0] * point.x + this->m[2][1] * point.y +
				this->m[2][2] * point.z + this->m[2][3]);
	}
	//---------------------------------------------------------
	float MeshBuilder::Xform::linearDeterminant() const
	{
		return this->m[0][0] * (this->m[1][1] * this->m[2][2] -
				this->m[1][2] * this->m[2][1])
			- this->m[0][1] * (this->m[1][0] * this->m[2][2] -
				this->m[1][2] * this->m[2][0])
			+ this->m[0][2] * (this->m[1][0] * this->m[2][1] -
				this->m[1][1] * this->m[2][0]);
	}
	//---------------------------------------------------------
	MeshBuilder::Vec3f MeshBuilder::Xform::transformNormal(
		Vec3f const & normal) const
	{
		// the TRUE inverse-transpose (not the cofactor matrix): under a
		// MIRRORING transform the cofactor form carries the negative
		// determinant into the direction and points the normal back INTO the
		// surface, which is exactly the case append() must get right
		const float determinant = this->linearDeterminant();
		if(std::fabs(determinant) < 1.0e-12f)
		{
			return normalise(normal, normal);
		}
		const float inverseDeterminant = 1.0f / determinant;
		// cofactor entries (adjugate transposed); inverse-transpose = cofactor
		// scaled by 1/determinant, applied row-wise to the normal
		const float c00 = (this->m[1][1] * this->m[2][2] -
			this->m[1][2] * this->m[2][1]) * inverseDeterminant;
		const float c01 = (this->m[1][2] * this->m[2][0] -
			this->m[1][0] * this->m[2][2]) * inverseDeterminant;
		const float c02 = (this->m[1][0] * this->m[2][1] -
			this->m[1][1] * this->m[2][0]) * inverseDeterminant;
		const float c10 = (this->m[0][2] * this->m[2][1] -
			this->m[0][1] * this->m[2][2]) * inverseDeterminant;
		const float c11 = (this->m[0][0] * this->m[2][2] -
			this->m[0][2] * this->m[2][0]) * inverseDeterminant;
		const float c12 = (this->m[0][1] * this->m[2][0] -
			this->m[0][0] * this->m[2][1]) * inverseDeterminant;
		const float c20 = (this->m[0][1] * this->m[1][2] -
			this->m[0][2] * this->m[1][1]) * inverseDeterminant;
		const float c21 = (this->m[0][2] * this->m[1][0] -
			this->m[0][0] * this->m[1][2]) * inverseDeterminant;
		const float c22 = (this->m[0][0] * this->m[1][1] -
			this->m[0][1] * this->m[1][0]) * inverseDeterminant;
		const Vec3f transformed(
			c00 * normal.x + c01 * normal.y + c02 * normal.z,
			c10 * normal.x + c11 * normal.y + c12 * normal.z,
			c20 * normal.x + c21 * normal.y + c22 * normal.z);
		return normalise(transformed, normal);
	}
	//---------------------------------------------------------
	//--- assembly -------------------------------------------------
	//---------------------------------------------------------
	MeshBuilder::Section & MeshBuilder::openSection(Mesh & mesh,
		String const & material)
	{
		if(!mesh.sections.empty() && mesh.sections.back().material == material)
		{
			return mesh.sections.back();
		}
		Section section;
		section.material = material;
		section.vertexStart = mesh.vertices.size();
		section.indexStart = mesh.indices.size();
		mesh.sections.push_back(section);
		return mesh.sections.back();
	}
	//---------------------------------------------------------
	void MeshBuilder::closeSection(Mesh & mesh)
	{
		if(mesh.sections.empty())
		{
			return;
		}
		Section & section = mesh.sections.back();
		section.vertexCount = mesh.vertices.size() - section.vertexStart;
		section.indexCount = mesh.indices.size() - section.indexStart;
	}
	//---------------------------------------------------------
	void MeshBuilder::append(Mesh & destination, Mesh const & source,
		Xform const & place, String const & material)
	{
		if(source.vertices.empty() || source.indices.empty())
		{
			return;
		}
		const bool mirrored = place.linearDeterminant() < 0.0f;
		const std::size_t base = destination.vertices.size();
		openSection(destination, material);
		destination.vertices.reserve(base + source.vertices.size());
		for(std::size_t each = 0; each < source.vertices.size(); ++each)
		{
			Vertex vertex = source.vertices[each];
			vertex.position = place.transformPoint(vertex.position);
			vertex.normal = place.transformNormal(vertex.normal);
			const Vec3f tangent(vertex.tangent.x, vertex.tangent.y,
				vertex.tangent.z);
			const Vec3f moved = normalise(Vec3f(
				place.m[0][0] * tangent.x + place.m[0][1] * tangent.y +
					place.m[0][2] * tangent.z,
				place.m[1][0] * tangent.x + place.m[1][1] * tangent.y +
					place.m[1][2] * tangent.z,
				place.m[2][0] * tangent.x + place.m[2][1] * tangent.y +
					place.m[2][2] * tangent.z), tangent);
			vertex.tangent = Vec4f(moved.x, moved.y, moved.z,
				mirrored ? -vertex.tangent.w : vertex.tangent.w);
			destination.vertices.push_back(vertex);
		}
		destination.indices.reserve(destination.indices.size() +
			source.indices.size());
		const std::size_t triangles = source.indices.size() / 3;
		for(std::size_t each = 0; each < triangles; ++each)
		{
			const unsigned int a = source.indices[each * 3 + 0];
			const unsigned int b = source.indices[each * 3 + 1];
			const unsigned int c = source.indices[each * 3 + 2];
			const unsigned int offset = static_cast<unsigned int>(base);
			destination.indices.push_back(a + offset);
			// a mirroring transform reverses the geometric winding, so flip the
			// triangle back to keep front faces outward
			destination.indices.push_back((mirrored ? c : b) + offset);
			destination.indices.push_back((mirrored ? b : c) + offset);
		}
		closeSection(destination);
	}
	//---------------------------------------------------------
	void MeshBuilder::appendSections(Mesh & destination, Mesh const & source,
		Xform const & place)
	{
		if(source.sections.empty())
		{
			append(destination, source, place, String());
			return;
		}
		for(std::size_t each = 0; each < source.sections.size(); ++each)
		{
			Section const & section = source.sections[each];
			if(section.indexCount == 0)
			{
				continue;
			}
			// lift the section into a standalone mesh (its indices already
			// address only its own vertex span - rebase them to 0) and append
			// that, so the destination's spans stay contiguous
			Mesh piece;
			piece.vertices.assign(
				source.vertices.begin() + static_cast<std::ptrdiff_t>(
					section.vertexStart),
				source.vertices.begin() + static_cast<std::ptrdiff_t>(
					section.vertexStart + section.vertexCount));
			piece.indices.reserve(section.indexCount);
			for(std::size_t at = 0; at < section.indexCount; ++at)
			{
				piece.indices.push_back(
					source.indices[section.indexStart + at] -
					static_cast<unsigned int>(section.vertexStart));
			}
			append(destination, piece, place, section.material);
		}
	}
	//---------------------------------------------------------
	void MeshBuilder::transform(Mesh & mesh, Xform const & place)
	{
		const bool mirrored = place.linearDeterminant() < 0.0f;
		for(std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			Vertex & vertex = mesh.vertices[each];
			vertex.position = place.transformPoint(vertex.position);
			vertex.normal = place.transformNormal(vertex.normal);
			const Vec3f tangent(vertex.tangent.x, vertex.tangent.y,
				vertex.tangent.z);
			const Vec3f moved = normalise(Vec3f(
				place.m[0][0] * tangent.x + place.m[0][1] * tangent.y +
					place.m[0][2] * tangent.z,
				place.m[1][0] * tangent.x + place.m[1][1] * tangent.y +
					place.m[1][2] * tangent.z,
				place.m[2][0] * tangent.x + place.m[2][1] * tangent.y +
					place.m[2][2] * tangent.z), tangent);
			vertex.tangent = Vec4f(moved.x, moved.y, moved.z,
				mirrored ? -vertex.tangent.w : vertex.tangent.w);
		}
		if(mirrored)
		{
			const std::size_t triangles = mesh.indices.size() / 3;
			for(std::size_t each = 0; each < triangles; ++each)
			{
				std::swap(mesh.indices[each * 3 + 1],
					mesh.indices[each * 3 + 2]);
			}
		}
	}
	//---------------------------------------------------------
	//--- finishing ------------------------------------------------
	//---------------------------------------------------------
	void MeshBuilder::computeFlatNormals(Mesh & mesh)
	{
		if(mesh.indices.empty())
		{
			return;
		}
		std::vector<Vertex> vertices;
		vertices.reserve(mesh.indices.size());
		const std::size_t triangles = mesh.indices.size() / 3;
		for(std::size_t each = 0; each < triangles; ++each)
		{
			Vertex corner[3];
			for(int at = 0; at < 3; ++at)
			{
				corner[at] = mesh.vertices[mesh.indices[each * 3 + at]];
			}
			const Vec3f faceNormal = normalise(cross(
				subtract(corner[1].position, corner[0].position),
				subtract(corner[2].position, corner[0].position)));
			for(int at = 0; at < 3; ++at)
			{
				corner[at].normal = faceNormal;
				vertices.push_back(corner[at]);
			}
		}
		// sections keep their triangle ranges; the split makes each section's
		// vertex span exactly its index span
		std::size_t running = 0;
		for(std::size_t each = 0; each < mesh.sections.size(); ++each)
		{
			Section & section = mesh.sections[each];
			section.vertexStart = running;
			section.vertexCount = section.indexCount;
			section.indexStart = running;
			running += section.indexCount;
		}
		mesh.vertices.swap(vertices);
		mesh.indices.resize(mesh.vertices.size());
		for(std::size_t each = 0; each < mesh.indices.size(); ++each)
		{
			mesh.indices[each] = static_cast<unsigned int>(each);
		}
	}
	//---------------------------------------------------------
	void MeshBuilder::computeSmoothNormals(Mesh & mesh,
		float smoothAngleDegrees, float weldTolerance)
	{
		if(mesh.indices.empty() || mesh.vertices.empty())
		{
			return;
		}
		if(!(weldTolerance > 0.0f))
		{
			weldTolerance = 1.0e-5f;
		}
		const std::size_t triangles = mesh.indices.size() / 3;
		// area-weighted face normals (the un-normalised cross product IS the
		// area weight) plus their unit direction for the angle test
		std::vector<Vec3f> faceWeighted(triangles);
		std::vector<Vec3f> faceUnit(triangles);
		for(std::size_t each = 0; each < triangles; ++each)
		{
			Vec3f const & a = mesh.vertices[mesh.indices[each * 3 + 0]].position;
			Vec3f const & b = mesh.vertices[mesh.indices[each * 3 + 1]].position;
			Vec3f const & c = mesh.vertices[mesh.indices[each * 3 + 2]].position;
			faceWeighted[each] = cross(subtract(b, a), subtract(c, a));
			faceUnit[each] = normalise(faceWeighted[each]);
		}
		// per-vertex adjacency (deterministic: triangle order)
		std::vector<std::vector<std::size_t> > adjacency(mesh.vertices.size());
		for(std::size_t each = 0; each < triangles; ++each)
		{
			for(int at = 0; at < 3; ++at)
			{
				adjacency[mesh.indices[each * 3 + at]].push_back(each);
			}
		}
		// weld groups by quantised position, stable-sorted
		std::vector<WeldKey> keys(mesh.vertices.size());
		for(std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			Vec3f const & position = mesh.vertices[each].position;
			keys[each].qx = quantise(position.x, weldTolerance);
			keys[each].qy = quantise(position.y, weldTolerance);
			keys[each].qz = quantise(position.z, weldTolerance);
			keys[each].index = each;
		}
		std::sort(keys.begin(), keys.end(), weldKeyLess);
		std::vector<std::size_t> groupOf(mesh.vertices.size(), 0);
		std::vector<std::vector<std::size_t> > groups;
		for(std::size_t each = 0; each < keys.size(); ++each)
		{
			if(each == 0 || !weldKeySamePosition(keys[each], keys[each - 1]))
			{
				groups.push_back(std::vector<std::size_t>());
			}
			groups.back().push_back(keys[each].index);
			groupOf[keys[each].index] = groups.size() - 1;
		}
		const float cosineLimit = std::cos(toRadians(
			std::max(0.0f, std::min(180.0f, smoothAngleDegrees))));
		for(std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			// the vertex's OWN faces set the reference direction
			Vec3f own;
			for(std::size_t at = 0; at < adjacency[each].size(); ++at)
			{
				own = add(own, faceWeighted[adjacency[each][at]]);
			}
			Vec3f reference = normalise(own, mesh.vertices[each].normal);
			Vec3f accumulated = own;
			std::vector<std::size_t> const & group = groups[groupOf[each]];
			for(std::size_t sibling = 0; sibling < group.size(); ++sibling)
			{
				if(group[sibling] == each)
				{
					continue;
				}
				std::vector<std::size_t> const & faces =
					adjacency[group[sibling]];
				for(std::size_t at = 0; at < faces.size(); ++at)
				{
					if(dot(faceUnit[faces[at]], reference) >= cosineLimit)
					{
						accumulated = add(accumulated, faceWeighted[faces[at]]);
					}
				}
			}
			mesh.vertices[each].normal = normalise(accumulated, reference);
		}
	}
	//---------------------------------------------------------
	void MeshBuilder::applyUV(Mesh & mesh, UvMode mode, Vec2f const & scale)
	{
		if(mesh.vertices.empty())
		{
			return;
		}
		const Bounds bounds = mesh.computeBounds();
		const Vec3f size = bounds.size();
		const Vec3f centre = bounds.centre();
		const float sizeX = (std::fabs(size.x) > 1.0e-6f) ? size.x : 1.0f;
		const float sizeY = (std::fabs(size.y) > 1.0e-6f) ? size.y : 1.0f;
		const float sizeZ = (std::fabs(size.z) > 1.0e-6f) ? size.z : 1.0f;
		const float scaleU = (std::fabs(scale.x) > 1.0e-6f) ? scale.x : 1.0f;
		const float scaleV = (std::fabs(scale.y) > 1.0e-6f) ? scale.y : 1.0f;
		for(std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			Vertex & vertex = mesh.vertices[each];
			Vec3f const & position = vertex.position;
			UvMode effective = mode;
			if(mode == UV_BOX)
			{
				// the dominant axis of the NORMAL picks the planar projection
				const float ax = std::fabs(vertex.normal.x);
				const float ay = std::fabs(vertex.normal.y);
				const float az = std::fabs(vertex.normal.z);
				if(ay >= ax && ay >= az)
				{
					effective = UV_PLANAR_XZ;
				}
				else if(ax >= az)
				{
					effective = UV_PLANAR_ZY;
				}
				else
				{
					effective = UV_PLANAR_XY;
				}
			}
			float u = 0.0f;
			float v = 0.0f;
			switch(effective)
			{
			case UV_PLANAR_XZ:
				u = (position.x - bounds.minimum.x) / sizeX;
				v = (position.z - bounds.minimum.z) / sizeZ;
				break;
			case UV_PLANAR_XY:
				u = (position.x - bounds.minimum.x) / sizeX;
				v = 1.0f - (position.y - bounds.minimum.y) / sizeY;
				break;
			case UV_PLANAR_ZY:
				u = (position.z - bounds.minimum.z) / sizeZ;
				v = 1.0f - (position.y - bounds.minimum.y) / sizeY;
				break;
			case UV_CYLINDRICAL:
				u = std::atan2(position.z - centre.z, position.x - centre.x) /
					TWO_PI + 0.5f;
				v = 1.0f - (position.y - bounds.minimum.y) / sizeY;
				break;
			case UV_SPHERICAL:
			{
				const Vec3f direction = normalise(subtract(position, centre));
				u = std::atan2(direction.z, direction.x) / TWO_PI + 0.5f;
				v = std::acos(std::max(-1.0f, std::min(1.0f, direction.y))) /
					3.14159265358979f;
				break;
			}
			case UV_BOX:
				break;	// resolved above
			}
			vertex.uv = Vec2f(u * scaleU, v * scaleV);
		}
	}
	//---------------------------------------------------------
	void MeshBuilder::computeTangents(Mesh & mesh)
	{
		if(mesh.vertices.empty() || mesh.indices.empty())
		{
			return;
		}
		std::vector<Vec3f> tangentSum(mesh.vertices.size());
		std::vector<Vec3f> bitangentSum(mesh.vertices.size());
		const std::size_t triangles = mesh.indices.size() / 3;
		for(std::size_t each = 0; each < triangles; ++each)
		{
			const unsigned int i0 = mesh.indices[each * 3 + 0];
			const unsigned int i1 = mesh.indices[each * 3 + 1];
			const unsigned int i2 = mesh.indices[each * 3 + 2];
			Vertex const & v0 = mesh.vertices[i0];
			Vertex const & v1 = mesh.vertices[i1];
			Vertex const & v2 = mesh.vertices[i2];
			const Vec3f edge1 = subtract(v1.position, v0.position);
			const Vec3f edge2 = subtract(v2.position, v0.position);
			const float du1 = v1.uv.x - v0.uv.x;
			const float dv1 = v1.uv.y - v0.uv.y;
			const float du2 = v2.uv.x - v0.uv.x;
			const float dv2 = v2.uv.y - v0.uv.y;
			const float determinant = du1 * dv2 - du2 * dv1;
			if(std::fabs(determinant) < 1.0e-12f)
			{
				continue;	// degenerate UV triangle - the fallback covers it
			}
			const float inverse = 1.0f / determinant;
			const Vec3f tangent(
				(edge1.x * dv2 - edge2.x * dv1) * inverse,
				(edge1.y * dv2 - edge2.y * dv1) * inverse,
				(edge1.z * dv2 - edge2.z * dv1) * inverse);
			const Vec3f bitangent(
				(edge2.x * du1 - edge1.x * du2) * inverse,
				(edge2.y * du1 - edge1.y * du2) * inverse,
				(edge2.z * du1 - edge1.z * du2) * inverse);
			const unsigned int corner[3] = { i0, i1, i2 };
			for(int at = 0; at < 3; ++at)
			{
				tangentSum[corner[at]] = add(tangentSum[corner[at]], tangent);
				bitangentSum[corner[at]] = add(bitangentSum[corner[at]],
					bitangent);
			}
		}
		for(std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			Vertex & vertex = mesh.vertices[each];
			const Vec3f normal = normalise(vertex.normal);
			// Gram-Schmidt: strip the normal component out of the accumulated
			// tangent, then fall back to ANY perpendicular when the result is
			// degenerate (a UV-less or seam vertex) - the Hlms rejects a zero
			// tangent, so this must always produce a finite unit frame
			Vec3f tangent = subtract(tangentSum[each],
				scale(normal, dot(normal, tangentSum[each])));
			if(length(tangent) < 1.0e-8f)
			{
				const Vec3f axis = (std::fabs(normal.y) < 0.9f)
					? Vec3f(0.0f, 1.0f, 0.0f) : Vec3f(1.0f, 0.0f, 0.0f);
				tangent = cross(axis, normal);
			}
			tangent = normalise(tangent, Vec3f(1.0f, 0.0f, 0.0f));
			const float handedness =
				(dot(cross(normal, tangent), bitangentSum[each]) < 0.0f)
				? -1.0f : 1.0f;
			vertex.normal = normal;
			vertex.tangent = Vec4f(tangent.x, tangent.y, tangent.z, handedness);
		}
	}
	//---------------------------------------------------------
	//--- helpers --------------------------------------------------
	//---------------------------------------------------------
	bool MeshBuilder::validate(Mesh const & mesh, String * outError)
	{
		char buffer[256];
		if(mesh.indices.size() % 3 != 0)
		{
			if(outError) *outError = "index count is not a multiple of 3";
			return false;
		}
		for(std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			Vertex const & vertex = mesh.vertices[each];
			if(!isFinite(vertex.position.x) || !isFinite(vertex.position.y) ||
				!isFinite(vertex.position.z) || !isFinite(vertex.normal.x) ||
				!isFinite(vertex.normal.y) || !isFinite(vertex.normal.z) ||
				!isFinite(vertex.uv.x) || !isFinite(vertex.uv.y) ||
				!isFinite(vertex.tangent.x) || !isFinite(vertex.tangent.y) ||
				!isFinite(vertex.tangent.z) || !isFinite(vertex.tangent.w))
			{
				std::snprintf(buffer, sizeof(buffer),
					"vertex %u carries a non-finite component",
					static_cast<unsigned int>(each));
				if(outError) *outError = buffer;
				return false;
			}
		}
		if(mesh.sections.empty())
		{
			if(!mesh.indices.empty())
			{
				if(outError) *outError = "geometry without a material section";
				return false;
			}
			return true;
		}
		std::size_t vertexRunning = 0;
		std::size_t indexRunning = 0;
		for(std::size_t each = 0; each < mesh.sections.size(); ++each)
		{
			Section const & section = mesh.sections[each];
			if(section.vertexStart != vertexRunning ||
				section.indexStart != indexRunning)
			{
				std::snprintf(buffer, sizeof(buffer),
					"section %u is not contiguous with its predecessor",
					static_cast<unsigned int>(each));
				if(outError) *outError = buffer;
				return false;
			}
			if(section.indexCount % 3 != 0)
			{
				std::snprintf(buffer, sizeof(buffer),
					"section %u index count is not a multiple of 3",
					static_cast<unsigned int>(each));
				if(outError) *outError = buffer;
				return false;
			}
			for(std::size_t at = 0; at < section.indexCount; ++at)
			{
				const unsigned int index = mesh.indices[section.indexStart + at];
				if(index < section.vertexStart ||
					index >= section.vertexStart + section.vertexCount)
				{
					std::snprintf(buffer, sizeof(buffer),
						"section %u index %u leaves its own vertex span",
						static_cast<unsigned int>(each), index);
					if(outError) *outError = buffer;
					return false;
				}
			}
			vertexRunning += section.vertexCount;
			indexRunning += section.indexCount;
		}
		if(vertexRunning != mesh.vertices.size() ||
			indexRunning != mesh.indices.size())
		{
			if(outError) *outError = "sections do not cover the mesh arrays";
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	unsigned int MeshBuilder::clampSegments(int requested, unsigned int minimum)
	{
		if(minimum < 1)
		{
			minimum = 1;
		}
		if(requested < static_cast<int>(minimum))
		{
			return minimum;
		}
		if(static_cast<unsigned int>(requested) > MAX_SEGMENTS)
		{
			return MAX_SEGMENTS;
		}
		return static_cast<unsigned int>(requested);
	}
	//---------------------------------------------------------
	bool MeshBuilder::isFinite(float value)
	{
		return std::isfinite(value);
	}
	//---------------------------------------------------------
	bool MeshBuilder::isPositiveExtent(float value)
	{
		return std::isfinite(value) && value > 0.0f;
	}
	//---------------------------------------------------------
	MeshBuilder::Vec3f MeshBuilder::add(Vec3f const & a, Vec3f const & b)
	{
		return Vec3f(a.x + b.x, a.y + b.y, a.z + b.z);
	}
	//---------------------------------------------------------
	MeshBuilder::Vec3f MeshBuilder::subtract(Vec3f const & a, Vec3f const & b)
	{
		return Vec3f(a.x - b.x, a.y - b.y, a.z - b.z);
	}
	//---------------------------------------------------------
	MeshBuilder::Vec3f MeshBuilder::scale(Vec3f const & a, float factor)
	{
		return Vec3f(a.x * factor, a.y * factor, a.z * factor);
	}
	//---------------------------------------------------------
	float MeshBuilder::dot(Vec3f const & a, Vec3f const & b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}
	//---------------------------------------------------------
	MeshBuilder::Vec3f MeshBuilder::cross(Vec3f const & a, Vec3f const & b)
	{
		return Vec3f(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x);
	}
	//---------------------------------------------------------
	float MeshBuilder::length(Vec3f const & a)
	{
		return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
	}
	//---------------------------------------------------------
	MeshBuilder::Vec3f MeshBuilder::normalise(Vec3f const & a,
		Vec3f const & fallback)
	{
		const float len = length(a);
		if(!(len > 1.0e-12f) || !std::isfinite(len))
		{
			const float fallbackLength = length(fallback);
			if(!(fallbackLength > 1.0e-12f) || !std::isfinite(fallbackLength))
			{
				return Vec3f(0.0f, 1.0f, 0.0f);
			}
			return scale(fallback, 1.0f / fallbackLength);
		}
		return scale(a, 1.0f / len);
	}
}
