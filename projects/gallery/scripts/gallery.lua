-- The UI gallery driver. The SCREENS are authored declaratively in
-- assets/gallery.oui (the tabbed widget suite) and assets/hud.oui (the
-- safe-area corners); this script only wires behaviour on top of them:
--
--   * the tab bar (its pairing is declarative; the driver just reports which
--     section is showing),
--   * the 1000-row virtualized list (a thousand rows are data, not layout),
--   * the overlay triggers (modal / confirm dialog / toast) and the dialog
--     answer readback,
--   * the slider -> progress-bar mirror, so one control visibly drives another.
--
-- Everything a test wants to observe is published into `shared.gallery`, the
-- same outside-observer contract the other project self-checks use.
local gui, factory
local widgets = {}
local dialogId = nil

local LIST_ROWS = 1000

local function publish(key, value)
	shared.gallery = shared.gallery or {}
	shared.gallery[key] = value
end

local function find(id)
	if gui == nil then
		return nil
	end
	return gui:findWidget(id)
end

function init(self)
	if not Engine.getSingleton():hasUISystem() then
		return
	end
	factory = GuiFactory()
	gui = GuiManager(factory, "gui_default", "OrkigeProject")
	gui:enableInputEvents()
	factory:loadLayout("gallery.oui")
	factory:loadLayout("hud.oui")

	widgets.tabs = gui:getTabBar("tabs")
	widgets.notes = find("notes")
	widgets.bigList = find("bigList")
	widgets.decorScroll = find("decorScroll")
	widgets.volume = find("volume")
	widgets.loading = find("loading")
	widgets.result = find("overlayResult")
	widgets.modalButton = find("showModalButton")
	widgets.confirmButton = find("showConfirmButton")
	widgets.toastButton = find("showToastButton")

	-- a thousand rows: the list is virtualized, so this is a thousand STRINGS,
	-- not a thousand widgets
	if widgets.bigList ~= nil then
		for index = 1, LIST_ROWS do
			widgets.bigList:addItem("Row " .. index)
		end
		publish("listRows", widgets.bigList:getItemCount())
	end

	publish("ready", true)
end

function update(self, dt)
	if gui == nil then
		return
	end

	-- which section is showing (the tab bar owns the panel visibility)
	if widgets.tabs ~= nil then
		publish("tab", widgets.tabs:getSelected())
	end

	-- one control driving another: the slider's snapped step mirrors into the
	-- progress bar (0..4 of five stops -> 0..1)
	if widgets.volume ~= nil and widgets.loading ~= nil then
		local step = widgets.volume:getSelectedItemIndex()
		if step ~= nil and step >= 0 then
			widgets.loading:setProgress(step / 4.0)
		end
	end

	-- overlay triggers
	if widgets.modalButton ~= nil and widgets.modalButton:wasClicked() then
		gui:showModal("galleryModal", true)
		publish("modalId", "galleryModal")
	end
	if widgets.confirmButton ~= nil and widgets.confirmButton:wasClicked() then
		dialogId = gui:showConfirm("Discard changes?",
			"The note you typed will be lost.", "Discard", "Keep")
	end
	if widgets.toastButton ~= nil and widgets.toastButton:wasClicked() then
		gui:showToast(loc("gallery.toast"), 2.0)
	end
	if dialogId ~= nil then
		local answer = gui:getDialogResult(dialogId)
		if answer ~= 0 then
			if widgets.result ~= nil then
				widgets.result:setText(answer == 1 and "Discarded" or "Kept")
			end
			publish("dialogAnswer", answer)
			dialogId = nil
		end
	end

	publish("modalCount", gui:getModalCount())
	if widgets.bigList ~= nil then
		publish("listMaterialized", widgets.bigList:getMaterializedCount())
		publish("listFirst", widgets.bigList:getFirstMaterializedIndex())
	end
	if widgets.notes ~= nil then
		publish("noteLines", widgets.notes:getLineCount())
	end
end

function shutdown(self)
	if gui ~= nil then
		gui:destroyAllWidgets()
	end
	widgets = {}
	gui, factory = nil, nil
end
