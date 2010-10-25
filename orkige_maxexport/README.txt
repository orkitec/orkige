[Artists Section:]

Install:

1. copy orkige_maxexport_release.dle to PathWhereMaxIsInstalled\Autodesk\3ds Max 2009\plugins\

2. copy OrkigeMaxGUI.ms to PathWhereMaxIsInstalled\Autodesk\3ds Max 2009\Scripts\

-----------------------------------------

Create Max Button:

1. In Max GoTo MAXScript->Run Script and run OrkigeMaxGui (don't worry if you don't see anything happening)

2. GoTo Customize->Customize User Interface...

3. There Select ToolBars and the click ond New...

3. Set as name Orkige and press OK.

4. You should see a floating toolbar under Actions search for 'Orkige Tools' and drag it onto the toolbar you just created

5. close Customize User Interface 

6. Place your newly created toolbar where you wan't (you can dock it to the standard max bars) (toolbar cand b dragged like a window)

7. by clicking on the button in the toolbar the Orkige Exporter should pop up

8. restart max

-----------------------------------------

Update Max Button Script:

if you need to update the button script its not just enaugh to overwrite the script in the  PathWhereMaxIsInstalled\Autodesk\3ds Max 2009\Scripts\ directory.

1. Open OrkigeMaxGUI.ms in a texteditor and select and copy all contents

2. Open max and rightcklick on the Orkige Tools button

3. select "Edit macro script"

4. select all contents and overwrite them with the contents you copied from  OrkigeMaxGUI.ms

5. save script (File->Save)

6. restart max


-----------------------------------------


[Programmers Section:]

Build from Source:

1. install max 2009 sdk into the default path

2. goto maxsdk\samples\modifiers\morpher\ and build the morpher.vcproj in Release mode

3. run cmake select BUILD_MAXEXPORTER

