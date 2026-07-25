-- levelB marker: proves the arriving scene loaded in BESIDE the persistent
-- survivor. Its init publishes the booted flag the selfcheck waits on.
function init(self)
	shared.persist = shared.persist or {}
	shared.persist.levelBBooted = 1
end
