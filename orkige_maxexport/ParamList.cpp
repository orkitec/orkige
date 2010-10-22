////////////////////////////////////////////////////////////////////////////////
// paramlist.cpp
// Author     : Francesco Giordana
// Start Date : January 13, 2005
// Copyright  : (C) 2006 by Francesco Giordana
// Email      : fra.giordana@tiscali.it
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Port to 3D Studio Max - Modified original version
// Author	  : Doug Perkowski - OC3 Entertainment, Inc.
// Start Date : December 10th, 2007
////////////////////////////////////////////////////////////////////////////////
/*********************************************************************************
*                                                                                *
*   This program is free software; you can redistribute it and/or modify         *
*   it under the terms of the GNU Lesser General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or            *
*   (at your option) any later version.                                          *
*                                                                                *
**********************************************************************************/

#include "ParamList.h"
#include "OrkigeMaxExporterLog.h"
/***** Class ParamList *****/
// method to parse arguments from command line
namespace OrkigeMaxExporter
{
	// Helper function for getting the filename from a full path.
	std::string StripToTopParent(const std::string& filepath)
	{
		int ri = filepath.find_last_of('\\');
		int ri2 = filepath.find_last_of('/');
		if(ri2 > ri)
		{
			ri = ri2;
		}
		return filepath.substr(ri+1);
	}

	// method to open output files for writing
	bool ParamList::openFiles()
	{
		std::string msg;
		if (exportMaterial)
		{
			outMaterial.open(materialFilename.c_str());
			if (!outMaterial)
			{
				OrkigeMaxExporterLog( "Error opening file: %s\n", materialFilename.c_str());
				return false;
			}
		}
		if (exportAnimCurves)
		{
			outAnim.open(animFilename.c_str());
			if (!outAnim)
			{
				OrkigeMaxExporterLog( "Error opening file: %s\n", animFilename.c_str());
				return false;
			}
		}
		if (exportCameras)
		{
			outCameras.open(camerasFilename.c_str());
			if (!outCameras)
			{
				OrkigeMaxExporterLog( "Error opening file: %s\n", camerasFilename.c_str());
				return false;
			}
		}
		if (exportParticles)
		{
			outParticles.open(particlesFilename.c_str());
			if (!outParticles)
			{
				OrkigeMaxExporterLog( "Error opening file: %s\n", particlesFilename.c_str());
				return false;
			}
		}
		return true;
	}

	// method to close open output files
	bool ParamList::closeFiles()
	{
		if (exportMaterial)
			outMaterial.close();
	
		if (exportAnimCurves)
			outAnim.close();
			
		if (exportCameras)
			outCameras.close();
		
		return true;
	}

}	//end namespace
