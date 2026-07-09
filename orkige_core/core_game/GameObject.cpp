/**************************************************************
	created:	2010/08/15 at 14:44
	filename: 	GameObject.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include <tinyxml2.h>
#include "core_game/GameObject.h"
#include "core_game/GameObjectManager.h"
#include <core_debug/ProfileManager.h>
#include <algorithm>
#include <string>
#include <stdexcept>

namespace Orkige
{
	//---------------------------------------------------------
	//! convert a "1"/"0" attribute string to bool (replacement for the old lexical_cast<bool>)
	static bool GameObjectStringToBool(String const & value)
	{
		if(value == "1")
			return true;
		if(value == "0")
			return false;
		throw std::invalid_argument("GameObject: invalid bool attribute value: " + value);
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GameObject::GameObject(String const & id) : ComponentHolder<GameObjectComponent>(id)
	{
		this->eventManager = new EventManager();
		this->activeSelf = true;
		this->activeInHierarchy = true;
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
		optr<tinyxml2::XMLDocument>	document =  onew(new tinyxml2::XMLDocument());
		document->LoadFile(templateFileName.c_str());
		if(!document || document->Error())
		{
			oDebugMsg("gameobject",0,"Error Loading file: "<<templateFileName<<std::endl <<document->GetErrorStr1());
			return false;
		}
		document->LoadFile(document->Value());
		if(!document || document->Error())
		{
			oDebugMsg("gameobject",0,"Error Loading file: "<<templateFileName<<std::endl <<document->GetErrorStr1());
			return false;
		}
		tinyxml2::XMLElement* xmlRoot = document->RootElement();
		if(!xmlRoot || document->Error())
		{
			oDebugMsg("gameobject",0,"Error finding XML-Root in file: "<<templateFileName<<std::endl <<document->GetErrorStr1());
			return false;
		}
		for(tinyxml2::XMLElement* elementType = xmlRoot->FirstChildElement(); elementType; elementType = elementType->NextSiblingElement())
		{
			String const & elementTypeName = elementType->Value();
			if(elementTypeName == "Attributes")
			{
				for(tinyxml2::XMLElement* element = elementType->FirstChildElement(); element; element = element->NextSiblingElement())
				{
					String const & attributeTypeName = element->Value();
					const char * id = element->Attribute("id");
					const char * value = element->Attribute("value");
					oAssert(id);
					oAssert(value);
					if(attributeTypeName == "int")
					{
						int i = std::stoi(String(value));
						this->setAttribute(id,i);
					}
					else if(attributeTypeName == "bool")
					{
						bool b = GameObjectStringToBool(value);
						this->setAttribute(id,b);
					}
					else if(attributeTypeName == "float")
					{
						float f = std::stof(String(value));
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
				for(tinyxml2::XMLElement* element = elementType->FirstChildElement(); element; element = element->NextSiblingElement())
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

							for(tinyxml2::XMLElement* attributesElement = element->FirstChildElement(); attributesElement; attributesElement = attributesElement->NextSiblingElement())
							{
								String const & elementTypeName = attributesElement->Value();
								if(elementTypeName == "Attributes")
								{
									for(tinyxml2::XMLElement* attributeElement = attributesElement->FirstChildElement(); attributeElement; attributeElement = attributeElement->NextSiblingElement())
									{
										String const & attributeTypeName = attributeElement->Value();
										const char * id = attributeElement->Attribute("id");
										const char * value = attributeElement->Attribute("value");
										oAssert(id);
										oAssert(value);
										if(attributeTypeName == "int")
										{
											int i = std::stoi(String(value));
											component->setAttribute(id,i);
										}
										else if(attributeTypeName == "bool")
										{
											bool b = GameObjectStringToBool(value);
											component->setAttribute(id,b);
										}
										else if(attributeTypeName == "float")
										{
											float f = std::stof(String(value));
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
						oDebugMsg("gameobject",0,"Error while parsing file: "<<templateFileName<<"!"<<std::endl <<document->GetErrorStr1());
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
		optr<tinyxml2::XMLDocument>	document =  onew(new tinyxml2::XMLDocument());
		if(!document || document->Error())
		{
			oDebugMsg("gameobject",0,"Error Saving file: "<<templateFileName<<std::endl <<document->GetErrorStr1());
			return false;
		}
		tinyxml2::XMLDeclaration* decl = document->NewDeclaration("1.0, UTF-8, yes");
		document->InsertEndChild(decl);

		if(document->Error())
		{
			oDebugMsg("gameobject",0,"Error Saving file: "<<templateFileName<<std::endl <<document->GetErrorStr1());
			return false;
		}
		tinyxml2::XMLElement* xmlRoot = document->NewElement("GameObject");
		tinyxml2::XMLElement* attributesElement = document->NewElement("Attributes");

		foreach(OwnedAttributeMap::value_type const & attribute, attributes)
		{
			TypeInfo const & attributeType = attribute.second->getTypeInfo();
			String const & id = attribute.first;
			if(attributeType == AttributeWrapper<int>::getClassTypeInfo())
			{
				tinyxml2::XMLElement* element = document->NewElement("int");
				element->SetAttribute("id",id.c_str());
				element->SetAttribute("value",this->getAttribute<int>(id));
				attributesElement->InsertEndChild(element);
			}
			else if(attributeType == AttributeWrapper<bool>::getClassTypeInfo())
			{
				tinyxml2::XMLElement* element = document->NewElement("bool");
				element->SetAttribute("id",id.c_str());
				element->SetAttribute("value",this->getAttribute<bool>(id));
				attributesElement->InsertEndChild(element);
			}
			else if(attributeType == AttributeWrapper<float>::getClassTypeInfo())
			{
				tinyxml2::XMLElement* element = document->NewElement("float");
				element->SetAttribute("id",id.c_str());
				element->SetAttribute("value",this->getAttribute<float>(id));
				attributesElement->InsertEndChild(element);
			}
			else if(attributeType == AttributeWrapper<String>::getClassTypeInfo())
			{
				tinyxml2::XMLElement* element = document->NewElement("string");
				element->SetAttribute("id",id.c_str());
				element->SetAttribute("value",this->getAttribute<String>(id).c_str());
				attributesElement->InsertEndChild(element);
			}
		}
		xmlRoot->InsertEndChild(attributesElement);

		ComponentMap const & components = this->getComponents();

		tinyxml2::XMLElement* componentsElement = document->NewElement("Components");
		foreach(ComponentMap::value_type const & current, components)
		{
			optr<OwnedComponentType> component = current.second;
			String const & componentTypeName = current.first.getName();
			tinyxml2::XMLElement* element = document->NewElement(componentTypeName.c_str());
			tinyxml2::XMLElement* componentAttributesElement = document->NewElement("Attributes");
			foreach(GameObjectComponent::OwnedAttributeMap::value_type const & attribute, component->getAttributes())
			{
				TypeInfo const & attributeType = attribute.second->getTypeInfo();
				String const & id = attribute.first;
				if(attributeType == AttributeWrapper<int>::getClassTypeInfo())
				{
					tinyxml2::XMLElement* element = document->NewElement("int");
					element->SetAttribute("id",id.c_str());
					element->SetAttribute("value",component->getAttribute<int>(id));
					componentAttributesElement->InsertEndChild(element);
				}
				else if(attributeType == AttributeWrapper<bool>::getClassTypeInfo())
				{
					tinyxml2::XMLElement* element = document->NewElement("bool");
					element->SetAttribute("id",id.c_str());
					element->SetAttribute("value",component->getAttribute<bool>(id));
					componentAttributesElement->InsertEndChild(element);
				}
				else if(attributeType == AttributeWrapper<float>::getClassTypeInfo())
				{
					tinyxml2::XMLElement* element = document->NewElement("float");
					element->SetAttribute("id",id.c_str());
					element->SetAttribute("value",component->getAttribute<float>(id));
					componentAttributesElement->InsertEndChild(element);
				}
				else if(attributeType == AttributeWrapper<String>::getClassTypeInfo())
				{
					tinyxml2::XMLElement* element = document->NewElement("string");
					element->SetAttribute("id",id.c_str());
					element->SetAttribute("value",component->getAttribute<String>(id).c_str());
					componentAttributesElement->InsertEndChild(element);
				}
			}
			element->InsertEndChild(componentAttributesElement);
			bool componentSaved = component->onSaveTemplate(element);
			if(!componentSaved)
			{
				oDebugMsg("gameobject",0,"Error while saving Component: "<<componentTypeName<< " to file: "<<templateFileName<<"!");
				retval = false;
			}
			componentsElement->InsertEndChild(element);

		}
		xmlRoot->InsertEndChild(componentsElement);
		document->InsertEndChild(xmlRoot);
		document->SaveFile(templateFileName.c_str());

		if(document->Error())
		{
			oDebugMsg("gameobject",0,"Error while saving file: "<<templateFileName<<"!"<<std::endl <<document->GetErrorStr1());
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
	woptr<GameObject> GameObject::getParent()
	{
		if(this->parentId.empty())
		{
			return oNull<GameObject>();
		}
		return GameObjectManager::getSingleton().getGameObject(this->parentId);
	}
	//---------------------------------------------------------
	bool GameObject::setParent(String const & newParentId, bool keepWorldTransform)
	{
		if(newParentId == this->parentId)
		{
			return true;
		}
		String const & id = this->getObjectID();
		GameObjectManager & manager = GameObjectManager::getSingleton();
		GameObject* newParent = NULL;
		if(!newParentId.empty())
		{
			if(newParentId == id)
			{
				oDebugMsg("core",0,"GameObject: " << id << " cannot be its own parent!");
				return false;
			}
			optr<GameObject> parent = manager.getGameObject(newParentId).lock();
			if(!parent)
			{
				oDebugMsg("core",0,"GameObject: " << id << " cannot be parented to unknown GameObject: " << newParentId << "!");
				return false;
			}
			// cycle guard: re-parenting onto an own descendant is refused
			if(manager.isDescendantOf(newParentId, id))
			{
				oDebugMsg("core",0,"GameObject: " << id << " cannot be parented to its own descendant: " << newParentId << "!");
				return false;
			}
			newParent = parent.get();
		}
		String const oldParentId = this->parentId;
		this->parentId = newParentId;
		manager.onObjectReparented(id, oldParentId, newParentId);
		// components map the new parent onto their scene state (the
		// TransformComponent re-parents its render node, preserving the
		// world transform when keepWorldTransform is set)
		foreach(ComponentMap::value_type const & current, components)
		{
			current.second->onParentChanged(newParent, keepWorldTransform);
		}
		// a new ancestor chain can change the effective active state
		this->refreshActiveInHierarchy();
		return true;
	}
	//---------------------------------------------------------
	StringVector const & GameObject::getChildIds() const
	{
		return GameObjectManager::getSingleton().getChildren(this->getObjectID());
	}
	//---------------------------------------------------------
	void GameObject::setActive(bool active)
	{
		if(this->activeSelf == active)
		{
			return;
		}
		this->activeSelf = active;
		this->refreshActiveInHierarchy();
	}
	//---------------------------------------------------------
	bool GameObject::hasTag(String const & tag) const
	{
		return std::find(this->tags.begin(), this->tags.end(), tag) != this->tags.end();
	}
	//---------------------------------------------------------
	void GameObject::addTag(String const & tag)
	{
		if(tag.empty() || this->hasTag(tag))
		{
			return;
		}
		StringVector const oldTags = this->tags;	// copy: the index diffs old vs new
		this->tags.push_back(tag);
		GameObjectManager::getSingleton().onObjectTagsChanged(this->getObjectID(), oldTags, this->tags);
	}
	//---------------------------------------------------------
	void GameObject::removeTag(String const & tag)
	{
		StringVector::iterator it = std::find(this->tags.begin(), this->tags.end(), tag);
		if(it == this->tags.end())
		{
			return;
		}
		StringVector const oldTags = this->tags;	// copy: the index diffs old vs new
		this->tags.erase(it);
		GameObjectManager::getSingleton().onObjectTagsChanged(this->getObjectID(), oldTags, this->tags);
	}
	//---------------------------------------------------------
	void GameObject::setTags(StringVector const & newTags)
	{
		// drop empties and duplicates, preserving first-seen order
		StringVector cleaned;
		foreach(String const & tag, newTags)
		{
			if(!tag.empty() && std::find(cleaned.begin(), cleaned.end(), tag) == cleaned.end())
			{
				cleaned.push_back(tag);
			}
		}
		if(cleaned == this->tags)
		{
			return;
		}
		StringVector const oldTags = this->tags;
		this->tags = cleaned;
		GameObjectManager::getSingleton().onObjectTagsChanged(this->getObjectID(), oldTags, this->tags);
	}
	//---------------------------------------------------------
	void GameObject::clearTags()
	{
		if(this->tags.empty())
		{
			return;
		}
		StringVector const oldTags = this->tags;
		this->tags.clear();
		GameObjectManager::getSingleton().onObjectTagsChanged(this->getObjectID(), oldTags, this->tags);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void GameObject::refreshActiveInHierarchy()
	{
		bool parentActive = true;
		if(optr<GameObject> parent = this->getParent().lock())
		{
			parentActive = parent->isActiveInHierarchy();
		}
		const bool effective = this->activeSelf && parentActive;
		if(effective == this->activeInHierarchy)
		{
			return;
		}
		this->activeInHierarchy = effective;
		foreach(ComponentMap::value_type const & current, components)
		{
			current.second->onSetActive(effective);
		}
		// descendants whose effective state changes with ours
		GameObjectManager & manager = GameObjectManager::getSingleton();
		StringVector const children = this->getChildIds();	// copy: hooks may mutate
		foreach(String const & childId, children)
		{
			if(optr<GameObject> child = manager.getGameObject(childId).lock())
			{
				child->refreshActiveInHierarchy();
			}
		}
	}
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
		// components joining a deactivated object start deactivated
		if(!this->activeInHierarchy)
		{
			goc->onSetActive(false);
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
		//--- hierarchy (Unity-style tree) ---
		OFUNCCR(getParentId)
		OFUNCWEAK(getParent)
		//Lua gets the keep-world-transform form (the Unity default);
		//scene loading uses the two-argument C++ overload
		OFUNCOVERL(setParent, bool (ExposedClassType::*)(String const &))
		OFUNCCR(getChildIds)
		//--- prefab instance (read-only from scripts) ---
		OFUNCCR(getPrefabRef)
		//--- active state ---
		OFUNC(setActive)
		OFUNC(isActiveSelf)
		OFUNC(isActiveInHierarchy)
		//--- tags (multi-tag labels) ---
		OFUNC(hasTag)
		OFUNC(addTag)
		OFUNC(removeTag)
	OOBJECT_END
}
