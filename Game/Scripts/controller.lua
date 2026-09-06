-- Stamina FPS controller: WASD to move, Space to jump, Left Shift to sprint.
--
-- Sprinting drains a stamina pool. Once you stop sprinting there is a delay
-- before it starts refilling, so repeatedly tapping Shift gains you nothing -
-- that delay is the whole point of the mechanic.
--
-- Rewritten 2026-09-06. The previous version was written against an API that
-- does not exist in this engine (globals Input/Entity/Vector/Raycast/UI/
-- DeltaTime, an Update() entry point, and no returned table). It could never
-- have run - it would have failed on the first line of its own body. The
-- behaviour it described is preserved here on the real API.
--
-- Differs from fps_controller.lua, which meters sprint as a 0..1 power bar
-- with a cooldown: this one is a classic drain-and-regen stamina pool.
local Controller = {}

function Controller:on_start()
    self.walk_speed = 5.0
    self.sprint_speed = 10.0
    self.jump_speed = 7.0
    self.gravity = -20.0

    self.max_stamina = 100.0
    self.stamina = self.max_stamina
    self.sprint_drain_per_sec = 20.0
    self.regen_delay_seconds = 2.0
    self.regen_per_sec = 30.0
    self.regen_timer = 0.0
    self.is_sprinting = false

    self.velocity_y = 0.0
    self.grounded = true

    -- Collision box for self.physics:resolve() - half-width in X/Z, full
    -- height in Y, matching fps_controller.lua's capsule.
    self.collider_radius = 0.35
    self.collider_height = 1.8

    self.camera:setMode("fps")

    -- Cursor lock belongs to whatever is actually driving the player, not to
    -- a per-entity checkbox. wantsCursorLock needs a registered manager, so
    -- without this the mouse is never captured.
    self.gameManager:setCursorLock(true)
    self.managers:register("controller")
end

function Controller:on_end()
    self.managers:unregister("controller")
    self.gameManager:setCursorLock(false)
end

-- 0..1, ready to drive a HUD stamina bar once one exists. The old version
-- pushed this to a HUD bar every frame through an API that does not exist,
-- so the value is exposed here for the host to read instead.
function Controller:get_stamina_percent()
    return self.stamina / self.max_stamina
end

function Controller:on_update(delta_time)
    -- Same WASD contract as fps_controller.lua. getRight() is screen-right;
    -- do not swap A/D - see the DO NOT CHANGE note on luaEntityGetRight.
    local forward_input = self.input:getAxis("W", "S")
    local strafe_input = self.input:getAxis("D", "A")

    local forward = self.entity:getForward()
    local right = self.entity:getRight()

    local move_x = forward.x * forward_input + right.x * strafe_input
    local move_z = forward.z * forward_input + right.z * strafe_input

    -- Normalise so diagonals are not faster than cardinals.
    local length = math.sqrt(move_x * move_x + move_z * move_z)
    if length > 0.0001 then
        move_x = move_x / length
        move_z = move_z / length
    end

    -- Sprint only while held, moving, and with stamina left.
    local wants_sprint = self.input:isKeyDown("LeftShift")
    local moving = length > 0.0001
    self.is_sprinting = wants_sprint and moving and self.stamina > 0.0

    local speed = self.walk_speed
    if self.is_sprinting then
        speed = self.sprint_speed
        self.stamina = self.stamina - self.sprint_drain_per_sec * delta_time
        -- Any sprinting restarts the wait before regen begins.
        self.regen_timer = 0.0
    else
        if self.regen_timer < self.regen_delay_seconds then
            self.regen_timer = self.regen_timer + delta_time
        else
            self.stamina = self.stamina + self.regen_per_sec * delta_time
        end
    end

    if self.stamina > self.max_stamina then self.stamina = self.max_stamina end
    if self.stamina < 0.0 then self.stamina = 0.0 end

    if self.grounded and self.input:isKeyPressed("Space") then
        self.velocity_y = self.jump_speed
        self.grounded = false
    end
    self.velocity_y = self.velocity_y + self.gravity * delta_time

    local position = self.entity:getPosition()
    position.x = position.x + move_x * speed * delta_time
    position.z = position.z + move_z * speed * delta_time
    position.y = position.y + self.velocity_y * delta_time

    -- Replaces the old downward Raycast() ground check, which did not exist.
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

return Controller
