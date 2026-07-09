-- Lua hot-reload fixture (WP #77), driven by the player_hotreload_selfcheck
-- ctest (ORKIGE_HOTRELOAD_SELFCHECK). This is "variant A": init publishes
-- value = 1 into the shared table. The selfcheck OVERWRITES this file at
-- runtime - first with a variant publishing value = 2 (proving a live
-- recompile-and-swap changed the running behavior while an engine-side value
-- persisted across the swap), then with broken Lua (proving a failed reload
-- keeps the OLD instance ticking and surfaces a non-fatal error) - and
-- restores this original content when it is done, so the committed file stays
-- clean.
function init(self)
	shared.hotreload = shared.hotreload or {}
	shared.hotreload.value = 1
	shared.hotreload.inits = (shared.hotreload.inits or 0) + 1
end

function update(self, dt)
	shared.hotreload.ticks = (shared.hotreload.ticks or 0) + 1
end
