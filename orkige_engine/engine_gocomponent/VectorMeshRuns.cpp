/**************************************************************
	created:	2026/07/17 at 10:00
	filename: 	VectorMeshRuns.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file VectorMeshRuns.cpp
//! @brief the tessellated-runs -> facade-sections converter
//! (@see VectorMeshRuns.h)

#include "engine_gocomponent/VectorMeshRuns.h"

namespace Orkige
{
	//---------------------------------------------------------
	VectorMeshRuns::VectorMeshRuns()
		: mSectioned(false)
	{
	}
	//---------------------------------------------------------
	void VectorMeshRuns::buildVertices(VectorTessellator::Mesh const & built,
		Color const & tint, std::vector<VectorMesh::Vertex> & out)
	{
		out.resize(built.positions.size());
		for(std::size_t each = 0; each < built.positions.size(); ++each)
		{
			VectorTessellator::Point const & point = built.positions[each];
			VectorTessellator::Colour const & colour = built.colours[each];
			VectorMesh::Vertex & vertex = out[each];
			vertex.position = Vec2(point.x, point.y);
			vertex.colour = Color(colour.r * tint.r, colour.g * tint.g,
				colour.b * tint.b, colour.a * tint.a);
			// the tessellator keeps uvs parallel to positions ((0,0) on
			// untextured geometry); a caller that never built runs (a plain
			// triangulate) may have an empty uv array
			vertex.uv = each < built.uvs.size()
				? Vec2(built.uvs[each].x, built.uvs[each].y) : Vec2(0.0f, 0.0f);
		}
	}
	//---------------------------------------------------------
	void VectorMeshRuns::push(VectorMesh & mesh,
		VectorTessellator::Mesh const & built,
		std::vector<VectorMesh::Vertex> const & vertices)
	{
		this->mRuns = built.runs;
		// an all-flat build (zero or one untextured run) takes the PLAIN
		// setMesh path - the exact pre-texture pipeline, byte-identical
		this->mSectioned = !(built.runs.empty() ||
			(built.runs.size() == 1 && built.runs.front().texture.empty()));
		if(!this->mSectioned)
		{
			this->mRebased.clear();
			mesh.setMesh(vertices.empty() ? NULL : vertices.data(),
				vertices.size(),
				built.indices.empty() ? NULL : built.indices.data(),
				built.indices.size());
			return;
		}
		// one facade Section per run: the run's vertex span plus its indices
		// rebased to the span (a run's triangles address only its own
		// vertices - the tessellator's contiguity contract)
		this->mRebased.resize(built.runs.size());
		std::vector<VectorMesh::Section> sections(built.runs.size());
		for(std::size_t r = 0; r < built.runs.size(); ++r)
		{
			VectorTessellator::Run const & run = built.runs[r];
			std::vector<unsigned int> & rebased = this->mRebased[r];
			rebased.resize(run.indexCount);
			for(std::size_t i = 0; i < run.indexCount; ++i)
			{
				rebased[i] = built.indices[run.indexStart + i] -
					static_cast<unsigned int>(run.vertexStart);
			}
			VectorMesh::Section & section = sections[r];
			section.texture = run.texture;
			section.vertices = vertices.empty()
				? NULL : vertices.data() + run.vertexStart;
			section.vertexCount = run.vertexCount;
			section.indices = rebased.empty() ? NULL : rebased.data();
			section.indexCount = rebased.size();
		}
		mesh.setMeshSections(sections.empty() ? NULL : sections.data(),
			sections.size());
	}
	//---------------------------------------------------------
	void VectorMeshRuns::update(VectorMesh & mesh,
		std::vector<VectorMesh::Vertex> const & vertices)
	{
		if(!this->mSectioned)
		{
			mesh.updateVertices(vertices.empty() ? NULL : vertices.data(),
				vertices.size());
			return;
		}
		for(std::size_t r = 0; r < this->mRuns.size(); ++r)
		{
			VectorTessellator::Run const & run = this->mRuns[r];
			if(run.vertexStart + run.vertexCount > vertices.size())
			{
				return;	// stale topology - the caller re-pushes first
			}
			mesh.updateSectionVertices(r,
				vertices.data() + run.vertexStart, run.vertexCount);
		}
	}
	//---------------------------------------------------------
	std::size_t VectorMeshRuns::texturedRunCount() const
	{
		std::size_t textured = 0;
		for(VectorTessellator::Run const & run : this->mRuns)
		{
			if(!run.texture.empty())
			{
				++textured;
			}
		}
		return textured;
	}
	//---------------------------------------------------------
	void VectorMeshRuns::clear()
	{
		this->mRebased.clear();
		this->mRuns.clear();
		this->mSectioned = false;
	}
}
