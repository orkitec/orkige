-- Fixture for the player_lifecycle_selfcheck ctest (ORKIGE_LIFECYCLE_SELFCHECK):
-- the game reacts to the engine's app-lifecycle hooks. The engine pauses the
-- sim and suspends audio on its own; these hooks only let the GAME react.
-- onAppPause/onAppResume record into the persistent `save` store so the
-- selfcheck reads the counts back (and, on pause, proves the store was FLUSHED
-- by the engine right after the hook ran).
function init(self)
	self.pauses = 0
	self.resumes = 0
end

function update(self, dt)
end

-- the app is going to the background: a real game would persist its own
-- progress here (the engine flushes the save store for us right after)
function onAppPause(self)
	self.pauses = self.pauses + 1
	save.set('lifecycle.pauses', self.pauses)
end

-- the app returned to the foreground: gameplay resumes running by default; a
-- real game might re-pause behind an overlay here instead
function onAppResume(self)
	self.resumes = self.resumes + 1
	save.set('lifecycle.resumes', self.resumes)
end
