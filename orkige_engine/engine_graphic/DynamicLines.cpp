/**************************************************************
	created:	2010/07/31 at 11:53
	filename: 	DynamicLines.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
***************************************************************/

#include "engine_graphic/DynamicLines.h"
#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	enum 
	{ 
		POSITION_BINDING, 
		TEXCOORD_BINDING 
	}; 
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	DynamicLines::DynamicLines(Ogre::RenderOperation::OperationType opType) 
	{ 
		this->initDynamicLines(Ogre::ColourValue::Blue, opType); 
	} 
	//---------------------------------------------------------
	DynamicLines::DynamicLines(Ogre::ColourValue colour, Ogre::RenderOperation::OperationType opType) 
	{ 
		this->initDynamicLines(colour, opType); 
	} 
	//---------------------------------------------------------
	DynamicLines::~DynamicLines() 
	{ 
	} 
	//---------------------------------------------------------
	void DynamicLines::setColour(Ogre::ColourValue colour) 
	{ 
		this->getMaterial()->setAmbient(colour); 
	} 
	//---------------------------------------------------------
	void DynamicLines::setOperationType(Ogre::RenderOperation::OperationType opType) 
	{ 
		this->mRenderOp.operationType = opType; 
	} 
	//---------------------------------------------------------
	Ogre::RenderOperation::OperationType DynamicLines::getOperationType() const 
	{ 
		return this->mRenderOp.operationType; 
	} 
	//---------------------------------------------------------
	void DynamicLines::addPoint(const Ogre::Vector3 &p) 
	{ 
		this->points.push_back(p); 
		this->isDirty = true; 
	}
	//---------------------------------------------------------
	void DynamicLines::addPoint(Ogre::Real x, Ogre::Real y, Ogre::Real z) 
	{ 
		this->points.push_back(Ogre::Vector3(x,y,z)); 
		this->isDirty = true; 
	} 
	//---------------------------------------------------------
	void DynamicLines::addSegment(const Ogre::Vector3 &p1, const Ogre::Vector3 &p2) 
	{ 
		this->points.push_back(p1); 
		this->points.push_back(p2); 
		this->isDirty = true; 
	} 
	//---------------------------------------------------------
	const Ogre::Vector3& DynamicLines::getPoint(unsigned short index) const 
	{ 
		assert(index < this->points.size() && "Point index is out of bounds!!"); 
		return this->points[index]; 
	}
	//---------------------------------------------------------
	unsigned short DynamicLines::getNumPoints(void) const 
	{ 
		return (unsigned short)this->points.size(); 
	}
	//---------------------------------------------------------
	void DynamicLines::setPoint(unsigned short index, const Ogre::Vector3 &value) 
	{ 
		assert(index < this->points.size() && "Point index is out of bounds!!"); 

		this->points[index] = value; 
		this->isDirty = true; 
	} 
	//---------------------------------------------------------
	void DynamicLines::clear() 
	{ 
		this->points.clear(); 
		this->isDirty = true; 
	} 
	//---------------------------------------------------------
	void DynamicLines::update() 
	{  
		if (this->isDirty) this->fillHardwareBuffers(); 
	} 
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void DynamicLines::createVertexDeclaration() 
	{ 
		Ogre::VertexDeclaration *decl = this->mRenderOp.vertexData->vertexDeclaration; 
		decl->addElement(POSITION_BINDING, 0, Ogre::VET_FLOAT3, Ogre::VES_POSITION); 
	} 
	//---------------------------------------------------------
	void DynamicLines::fillHardwareBuffers() 
	{ 
		std::size_t size = this->points.size(); 

		this->prepareHardwareBuffers(size,0); 

		if (!size) 
		{ 
			this->mBox.setExtents(Ogre::Vector3::ZERO,Ogre::Vector3::ZERO); 
			this->isDirty=false; 
			return; 
		} 

		Ogre::Vector3 vaabMin = this->points[0]; 
		Ogre::Vector3 vaabMax = this->points[0]; 

		Ogre::HardwareVertexBufferSharedPtr vbuf = this->mRenderOp.vertexData->vertexBufferBinding->getBuffer(0); 

		Ogre::Real *prPos = static_cast<Ogre::Real*>(vbuf->lock(Ogre::HardwareBuffer::HBL_DISCARD)); 
		{ 
			for(unsigned int i = 0; i < size; i++) 
			{ 
				*prPos++ = this->points[i].x; 
				*prPos++ = this->points[i].y; 
				*prPos++ = this->points[i].z; 

				if(this->points[i].x < vaabMin.x) 
					vaabMin.x = this->points[i].x; 
				if(this->points[i].y < vaabMin.y) 
					vaabMin.y = this->points[i].y; 
				if(this->points[i].z < vaabMin.z) 
					vaabMin.z = this->points[i].z; 

				if(this->points[i].x > vaabMax.x) 
					vaabMax.x = this->points[i].x; 
				if(this->points[i].y > vaabMax.y) 
					vaabMax.y = this->points[i].y; 
				if(this->points[i].z > vaabMax.z) 
					vaabMax.z = this->points[i].z; 
			} 
		} 
		vbuf->unlock(); 

		this->mBox.setExtents(vaabMin, vaabMax); 

		this->isDirty = false; 
	}
	//---------------------------------------------------------
	void DynamicLines::initDynamicLines(Ogre::ColourValue colour, Ogre::RenderOperation::OperationType opType) 
	{ 
		this->initialize(opType,false); 
		static int matIndex = 0; 
		String matName = "DL" + Ogre::StringConverter::toString(matIndex++); 
		Ogre::MaterialPtr materialPtr = Ogre::MaterialManager::getSingleton().create(matName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME); 
		materialPtr->setAmbient(colour); 
		this->setMaterial(matName); 

		this->isDirty = true; 
	} 
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
