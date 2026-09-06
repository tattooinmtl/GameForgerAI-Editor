-- Third-person capsule controller: WASD to move, Space to jump, Left Shift to
-- sprint, C to swap to a first-person camera and back. Shares the FPS
-- controller's movement/sprint/gravity logic; the differences are the default
-- camera mode and that the capsule turns to face the direction it's moving,
-- which reads better from a trailing camera.
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

local ThirdPersonController = {}

function ThirdPersonController:on_start()
    -- Movement.
    self.walk_speed = 4.5          -- units/second
    self.sprint_multiplier = 1.8   -- speed multiplier while sprinting
    self.jump_speed = 6.0          -- initial upward velocity on jump
    self.gravity = -18.0           -- units/second^2
    self.velocity_y = 0.0
    self.grounded = true
    self.turn_speed_degrees = 720.0 -- how fast the capsule turns to face travel direction

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

    -- Camera: third-person by default; press C during Play to swap to a
    -- first-person view (and back).
    self.camera_mode = "third_person"
    self.camera:setMode(self.camera_mode)
    -- Claim the cursor while this controller is driving the player. This used
    -- to be a per-entity "Lock Cursor" checkbox in the Inspector; it belongs
    -- to whatever is actually controlling the camera.
    self.gameManager:setCursorLock(true)
    self.managers:register("third_person_controller")
end

-- Runs when Play stops or this script is detached. Releasing the registration
-- is what lets the engine know nothing is driving the player any more, so the
-- cursor is not left captured.
function ThirdPersonController:on_end()
    self.managers:unregister("third_person_controller")
    self.gameManager:setCursorLock(false)
end

-- 0..1, ready to feed a HUD sprint bar once one exists.
function ThirdPersonController:get_sprint_percent()
    return self.sprint / self.sprint_max
end

function ThirdPersonController:update_sprint(delta_time, wants_sprint, is_moving)
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

function ThirdPersonController:on_update(delta_time)
    if self.input:isKeyPressed("C") then
        self.camera_mode = (self.camera_mode == "third_person") and "fps" or "third_person"
        self.camera:setMode(self.camera_mode)
    end

    -- While operating a catapult (E, main.cpp) the mouse is driving its aim
    -- instead of the camera, so WASD is frozen here too - stepping off
    -- mid-aim would be disorienting and isn't how the real interaction is
    -- meant to work.
    local operating_catapult = self.world:isAimingCatapult()
    -- Same WASD / getRight contract as fps_controller.lua — do not swap A/D.
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
    local right = self.entity:getRight()
    local move_x = forward.x * forward_axis + right.x * strafe_axis
    local move_z = forward.z * forward_axis + right.z * strafe_axis
    if is_moving then
        local length = math.sqrt(move_x * move_x + move_z * move_z)
        if length > 0.0001 then
            move_x = move_x / length
            move_z = move_z / length
        end

        -- Turn the capsule to face the direction it's moving, capped by
        -- turn_speed_degrees so it reads as a turn rather than a snap.
        local target_yaw = math.deg(math.atan(move_x, move_z))
        local rotation = self.entity:getRotation()
        local yaw_delta = (target_yaw - rotation.y + 180) % 360 - 180
        local max_step = self.turn_speed_degrees * delta_time
        if yaw_delta > max_step then yaw_delta = max_step end
        if yaw_delta < -max_step then yaw_delta = -max_step end
        self.entity:setRotation({x = rotation.x, y = rotation.y + yaw_delta, z = rotation.z})
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

return ThirdPersonController
