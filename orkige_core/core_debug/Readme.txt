DebugOut library
dependencies:	boost::smart_pointers
				boost::python
				tinyxml
				(stl(vector/string))

USAGE:
------------------------------------
CONFIG:
///create file LogConfig.xml with following content:

<LogConfig>

<LogFile name="LogFile.xml" fileNames="true" />
<DebugChannels>
	<global level="0" filterLog="true"/>
	<default level="0" />
	<bla level="0" />
</DebugChannels>

</LogConfig>

--------------------------------------
CODE:
///Attention: don't create a LogManager object this gets done by the library itself
///loads a config file
LogManager::getSingleton().loadConfig("LogConfig.xml");

///test logging function macros
oDebugMsg("default",0,"test2");
oWarning("blubb");
oWarning("lalalala");
oDebugMsg("global",0,"test0r");
oDebugMsg("unknown",0,"test0r2");
oNotify("notiiiiiiiiiii");
oDebugMsg("bla",0,"bla");

--------------------------------------
RESULT:
///LogFile.xml should look something like this

<LogMessages>
    <NOTIFY>
        <file name="c:\svn\debugtest\app\app.cpp" line="34">
            <message value="notiiiiiiiiiii" />
        </file>
    </NOTIFY>
    <WARNING>
        <file name="d:\svn\debugtest\app\app.cpp" line="30">
            <message value="blubb" />
        </file>
        <file name="d:\svn\debugtest\app\app.cpp" line="31">
            <message value="lalalala" />
        </file>
    </WARNING>
    <ERROR />
    <DEBUG>
        <file name="d:\svn\debugtest\app\app.cpp" line="29">
            <message value="test2" />
        </file>
        <file name="d:\svn\debugtest\app\app.cpp" line="32">
            <message value="test0r" />
        </file>
        <file name="d:\svn\debugtest\app\app.cpp" line="35">
            <message value="bla" />
        </file>
    </DEBUG>
</LogMessages>

///traceable code output should look something like this
d:\svn\debugtest\debugout\olog.cpp (23): 	...LOGGING SYSTEM INITIALIZED!...
d:\svn\debugtest\debugout\ologconfig.cpp (19): Parsing LogConfig: <LogConfig.xml> !
d:\svn\debugtest\debugout\olog.cpp (140): trying to init logfile: LogFile.xml
d:\svn\debugtest\debugout\olog.cpp (129): Logging FileNames is now enabled!
d:\svn\debugtest\debugout\olog.cpp (107): DebugChannel <global> added!
d:\svn\debugtest\debugout\olog.cpp (116): Log Filtering is now enabled!
d:\svn\debugtest\debugout\olog.cpp (107): DebugChannel <default> added!
d:\svn\debugtest\debugout\olog.cpp (107): DebugChannel <bla> added!
d:\svn\debugtest\debugout\ologconfig.cpp (59): LogConfig: <LogConfig.xml> parsed!
d:\svn\debugtest\app\app.cpp (29):	DEBUG:			 test2 
d:\svn\debugtest\app\app.cpp (30):	WARNING:		 blubb 
d:\svn\debugtest\app\app.cpp (31):	WARNING:		 lalalala 
d:\svn\debugtest\app\app.cpp (32):	DEBUG:			 test0r 
d:\svn\debugtest\app\app.cpp (34):	NOTIFY:			 notiiiiiiiiiii 
d:\svn\debugtest\app\app.cpp (35):	DEBUG:			 bla 
