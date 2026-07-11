-- drive a looping squash morph on the sibling soft-body shape
function init(self)
    if self.shape then
        self.shape:playMorph(0, 1.5, true)
    end
end
function update(self, dt)
end
