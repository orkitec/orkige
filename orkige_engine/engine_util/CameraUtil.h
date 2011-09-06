/********************************************************************
	created:	Friday 2010/08/06 at 18:57
	filename: 	CameraUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __CameraUtil_h__6_8_2010__18_57_46__
#define __CameraUtil_h__6_8_2010__18_57_46__

#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	//! camera utilities
	namespace CameraUtil
	{
		//! Converts a 2D screen coordinate (in pixels) to a 3D ray into the scene.
		inline Ogre::Ray screenToScene(Ogre::Camera* cam, const Ogre::Vector2& pt)
		{
			return cam->getCameraToViewportRay(pt.x, pt.y);
		}
		//---------------------------------------------------------------
		//! Converts a 3D scene position to a 2D screen coordinate (in pixels).
		inline  Ogre::Vector2 sceneToScreen(Ogre::Camera* cam, const Ogre::Vector3& pt)
		{
			Ogre::Vector3 result = cam->getProjectionMatrix() * cam->getViewMatrix() * pt;
			return Ogre::Vector2((result.x + 1) / 2, -(result.y + 1) / 2);
		}
		//! Converts a 3D scene position to a 2D screen relative coordinate (in pixels).
		inline  Ogre::Vector2 sceneToScreenRelative(Ogre::Camera* cam, const Ogre::Vector3& pt)
		{
			Ogre::Vector3 hcsPosition = cam->getProjectionMatrix() * (cam->getViewMatrix() * pt);

			Ogre::Vector3 directionCam =  cam->getRealDirection();
			Ogre::Vector3 directionObj =  pt - cam->getRealPosition();
			float anglee =	directionCam.angleBetween(directionObj).valueDegrees();

			int nCWidth = (cam->getViewport()->getActualWidth()/2);
			int nCHeight = (cam->getViewport()->getActualHeight()/2);

			Ogre::Vector2 screenPos;



			screenPos.x = nCWidth + (nCWidth * hcsPosition.x);
			screenPos.y = nCHeight + (nCHeight * -hcsPosition.y);

			switch (cam->getViewport()->getOrientationMode())
			{
			case Ogre::OR_DEGREE_0:   //OR_PORTRAIT
				screenPos.x = nCWidth + (nCWidth * hcsPosition.x);
				screenPos.y = nCHeight + (nCHeight * -hcsPosition.y);
				break;
			case Ogre::OR_DEGREE_90:  //OR_LANDSCAPERIGHT

				screenPos.x = nCWidth + (nCWidth * -hcsPosition.y);
				screenPos.y = nCHeight + (nCHeight * -hcsPosition.x);				
				break;
			case Ogre::OR_DEGREE_180:
				screenPos.x = nCWidth + (nCWidth * hcsPosition.x);
				screenPos.y = nCHeight + (nCHeight * -hcsPosition.y);
				break;
			case Ogre::OR_DEGREE_270: //OR_LANDSCAPELEFT
				screenPos.x = nCWidth + (nCWidth * -hcsPosition.y);
				screenPos.y = nCHeight + (nCHeight * -hcsPosition.x);				
				break;
			}

 			if (anglee > 90.0)
 			{
 				return Ogre::Vector2(cam->getViewport()->getActualWidth()-screenPos.x, Ogre::Math::Abs(screenPos.y) + cam->getViewport()->getActualHeight());
 			}
 			else
			{
				return screenPos;
			}			

		}
		//---------------------------------------------------------------
		//! create high resolution screenshots res = current window resolution * gridSize
		inline void makeGridScreenShot(Ogre::Camera* cam, Ogre::RenderWindow* win,  std::size_t gridSize)
		{
			String fileName = "ScreenShot";
			String fileExtention = ".png";
			bool stitchGridImages = true;

			Ogre::Vector3 posOld = cam->getPosition();
			Ogre::Quaternion orientOld = cam->getOrientation();
			cam->setPosition(cam->getRealPosition());
			cam->setOrientation(cam->getRealOrientation());
			cam->setCustomProjectionMatrix(false); // reset projection matrix 
			Ogre::Matrix4 standard = cam->getProjectionMatrix(); 
			Ogre::Real nearDist = cam->getNearClipDistance(); 
			Ogre::Real nearWidth = (cam->getWorldSpaceCorners()[0] - cam->getWorldSpaceCorners()[1]).length(); 
			Ogre::Real nearHeight = (cam->getWorldSpaceCorners()[1] - cam->getWorldSpaceCorners()[2]).length(); 
			Ogre::Image sourceImage; 
			Ogre::String gridFilename; 
			Ogre::uchar* stitchedImageData; 
			int nbScreenshots = 0; 

			for (std::size_t n = 0; n < gridSize * gridSize; n++) 
			{ 
				// Use asymmetrical perspective projection. For more explanations check out: 
				// http://www.cs.kuleuven.ac.be/cwis/research/graphics/INFOTEC/viewing-in-3d/node8.html 
				int y = (n / gridSize); 
				int x = (n - y * gridSize); 
				Ogre::Matrix4 shearing( 
					1, 0,(x - (gridSize - 1) * 0.5f) * nearWidth / nearDist, 0, 
					0, 1, -(y - (gridSize - 1) * 0.5f) * nearHeight / nearDist, 0, 
					0, 0, 1, 0, 
					0, 0, 0, 1); 
				Ogre::Matrix4 scale( 
					(Ogre::Real)gridSize, 0, 0, 0, 
					0, (Ogre::Real)gridSize, 0, 0, 
					0, 0, 1, 0, 
					0, 0, 0, 1); 
				cam->setCustomProjectionMatrix(true, standard * shearing * scale); 
				Ogre::Root::getSingletonPtr()->renderOneFrame(); 
				gridFilename = fileName + Ogre::StringConverter::toString(nbScreenshots++) + fileExtention; 
				win->writeContentsToFile(gridFilename); 

				if(stitchGridImages) 
				{ 
					sourceImage.load(gridFilename, "General"); 
					Ogre::ColourValue colourValue; 
					int stitchedX, stitchedY, stitchedIndex; 
					if(n == 0) 
						stitchedImageData = new Ogre::uchar[sourceImage.getWidth() * gridSize * sourceImage.getHeight() * gridSize * 3]; // 3 colors per pixel 
					for(int rawY = 0; rawY < (int) sourceImage.getHeight(); rawY++) 
					{ 
						for(int rawX = 0; rawX < (int) sourceImage.getWidth(); rawX++) 
						{ 
							colourValue = sourceImage.getColourAt(rawX, rawY, 0); 
							stitchedY = y * (int) sourceImage.getHeight() + rawY; 
							stitchedX = x * (int) sourceImage.getWidth() + rawX; 
							stitchedIndex = stitchedY * (int) sourceImage.getWidth() * gridSize + stitchedX; 
							Ogre::PixelUtil::packColour(sourceImage.getColourAt(rawX, rawY, 0), 
								Ogre::PF_R8G8B8, 
								(void*) &stitchedImageData[stitchedIndex * 3]); 
						} 
					} 
				} 
			} 
			cam->setCustomProjectionMatrix(false); // reset projection matrix 

			cam->setPosition(posOld);
			cam->setOrientation(orientOld);

			if(stitchGridImages) 
			{ 
				Ogre::Image targetImage; 
				targetImage.loadDynamicImage(stitchedImageData, 
					sourceImage.getWidth() * gridSize, 
					sourceImage.getHeight() * gridSize, 
					1, // depth 
					Ogre::PF_R8G8B8, 
					false); 
				targetImage.save(fileName + fileExtention); 
				delete[] stitchedImageData; 
			}
		}
	};
}

#endif //__CameraUtil_h__6_8_2010__18_57_46__