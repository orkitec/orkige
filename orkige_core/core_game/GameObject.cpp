/**************************************************************
	created:	2010/08/15 at 14:44
	filename: 	GameObject.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "core_tinyxml/tinyxml.h"
#include "core_game/GameObject.h"
#include "core_game/GameObjectManager.h"
#include <boost/lexical_cast.hpp>
#include <core_debug/ProfileManager.h>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GameObject::GameObject(String const & id) : ComponentHolder<GameObjectComponent>(id)
	{
		this->eventManager = new EventManager();
	}
	//---------------------------------------------------------
	GameObject::~GameObject()
	{
		this->updatableComponents.clear();
		this->removeAllComponents();
		delete this->eventManager;
		this->eventManager = NULL;
	}
	//---------------------------------------------------------
	bool GameObject::loadTemplate(String const & templateFileName)
	{
		bool retval = true;
		optr<TiXmlDocument>	document =  onew(new TiXmlDocument(templateFileName.c_str()));
		if(!document || document->Error())
		{
			oDebugMsg("gameobject",0,"Error Loading file: "<<templateFileName<<std::endl <<document->ErrorDesc());
			return false;
		}
		document->LoadFile(document->Value(), TIXML_ENCODING_UTF8);
		if(!document || document->Error())
		{
			oDebugMsg("gameobject",0,"Error Loading file: "<<templateFileName<<std::endl <<document->ErrorDesc());
			return false;
		}
		TiXmlElement* xmlRoot = document->RootElement();
		if(!xmlRoot || document->Error())
		{
			oDebugMsg("gameobject",0,"Error finding XML-Root in file: "<<templateFileName<<std::endl <<document->ErrorDesc());
			return false;
		}
		for(TiXmlElement* elementType = xmlRoot->FirstChildElement(); elementType; elementType = elementType->NextSiblingElement())
		{
			String const & elementTypeName = elementType->Value();
			if(elementTypeName == "Attributes")
			{
				for(TiXmlElement* element = elementType->FirstChildElement(); element; element = element->NextSiblingElement())
				{
					String const & attributeTypeName = element->Value();
					const char * id = element->Attribute("id");
					const char * value = element->Attribute("value");
					oAssert(id);
					oAssert(value);
					if(attributeTypeName == "int")
					{
						int i = boost::lexical_cast<int>(value);
						this->setAttribute(id,i);
					}
					else if(attributeTypeName == "bool")
					{
						bool b = boost::lexical_cast<bool>(value);
						this->setAttribute(id,b);
					}
					else if(attributeTypeName == "float")
					{
						float f = boost::lexical_cast<float>(value);
						this->setAttribute(id,f);
					}
					else if(attributeTypeName == "string")
					{
						String s = value;
						this->setAttribute(id,s);
					}
					else
					{
						oDebugMsg("gameobject",0,"Unsupported Attribute Type \""<<attributeTypeName<<"\"!");
					}
				}
			}
			else if(elementTypeName == "Components")
			{
				for(TiXmlElement* element = elementType->FirstChildElement(); element; element = element->NextSiblingElement())
				{
					String const & componentTypeName = element->Value();
					TypeInfo componentType(componentTypeName);
					if(GameObject::isComponentRegistered(componentType))
					{
						bool componentAdded = this->addComponent(componentType);
						if(this->hasComponent(componentType))
						{
							optr<GameObjectComponent> component = this->getComponent(componentType).lock();
							oAssert(component);

							for(TiXmlElement* attributesElement = element->FirstChildElement(); attributesElement; attributesElement = attributesElement->NextSiblingElement())
							{
								String const & elementTypeName = attributesElement->Value();
								if(elementTypeName == "Attributes")
								{
									for(TiXmlElement* attributeElement = attributesElement->FirstChildElement(); attributeElement; attributeElement = attributeElement->NextSiblingElement())
									{
										String const & attributeTypeName = attributeElement->Value();
										const char * id = attributeElement->Attribute("id");
										const char * value = attributeElement->Attribute("value");
										oAssert(id);
										oAssert(value);
										if(attributeTypeName == "int")
										{
											int i = boost::lexical_cast<int>(value);
											component->setAttribute(id,i);
										}
										else if(attributeTypeName == "bool")
										{
											bool b = boost::lexical_cast<bool>(value);
											component->setAttribute(id,b);
										}
										else if(attributeTypeName == "float")
										{
											float f = boost::lexical_cast<float>(value);
											component->setAttribute(id,f);
										}
										else if(attributeTypeName == "string")
										{
											String s = value;
											component->setAttribute(id,s);
										}
										else
										{
											oDebugMsg("gameobject",0,"Unsupported Attribute Type \""<<attributeTypeName<<"\"!");
										}
									}
								}
							}
							if( ! component->onLoadTemplate(element))
							{
								oDebugMsg("gameobject",0,"GameObjectComponent: \""<<componentTypeName<<"\" had Errors while loading Template!");
								retval = false;
							}
						}
						else
						{
							oDebugMsg("gameobject",0,"GameObjectComponent: \""<<componentTypeName<<"\" could not be added to GameObject: "<<this->getObjectID()<<"!");
							retval = false;
						}
					}
					else
					{
						oDebugMsg("gameobject",0,"GameObjectComponent: \""<<componentTypeName<<"\" is not registered!");
						retval = false;
					}

					if(document->Error())
					{
						oDebugMsg("gameobject",0,"Error while parsing file: "<<templateFileName<<" Row: "<<document->ErrorRow()<<" Column: "<<document->ErrorCol()<<"!"<<std::endl <<document->ErrorDesc());
						document->ClearError();
						retval = false;
					}
				}
			}
			else
			{
				oDebugMsg("gameobject",0,"Unsupported Element Type \""<<elementTypeName<<"\"!");
			}
		}
		return retval;
	}
	//---------------------------------------------------------
	bool GameObject::saveTemplate(String const & templateFileName)
	{
		bool retval = true;
		optr<TiXmlDocument>	document =  onew(new TiXmlDocument(templateFileName.c_str()));
		if(!document || document->Error())
		{
			oDebugMsg("gameobject",0,"Error Saving file: "<<templateFileName<<std::endl <<document->ErrorDesc());
			return false;
		}
		TiXmlDeclaration decl("1.0","UTF-8","yes");
		document->InsertEndChild(decl);

		if(document->Error())
		{
			oDebugMsg("gameobject",0,"Error Saving file: "<<templateFileName<<std::endl <<document->ErrorDesc());
			return false;
		}
		TiXmlElement xmlRoot("GameObject");
		TiXmlElement attributesElement("Attributes");

		foreach(OwnedAttributeMap::value_type const & attribute, attributes)
		{
			TypeInfo const & attributeType = attribute.second->getTypeInfo();
			String const & id = attribute.first;
			if(attributeType == AttributeWrapper<int>::getClassTypeInfo())
			{
				TiXmlElement element("int");
				element.SetAttribute("id",id.c_str());
				element.SetAttribute("value",this->getAttribute<int>(id));
				attributesElement.InsertEndChild(element);
			}
			else if(attributeType == AttributeWrapper<bool>::getClassTypeInfo())
			{
				TiXmlElement element("bool");
				element.SetAttribute("id",id.c_str());
				element.SetAttribute("value",this->getAttribute<bool>(id));
				attributesElement.InsertEndChild(element);
			}
			else if(attributeType == AttributeWrapper<float>::getClassTypeInfo())
			{
				TiXmlElement element("float");
				element.SetAttribute("id",id.c_str());
				element.SetDoubleAttribute("value",this->getAttribute<float>(id));
				attributesElement.InsertEndChild(element);
			}
			else if(attributeType == AttributeWrapper<String>::getClassTypeInfo())
			{
				TiXmlElement element("string");
				element.SetAttribute("id",id.c_str());
#ifndef __WIN32__
				element.SetAttribute("value",this->getAttribute<String>(id).c_str());
#else
				element.SetAttribute("value",this->getAttribute<String>(id));
#endif
				attributesElement.InsertEndChild(element);
			}
		}
		xmlRoot.InsertEndChild(attributesElement);

		ComponentMap const & components = this->getComponents();

		TiXmlElement componentsElement("Components");
		foreach(ComponentMap::value_type const & current, components)
		{
			optr<OwnedComponentType> component = current.second;
			String const & componentTypeName = current.first.getName();
			TiXmlElement element(componentTypeName.c_str());
			TiXmlElement componentAttributesElement("Attributes");
			foreach(GameObjectComponent::OwnedAttributeMap::value_type const & attribute, component->getAttributes())
			{
				TypeInfo const & attributeType = attribute.second->getTypeInfo();
				String const & id = attribute.first;
				if(attributeType == AttributeWrapper<int>::getClassTypeInfo())
				{
					TiXmlElement element("int");
					element.SetAttribute("id",id.c_str());
					element.SetAttribute("value",component->getAttribute<int>(id));
					componentAttributesElement.InsertEndChild(element);
				}
				else if(attributeType == AttributeWrapper<bool>::getClassTypeInfo())
				{
					TiXmlElement element("bool");
					element.SetAttribute("id",id.c_str());
					element.SetAttribute("value",component->getAttribute<bool>(id));
					componentAttributesElement.InsertEndChild(element);
				}
				else if(attributeType == AttributeWrapper<float>::getClassTypeInfo())
				{
					TiXmlElement element("float");
					element.SetAttribute("id",id.c_str());
					element.SetDoubleAttribute("value",component->getAttribute<float>(id));
					componentAttributesElement.InsertEndChild(element);
				}
				else if(attributeType == AttributeWrapper<String>::getClassTypeInfo())
				{
					TiXmlElement element("string");
					element.SetAttribute("id",id.c_str());
#ifndef __WIN32__
					element.SetAttribute("value",component->getAttribute<String>(id).c_str());
#else
					element.SetAttribute("value",component->getAttribute<String>(id));
#endif
					componentAttributesElement.InsertEndChild(element);
				}
			}
			element.InsertEndChild(componentAttributesElement);
			bool componentSaved = component->onSaveTemplate(&element);
			if(!componentSaved)
			{
				oDebugMsg("gameobject",0,"Error while saving Component: "<<componentTypeName<< " to file: "<<templateFileName<<"!");
				retval = false;
			}
			componentsElement.InsertEndChild(element);

		}
		xmlRoot.InsertEndChild(componentsElement);
		document->InsertEndChild(xmlRoot);
		document->SaveFile();

		if(document->Error())
		{
			oDebugMsg("gameobject",0,"Error while saving file: "<<templateFileName<<" Row: "<<document->ErrorRow()<<" Column: "<<document->ErrorCol()<<"!"<<std::endl <<document->ErrorDesc());
			document->ClearError();
			retval = false;
		}

		return retval;
	}
	//---------------------------------------------------------
	void GameObject::enableUpdates(TypeInfo const & componentType)
	{
		if(this->hasComponent(componentType))
		{
			GameObjectComponent* component = this->getComponentPtr(componentType);
			oAssert(component);
			GameObjectManager::getSingleton().enableUpdates(component);
		}
	}
	//---------------------------------------------------------
	void GameObject::disableUpdates(TypeInfo const & componentType)
	{
		if(this->hasComponent(componentType))
		{
			GameObjectComponent* component = this->getComponentPtr(componentType);
			oAssert(component);
			GameObjectManager::getSingleton().disableUpdates(component);
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void GameObject::onComponentAdded(TypeInfo const & componentType)
	{
		ComponentMap::iterator it = this->components.find(componentType);

		oAssert(it != this->components.end());
		
		optr<GameObjectComponent> goc = it->second;
		
		oAssert(goc);

		if(goc->getWantsUpdates())
		{
			this->enableUpdates(componentType);
		}
	}
	//---------------------------------------------------------
	void GameObject::onComponentRemoved(TypeInfo const & componentType)
	{
		this->disableUpdates(componentType);
	}
	//---------------------------------------------------------
	void GameObject::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
	}
	//---------------------------------------------------------
	void GameObject::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------

	IMPLEMENT_COMPONENTHOLDER(GameObjectComponent)

	OOBJECT_IMPL(GameObject)
		OCONSTRUCTOR1(String)
		OFUNC(loadTemplate)
		OFUNC(saveTemplate)
	OOBJECT_END
}
