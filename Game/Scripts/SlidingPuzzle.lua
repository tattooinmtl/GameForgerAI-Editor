-- Sliding-puzzle placeholder: nudges the entity one tile per arrow-key press.
--
-- Rewritten 2026-09-06. The previous version was the single line
-- print('Sliding puzzle script') with no returned table, so the runtime had
-- nothing to attach and the script did nothing beyond one log line.
--
-- This is deliberately minimal - grid snapping, a board model and win
-- detection are not implemented. It is a working starting point rather than a
-- finished puzzle.
local SlidingPuzzle = {}

function SlidingPuzzle:on_start()
    self.tile_size = 1.0
end

function SlidingPuzzle:on_update(delta_time)
    -- Discrete, one tile per press, so delta_time is deliberately unused.
    local _ = delta_time

    local dx = 0.0
    local dz = 0.0
    if self.input:isKeyPressed("Left")  then dx = -self.tile_size end
    if self.input:isKeyPressed("Right") then dx =  self.tile_size end
    if self.input:isKeyPressed("Up")    then dz =  self.tile_size end
    if self.input:isKeyPressed("Down")  then dz = -self.tile_size end
    if dx == 0.0 and dz == 0.0 then
        return
    end

    local position = self.entity:getPosition()
    position.x = position.x + dx
    position.z = position.z + dz
    self.entity:setPosition(position)
end

return SlidingPuzzle
