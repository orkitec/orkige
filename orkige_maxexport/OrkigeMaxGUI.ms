--//////////////////////////////////////////////////////////////////////////////
-- ogreExporter.cpp
-- Author	  : Doug Perkowski - OC3 Entertainment, Inc.
-- Start Date : December 10th, 2007
-- Copyright  : (C) 2007 OC3 Entertainment, Inc.
--//////////////////////////////////////////////////////////////////////////////
--********************************************************************************
--                                                                               *
--  This script is released under the											 *
--  Creative Commons Attribution 3.0 United States License						 *
--  http://creativecommons.org/licenses/by/3.0/us/								 *
--                                                                               *
--********************************************************************************

macroScript showOrkigeTools
	category:"Orkige Tools"
	internalCategory:"Orkige Tools"
	buttonText:"Orkige Tools"
	tooltip:"Orkige Tools"
	Icon:#("Maintoolbar",49)
(
OrkigeMaxExporterDialog = newRolloutFloater "Orkige Max Exporter" 400 800

OrkigeObject = Orkige();

OrkigeObject.set_exportScene("off");
OrkigeObject.set_exportAll("off")
OrkigeObject.set_exportBlendShapes("off");

-- We have 3 identical animation GUIs, so abstract this functionality into functions
-- that can be used by all 3.
function animRefresh roll isEnabled =
(
	roll.anims.selection = 0
	roll.animName.text = ""
	roll.sampleFrames.text = 1 as String 
	roll.sampleSeconds.text = (1.0 / frameRate ) as String   
	if roll.rangeType.state == 1 then
	(
		if roll.rangeUnit.state == 2 then
		(
			roll.start.text = ((animationRange.start as float) / 4800 ) as String
			roll.stop.text = ((animationRange.end as float) / 4800 ) as String
		)
		else
		(
			roll.start.text = ((animationRange.start as integer) / TicksPerFrame ) as String
			roll.stop.text = ((animationRange.end as integer) / TicksPerFrame ) as String			
		)
	)
	if isEnabled then
	(
		roll.grp1.enabled = true
		roll.add.enabled = true
		roll.delete.enabled = true
		roll.rangeType.enabled = true
		roll.rangeUnit.enabled = true
		roll.sampleBy.enabled = true
		roll.anims.enabled = true
		roll.animName.enabled = true
		if roll.rangeType.state == 2 then
		(
			roll.start.enabled = true
			roll.stop.enabled = true
		)
		else
		(
			roll.start.enabled = false
			roll.stop.enabled = false
		)
		if roll.sampleBy.state == 2 then
		(
			roll.sampleSeconds.enabled = true
			roll.sampleFrames.enabled = false
		)
		else
		(
			roll.sampleSeconds.enabled = false	
			roll.sampleFrames.enabled = true	
		)
	)
	else
	(
		roll.grp1.enabled = false
		roll.add.enabled = false
		roll.delete.enabled = false
		roll.rangeType.enabled = false
		roll.rangeUnit.enabled = false
		roll.sampleBy.enabled = false
		roll.sampleFrames.enabled = false
		roll.sampleSeconds.enabled = false
		roll.anims.enabled = false
		roll.animName.enabled = false
		roll.start.enabled = false
		roll.stop.enabled = false	
	)
)
function rangeTypeChange roll =
(
	if roll.rangeType.state == 1 then 
	(
		if roll.rangeUnit.state == 2 then
		(
			roll.start.text = ((animationRange.start as float) / 4800 ) as String
			roll.stop.text = ((animationRange.end as float) / 4800 ) as String
		)
		else
		(
			roll.start.text = ((animationRange.start as integer) / TicksPerFrame ) as String
			roll.stop.text = ((animationRange.end as integer) / TicksPerFrame ) as String			
		)
		roll.start.enabled = false
		roll.stop.enabled = false
	)
	else
	(
		roll.start.enabled = true
		roll.stop.enabled = true
	)
)
function rangeUnitChange roll =
(		
	tempStart = roll.start.text as float
	tempStop = roll.stop.text as float			
	if roll.rangeUnit.state == 2 then
	(
		roll.start.text = (tempStart * TicksPerFrame  / 4800 ) as String
		roll.stop.text = (tempStop * TicksPerFrame / 4800 ) as String
	)
	else
	(
		roll.start.text = ((tempStart *  4800 / TicksPerFrame + .5) as integer )as String
		roll.stop.text = ((tempStop * 4800 / TicksPerFrame + .5) as integer ) as String	
	)	
)
function sampleByChange roll =
(
	if roll.sampleBy.state == 1 then 
	(
		roll.sampleFrames.enabled = true
		roll.sampleSeconds.enabled = false
	)
	else
	(
		roll.sampleFrames.enabled = false
		roll.sampleSeconds.enabled = true
	)
)
function getSkelAnims =
(
	animNames = #()
	numClips = OrkigeObject.get_numClips_skelClipList()
	for i = 0 to (numClips - 1) do
	(
		name = OrkigeObject.get_name_skelClipList(i)
		append animNames name
	)
	return animNames
)
function getBSAnims =
(
	animNames = #()
	numClips = OrkigeObject.get_numClips_BSClipList()
	for i = 0 to (numClips - 1) do
	(
		name = OrkigeObject.get_name_BSClipList(i)
		append animNames name
	)
	return animNames
)
function getVertAnims =
(
	animNames = #()
	numClips = OrkigeObject.get_numClips_vertClipList()
	for i = 0 to (numClips - 1) do
	(
		name = OrkigeObject.get_name_vertClipList(i)
		append animNames name
	)
	return animNames
)
rollout MeshRollout "Mesh" width:350 height:215
(
	checkbox exportVBA "Include vertex bone assignments" pos:[9,47] width:221 height:18
	editText meshFilename "" pos:[3,137] width:328 height:22 enabled:false
	label meshFilenameLabel "Mesh Filename" pos:[7,120] width:218 height:17
	checkbox exportMesh "Export Mesh" pos:[9,11] width:159 height:18
	checkbox useSharedGeom "Use Shared Geometry" pos:[9,29] width:159 height:18 checked:false
	checkbox exportVertNorm "Include vertex normals" pos:[9,66] width:221 height:18
	checkbox exportVertCol "Include diffuse vertex colors" pos:[9,84] width:221 height:18
	checkbox exportTexCoord "Include texture coordinates" pos:[9,102] width:221 height:18
	checkbox buildEdges "Build edges list (for shadows)" pos:[10,162] width:221 height:18
	checkbox buildTangents "Build tangent vectors" pos:[10,180] width:221 height:18
	fn refresh = 
	(
		exportMesh.checked = OrkigeObject.get_exportMesh()
		useSharedGeom.checked = OrkigeObject.get_useSharedGeom();
		exportVBA.checked = OrkigeObject.get_exportVBA();
		exportVertNorm.checked = OrkigeObject.get_exportVertNorm();
		exportVertCol.checked = OrkigeObject.get_exportVertCol();
		buildEdges.checked = OrkigeObject.get_buildEdges();
		buildTangents.checked = OrkigeObject.get_buildTangents();
		MeshFilename.text = OrkigeObject.get_meshFilename()
		if exportMesh.checked then
		(
			useSharedGeom.enabled = true
			exportVBA.enabled = true
			exportVertNorm.enabled = true
			exportVertCol.enabled = true
			exportTexCoord.enabled = true
			meshFilename.enabled = true
			meshFilenameLabel.enabled = true
			buildEdges.enabled = true
			buildTangents.enabled = true
		)
		else
		(
			useSharedGeom.enabled = false
			exportVBA.enabled = false
			exportVertNorm.enabled = false
			exportVertCol.enabled = false
			exportTexCoord.enabled = false
			meshFilename.enabled = false
			meshFilenameLabel.enabled = false
			buildEdges.enabled = false
			buildTangents.enabled = false
		)	
	)
	on MeshRollout open  do
	(
		refresh()
	)
	on exportVBA changed state do
	(
		if exportVBA.checked then
			OrkigeObject.set_exportVBA("on");
		else
			OrkigeObject.set_exportVBA("off");		
	)
	on exportMesh changed state do
	(
		if exportMesh.checked then
		(
			OrkigeObject.set_exportMesh("on");
			useSharedGeom.enabled = true
			exportVBA.enabled = true
			exportVertNorm.enabled = true
			exportVertCol.enabled = true
			exportTexCoord.enabled = true
			meshFilename.enabled = true
			meshFilenameLabel.enabled = true
			buildEdges.enabled = true
			buildTangents.enabled = true
		)
		else
		(
			OrkigeObject.set_exportMesh("off");
			useSharedGeom.enabled = false
			exportVBA.enabled = false
			exportVertNorm.enabled = false
			exportVertCol.enabled = false
			exportTexCoord.enabled = false
			meshFilename.enabled = false
			meshFilenameLabel.enabled = false
			buildEdges.enabled = false
			buildTangents.enabled = false
		)
	)
	on useSharedGeom changed state do
	(
		if useSharedGeom.checked then
			OrkigeObject.set_useSharedGeom("on");
		else
			OrkigeObject.set_useSharedGeom("off");
	)
	on exportVertNorm changed state do
	(
		if exportVertNorm.checked then
			OrkigeObject.set_exportVertNorm("on");
		else
			OrkigeObject.set_exportVertNorm("off");
	)
	on exportVertCol changed state do
	(
		if exportVertCol.checked then
			OrkigeObject.set_exportVertCol("on");
		else
			OrkigeObject.set_exportVertCol("off");
	)
	on exportTexCoord changed state do
	(
		if exportTexCoord.checked then
			OrkigeObject.set_exportTexCoord("on");

		else
			OrkigeObject.set_exportTexCoord("off");
	)
	on buildEdges changed state do
	(
		if buildEdges.checked then
			OrkigeObject.set_buildEdges("on");
		else
			OrkigeObject.set_buildEdges("off");
	)
	on buildTangents changed state do
	(
		if buildTangents.checked then
			OrkigeObject.set_buildTangents("on");
		else
			OrkigeObject.set_buildTangents("off");
	)
)
rollout MaterialsRollout "Materials" width:350 height:168
(
	editText materialFilename "" pos:[11,49] width:328 height:22 enabled:false
	label MaterialFilenameLabel "Material Filename" pos:[11,32] width:218 height:17 enabled:false
	checkbox exportMaterial "Export Materials to Ogre .material file" pos:[11,10] width:284 height:18
	editText matPrefix "" pos:[15,117] width:328 height:22 enabled:false
	label matPrefixLabel "Material name prefix" pos:[20,96] width:218 height:17 enabled:false
	checkbox copyTextures "Copy texture files to output dir" pos:[11,146] width:271 height:21
	fn refresh = 
	(
		exportMaterial.checked = OrkigeObject.get_exportMaterial()
		copyTextures.checked = OrkigeObject.get_copyTextures()
		MaterialFilename.text = OrkigeObject.get_materialFilename()
		matPrefix.text = OrkigeObject.get_matPrefix()
		if exportMaterial.checked then
		(
			MaterialFilename.enabled = true
			MaterialFilenameLabel.enabled = true
			copyTextures.enabled = true	
			matPrefix.enabled = true
			matPrefixLabel.enabled = true			
		)
		else
		(
			MaterialFilename.enabled = false
			matPrefix.enabled = false
			MaterialFilenameLabel.enabled = false
			matPrefixLabel.enabled = false
			copyTextures.enabled = false
		)	

	)
	on MaterialsRollout open  do
	(
		refresh()
	)
	on exportMaterial changed state do
	(
		if exportMaterial.checked then
		(
			OrkigeObject.set_exportMaterial("on");
			materialFilename.enabled = true;
			materialFilenameLabel.enabled = true;
			matPrefix.enabled = true;
			matPrefixLabel.enabled = true;
			copyTextures.enabled = true;
		)
		else
		(
			OrkigeObject.set_exportMaterial("off");
			materialFilename.enabled = false;
			materialFilenameLabel.enabled = false;
			matPrefix.enabled = false;
			matPrefixLabel.enabled = false;
			copyTextures.enabled = false;
		)
	)
	on copyTextures changed state do
	(
		if copyTextures.checked then
		 OrkigeObject.set_copyTextures("on");
		else
		 OrkigeObject.set_copyTextures("off");
	)
)

--rollout ScenesRollout "Scene" width:350 height:168
--(
--	editText sceneFilename "" pos:[11,49] width:328 height:22 enabled:false
--	label SceneFilenameLabel "Scene Filename" pos:[11,32] width:218 height:17 enabled:false
--	checkbox exportScene "Export mesh, cameras and lights to .scene file" pos:[11,10] width:300 height:18
--	fn refresh = 
--	(
--		exportScene.checked = OrkigeObject.get_exportScene()
--		SceneFilename.text = OrkigeObject.get_sceneFilename()
--		if exportScene.checked then
--		(
--			SceneFilename.enabled = true
--			SceneFilenameLabel.enabled = true
--		)
--		else
--		(
--			SceneFilename.enabled = false
--			SceneFilenameLabel.enabled = false
--		)	
--
--	)
--	on ScenesRollout open  do
--	(
--		refresh()
--	)
--	on exportScene changed state do
--	(
--		if exportScene.checked then
--		(
--			OrkigeObject.set_exportScene("on");
--			sceneFilename.enabled = true;
--			sceneFilenameLabel.enabled = true;
--		)
--		else
--		(
--			OrkigeObject.set_exportScene("off");
--			sceneFilename.enabled = false;
--			sceneFilenameLabel.enabled = false;
--		)
--	)
--)

rollout SkeletonRollout "Skeleton" width:400 height:367
(
	checkbox exportSkeleton "Export skeleton" pos:[11,10] width:335 height:18
	editText skeletonFilename "" pos:[28,63] width:292 height:25
	label skeletonFilenameLabel "Skeleton Filename" pos:[28,46] width:183 height:17
	checkbox exportSkelAnims "Export animations (requires export of skeleton)" pos:[28,93] width:335 height:18
	checkbox skelBB "Include animations in bounding box" pos:[47,112] width:335 height:18
	checkbox normalizeScale "Normalize scale" pos:[28,28] width:335 height:18
	groupBox grp1 "Animation Clips" pos:[5,142] width:388 height:201
	button add "Add / Edit" pos:[13,309] width:73 height:25
	button delete "Delete" pos:[12,279] width:73 height:25
	radioButtons rangeType "Time Range" pos:[217,157] width:166 height:30 labels:#("Timer", "Start/End") columns:2
	radioButtons rangeUnit "" pos:[211,246] width:175 height:16 labels:#("Frames", "Seconds") columns:2
	editText start "start" pos:[243,194] width:90 height:25
	editText stop "stop" pos:[243,218] width:90 height:25
	radioButtons sampleBy "Sample by:" pos:[201,277] width:175 height:30 labels:#("Frames", "Seconds") columns:2
	editText sampleFrames "" pos:[205,311] width:58 height:25
	editText sampleSeconds "" pos:[291,311] width:61 height:25
	listBox anims "" pos:[19,164] width:158 height:6
	editText animName "" pos:[93,312] width:88 height:25
	fn refresh =
	(
		exportSkeleton.checked = OrkigeObject.get_exportSkeleton()
		exportSkelAnims.checked = OrkigeObject.get_exportSkelAnims()
		normalizeScale.checked = OrkigeObject.get_normalizeScale()
		skeletonFilename.text = OrkigeObject.get_skeletonFilename()
		skelBB.checked = OrkigeObject.get_skelBB()	
		shouldEnableAnimations = false
		if exportSkeleton.checked then
		(
			skeletonFilename.enabled = true
			skeletonFilenameLabel.enabled = true
			exportSkelAnims.enabled = true
			normalizeScale.enabled = true	
			if exportSkelAnims.checked then 
			(
				shouldEnableAnimations = true	
				skelBB.enabled = true 
			)
		)
		else
		(
			skeletonFilename.enabled = false
			skeletonFilenameLabel.enabled = false
			exportSkelAnims.enabled = false
			skelBB.enabled = false
			normalizeScale.enabled = false
		)	
		anims.items = getSkelAnims()
		animRefresh SkeletonRollout shouldEnableAnimations
	)
	on SkeletonRollout open  do
	(
		refresh()
	)
	on exportSkeleton changed state do
	(
		shouldEnableAnimations = false
		if exportSkeleton.checked then
		(
			OrkigeObject.set_exportSkeleton("on");
			skeletonFilename.enabled = true
			skeletonFilenameLabel.enabled = true
			exportSkelAnims.enabled = true
			normalizeScale.enabled = true				
			if exportSkelAnims.checked then 
			(
				shouldEnableAnimations = true	
				skelBB.enabled = true 
			)
		)
		else
		(
			OrkigeObject.set_exportSkeleton("off");
			skeletonFilename.enabled = false
			skeletonFilenameLabel.enabled = false
			exportSkelAnims.enabled = false
			normalizeScale.enabled = false	
			skelBB.enabled = false		
		)
		animRefresh SkeletonRollout shouldEnableAnimations
	)
	on exportSkelAnims changed state do
	(
		if exportSkelAnims.checked then
		(
			OrkigeObject.set_exportSkelAnims("on");
			skelBB.enabled = true
			animRefresh SkeletonRollout true
		)
		else
		(
			skelBB.enabled = false
			OrkigeObject.set_exportSkelAnims("off");
			animRefresh SkeletonRollout false
		)
	)
	on skelBB changed state do
	(
		if skelBB.checked then
			OrkigeObject.set_skelBB("on");
		else
			OrkigeObject.set_skelBB("off");
	)
	on normalizeScale changed state do
	(
		if normalizeScale.checked then
			OrkigeObject.set_normalizeScale("on");
		else
			OrkigeObject.set_normalizeScale("off");
	)
	on add pressed  do
	(
		startTemp = start.text as float
		stopTemp = stop.text as float
		rateTemp = sampleSeconds.text as float
		if rangeUnit.state == 1 then
		(
			startTemp = startTemp / frameRate
			stopTemp = stopTemp / frameRate
		)
		if sampleBy.state == 1 then 
		(
			rateTemp = (sampleFrames.text as float) / frameRate
		)
		OrkigeObject.add_skelClipList animName.text startTemp stopTemp rateTemp
		anims.items = getSkelAnims()
		anims.selection = 0
		animName.text = ""
	)
	on delete pressed  do
	(
		newIndex = anims.selection;
		if newIndex  == OrkigeObject.get_numClips_skelClipList() then 
			if newIndex > 0 then
				newIndex = newIndex  - 1
		OrkigeObject.remove_skelClipList (anims.selection - 1)
		anims.items = getSkelAnims()
		anims.selection = 0
		animName.text = ""
	)
	on rangeType changed stat do
	(
		rangeTypeChange SkeletonRollout
	)
	on rangeUnit changed stat do
	(
		rangeUnitChange SkeletonRollout
	)
	on sampleBy changed stat do
	(
		sampleByChange SkeletonRollout
	)
	on anims selected sel do
	(
		sampleBy.state = 1
		rangeUnit.state = 2
		rangeType.state = 2
		start.enabled = true
		stop.enabled = true	
		start.text = (OrkigeObject.get_start_skelClipList (anims.selection - 1) ) as String
		stop.text = (OrkigeObject.get_stop_skelClipList (anims.selection - 1) ) as String
		sampleSeconds.text = (OrkigeObject.get_rate_skelClipList (anims.selection - 1) ) as String
		sampleFrames.text = ((OrkigeObject.get_rate_skelClipList (anims.selection - 1) * frameRate  ) as Integer ) as String
		animName.text = anims.selected
	)
)
rollout BlendShapesRollout "Blend Shapes" width:350 height:130
(
	checkbox exportBlendShapes "Export blend shapes (to mesh file)" pos:[10,10] width:335 height:18
	checkbox exportBSAnims "Export animations" pos:[47,50] width:335 height:18
	checkbox bsBB "Include blendshapes in bounding box" pos:[10,31] width:335 height:18
	groupBox grp1 "Animation Clips" pos:[5,80] width:388 height:201
	button add "Add / Edit" pos:[13,247] width:73 height:25
	button delete "Delete" pos:[12,217] width:73 height:25
	radioButtons rangeType "Time Range" pos:[217,95] width:166 height:30 labels:#("Timer", "Start/End") columns:2
	radioButtons rangeUnit "" pos:[211,184] width:175 height:16 labels:#("Frames", "Seconds") columns:2
	editText start "start" pos:[243,132] width:90 height:25
	editText stop "stop" pos:[243,156] width:90 height:25
	radioButtons sampleBy "Sample by:" pos:[201,215] width:175 height:30 labels:#("Frames", "Seconds") columns:2
	editText sampleFrames "" pos:[205,250] width:60 height:25
	editText sampleSeconds "" pos:[291,250] width:60 height:25
	listBox anims "" pos:[19,102] width:158 height:6
	editText animName "" pos:[93,250] width:88 height:25
	fn refresh = 
	(
		exportBlendShapes.checked = OrkigeObject.get_exportBlendShapes()
		exportBSAnims.checked = OrkigeObject.get_exportBSAnims()
		bsBB.checked = OrkigeObject.get_bsBB()	
		shouldEnableAnimations = false
		if exportBlendShapes.checked then
		(
			exportBSAnims.enabled = true
			bsBB.enabled = true 
			if exportBSAnims.checked then 
				shouldEnableAnimations = true	
		)
		else
		(
			exportBSAnims.enabled = false
			bsBB.enabled = false
		)	
		anims.items = getBSAnims()
		animRefresh BlendShapesRollout shouldEnableAnimations	
	)
	on BlendShapesRollout open  do
	(
		refresh()
	)
	on exportBlendShapes changed state do
	(
		shouldEnableAnimations = false
		if exportBlendShapes.checked then
		(
			OrkigeObject.set_exportBlendShapes("on");
			exportBSAnims.enabled = true	
			bsBB.enabled = true	
			if exportBSAnims.checked then 
				shouldEnableAnimations = true	
		)
		else
		(
			OrkigeObject.set_exportBlendShapes("off");
			exportBSAnims.enabled = false
			bsBB.enabled = false		
		)
		animRefresh BlendShapesRollout shouldEnableAnimations
	)
	on exportBSAnims changed state do
	(
		if exportBSAnims.checked then
		(
			OrkigeObject.set_exportBSAnims("on");
			animRefresh BlendShapesRollout true
		)
		else
		(
			OrkigeObject.set_exportBSAnims("off");
			animRefresh BlendShapesRollout false
		)
	)
	on bsBB changed state do
	(
		if bsBB.checked then
			OrkigeObject.set_bsBB("on");
		else
			OrkigeObject.set_bsBB("off");
	)
	on add pressed  do
	(
		startTemp = start.text as float
		stopTemp = stop.text as float
		rateTemp = sampleSeconds.text as float
		if rangeUnit.state == 1 then
		(
			startTemp = startTemp / frameRate
			stopTemp = stopTemp / frameRate
		)
		if sampleBy.state == 1 then 
		(
			rateTemp = (sampleFrames.text as float) / frameRate
		)
		OrkigeObject.add_BSClipList animName.text startTemp stopTemp rateTemp
		anims.items = getBSAnims()
		anims.selection = 0
		animName.text = ""
	)
	on delete pressed  do
	(
		newIndex = anims.selection;
		if newIndex  == OrkigeObject.get_numClips_BSClipList() then 
			if newIndex > 0 then
				newIndex = newIndex  - 1
		OrkigeObject.remove_BSClipList (anims.selection - 1)
		anims.items = getBSAnims()
		anims.selection = 0
		animName.text = ""
	)
	on rangeType changed stat do
	(
		rangeTypeChange BlendShapesRollout
	)
	on rangeUnit changed stat do
	(
		rangeUnitChange BlendShapesRollout
	)
	on sampleBy changed stat do
	(
		sampleByChange BlendShapesRollout
	)
	on anims selected sel do
	(
		sampleBy.state = 1
		rangeUnit.state = 2
		rangeType.state = 2
		start.enabled = true
		stop.enabled = true	
		start.text = (OrkigeObject.get_start_BSClipList (anims.selection - 1) ) as String
		stop.text = (OrkigeObject.get_stop_BSClipList (anims.selection - 1) ) as String
		sampleSeconds.text = (OrkigeObject.get_rate_BSClipList (anims.selection - 1) ) as String
		sampleFrames.text = ((OrkigeObject.get_rate_BSClipList (anims.selection - 1) * frameRate  ) as Integer ) as String
		animName.text = anims.selected
	)	
)
rollout VertAnimsRollout "Vertex Animations" width:350 height:130
(
	checkbox exportVertAnims "Export vertex animations to mesh file" pos:[10,10] width:340 height:18
	checkbox vertBB "Include animations in bounding box" pos:[28,31] width:335 height:18
	groupBox grp1 "Animation Clips" pos:[5,60] width:388 height:201
	button add "Add / Edit" pos:[13,227] width:73 height:25
	button delete "Delete" pos:[12,197] width:73 height:25
	radioButtons rangeType "Time Range" pos:[217,75] width:166 height:30 labels:#("Timer", "Start/End") columns:2
	radioButtons rangeUnit "" pos:[211,164] width:175 height:16 labels:#("Frames", "Seconds") columns:2
	editText start "start" pos:[243,112] width:90 height:25
	editText stop "stop" pos:[243,136] width:90 height:25
	radioButtons sampleBy "Sample by:" pos:[201,195] width:175 height:30 labels:#("Frames", "Seconds") columns:2
	editText sampleFrames "" pos:[205,230] width:60 height:25
	editText sampleSeconds "" pos:[291,230] width:60 height:25
	listBox anims "" pos:[19,82] width:158 height:6
	editText animName "" pos:[93,230] width:88 height:25
	fn refresh = 
	(
		exportVertAnims.checked = OrkigeObject.get_exportVertAnims()
		vertBB.checked = OrkigeObject.get_vertBB()	
		shouldEnableAnimations = false
		if exportVertAnims.checked then 
		(
			shouldEnableAnimations = true	
			vertBB.enabled = true 
		)
		else
		(
			vertBB.enabled = false
		)	
		anims.items = getVertAnims()
		animRefresh VertAnimsRollout shouldEnableAnimations
	)
	on VertAnimsRollout open  do
	(
		refresh()
	)
	on exportVertAnims changed state do
	(
		if exportVertAnims.checked then
		(
			OrkigeObject.set_exportVertAnims("on");
			vertBB.enabled = true
			animRefresh VertAnimsRollout true
		)
		else
		(
			vertBB.enabled = false
			OrkigeObject.set_exportVertAnims("off");
			animRefresh VertAnimsRollout false
		)
	)
	on vertBB changed state do
	(
		if vertBB.checked then
			OrkigeObject.set_vertBB("on");
		else
			OrkigeObject.set_vertBB("off");
	)
	on add pressed  do
	(
		startTemp = start.text as float
		stopTemp = stop.text as float
		rateTemp = sampleSeconds.text as float
		if rangeUnit.state == 1 then
		(
			startTemp = startTemp / frameRate
			stopTemp = stopTemp / frameRate
		)
		if sampleBy.state == 1 then 
		(
			rateTemp = (sampleFrames.text as float) / frameRate
		)
		OrkigeObject.add_vertClipList animName.text startTemp stopTemp rateTemp
		anims.items = getVertAnims()
		anims.selection = 0
		animName.text = ""
	)
	on delete pressed  do
	(
		newIndex = anims.selection;
		if newIndex  == OrkigeObject.get_numClips_vertClipList() then 
			if newIndex > 0 then
				newIndex = newIndex  - 1
		OrkigeObject.remove_vertClipList (anims.selection - 1)
		anims.items = getVertAnims()
		anims.selection = 0
		animName.text = ""
	)
	on rangeType changed stat do
	(
		rangeTypeChange VertAnimsRollout
	)
	on rangeUnit changed stat do
	(
		rangeUnitChange VertAnimsRollout
	)
	on sampleBy changed stat do
	(
		sampleByChange VertAnimsRollout
	)
	on anims selected sel do
	(
		sampleBy.state = 1
		rangeUnit.state = 2
		rangeType.state = 2
		start.enabled = true
		stop.enabled = true	
		start.text = (OrkigeObject.get_start_vertClipList (anims.selection - 1) ) as String
		stop.text = (OrkigeObject.get_stop_vertClipList (anims.selection - 1) ) as String
		sampleSeconds.text = (OrkigeObject.get_rate_vertClipList (anims.selection - 1) ) as String
		sampleFrames.text = ((OrkigeObject.get_rate_vertClipList (anims.selection - 1) * frameRate  ) as Integer ) as String
	)	
)
rollout CommonParametersRollout "Common Parameters" width:350 height:145
(
	radioButtons exportAll "Export:" pos:[13,10] width:75 height:46 labels:#("all       ", "selected") default:2 columns:1
	radioButtons exportWorldCoords "Coordinate Space:" pos:[129,10] width:60 height:46 labels:#("world", "object") default:1 columns:1
	radioButtons yUpAxis "UpAxis:" pos:[255,10] width:60 height:46 labels:#("Y", "Z") default:1 columns:1
	editText lum "Scale All by:          " pos:[18,64] width:275 height:21
	dropDownList animType "" pos:[127,90] width:166 height:24 items:#("Skeleton / Blendshape", "Vertex")
	label animTypeLabel "Animation Type:" pos:[16,90] width:101 height:20
	fn refresh =
	(
		if OrkigeObject.get_exportWorldCoords() then
			exportWorldCoords.state = 1
		else
			exportWorldCoords.state = 2
		if OrkigeObject.get_yUpAxis() then
			yUpAxis.state = 1
		else
			yUpAxis.state = 2
		if OrkigeObject.get_exportAll() then
			exportAll.state = 1
		else
			exportAll.state = 2
		lum.text = OrkigeObject.get_lum() as string	
		if animType.text == "Vertex" then
		(	
			OrkigeObject.set_exportSkeleton "off"
			OrkigeObject.set_exportBlendShapes "off"
			VertAnimsRollout.exportVertAnims.enabled = true
			SkeletonRollout.exportSkeleton.enabled = false
			BlendShapesRollout.exportBlendShapes.enabled = false
		)
		else
		(
			OrkigeObject.set_exportVertAnims "off"
			VertAnimsRollout.exportVertAnims.enabled = false
			SkeletonRollout.exportSkeleton.enabled = true
			BlendShapesRollout.exportBlendShapes.enabled = true		
		)
		SkeletonRollout.refresh()
		BlendShapesRollout.refresh()
		VertAnimsRollout.refresh()
	)
	on CommonParametersRollout open  do
	(	
		refresh()
	)
	on exportAll changed stat do
	(
		if exportAll.state == 1 then
			OrkigeObject.set_exportAll("on")
		else
			OrkigeObject.set_exportAll("off")
	)
	on exportWorldCoords changed stat do
	(
		if exportWorldCoords.state == 1 then
			OrkigeObject.set_exportWorldCoords("on")
		else
			OrkigeObject.set_exportWorldCoords("off")
	)
	on yUpAxis changed stat do
	(
		if yUpAxis.state == 1 then
			OrkigeObject.set_yUpAxis("on")
		else
			OrkigeObject.set_yUpAxis("off")
	)	
	on animType selected stat do
	(
		refresh()
	)
	on lum entered text do
		OrkigeObject.set_lum(lum.text as float);
)
function refreshControls =
(
	CommonParametersRollout.refresh()
	MeshRollout.refresh()
	MaterialsRollout.refresh()
	SkeletonRollout.refresh()
	BlendShapesRollout.refresh()
	VertAnimsRollout.refresh()
	--ScenesRollout.refresh()
)
rollout MainRollout "Main"
(
	button defaultButton "Default" pos:[20,10] width:110 height:25
	button refreshButton "Refresh" pos:[140,10] width:110 height:25
	button clearPathButton "Clear Paths" pos:[260,10] width:110 height:25
	button exportButton "Export" pos:[140,40] width:110 height:25
	on exportButton pressed do 
	(
		exportString = MeshRollout.meshFilename.text
		
		bIsLegal = pathConfig.isLegalPath exportString
		if exportString == "" then
			bIsLegal = false
		if bIsLegal then
			OrkigeObject.orkigemaxexport()
		else
		(
			exportString = getSaveFileName caption:"Save Ogre .mesh File" types:"Mesh(*.mesh)|*.mesh|"
			if exportString != undefined then
			(
				OrkigeObject.set_meshFilename exportString
				OrkigeObject.orkigemaxexport()
			)
		)
		refreshControls()
	)
	on clearPathButton pressed do
	(
		OrkigeObject.set_meshFilename ""
		OrkigeObject.set_skeletonFilename ""
		OrkigeObject.set_materialFilename ""
		OrkigeObject.set_animFilename ""
		OrkigeObject.set_camerasFilename ""
		OrkigeObject.set_particlesFilename ""
		OrkigeObject.set_matPrefix ""
		OrkigeObject.set_texOutputDir ""
		OrkigeObject.set_sceneFilename ""
		refreshControls()
	)
	on refreshButton pressed do
		refreshControls()
	on defaultButton pressed do
	(
		OrkigeObject.reset_params()
		refreshControls()
	)
		
)
addrollout MainRollout OrkigeMaxExporterDialog
addrollout CommonParametersRollout OrkigeMaxExporterDialog
addrollout MeshRollout OrkigeMaxExporterDialog
addrollout MaterialsRollout OrkigeMaxExporterDialog
--addrollout ScenesRollout OrkigeMaxExporterDialog
addrollout SkeletonRollout OrkigeMaxExporterDialog
addrollout BlendShapesRollout OrkigeMaxExporterDialog
addrollout VertAnimsRollout OrkigeMaxExporterDialog
)
