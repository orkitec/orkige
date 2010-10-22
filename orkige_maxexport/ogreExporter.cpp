////////////////////////////////////////////////////////////////////////////////
// ogreExporter.cpp
// Author	  : Doug Perkowski - OC3 Entertainment, Inc.
// Start Date : December 10th, 2007
// Copyright  : (C) 2007 OC3 Entertainment, Inc.
// OgreExporter::writeOgreData() and other portions from Francesco Giordana's ogreExporter 
////////////////////////////////////////////////////////////////////////////////

/*********************************************************************************
*                                                                                *
*   This program is free software; you can redistribute it and/or modify         *
*   it under the terms of the GNU Lesser General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or            *
*   (at your option) any later version.                                          *
*                                                                                *
**********************************************************************************/
#include "OgreExporter.h"
#include "OrkigeMaxExporterData.h"
#include "OrkigeMaxExporterLog.h"
#include "decomp.h"

#if defined(WIN32)
// For SHGetFolderPath.  Requires Windows XP or greater.
#include <stdarg.h>
#include <Shlobj.h>
#endif // defined(WIN32)

namespace OrkigeMaxExporter
{

OgreSceneExporter::OgreSceneExporter()
{
#ifdef WIN32
	TCHAR szPath[MAX_PATH];
	TSTR logFileName;
	if( SUCCEEDED(SHGetFolderPath(NULL,CSIDL_PERSONAL, NULL, 0, szPath))) 
	{
		logFileName = szPath;
		logFileName.Append("\\Orkige Plugins\\Ogre Exporters");

		DWORD attributes = GetFileAttributes(logFileName.data());
		if (attributes != 0xFFFFFFFF)
		{
			logFileName.Append("\\OrkigeMaxExporter_Log.txt");
			OrkigeMaxExporter::OrkigeMaxExporterLogFile::SetPath(logFileName.data());
		}
	}
#endif// defined(WIN32)
}

OgreSceneExporter::~OgreSceneExporter()
{
}

int OgreSceneExporter::ExtCount( void )
{
	return 1;
}

const TCHAR* OgreSceneExporter::Ext( int n )
{		
	return _T("mesh");
}

const TCHAR* OgreSceneExporter::LongDesc( void )
{
	return _T("Orkige Ogre Exporter");
}

const TCHAR* OgreSceneExporter::ShortDesc( void ) 
{			
	return _T("Ogre");
}

const TCHAR* OgreSceneExporter::AuthorName( void )
{			
	return _T("Francesco Giordana and OC3 Entertainment, Inc.");
}

const TCHAR* OgreSceneExporter::CopyrightMessage( void ) 
{	
	return _T("Copyright (c) 2010 orkitec, Steffen Roemer");
}

const TCHAR* OgreSceneExporter::OtherMessage1( void ) 
{		
	return _T("");
}

const TCHAR* OgreSceneExporter::OtherMessage2( void ) 
{		
	return _T("");
}

unsigned int OgreSceneExporter::Version( void )
{				
	// Return Version number * 100.  100 = 1.00.
	return 100;
}

void OgreSceneExporter::ShowAbout( HWND hWnd )
{			
	//@todo
}

BOOL OgreSceneExporter::SupportsOptions( int ext, DWORD options )
{
	//@todo Decide which options to support.  Simply return
	//      true for each option supported by each Extension 
	//      the exporter supports.

	return TRUE;
}
int	OgreSceneExporter::DoExport( const TCHAR* name, ExpInterface* pExpInterface, 
							 Interface* pInterface, BOOL suppressPrompts, 
							 DWORD options )
{
	
	ParamList copyParams, params;
	copyParams = OrkigeMaxExporterData::maxInterface.m_params;

	// Using only a mesh filename, construct the other filenames according to the
	// default behavior: Skeleton and material filenames match mesh, and material prefix
	// matching the mesh filename.
	std::string temp, meshFilename, skeletonFilename, materialFilename, matPrefix, filenamePath, sceneFilename;
	temp = meshFilename = name;
	for (int i=0; i<temp.length(); ++i)
	{
		temp[i]=toupper(temp[i]);
	} 
	size_t meshIndex = temp.rfind(".MESH", temp.length() -1);
	size_t folderIndexForward = temp.rfind("/", temp.length() -1);
	size_t folderIndexBackward = temp.rfind("\\", temp.length() -1);
	size_t folderIndex;
	if(folderIndexForward == std::string::npos)
	{
		folderIndex = folderIndexBackward;
	}
	else if(folderIndexBackward == std::string::npos)
	{
		folderIndex = folderIndexForward;
	}
	else
	{
		folderIndex = folderIndexBackward > folderIndexForward ? folderIndexBackward : folderIndexForward;
	}
	if(meshIndex == std::string::npos || folderIndex == std::string::npos)
	{
		OrkigeMaxExporterLog("Invalid mesh filename: %s\n", meshFilename.c_str());
		return false;
	}

	skeletonFilename = meshFilename.substr(0,meshIndex).append(".SKELETON");
	materialFilename = meshFilename.substr(0,meshIndex).append(".MATERIAL");
	sceneFilename = meshFilename.substr(0,meshIndex).append(".SCENE");

	matPrefix = meshFilename.substr(folderIndex, temp.length() -1);
	filenamePath =  meshFilename.substr(0, folderIndex);

	// Setup the paramlist.
	params.meshFilename = meshFilename.c_str();
	params.skeletonFilename = skeletonFilename.c_str();
	params.matPrefix = matPrefix.c_str();
	params.texOutputDir = filenamePath.c_str();
	params.sceneFilename = sceneFilename.c_str();

	OrkigeMaxExporterData::maxInterface.m_params = params;
	bool success = OrkigeMaxExporterData::maxInterface.exportScene();
	OrkigeMaxExporterData::maxInterface.m_params = copyParams;
	return success;
}

// Dummy function for progress bar.
DWORD WINAPI fn( LPVOID arg )
{
	return 0;
}

OgreExporter::OgreExporter()
	: pIGame(NULL)
{
}

OgreExporter::~OgreExporter()
{
}

bool OgreExporter::exportScene( )
{

	// Run some cleanup just in case.
	size_t meshIndex = m_params.meshFilename.rfind(".MESH", m_params.meshFilename.length() -1);
	MaxSDK::Util::Path meshPath(m_params.meshFilename.c_str());
	if(!meshPath.IsLegal() || meshIndex == std::string::npos)
	{	
		OrkigeMaxExporterLog( "Invalid meshfile path: %s\n", meshPath.GetCStr());
		MessageBox(GetCOREInterface()->GetMAXHWnd(), _T("Invalid meshfile path"), _T("Error"), MB_OK);
		return false;
		// Pop up save dialog and update path?
	}
	OrkigeMaxExporterLog( "Mesh file: %s\n" , m_params.meshFilename.c_str());

	size_t skelIndex = m_params.skeletonFilename.rfind(".SKELETON", m_params.skeletonFilename.length() -1);
	if(skelIndex == std::string::npos)
	{
		OrkigeMaxExporterLog( "Empty or invalid skeleton file detected.  Using mesh filename as base.\n");
		m_params.skeletonFilename = m_params.meshFilename.substr(0,meshIndex).append(".SKELETON");
	}
	size_t matIndex = m_params.materialFilename.rfind(".MATERIAL", m_params.materialFilename.length() -1);
	if(matIndex == std::string::npos)
	{
		OrkigeMaxExporterLog( "Empty or invalid material file detected.  Using mesh filename as base.\n");
		m_params.materialFilename = m_params.meshFilename.substr(0,meshIndex).append(".MATERIAL");
	}
	size_t sceneIndex = m_params.sceneFilename.rfind(".SCENE", m_params.sceneFilename.length() -1);
	if(sceneIndex == std::string::npos)
	{
		OrkigeMaxExporterLog( "Empty or invalid scene file detected.  Using mesh filename as base.\n");
		m_params.sceneFilename = m_params.meshFilename.substr(0,meshIndex).append(".SCENE");
	}
	if(m_params.texOutputDir == "")
	{
		OrkigeMaxExporterLog( "Empty or invalid texture output dir detected.  Using material filename as base.\n");
		MaxSDK::Util::Path path(m_params.materialFilename.c_str());
		MaxSDK::Util::Path folderpath = path.RemoveLeaf();
		m_params.texOutputDir = folderpath.GetCStr();
	}

	m_params.loadedSubmeshes.clear();
	m_params.currentRootJoints.clear();

	pIGame = GetIGameInterface();

	// Passing in true causing crash on IGameNode->GetNodeParent.  
	// Test for selection in Translate node.
	pIGame->InitialiseIGame(false);
	pIGame->SetStaticFrame(0);

	IGameConversionManager* pConversionManager = GetConversionManager();
	if( m_params.yUpAxis )
	{
		pConversionManager->SetCoordSystem(IGameConversionManager::IGAME_OGL);
	}
	else
	{
		pConversionManager->SetCoordSystem(IGameConversionManager::IGAME_MAX);
	}


	Interval animInterval = GetCOREInterface()->GetAnimRange(); 

	// Create output files
	m_params.openFiles();
	// Create a new empty mesh
	m_pMesh = new Mesh();
	// Create a new empty material set
	m_pMaterialSet = new MaterialSet();
	for( int node = 0; node < pIGame->GetTopLevelNodeCount(); ++node )
	{
		IGameNode* pGameNode = pIGame->GetTopLevelNode(node);
		if( pGameNode )
		{
			// Ignore light and camera targets here.  They are taken care of when
			// exporting lights and cameras.
			if( pGameNode->IsTarget() )
			{
				continue;
			}
			translateNode(pGameNode);
		}
	}
	// Load skeleton animation (do it now, so we have loaded all needed joints)
	if (m_pMesh->getSkeleton() && m_params.exportSkelAnims)
	{
		// Restore skeleton to correct pose
		m_pMesh->getSkeleton()->restorePose();
		// Load skeleton animations
		m_pMesh->getSkeleton()->loadAnims(m_params);
	}
	
	// Load vertex animations
	if (m_params.exportVertAnims)
		m_pMesh->loadAnims(m_params);
	// Load blend shapes
	if (m_params.exportBlendShapes)
		m_pMesh->loadBlendShapes(m_params);

	// We export exactly one mesh at the origin in the .Scene file.
	// Exporting somewhere other than the origin could be useful when there is
	// 1) no skeleton and 2) m_params.exportWorldCoords is false (local space)
	// but even in this case, if there are multiple submeshes, I'm not sure which 
	// transform to use in the scene.  Exporting more than one mesh doesn't make 
	// sense because the exporter is designed to export one .MESH file at a time.
	if(m_params.exportScene && m_params.exportMesh)
	{
		OrkigeMesh meshNode;
		meshNode.node.trans.scale = OrkigePoint3(1.0f, 1.0f, 1.0f);
		meshNode.node.trans.rot = OrkigePoint4(1.0f, 0.0f, 0.0f, 0.0f);
		std::string filename = StripToTopParent(m_params.meshFilename);
		meshNode.meshFile = filename.c_str();
		int ri = filename.find_last_of(".");
		meshNode.node.name = filename.substr(0,filename.length()-5);
		m_OrkigeScene.addMesh(meshNode);
	}
	stat = writeOgreData();

	if (m_pMesh)
		delete m_pMesh;
	if (m_pMaterialSet)
		delete m_pMaterialSet;
	m_params.closeFiles();
	pIGame->ReleaseIGame();
	return TRUE;
}

bool OgreExporter::translateNode(IGameNode* pGameNode)
{
	if( pGameNode )
	{
		IGameObject* pGameObject = pGameNode->GetIGameObject();
		if( pGameObject )
		{
			bool bShouldExport = true;
			INode* node = pGameNode->GetMaxNode();
			if(node)
			{
				if(node->IsObjectHidden())
				{
					bShouldExport = false;
				}
				// Only export selection if exportAll = false
				if(node->Selected() == 0 && !m_params.exportAll)
				{
					bShouldExport = false;
				}
			}
			if(bShouldExport)
			{
				switch( pGameObject->GetIGameType() )
				{
				case IGameObject::IGAME_MESH:
					{
						IGameMesh* pGameMesh = (IGameMesh*)pGameObject;
						if( pGameMesh )
						{
							if( pGameMesh->InitializeData() )
							{
								OrkigeMaxExporterLog("Found mesh node: %s\n", pGameNode->GetName());
				
								stat = m_pMesh->load(pGameNode,pGameObject,pGameMesh,m_params);
								if (true != stat)
								{
									OrkigeMaxExporterLog("Error, mesh skipped\n");
								}
							}
						}
					}
					break;
				case IGameObject::IGAME_LIGHT:
					{
						IGameLight* pGameLight = (IGameLight*)pGameObject;
						if( pGameLight )
						{
							addLightToScene(pGameNode, pGameLight);
						}
					}
					break;
				case IGameObject::IGAME_CAMERA:
					{
						IGameCamera* pGameCamera = (IGameCamera*)pGameObject;
						if( pGameCamera )
						{
							addCameraToScene(pGameNode, pGameCamera);
						}
					}
					break;
				default:
					break;
				}
			}
			for( int i = 0; i < pGameNode->GetChildCount(); ++i )
			{
				IGameNode* pChildGameNode = pGameNode->GetNodeChild(i);
				if( pChildGameNode )
				{
					// Ignore light and camera targets here.  They are taken care of when
					// exporting lights and cameras.
					if( pChildGameNode->IsTarget() )
					{
						continue;
					}
					translateNode(pChildGameNode);
				}
			}
			pGameNode->ReleaseIGameObject();
		}
	}
	return true;
}
OrkigeNode OgreExporter::getOrkigeNode( IGameNode* pGameNode )
{
	assert(pGameNode != NULL);
	OrkigeNode node;
	node.name = pGameNode->GetName();
	Matrix3 worldTM = pGameNode->GetWorldTM(0).ExtractMatrix3();
	AffineParts ap;
	decomp_affine(worldTM, &ap);
	node.trans.pos.x = ap.t.x;
	node.trans.pos.y = ap.t.y;
	node.trans.pos.z = ap.t.z;
	node.trans.rot.w = ap.q.w;
	node.trans.rot.x = ap.q.x;
	node.trans.rot.y = ap.q.y;
	node.trans.rot.z = ap.q.z;
	node.trans.scale.x = ap.k.x;
	node.trans.scale.y = ap.k.y;
	node.trans.scale.z = ap.k.z;
	return node;
}
void OgreExporter::addCameraToScene( IGameNode* pGameNode, IGameCamera* pGameCamera )
{
	if( pGameNode && pGameCamera && pIGame )
	{
		OrkigeMaxExporterLog("Exporting camera %s...\n", pGameNode->GetName());
		OrkigeCamera camera;
		camera.node = getOrkigeNode(pGameNode);
		IGameProperty* pGameProperty = pGameCamera->GetCameraFOV();
		if( pGameProperty )
		{
			pGameProperty->GetPropertyValue(camera.fov);
		}
		pGameProperty = pGameCamera->GetCameraNearClip ();
		if( pGameProperty )
		{
			pGameProperty->GetPropertyValue(camera.clipNear);
		}
		pGameProperty = pGameCamera->GetCameraFarClip();
		if( pGameProperty )
		{
			pGameProperty->GetPropertyValue(camera.clipFar);
		}
		m_OrkigeScene.addCamera(camera);
	}
}
void OgreExporter::addLightToScene( IGameNode* pGameNode, IGameLight* pGameLight )
{
	if( pGameNode && pGameLight && pIGame )
	{
		OrkigeMaxExporterLog("Exporting light %s...\n", pGameNode->GetName());
		
		OrkigeLight light;
		light.node = getOrkigeNode(pGameNode);

		switch(  pGameLight->GetLightType()  )
		{
		case IGameLight::IGAME_OMNI:
			light.type = OGRE_LIGHT_POINT;
			break;
		case IGameLight::IGAME_DIR:
			light.type = OGRE_LIGHT_DIRECTIONAL;
			break;
		case IGameLight::IGAME_FSPOT:
			light.type = OGRE_LIGHT_SPOT;
			break;
		// No support for targeted lights at the moment, but
		// We can still export them facing the initial direction.
		case IGameLight::IGAME_TSPOT:
			light.type = OGRE_LIGHT_SPOT;
			break;
		case IGameLight::IGAME_TDIR:
			light.type = OGRE_LIGHT_DIRECTIONAL;
			break;
		case IGameLight::IGAME_UNKNOWN:
		default:
			OrkigeMaxExporterLog("Unsupported light type.  Failed to export.\n");
			return;
		}

		IGameProperty* pGameProperty = pGameLight->GetLightColor();
		float propertyValue;
		Point3 lightColor;
		if( pGameProperty )
		{
			pGameProperty->GetPropertyValue(lightColor);
			light.diffuseColour.x = lightColor.x;
			light.diffuseColour.y = lightColor.y;
			light.diffuseColour.z = lightColor.z;
		}
		// Todo: How to get the specular color?
		light.attenuation_range = 0.0f;
		if(light.type == OGRE_LIGHT_POINT)
		{
			// Max lets attenuation start at a specific point.  Ogre seems to 
			// require that attenuation begins immediately.  We use the
			// Attenuation end as the range.
			pGameProperty = pGameLight->GetLightAttenEnd();
			if(pGameProperty)
			{	
				pGameProperty->GetPropertyValue(propertyValue);	
				light.attenuation_range = propertyValue;
			}
			// Inverse decay
			if( pGameLight->GetLightDecayType() == 1 )
			{
				light.attenuation_linear = 1.0f;
			}
			// Inverse Square decay
			else if( pGameLight->GetLightDecayType() == 2 )
			{
				light.attenuation_quadratic = 1.0f;
			}
		}
		if(light.type == OGRE_LIGHT_SPOT)
		{
			light.range_inner = 0.0f;
			light.range_falloff  = 1.0f;
			light.range_outer = 1.0f;
			pGameProperty = pGameLight->GetLightFallOff();
			if(pGameProperty)
			{
				// Max seems to use GetLightFallOff to get the outer angle.  
				// This doesn't leave anything for falloff. 
				pGameProperty->GetPropertyValue(propertyValue);	
				light.range_outer = propertyValue * PI / 180.0f;
			}
			pGameProperty = pGameLight->GetLightHotSpot();
			if(pGameProperty)
			{
				pGameProperty->GetPropertyValue(propertyValue);	
				light.range_inner = propertyValue * PI / 180.0f;
			}
			
		}
		m_OrkigeScene.addLight(light);
	}	
}

	// Below code from ogreExporter.cpp by Francesco Giordana
	/********************************************************************************************************
	*                           Method to write data to OGRE format                                         *
	********************************************************************************************************/
	bool OgreExporter::writeOgreData()
	{
		// Create Ogre Root
//		Ogre::Root ogreRoot;
		// Create singletons
		Ogre::LogManager logMgr;
		Ogre::LogManager::getSingleton().createLog("Ogre.log", true);
		Ogre::ResourceGroupManager rgm;
		Ogre::MeshManager meshMgr;
		Ogre::SkeletonManager skelMgr;
		Ogre::MaterialManager matMgr;
		Ogre::DefaultHardwareBufferManager hardwareBufMgr;
		// Doug Perkowski  - 03/09/10
		// Creating LodStrategyManager
		// http://www.ogre3d.org/forums/viewtopic.php?f=8&t=55844
		Ogre::LodStrategyManager lodstrategymanager;   
		// Write mesh binary
		if (m_params.exportMesh)
		{
			OrkigeMaxExporterLog("Writing mesh binary...\n");		
			stat = m_pMesh->writeOgreBinary(m_params);
			if (stat != true)
			{
				OrkigeMaxExporterLog("Error writing mesh binary file\n");
			}
		}
		// Write skeleton binary
		if (m_params.exportSkeleton)
		{
			if (m_pMesh->getSkeleton())
			{
				OrkigeMaxExporterLog("Writing skeleton binary...\n");
				stat = m_pMesh->getSkeleton()->writeOgreBinary(m_params);
				if (stat != true)
				{
					OrkigeMaxExporterLog("Error writing mesh binary file\n");
				}
			}
		}
		// Write materials data
		if (m_params.exportMaterial)
		{
			OrkigeMaxExporterLog("Writing materials data...\n");
			
			stat  = m_pMaterialSet->writeOgreScript(m_params);
			if (stat != true)
			{
				OrkigeMaxExporterLog("Error writing materials file\n");
			}
		}
		// Write Scene
		if (m_params.exportScene)
		{
			OrkigeMaxExporterLog("Writing scene data...\n");
			
			stat  = m_OrkigeScene.writeSceneFile(m_params);
			if (stat != true)
			{
				OrkigeMaxExporterLog("Error writing scene file\n");
			}
		}
		m_OrkigeScene.clear();
		return true;
	}
} // end namespace



class OrkigeMaxExporterClassDesc:public ClassDesc2 
{
	public:
	int 			IsPublic() { return TRUE; }
	void *			Create(BOOL loading = FALSE) { return new OrkigeMaxExporter::OgreSceneExporter(); }
	const TCHAR *	ClassName() { return _T("OgreExporter"); }
	SClass_ID		SuperClassID() { return SCENE_EXPORT_CLASS_ID; }
	Class_ID		ClassID() { return Class_ID(0x6a042a9d, 0x75b54fc4); }
	const TCHAR* 	Category() { return _T("OGRE"); }
	const TCHAR*	InternalName() { return _T("OgreExporter"); }	
	HINSTANCE		HInstance() { return hInstance; }				
};
static OrkigeMaxExporterClassDesc OrkigeMaxExporterDesc;
ClassDesc2* GetOrkigeMaxExporterDesc() { return &OrkigeMaxExporterDesc; }


