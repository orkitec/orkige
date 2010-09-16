/**************************************************************
	created:	2010/09/07 at 2:59
	filename: 	DynamicRenderable.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_graphic/DynamicRenderable.h"
#include <limits>
#include <algorithm>
#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	DynamicRenderable::DynamicRenderable()
	{
	}
	//---------------------------------------------------------
	DynamicRenderable::~DynamicRenderable()
	{
		delete this->mRenderOp.vertexData;
		delete this->mRenderOp.indexData;
	}
	//---------------------------------------------------------
	void DynamicRenderable::initialize(Ogre::RenderOperation::OperationType operationType, bool useIndices)
	{
		// Initialize render operation
		this->mRenderOp.operationType = operationType;
		this->mRenderOp.useIndexes = useIndices;
		this->mRenderOp.vertexData = new Ogre::VertexData();
		if (this->mRenderOp.useIndexes)
		{
			this->mRenderOp.indexData = new Ogre::IndexData();
		}

		// Reset buffer capacities
		this->vertexBufferCapacity = 0;
		this->indexBufferCapacity = 0;

		// Create vertex declaration
		this->createVertexDeclaration();
	}
	//---------------------------------------------------------
	Ogre::Real DynamicRenderable::getBoundingRadius(void) const
	{
		return Ogre::Math::Sqrt(std::max(this->mBox.getMaximum().squaredLength(), this->mBox.getMinimum().squaredLength()));
	}
	//---------------------------------------------------------
	Ogre::Real DynamicRenderable::getSquaredViewDepth(const Ogre::Camera* cam) const
	{
		Ogre::Vector3 vMin, vMax, vMid, vDist;
		vMin = this->mBox.getMinimum();
		vMax = this->mBox.getMaximum();
		vMid = ((vMin - vMax) * 0.5) + vMin;
		vDist = cam->getDerivedPosition() - vMid;

		return vDist.squaredLength();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void DynamicRenderable::prepareHardwareBuffers(std::size_t vertexCount, std::size_t indexCount)
	{
		// Prepare vertex buffer
		std::size_t newVertCapacity = this->vertexBufferCapacity;
		if ((vertexCount > this->vertexBufferCapacity) || (!this->vertexBufferCapacity))
		{
			// vertexCount exceeds current capacity!
			// It is necessary to reallocate the buffer.

			// Check if this is the first call
			if (!newVertCapacity)
			{
				newVertCapacity = 1;
			}

			// Make capacity the next power of two
			while (newVertCapacity < vertexCount)
			{
				newVertCapacity <<= 1;
			}
		}
		else if (vertexCount < this->vertexBufferCapacity>>1) 
		{
			// Make capacity the previous power of two
			while (vertexCount < newVertCapacity>>1)
			{
				newVertCapacity >>= 1;
			}
		}
		if (newVertCapacity != this->vertexBufferCapacity) 
		{
			this->vertexBufferCapacity = newVertCapacity;
			// Create new vertex buffer
			Ogre::HardwareVertexBufferSharedPtr vbuf = Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(
				this->mRenderOp.vertexData->vertexDeclaration->getVertexSize(0),
				this->vertexBufferCapacity,
				Ogre::HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY); // TODO: Custom HBU_?

			// Bind buffer
			this->mRenderOp.vertexData->vertexBufferBinding->setBinding(0, vbuf);
		}
		// Update vertex count in the render operation
		this->mRenderOp.vertexData->vertexCount = vertexCount;

		if (this->mRenderOp.useIndexes)
		{
			OgreAssert(indexCount <= std::numeric_limits<unsigned short>::max(), "indexCount exceeds 16 bit");

			std::size_t newIndexCapacity = this->indexBufferCapacity;
			// Prepare index buffer
			if ((indexCount > newIndexCapacity) || (!newIndexCapacity))
			{
				// indexCount exceeds current capacity!
				// It is necessary to reallocate the buffer.

				// Check if this is the first call
				if (!newIndexCapacity)
				{
					newIndexCapacity = 1;
				}

				// Make capacity the next power of two
				while (newIndexCapacity < indexCount)
				{
					newIndexCapacity <<= 1;
				}

			}
			else if (indexCount < newIndexCapacity>>1) 
			{
				// Make capacity the previous power of two
				while (indexCount < newIndexCapacity>>1)
				{
					newIndexCapacity >>= 1;
				}
			}

			if (newIndexCapacity != this->indexBufferCapacity)
			{
				this->indexBufferCapacity = newIndexCapacity;
				// Create new index buffer
				this->mRenderOp.indexData->indexBuffer = Ogre::HardwareBufferManager::getSingleton().createIndexBuffer(
					Ogre::HardwareIndexBuffer::IT_16BIT,
					this->indexBufferCapacity,
					Ogre::HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY); // TODO: Custom HBU_?
			}

			// Update index count in the render operation
			this->mRenderOp.indexData->indexCount = indexCount;
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
