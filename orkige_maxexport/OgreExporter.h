////////////////////////////////////////////////////////////////////////////////
// ogreExporter.h
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

#ifndef OGRE_EXPORTER_H
#define OGRE_EXPORTER_H

#include "Mesh.h"
#include "Scene.h"
#include "MaxExportLayer.h"

namespace OrkigeMaxExporter
{

class OgreSceneExporter : public SceneExport 
{
public:

	// public methods
	OgreSceneExporter();
	virtual ~OgreSceneExporter();		

	int ExtCount( void );					
	const TCHAR* Ext( int n );					
	const TCHAR* LongDesc( void );					
	const TCHAR* ShortDesc( void );				
	const TCHAR* AuthorName( void );				
	const TCHAR* CopyrightMessage( void );			
	const TCHAR* OtherMessage1( void );			
	const TCHAR* OtherMessage2( void );			
	unsigned int Version( void );					
	void ShowAbout( HWND hWnd );		
	BOOL SupportsOptions( int ext, DWORD options );
	int	DoExport( const TCHAR* name, ExpInterface* pExpInterface, 
		          Interface* pInterface, BOOL suppressPrompts = FALSE, 
				  DWORD options = 0 );
};

class OgreExporter 
{
public:
	// public methods
	OgreExporter();
	~OgreExporter();		

	bool exportScene();
	bool translateNode(IGameNode* pGameNode);
	bool writeOgreData();
	

	ParamList m_params;
private:
	// private members
	OrkigeNode getOrkigeNode( IGameNode* pGameNode );
	void addLightToScene( IGameNode* pGameNode, IGameLight* pGameLight );
	void addCameraToScene( IGameNode* pGameNode, IGameCamera* pGameCamera );

	bool stat;
	Mesh* m_pMesh;
	MaterialSet* m_pMaterialSet;
	TimeValue m_curTime;
	IGameScene* pIGame;
	
	OrkigeScene m_OrkigeScene;

};

}	//end namespace

#endif
