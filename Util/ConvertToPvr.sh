#!/bin/bash

rm ConvertToPvr.log
echo "Conversion errors" > ConvertToPvr.log
for f in *.png
do
  echo "Processing $f file..."
  /Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/texturetool -e PVRTC -f PVR --bits-per-pixel-4 -m -o ${f%.*}.pvr $f	
  if [ $? == 0 ]; then
  {
	FILENAME_OLD=$f
	FILENAME_NEW=${f%.*}.pvr
	sed -i '' 's/'$FILENAME_OLD'/'$FILENAME_NEW'/g'  *.material
	rm $f	
  } 
  else
  {
	echo "-------------------------">> ConvertToPvr.log
	echo "Error converting file:" $f " Reason:">> ConvertToPvr.log
	/Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/texturetool -e PVRTC -f PVR --bits-per-pixel-4 -m -o ${f%.*}.pvr $f 2>> ConvertToPvr.log
  } fi

done
echo "-------------------------">> ConvertToPvr.log


