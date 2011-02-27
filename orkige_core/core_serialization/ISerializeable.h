/**************************************************************
	created:	2010/08/17 at 12:05
	filename: 	ISerializeable.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __ISerializeable_h__17_8_2010__12_05_38__
#define __ISerializeable_h__17_8_2010__12_05_38__

#include "core_base/Meta.h"
#include "core_base/Interface.h"
#include "core_serialization/IArchive.h"

namespace Orkige
{
	//! base class for serializable objects
	class ORKIGE_DLL ISerializeable : public Interface
	{
		OOBJECT(ISerializeable, Interface)
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! overridable method to save stuff to archive
		virtual void save(optr<IArchive> const & ar)=0;
		//! overridable method to load stuff from archive
		virtual void load(optr<IArchive> const & ar)=0;
		//! create object before loading (default is true but should be false for singletons)
		virtual bool createBeforeLoad();
		virtual ~ISerializeable();
	protected:
		ISerializeable();
	private:
	};
	//---------------------------------------------------------
}

#endif //__ISerializeable_h__17_8_2010__12_05_38__
