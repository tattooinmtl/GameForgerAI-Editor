-- Fires a gravity-arced projectile forward on E.
--
-- Rewritten 2026-09-06. The previous version had three faults that stopped it
-- working at all:
--   * self.camera:setCrosshair(true) - no such method. This threw on the very
--     first frame of on_start, so nothing below it ever ran.
--   * self.input:isKeyPressed("LeftMouse") - mouse buttons are not bridged to
--     Lua. self.input exposes getAxis, isKeyDown, isKeyPressed,
--     getMouseDeltaX and getMouseDeltaY only, over KEY names.
--   * It simulated projectiles as plain Lua tables that were never drawn, with
--     a comment conceding "optionally create visual sphere entity here".
--
-- The engine already owns projectiles: self.world:fireGravityProjectile
-- spawns a real one that moves, collides and despawns without the script
-- tracking it. That replaces the whole hand-rolled table simulation.
local Capsule = {}

function Capsule:on_start()
    self.projectile_speed = 25.0
    -- Which tag counts as a hit. Entities carrying it are what the engine
    -- tests the projectile against.
    self.hit_tag = "Enemy"
end

function Capsule:on_update(delta_time)
    -- Discrete, one shot per press, so delta_time is deliberately unused.
    local _ = delta_time

    if not self.input:isKeyPressed("E") then
        return
    end

    local position = self.entity:getPosition()
    local forward = self.entity:getForward()

    -- Spawn just in front of and above the muzzle so the projectile does not
    -- immediately collide with the entity that fired it.
    local origin = {
        x = position.x + forward.x * 0.5,
        y = position.y + 0.5,
        z = position.z + forward.z * 0.5,
    }

    -- Slight upward bias gives the arc its lob; the engine applies gravity.
    local direction = { x = forward.x, y = 0.25, z = forward.z }

    self.world:fireGravityProjectile(origin, direction, self.projectile_speed, self.hit_tag)
end

return Capsule
