/**************************************************************
	created:	2010/09/07 at 2:48
	filename: 	DynamicRenderable.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __DynamicRenderable_h__7_9_2010__2_48_31__
#define __DynamicRenderable_h__7_9_2010__2_48_31__

#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	//! @brief Abstract base class providing mechanisms for dynamically growing hardware buffers.
	//! @see http://www.ogre3d.org/tikiwiki/DynamicGrowingBuffers
	class ORKIGE_ENGINE_DLL DynamicRenderable : public Ogre::SimpleRenderable
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		std::size_t vertexBufferCapacity;	//!< Maximum capacity of the currently allocated vertex buffer.
		std::size_t indexBufferCapacity;	//!< Maximum capacity of the currently allocated index buffer.
	private:
		//--- Methods -----------------------------------------
	public:
		//! Constructor
		DynamicRenderable();
		//! Virtual destructor
		virtual ~DynamicRenderable();

		//! Initializes the dynamic renderable.
		//! @remarks
		//! This function should only be called once. It initializes the
		//! render operation, and calls the abstract function
		//! createVertexDeclaration().
		//! @param operationType The type of render operation to perform.
		//! @param useIndices Specifies whether to use indices to determine the
		//! vertices to use as input.
		void initialize(Ogre::RenderOperation::OperationType operationType,	bool useIndices);

		//! Implementation of Ogre::SimpleRenderable
		virtual Ogre::Real getBoundingRadius(void) const;
		//! Implementation of Ogre::SimpleRenderable
		virtual Ogre::Real getSquaredViewDepth(const Ogre::Camera* cam) const;
	protected:
		//! Creates the vertex declaration.
		//! @remarks
		//! Override and set mRenderOp.vertexData->vertexDeclaration here.
		//! mRenderOp.vertexData will be created for you before this method
		//! is called.
		virtual void createVertexDeclaration() = 0;

		//! Prepares the hardware buffers for the requested vertex and index counts.
		//! @remarks
		//! This function must be called before locking the buffers in
		//! fillHardwareBuffers(). It guarantees that the hardware buffers
		//! are large enough to hold at least the requested number of
		//! vertices and indices (if using indices). The buffers are
		//! possibly reallocated to achieve this.
		//! @par
		//! The vertex and index count in the render operation are set to
		//! the values of vertexCount and indexCount respectively.
		//! @param vertexCount The number of vertices the buffer must hold.
		//! 
		//! @param indexCount The number of indices the buffer must hold. This
		//! parameter is ignored if not using indices.
		void prepareHardwareBuffers(std::size_t vertexCount, std::size_t indexCount);

		//! Fills the hardware vertex and index buffers with data.
		//! @remarks
		//! This function must call prepareHardwareBuffers() before locking
		//! the buffers to ensure the they are large enough for the data to
		//! be written. Afterwards the vertex and index buffers (if using
		//! indices) can be locked, and data can be written to them.
		virtual void fillHardwareBuffers() = 0;
	private:
	};
	//---------------------------------------------------------
}

#endif //__DynamicRenderable_h__7_9_2010__2_48_31__
