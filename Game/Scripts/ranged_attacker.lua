-- Aims at (rotates to continuously face) and fires projectiles at the
-- nearest object tagged target_tag once it's within fire_range - attach
-- to ANY entity (an enemy with target_tag = "Player", the player with
-- target_tag = "Enemy", a future tower/turret with either) as a
-- standalone add-on, same as Collider Only: it never touches this
-- entity's own POSITION, so it combines freely with Enemy AI, a
-- movement controller, or nothing else at all - only rotation is set,
-- and only while a target is in range (see on_update).
--
-- Note if combined with Enemy AI on the same entity: both scripts set
-- rotation every frame (Enemy AI faces its movement direction, this
-- faces the target) - whichever is listed LATER in the entity's Scripts
-- list wins for that frame. Listing this one after Enemy AI (the
-- default order when both are added via Presets at once) means the
-- entity visibly aims at its target even while chasing, which is
-- normally what you want.
--
-- The actual projectile - movement, collision, despawn - is owned by
-- the engine (self.world:fireProjectile, see PlayModeState::Projectile /
-- tickProjectiles in main.cpp), not a script of its own: there's no
-- per-projectile behavior to author here, just "travel in a straight
-- line and check for a hitTag match," so it isn't worth a whole
-- scripted entity per shot. self.detected (0 or 1) drives the same red
-- "detected!" icon enemy_ai.lua uses.

local RangedAttacker = {}

function RangedAttacker:on_start()
    self.target_tag = "Player"
    self.fire_range = 10.0
    self.fire_rate = 1.0          -- shots per second
    self.projectile_speed = 14.0
    -- Optional: the name of a tag (e.g. "player_gun_muzzle" or
    -- "shooting_point") on a SEPARATE small marker entity placed at the
    -- gun's tip - a plain Inspector tag, add it via Tags on any object,
    -- no extra script needed for the marker itself. Leave empty to fire
    -- from this entity's own position instead.
    self.muzzle_tag = ""
    self.detected = 0
    self.cooldown_remaining = 0.0
end

function RangedAttacker:on_update(delta_time)
    if self.cooldown_remaining > 0.0 then
        self.cooldown_remaining = self.cooldown_remaining - delta_time
    end

    local target_position, target_distance = self.world:findNearestWithTag(self.target_tag)
    if target_position == nil or target_distance > self.fire_range then
        self.detected = 0
        return
    end
    self.detected = 1

    -- Continuously aim at the target while it's in range, independent of
    -- the fire cooldown below - a turret should track its target
    -- smoothly every frame, not snap to face it only at the instant it
    -- fires. Yaw only (no pitch), matching how every other script in
    -- this project (fps_controller/third_person_controller/enemy_ai)
    -- represents facing.
    local aim_position = self.entity:getPosition()
    local aim_dx = target_position.x - aim_position.x
    local aim_dz = target_position.z - aim_position.z
    if aim_dx ~= 0.0 or aim_dz ~= 0.0 then
        self.entity:setRotation({x = 0.0, y = math.atan(aim_dx, aim_dz) * 180.0 / math.pi, z = 0.0})
    end

    if self.cooldown_remaining > 0.0 then
        return
    end

    local from = self.entity:getPosition()
    if self.muzzle_tag ~= "" then
        local muzzle_position = self.world:findPositionByTag(self.muzzle_tag)
        if muzzle_position ~= nil then
            from = muzzle_position
        end
    end

    self.world:fireProjectile(from, target_position, self.projectile_speed, self.target_tag)
    self.cooldown_remaining = 1.0 / math.max(self.fire_rate, 0.01)
end

return RangedAttacker
