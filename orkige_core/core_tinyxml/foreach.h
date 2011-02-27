/**************************************************************
	created:	2010/09/12 at 0:01
	filename: 	foreach.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __foreach_h__12_9_2010__0_01_51__
#define __foreach_h__12_9_2010__0_01_51__

#define foreach_xml_element(parent, element, name) for (TiXmlElement* element = parent->FirstChildElement( name ); element; element = element->NextSiblingElement( name )) 

#endif //__foreach_h__12_9_2010__0_01_51__


