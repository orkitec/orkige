////////////////////////////////////////////////////////////////////////////////
// material.cpp
// Author     : Francesco Giordana
// Start Date : January 13, 2005
// Copyright  : (C) 2006 by Francesco Giordana
// Email      : fra.giordana@tiscali.it
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

#include "material.h"
#include "OrkigeMaxExporterLog.h"
#ifdef PRE_MAX_2010
#include "IPathConfigMgr.h"
#else
#include "IFileResolutionManager.h"
#endif	//PRE_MAX_2010 

namespace OrkigeMaxExporter
{
	// Constructor
	Material::Material()
	{
		clear();
	}


	// Destructor
	Material::~Material()
	{
	}


	// Get material name
	std::string& Material::name()
	{
		return m_name;
	}


	// Clear data
	void Material::clear()
	{
		m_name = "";
		m_type = MT_LAMBERT;
		m_lightingOff = false;
		m_isTransparent = false;
		m_isTextured = false;
		m_isMultiTextured = false;
		m_ambient = Point4(1,1,1,1);
		m_diffuse = Point4(1,1,1,1);
		m_specular = Point4(0,0,0,0);
		m_emissive = Point4(0,0,0,0);
		m_textures.clear();
	}

	// Load material data
	bool Material::load(IGameMaterial* pGameMaterial,std::vector<int>& uvsets,ParamList& params)
	{

		clear();
		//read material name, adding the requested prefix
		std::string tmpStr = params.matPrefix;
		if (tmpStr != "")
			tmpStr += "/";
		m_name = "";
		if(pGameMaterial)
			tmpStr.append(pGameMaterial->GetMaterialName());
		for(size_t i = 0; i < tmpStr.size(); ++i)
		{
			if(tmpStr[i] == ':')
			{
				m_name.append("_");
			}
			else
			{
				m_name.append(tmpStr.substr(i, 1));
			}
		}

		//check if we want to export with lighting off option
		m_lightingOff = params.lightingOff;

		// GET MATERIAL DATA
		if( pGameMaterial )
		{
		
			if(!pGameMaterial->IsEntitySupported() )
			{
				OrkigeMaxExporterLog( "Warning: IsEntitySupported() returned false for IGameMaterial ...\n");
			}
			else
			{
				int texCount = pGameMaterial->GetNumberOfTextureMaps();
				OrkigeMaxExporterLog( "Exporting %d textures...\n", texCount);
				for( int i = 0; i < texCount; ++i )
				{
					
					IGameTextureMap* pGameTexture = pGameMaterial->GetIGameTextureMap(i);
					if( pGameTexture && pGameTexture->IsEntitySupported())
					{

						OrkigeMaxExporterLog( "Texture Index: %d\n",i);
						OrkigeMaxExporterLog( "Texture Name: %s\n", pGameTexture->GetTextureName());

						Texture tex;
									
						MaxSDK::Util::Path textureName(pGameTexture->GetBitmapFileName());
						if(!DoesFileExist(pGameTexture->GetBitmapFileName()))
						{
							bool bFoundTexture = false;
#ifdef PRE_MAX_2010
							if(IPathConfigMgr::GetPathConfigMgr()->SearchForXRefs(textureName))
							{
								bFoundTexture = true;
							}
#else
							IFileResolutionManager* pIFileResolutionManager = IFileResolutionManager::GetInstance();
							if(pIFileResolutionManager)
							{
								if( pIFileResolutionManager->GetFullFilePath(textureName, MaxSDK::AssetManagement::kXRefAsset) )
								{
									bFoundTexture = true;
								}
								else if( pIFileResolutionManager->GetFullFilePath(textureName, MaxSDK::AssetManagement::kBitmapAsset) )
								{
									bFoundTexture = true;
								}
							}
#endif // PRE_MAX_2010
							if( true == bFoundTexture )
							{
								OrkigeMaxExporterLog( "Updated texture location: %s.\n", textureName.GetCStr());
							}
							else
							{
								OrkigeMaxExporterLog( "Warning: Couldn't locate texture: %s.\n", pGameTexture->GetTextureName());
							}				
						}
						int texSlot = pGameTexture->GetStdMapSlot();
						switch(texSlot)
						{
						case ID_AM:
							OrkigeMaxExporterLog( "Ambient channel texture.\n");
							break;
						case ID_DI:
							OrkigeMaxExporterLog( "Diffuse channel texture.\n");
							tex.bCreateTextureUnit = true;
							break;
						case ID_SP:
							OrkigeMaxExporterLog( "Specular channel texture.\n");
							break;
						case ID_SH:
							OrkigeMaxExporterLog( "SH channel texture.\n");
							break;
						case ID_SS:
							OrkigeMaxExporterLog( "Shininess Strenth channel texture.\n");
							break;
						case ID_SI:
							OrkigeMaxExporterLog( "Self-illumination channel texture.\n");
							break;
						case ID_OP:
							OrkigeMaxExporterLog( "opacity channel texture.\n");
							m_isTransparent = true;
							break;
						case ID_FI:
							OrkigeMaxExporterLog( "Filter Color channel texture.\n");
							break;
						case ID_BU:
							OrkigeMaxExporterLog( "Bump channel texture.\n");
							break;
						case ID_RL:
							OrkigeMaxExporterLog( "Reflection channel texture.\n");
							break; 
						case ID_RR:
							OrkigeMaxExporterLog( "Refraction channel texture.\n");
							break;
						case ID_DP:
							OrkigeMaxExporterLog( "Displacement channel texture.\n");
							break; 
						}

						tex.absFilename = textureName.GetCStr();
						std::string filename = textureName.StripToLeaf().GetCStr();
						tex.filename = filename; 
						tex.uvsetIndex = 0;
						tex.uvsetName = pGameTexture->GetTextureName();

						IGameUVGen *pUVGen = pGameTexture->GetIGameUVGen();
						IGameProperty *prop = NULL;
						if(pUVGen)
						{
							float covU,covV;
							prop = pUVGen->GetUTilingData();
							if(prop)
							{
								prop->GetPropertyValue(covU);
								if (fabs(covU) < PRECISION)
									covU = 666;
								else
									covU = 1/covU;
								tex.scale_u = covU;
								if (fabs(tex.scale_u) < PRECISION)
									tex.scale_u = 0;
							}
							prop = pUVGen->GetVTilingData();
							if(prop)
							{
								prop->GetPropertyValue(covV);
								if (fabs(covV) < PRECISION)
									covV = 666;
								else
									covV = 1/covV;
								tex.scale_v = covV;
								if (fabs(tex.scale_v) < PRECISION)
									tex.scale_v = 0;
							}
							float rot;
							prop = pUVGen->GetWAngleData();
							if(prop)
							{
								prop->GetPropertyValue(rot);
								tex.rot = -rot;
								// convert radians to degrees.
								tex.rot = tex.rot * 180.0f/3.1415926535f;
								if (fabs(rot) < PRECISION)
									tex.rot = 0;
							}

							float rotU, rotV;
							prop = pUVGen->GetUAngleData();
							if(prop)
							{
								prop->GetPropertyValue(rotU);
								if (fabs(rotU) > PRECISION)
									OrkigeMaxExporterLog( "Warning: U rotation detected. This is unsupported.\n");
							}
							prop = pUVGen->GetVAngleData();
							{
								prop->GetPropertyValue(rotV);
								if (fabs(rotV) > PRECISION)
									OrkigeMaxExporterLog( "Warning: V rotation detected. This is unsupported.\n");
							}

							float transU,transV;
							prop = pUVGen->GetUOffsetData();
							if(prop)
							{	
								prop->GetPropertyValue(transU);
								tex.scroll_u = -0.5 * (covU-1.0)/covU - transU/covU;
								if (fabs(tex.scroll_u) < PRECISION)
									tex.scroll_u = 0;
							}
							prop = pUVGen->GetVOffsetData();
							if(prop)
							{							
								prop->GetPropertyValue(transV);
								tex.scroll_v = 0.5 * (covV-1.0)/covV + transV/covV;
								if (fabs(tex.scroll_v) < PRECISION)
									tex.scroll_v = 0;
							}

							// Set texture operation type
							// Not sure when to set TOT_REPLACE, TOT_ADD, or TOT_ALPHABLEND
							tex.opType = TOT_MODULATE;

							bool foundUniqueUVset = false;
							for (int k=0; k<uvsets.size() && !foundUniqueUVset; k++)
							{
								if (uvsets[k] == pGameTexture->GetMapChannel())
								{
									tex.uvsetIndex = k;
									foundUniqueUVset = true;
									for(int iTex = 0; iTex < m_textures.size(); ++iTex)
									{
										// I'm getting dupliacate texture_unit blocks.
										// Not sure of the "right way" to prevent this, but
										// the below check does the trick.
										if(m_textures[iTex].uvsetIndex == k && m_textures[iTex].filename == tex.filename)
										{
											foundUniqueUVset = false;
										}
									}
								}
							}
							if(foundUniqueUVset)
								m_textures.push_back(tex);
						}
					}
				}
				float fOpacity = 1.0f;
				pGameMaterial->GetOpacityData()->GetPropertyValue( fOpacity, 0 );
				exportColor( m_emissive, pGameMaterial->GetEmissiveData(), fOpacity );
				exportColor( m_diffuse, pGameMaterial->GetDiffuseData(), fOpacity );
				// I can't seem to get valid specular with this.  Default is better for now.
				// exportColor( m_specular, pGameMaterial->GetSpecularData(), fOpacity );
				exportColor( m_ambient, pGameMaterial->GetAmbientData(), fOpacity );
			}
		}
		return true;
	}
	bool Material::exportColor(Point4& color, IGameProperty* pGameProperty, float opacity)
	{
		if(IGAME_POINT4_PROP == pGameProperty->GetType())
		{
			Point4 emptyColor(0,0,0,0);
			Point4 matColor;
			if(pGameProperty->GetPropertyValue(matColor))
			{
				if(emptyColor != matColor)
				{
					color = matColor;
					return true;
				}
			}
		}
		else if (IGAME_POINT3_PROP == pGameProperty->GetType())
		{
			Point3 color3;
			Point3 emptyColor(0,0,0);
			if(pGameProperty->GetPropertyValue(color3))
			{
				if(color3 != emptyColor)
				{
					color.w = color3.x;
					color.x = color3.y;
					color.y = color3.z;
					color.z = 0; 
					return true;
				}
			}
		}
		return false;
	}
	
	// Write material data to an Ogre material script file
	bool Material::writeOgreScript(ParamList &params)
	{
		//Start material description
		//params.outMaterial << "material \"" << m_name.c_str() << "\"\n";
		params.outMaterial << "material " << m_name.c_str() << "\n";
		params.outMaterial << "{\n";

		//Start technique description
		params.outMaterial << "\ttechnique\n";
		params.outMaterial << "\t{\n";

		//Start render pass description
		params.outMaterial << "\t\tpass\n";
		params.outMaterial << "\t\t{\n";
		//set lighting off option if requested
		if (m_lightingOff)
			params.outMaterial << "\t\t\tlighting off\n\n";
		//set phong shading if requested (default is gouraud)
		if (m_type == MT_PHONG)
			params.outMaterial << "\t\t\tshading phong\n";
		//ambient colour
		// Format: ambient (<red> <green> <blue> [<alpha>]| vertexcolour)
		params.outMaterial << "\t\t\tambient " << m_ambient.w << " " << m_ambient.x << " " << m_ambient.y
			<< " " << m_ambient.z << "\n";
		//diffuse colour
		//Format: diffuse (<red> <green> <blue> [<alpha>]| vertexcolour)
		params.outMaterial << "\t\t\tdiffuse " << m_diffuse.w << " " << m_diffuse.x << " " << m_diffuse.y
			<< " " << m_diffuse.z << "\n";
		//specular colour
		params.outMaterial << "\t\t\tspecular " << m_specular.w << " " << m_specular.x << " " << m_specular.y
			<< " " << m_specular.z << "\n";
		//emissive colour
		params.outMaterial << "\t\t\temissive " << m_emissive.w << " " << m_emissive.x << " " 
			<< m_emissive.y << "\n";
		//if material is transparent set blend mode and turn off depth_writing
		if (m_isTransparent)
		{
			params.outMaterial << "\n\t\t\tscene_blend alpha_blend\n";
			params.outMaterial << "\t\t\tdepth_write off\n";
		}
		//write texture units
		for (int i=0; i<m_textures.size(); i++)
		{
			if(m_textures[i].bCreateTextureUnit == true)
			{
				//start texture unit description
				params.outMaterial << "\n\t\t\ttexture_unit\n";
				params.outMaterial << "\t\t\t{\n";
				//write texture name
				params.outMaterial << "\t\t\t\ttexture " << m_textures[i].filename.c_str() << "\n";
				//write texture coordinate index
				params.outMaterial << "\t\t\t\ttex_coord_set " << m_textures[i].uvsetIndex << "\n";
				//write colour operation
				switch (m_textures[i].opType)
				{
				case TOT_REPLACE:
					params.outMaterial << "\t\t\t\tcolour_op replace\n";
					break;
				case TOT_ADD:
					params.outMaterial << "\t\t\t\tcolour_op add\n";
					break;
				case TOT_MODULATE:
					params.outMaterial << "\t\t\t\tcolour_op modulate\n";
					break;
				case TOT_ALPHABLEND:
					params.outMaterial << "\t\t\t\tcolour_op alpha_blend\n";
					break;
				}
				//write texture transforms
				params.outMaterial << "\t\t\t\tscale " << m_textures[i].scale_u << " " << m_textures[i].scale_v << "\n";
				params.outMaterial << "\t\t\t\tscroll " << m_textures[i].scroll_u << " " << m_textures[i].scroll_v << "\n";
				params.outMaterial << "\t\t\t\trotate " << m_textures[i].rot << "\n";
				//end texture unit desription
				params.outMaterial << "\t\t\t}\n";
			}
		}

		//End render pass description
		params.outMaterial << "\t\t}\n";

		//End technique description
		params.outMaterial << "\t}\n";

		//End material description
		params.outMaterial << "}\n";

		//Copy textures to output dir if required
		if (params.copyTextures)
			copyTextures(params);

		return true;
	}


	// Copy textures to path specified by params
	bool Material::copyTextures(ParamList &params)
	{
		for (int i=0; i<m_textures.size(); i++)
		{
			// Copy file texture to output dir
			std::string command = "copy \"";
			command += m_textures[i].absFilename;
			command += "\" \"";
			command += params.texOutputDir;
			command += "\"";
			system(command.c_str());
		}
		return true;
	}

};	//end namespace
