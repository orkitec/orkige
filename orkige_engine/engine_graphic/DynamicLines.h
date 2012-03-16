/**************************************************************
	created:	2010/07/31 at 11:51
	filename: 	DynamicLines.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __DynamicLines_h__31_7_2010__11_51_35__
#define __DynamicLines_h__31_7_2010__11_51_35__

#include <core_base/Meta.h>
#include "engine_graphic/DynamicRenderable.h"
#include <vector>

namespace Orkige
{
	//! @brief simple dynamic line drawing
	//! @see http://www.ogre3d.org/tikiwiki/DynamicLineDrawing
	class ORKIGE_ENGINE_DLL DynamicLines : public DynamicRenderable
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		std::vector<Ogre::Vector3> points; 
		bool isDirty;
		//--- Methods -----------------------------------------
	public:
		//! constructor - see setOperationType() for description of argument. 
		DynamicLines(Ogre::RenderOperation::OperationType opType); 
		//! constructor with optional line color
		DynamicLines(Ogre::ColourValue colour=Ogre::ColourValue::White, Ogre::RenderOperation::OperationType opType=Ogre::RenderOperation::OT_LINE_STRIP); 
		//! destructor
		virtual ~DynamicLines(); 

		//! change the line colour 
		void setColour(Ogre::ColourValue colour); 

		//! Add a point to the point list 
		void addPoint(const Ogre::Vector3 &p); 
		//! Add a point to the point list 
		void addPoint(Ogre::Real x, Ogre::Real y, Ogre::Real z); 

		//! Add a line segment 
		void addSegment(const Ogre::Vector3 &p1, const Ogre::Vector3 &p2); 

		//! Change the location of an existing point in the point list 
		void setPoint(unsigned short index, const Ogre::Vector3 &value); 

		//! Return the location of an existing point in the point list 
		const Ogre::Vector3& getPoint(unsigned short index) const; 

		//! Return the total number of points in the point list 
		unsigned short getNumPoints(void) const; 

		//! Remove all points from the point list 
		void clear(); 

		//! Call this to update the hardware buffer after making changes.  
		void update(); 

		//! Set the type of operation to draw with. 
		//! @param opType Can be one of 
		//!    - RenderOperation::OT_LINE_STRIP 
		//!    - RenderOperation::OT_LINE_LIST 
		//!    - RenderOperation::OT_POINT_LIST 
		//!    - RenderOperation::OT_TRIANGLE_LIST 
		//!    - RenderOperation::OT_TRIANGLE_STRIP 
		//!    - RenderOperation::OT_TRIANGLE_FAN 
		//!    The default is OT_LINE_STRIP.
		void setOperationType(Ogre::RenderOperation::OperationType opType);
		//! get operation type
		Ogre::RenderOperation::OperationType getOperationType() const; 
	protected:
		//! Implementation DynamicRenderable, creates a simple vertex-only decl 
		virtual void createVertexDeclaration(); 
		//! Implementation DynamicRenderable, pushes point list out to hardware memory 
		virtual void fillHardwareBuffers();
		//! init lines with given color and OperationType
		void initDynamicLines(Ogre::ColourValue colour, Ogre::RenderOperation::OperationType opType); 
	private:
	};
	//---------------------------------------------------------
}

#endif //__DynamicLines_h__31_7_2010__11_51_35__
