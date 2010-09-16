/**************************************************************
	created:	2010/08/19 at 23:14
	filename: 	foreach.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __foreach_h__19_8_2010__23_14_02__
#define __foreach_h__19_8_2010__23_14_02__

#include <boost/foreach.hpp>

//!	@brief For iterating over collections. 
//!
//!	Collections can be
//!	arrays, null-terminated strings, or STL containers.
//!	The loop variable can be a value or reference. For
//!	example:
//!
//!	std::list<int> int_list(/*stuff*/);
//!	foreach(int &i, int_list)
//!	{
//!		...
//!	}
//!
//!	Alternately, you can declare the loop variable first,
//!	so you can access it after the loop finishes. Obviously,
//!	if you do it this way, then the loop variable cannot be
//!	a reference.
//!
//!	int i;
//!	foreach(i, int_list)
//!	{ 
//!		... 
//!	}
#define foreach BOOST_FOREACH

#endif //__foreach_h__19_8_2010__23_14_02__
