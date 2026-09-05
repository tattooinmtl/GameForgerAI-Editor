-- First-person capsule controller: WASD to move, Space to jump, Left Shift to
-- sprint, C to swap to a third-person camera and back. Sprint drains a power
-- meter while held; once it empties there is a COOLDOWN_SECONDS pause before
-- it starts regenerating again at REGEN_PER_SEC.
--
-- Matches this project's script convention (see player_controller.lua):
-- on_start() runs once when Play starts, on_update(delta_time) runs every
-- frame. self.entity, self.input, self.camera, and self.physics are attached
-- by the engine before on_start() runs.
--
-- Ground/wall collision: self.physics:resolve() treats this capsule as a
-- simple box (collider_radius/collider_height below) and pushes it out of
-- any entity that has its Inspector "Collider" checkbox on. This is basic
-- AABB collision, not full rigidbody physics - no rotation-aware shapes, no
-- physics materials, no other moving colliders pushing each other around.

local FpsController = {}

function FpsController:on_start()
    -- Movement.
    self.walk_speed = 4.5          -- units/second
    self.sprint_multiplier = 1.8   -- speed multiplier while sprinting
    self.jump_speed = 6.0          -- initial upward velocity on jump
    self.gravity = -18.0           -- units/second^2
    self.velocity_y = 0.0
    self.grounded = true
    -- Collision box used by self.physics:resolve() (half-width in X/Z, full
    -- height in Y) - roughly Unity's default capsule collider dimensions.
    self.collider_radius = 0.4
    self.collider_height = 2.0

    -- Sprint power meter (e.g. to drive a UI sprint bar's fill amount).
    self.sprint_max = 100.0
    self.sprint = self.sprint_max
    self.sprint_drain_per_sec = 25.0
    self.sprint_regen_per_sec = 15.0
    self.sprint_cooldown_seconds = 2.0
    self.sprint_cooldown_remaining = 0.0

    -- Camera: first-person by default; press C during Play to swap to a
    -- third-person view (and back).
    self.camera_mode = "fps"
    self.camera:setMode(self.camera_mode)
end

-- 0..1, ready to feed a HUD sprint bar once one exists.
function FpsController:get_sprint_percent()
    return self.sprint / self.sprint_max
end

function FpsController:update_sprint(delta_time, wants_sprint, is_moving)
    local sprinting = wants_sprint and is_moving and self.sprint > 0.0

    if sprinting then
        self.sprint = math.max(0.0, self.sprint - self.sprint_drain_per_sec * delta_time)
        self.sprint_cooldown_remaining = self.sprint_cooldown_seconds
    elseif self.sprint_cooldown_remaining > 0.0 then
        self.sprint_cooldown_remaining = math.max(0.0, self.sprint_cooldown_remaining - delta_time)
    else
        self.sprint = math.min(self.sprint_max, self.sprint + self.sprint_regen_per_sec * delta_time)
    end

    return sprinting
end

function FpsController:on_update(delta_time)
    if self.input:isKeyPressed("C") then
        self.camera_mode = (self.camera_mode == "fps") and "third_person" or "fps"
        self.camera:setMode(self.camera_mode)
    end

    -- While operating a catapult (E, main.cpp) the mouse is driving its aim
    -- instead of the camera, so WASD is frozen here too - stepping off
    -- mid-aim would be disorienting and isn't how the real interaction is
    -- meant to work.
    local operating_catapult = self.world:isAimingCatapult()
    -- DO NOT CHANGE this WASD mapping or the getRight usage below.
    -- Verified in Play: W/S = getAxis("W","S"), A/D = getAxis("D","A"),
    -- move += forward * forward_axis + right * strafe_axis, with
    -- entity:getRight() = cross(forward, +Y) (FPS camera screen-right).
    -- Swapping A/D, negating strafe, or "fixing" getRight to cross(+Y, forward)
    -- inverts left/right in the Game view.
    local forward_axis = operating_catapult and 0.0 or self.input:getAxis("W", "S")
    local strafe_axis = operating_catapult and 0.0 or self.input:getAxis("D", "A")
    local is_moving = forward_axis ~= 0.0 or strafe_axis ~= 0.0
    local wants_sprint = self.input:isKeyDown("LeftShift")

    -- Carrying a held weapon (F, main.cpp) caps movement to walk speed -
    -- can't sprint while holding something.
    local holding_item = self.world:isHoldingItem()
    local sprinting = self:update_sprint(delta_time, wants_sprint and not holding_item, is_moving)
    local speed = self.walk_speed * (sprinting and self.sprint_multiplier or 1.0)

    local forward = self.entity:getForward()
    local right = self.entity:getRight() -- do not negate; see WASD note above
    local move_x = forward.x * forward_axis + right.x * strafe_axis
    local move_z = forward.z * forward_axis + right.z * strafe_axis
    if is_moving then
        local length = math.sqrt(move_x * move_x + move_z * move_z)
        if length > 0.0001 then
            move_x = move_x / length
            move_z = move_z / length
        end
    end

    -- Gravity + jump.
    if self.grounded and self.input:isKeyPressed("Space") then
        self.velocity_y = self.jump_speed
        self.grounded = false
    end
    self.velocity_y = self.velocity_y + self.gravity * delta_time

    local position = self.entity:getPosition()
    position.x = position.x + move_x * speed * delta_time
    position.z = position.z + move_z * speed * delta_time
    position.y = position.y + self.velocity_y * delta_time

    local resolved, grounded = self.physics:resolve(position, self.collider_radius, self.collider_height)
    position = resolved
    self.grounded = grounded
    if grounded then
        self.velocity_y = 0.0
    end

    -- Fallback world floor for scenes with no Collider-enabled objects.
    if position.y <= 0.0 then
        position.y = 0.0
        self.velocity_y = 0.0
        self.grounded = true
    end

    self.entity:setPosition(position)
end

return FpsController
