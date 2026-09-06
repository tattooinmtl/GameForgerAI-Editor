-- Moves this entity forward at a constant speed, then stops after 5 seconds.
--
-- Rewritten 2026-09-06. The previous version could not run:
--   * self.direction * self.speed * dt multiplied a TABLE by a number, which
--     is an immediate Lua error - vectors here are plain {x,y,z} tables with
--     no arithmetic metamethods.
--   * It indexed positions as arrays (pos[1], move[1]). getPosition returns
--     {x=,y=,z=}, so those were all nil.
--   * setPosition was called with three numbers; it takes one vec3 table.
--   * self.entity:destroy() - no such method. Scripts cannot delete their own
--     entity; removal is the host's job through the command bus.
--
-- Rather than invent a destroy API, the entity simply stops moving once its
-- lifetime expires, which is the visible part of what was intended.
local Projectile = {}

function Projectile:on_start()
    self.speed = 30.0
    self.max_lifetime_seconds = 5.0
    self.lifetime = 0.0
    self.expired = false
    -- Captured once: the direction it was facing when it started, so later
    -- rotation of the entity does not curve the flight path.
    self.direction = self.entity:getForward()
end

function Projectile:on_update(delta_time)
    if self.expired then
        return
    end

    self.lifetime = self.lifetime + delta_time
    if self.lifetime > self.max_lifetime_seconds then
        self.expired = true
        return
    end

    local position = self.entity:getPosition()
    position.x = position.x + self.direction.x * self.speed * delta_time
    position.y = position.y + self.direction.y * self.speed * delta_time
    position.z = position.z + self.direction.z * self.speed * delta_time
    self.entity:setPosition(position)
end

return Projectile
