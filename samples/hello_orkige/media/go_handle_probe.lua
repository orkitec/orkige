-- go_handle_probe.lua : the GameObject/component weak-handle regression probe.
-- The ScriptComponent hands `self.gameObject` and the sibling `self.transform`
-- as WEAK handles now (never raw pointers). init() caches them into the global
-- `shared` table (which outlives this instance) so the C++ side can drop the
-- owning GameObject and prove a later touch raises the honest, pcall-catchable
-- "handle is dead (... 'hero')" error at the touching line, app still running.
function init(self)
	shared.probe_self_go = self.gameObject		-- a GameObjectHandle (owner)
	shared.probe_self_tf = self.transform		-- a component handle (sibling)
	-- a LIVE read confirms the handles dispatch before any teardown
	shared.probe_live_id = self.gameObject:getObjectID()
	shared.probe_live_active = self.gameObject:isActiveSelf()
end
