-- Wanders near its own spawn point until an object tagged target_tag
-- (e.g. "Player" - a plain entry in the target's Inspector > Tags list,
-- nothing new) comes within search_radius, then chases it until it gets
-- beyond the larger escape_radius, at which point it gives up and goes
-- back to wandering. self.detected (0 or 1) is read every frame by the
-- engine (drawGameViewPanel, main.cpp) to show the red starburst
-- "detected!" icon above this entity while chasing.
--
-- Uses the same self.physics:resolve() ground/collision system as
-- Rigidbody/FPS Controller (see rigidbody.lua for the center-vs-feet
-- position convention mirrored below), and the new self.world:
-- findNearestWithTag(tag) -> position, distance (ScriptRuntime.cpp) to
-- locate the target - a read-only query; this script only ever moves
-- its own entity.

local EnemyAI = {}

local function distance_xz(ax, az, bx, bz)
    local dx = ax - bx
    local dz = az - bz
    return math.sqrt(dx * dx + dz * dz)
end

function EnemyAI:on_start()
    self.target_tag = "Player"
    self.search_radius = 8.0    -- world units - starts chasing once the target is this close
    self.escape_radius = 14.0   -- world units - keep >= search_radius, or it'd give up the instant it detects
    self.wander_radius = 6.0    -- how far from its spawn point it wanders while idle
    self.wander_speed = 2.0
    self.chase_speed = 3.5
    self.gravity = -18.0

    local position = self.entity:getPosition()
    self.spawn_x = position.x
    self.spawn_z = position.z
    self.state = "wander"
    self.detected = 0
    self.velocity_y = 0.0
    self.wander_target_x = position.x
    self.wander_target_z = position.z
    self.wander_wait_remaining = 0.0

    local scale = self.entity:getScale()
    self.collider_radius = math.max(scale.x, scale.z)
    self.collider_height = scale.y * 2.0
end

function EnemyAI:pick_new_wander_target()
    local angle = math.random() * math.pi * 2.0
    local radius = math.random() * self.wander_radius
    self.wander_target_x = self.spawn_x + math.cos(angle) * radius
    self.wander_target_z = self.spawn_z + math.sin(angle) * radius
    self.wander_wait_remaining = 1.0 + math.random() * 2.0
end

function EnemyAI:on_update(delta_time)
    local position = self.entity:getPosition()
    local target_position, target_distance = self.world:findNearestWithTag(self.target_tag)

    local move_x, move_z, speed = 0.0, 0.0, 0.0

    if self.state == "wander" then
        self.detected = 0
        if target_position ~= nil and target_distance <= self.search_radius then
            self.state = "chase"
        else
            local to_target_dist = distance_xz(position.x, position.z, self.wander_target_x, self.wander_target_z)
            if to_target_dist < 0.3 then
                if self.wander_wait_remaining > 0.0 then
                    self.wander_wait_remaining = self.wander_wait_remaining - delta_time
                else
                    self:pick_new_wander_target()
                end
            else
                move_x = (self.wander_target_x - position.x) / to_target_dist
                move_z = (self.wander_target_z - position.z) / to_target_dist
                speed = self.wander_speed
            end
        end
    end

    if self.state == "chase" then
        if target_position == nil or target_distance > self.escape_radius then
            self.state = "wander"
            self.detected = 0
            self.wander_wait_remaining = 0.0
        else
            self.detected = 1
            local to_target_dist = distance_xz(position.x, position.z, target_position.x, target_position.z)
            if to_target_dist > 0.5 then
                move_x = (target_position.x - position.x) / to_target_dist
                move_z = (target_position.z - position.z) / to_target_dist
            end
            speed = self.chase_speed
        end
    end

    if move_x ~= 0.0 or move_z ~= 0.0 then
        self.entity:setRotation({x = 0.0, y = math.atan(move_x, move_z) * 180.0 / math.pi, z = 0.0})
    end

    self.velocity_y = self.velocity_y + self.gravity * delta_time

    -- self.entity:getPosition()/setPosition() is this object's center,
    -- self.physics:resolve() expects a feet/base position - same
    -- convert-down/resolve/convert-up pattern as rigidbody.lua.
    local half_height = self.collider_height * 0.5
    position.x = position.x + move_x * speed * delta_time
    position.z = position.z + move_z * speed * delta_time
    position.y = position.y + self.velocity_y * delta_time

    local feet = {x = position.x, y = position.y - half_height, z = position.z}
    local resolved, grounded = self.physics:resolve(feet, self.collider_radius, self.collider_height)
    position.x = resolved.x
    position.z = resolved.z
    position.y = resolved.y + half_height

    if grounded then
        self.velocity_y = 0.0
    end
    -- Fallback world floor for scenes with no other Collider-enabled objects.
    if position.y <= half_height then
        position.y = half_height
        self.velocity_y = 0.0
    end

    self.entity:setPosition(position)
end

return EnemyAI
