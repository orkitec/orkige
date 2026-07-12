-- UI hot-reload fixture, driven by the editor_ui_hotreload ctest. init loads a
-- declarative .oui screen (a single positioned label). The ctest OVERWRITES
-- assets/hud.oui at runtime - first moving the label (proving the editor's .oui
-- watcher hot-reloaded the running screen, observed via MSG_UI_LAYOUT), then
-- with unparseable content (proving a broken reload keeps the OLD screen up and
-- surfaces a non-fatal error) - and restores the original when it is done, so
-- the committed file stays clean.
local gui, factory

function init(self)
	if not Engine.getSingleton():hasUISystem() then
		return
	end
	factory = GuiFactory()
	gui = GuiManager(factory, "gui_default", "OrkigeProject")
	factory:loadLayout("hud.oui")
	-- the screen rebuilds on a hot-reload; a real game re-acquires its widget
	-- handles here (this fixture just counts the mirror events)
	pcall(function()
		events.subscribe("ui.reloaded", function(payload)
			shared.uiReloads = (shared.uiReloads or 0) + 1
		end)
	end)
end

function update(self, dt)
end
