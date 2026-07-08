@echo off

echo copying menuviewer
xcopy /y /q /d /r  ..\..\..\build\Win32\orkige_menuviewer\release\*.exe .

echo copying fontconverter
xcopy /y /q /d /r  ..\..\..\build\Win32\orkige_fontconverter\release\*.exe .

pause