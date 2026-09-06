-- Orbits this entity in a circle around the nearest entity tagged "Player".
--
-- Rewritten 2026-09-06. The previous version called player:getPosition() on a
-- bare global `player` that the runtime never defines, so on_start threw and
-- the script never ran. Scripts reach other entities through
-- self.world:findNearestWithTag(tag), which returns position, distance, name -
-- or nil when nothing carries the tag.
--
-- Tag the entity you want to orbit with "Player" in the Inspector. With no
-- such entity, this orbits the position it started at instead of failing.
local Orbit = {}

function Orbit:on_start()
    self.radius = 5.0
    self.angle = 0.0
    self.radians_per_second = 1.0
    self.target_tag = "Player"

    -- Resolved once here and refreshed each frame in on_update, so the orbit
    -- follows a target that moves.
    self.center = self.world:findNearestWithTag(self.target_tag)
    if self.center == nil then
        self.center = self.entity:getPosition()
    end
end

function Orbit:on_update(delta_time)
    self.angle = self.angle + delta_time * self.radians_per_second

    -- Re-acquire so the orbit tracks a moving player. Keep the last known
    -- centre if the target disappears mid-session rather than snapping home.
    local found = self.world:findNearestWithTag(self.target_tag)
    if found ~= nil then
        self.center = found
    end

    self.entity:setPosition({
        x = self.center.x + self.radius * math.cos(self.angle),
        y = self.center.y,
        z = self.center.z + self.radius * math.sin(self.angle),
    })
end

return Orbit
