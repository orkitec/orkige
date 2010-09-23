#!/bin/bash

for f in `ls *.jpg *.png *.tga`
do
  echo "Processing $f file..."
  /Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/texturetool -e PVRTC -f PVR --bits-per-pixel-2 -m -o ${f%.*}.pvr $f
  rm $f
done
