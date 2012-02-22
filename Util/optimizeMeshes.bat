@echo off

echo --- Mesh Optimizer ---
echo Optimizes all meshes in a specific folder

echo Enter Path to the Meshes Folder: 
set /p meshPath=


echo Overwrite old mesh files? (y / n) 

:Question
SET /p choice= 
if /i not '%choice%' == 'n' (if /i '%choice%' == 'y' (goto Yes ) ) else goto No  
if defined choice ECHO Don't start a conversation i'm not interested! 
ECHO Answer with y for Yes and n for No. & goto Question 

:No
for %%f IN (%meshPath%\*.mesh) DO call OgreMeshMagick.exe optimise %%f -- %meshPath%\%%~nf_optimized%%~xf

:Yes
for %%f IN (%meshPath%\*.mesh) DO call OgreMeshMagick.exe optimise %%f -- %meshPath%\%%~nxf

echo Optimizing complete!
pause