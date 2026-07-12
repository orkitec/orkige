-- drive the sibling vector-animation rig: idle, then a crossfade to
-- the one-shot hop; record clip + the ended event into shared.heroanim
function init(self)
    self.elapsed = 0.0
    self.hopped = false
    shared.heroanim = shared.heroanim or {}
    shared.heroanim.ended = 0
    shared.heroanim.lastEnded = ""
    events.subscribe("animation.ended", function(e)
        shared.heroanim.ended = (shared.heroanim.ended or 0) + 1
        shared.heroanim.lastEnded = e.clip
    end)
    if self.anim then self.anim:play("idle") end
end
function update(self, dt)
    self.elapsed = self.elapsed + dt
    -- after a beat of idle, crossfade to the one-shot hop once
    if self.anim and not self.hopped and self.elapsed > 0.6 then
        self.hopped = true
        self.anim:crossFade("hop", 0.3)
    end
    if self.anim then
        shared.heroanim.clip = self.anim:currentClip()
    end
end
