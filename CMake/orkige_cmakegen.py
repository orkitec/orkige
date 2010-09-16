#**************************************************************
#	created:	2010/08/30 at 11:01
#	filename: 	orkige_cmakegen.py
#	author:	steffen.roemer
#	notice:	This source file is part of orkige (orkitec Game engine)
#			For the latest info, see http://www.orkitec.com/
#	copyright:	(c) 2009-2010 orkitec
#
#	purpose:	little helper script for faster writing CmakeList.txt files
#***************************************************************

import sys, os, dircache, string, time, glob

currentDir = os.getcwd()



for f in os.listdir(currentDir):
	basePath = os.path.join(currentDir, f)
	if os.path.isdir(basePath):
		print "set("+f+"_SOURCE\n\t# ----- Source -----"
		for f2 in os.listdir(basePath):
			subPath = os.path.join(basePath, f2)
			if os.path.isfile(subPath) and (subPath.endswith('.c') or subPath.endswith('.cpp') or subPath.endswith('.cxx') or subPath.endswith('.m') or subPath.endswith('.mm') ):
				print "\t" + f + "/" + f2
		print "\t)\n"
		
		print "set("+f+"_HEADER\n\t# ----- Headers -----"
		for f2 in os.listdir(basePath):
			subPath = os.path.join(basePath, f2)
			if os.path.isfile(subPath) and (subPath.endswith('.h') or subPath.endswith('.hpp') or subPath.endswith('.hxx') or subPath.endswith('.inl') ):
				print "\t" + f + "/" + f2
		print ")\n"
	print "source_group(" + f + " FILES ${" + f + "_HEADER} ${" +f  + "_SOURCE}) \n"
	print "# ---------------------------------------------------------\n"
	
print "### your cmake code goas here ###\n"
print "# ---------------------------------------------------------\n\n"
print "add_library("+"!!!REPLACE_ME_WITH_LIB_NAME!!!" + " STATIC"

for f in os.listdir(currentDir):
	basePath = os.path.join(currentDir, f)
	if os.path.isdir(basePath):
		print "\t${" + f + "_SOURCE}"
		print "\t${" + f + "_HEADER}"

print ")"
		