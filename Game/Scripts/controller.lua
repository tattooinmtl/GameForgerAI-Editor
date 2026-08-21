-- FPS controller for Capsule
local entity = "Capsule"

-- movement constants
local walkSpeed = 5
local sprintSpeed = 10
local jumpForce = 7
local gravity = 20

-- stamina (sprint) settings
local maxStamina = 100
local stamina = maxStamina
local sprintDrainRate = 20   -- per second while sprinting
local regenDelay = 2           -- seconds to wait before regen starts
local regenRate = 30           -- per second after delay
local regenTimer = 0
local isSprinting = false

-- input keys
local KEY_W = "W"
local KEY_A = "A"
local KEY_S = "S"
local KEY_D = "D"
local KEY_SPACE = "Space"
local KEY_LSHIFT = "LeftShift"

function Update()
    -- read movement input
    local moveX, moveZ = 0, 0
    if Input.IsKeyDown(KEY_W) then moveZ = moveZ + 1 end
    if Input.IsKeyDown(KEY_S) then moveZ = moveZ - 1 end
    if Input.IsKeyDown(KEY_D) then moveX = moveX + 1 end
    if Input.IsKeyDown(KEY_A) then moveX = moveX - 1 end

    -- normalize movement vector
    if moveX ~= 0 or moveZ ~= 0 then
        local len = math.sqrt(moveX*moveX + moveZ*moveZ)
        moveX, moveZ = moveX/len, moveZ/len
    end

    -- determine speed and handle stamina
    local speed = walkSpeed
    isSprinting = Input.IsKeyDown(KEY_LSHIFT)
    if isSprinting and stamina > 0 then
        speed = sprintSpeed
        stamina = stamina - sprintDrainRate * DeltaTime()
    else
        -- regen timer logic (non‑obvious: start regen only after delay)
        if regenTimer < regenDelay then
            regenTimer = regenTimer + DeltaTime()
        elseif regenTimer >= regenDelay then
            stamina = stamina + regenRate * DeltaTime()
            if stamina > maxStamina then stamina = maxStamina end
        end
    end
    if stamina < 0 then stamina = 0 end

    -- apply horizontal movement
    local forward = Vector(moveX, 0, moveZ)
    local moveVec = forward * speed
    local vel = Entity.GetVelocity(entity)
    vel.x, vel.z = moveVec.x, moveVec.z
    Entity.SetVelocity(entity, vel)

    -- jump handling
    if Input.IsKeyDown(KEY_SPACE) and IsGrounded(entity) then
        local v = Entity.GetVelocity(entity)
        v.y = jumpForce
        Entity.SetVelocity(entity, v)
    end

    -- apply gravity
    local gVel = Entity.GetVelocity(entity)
    gVel.y = gVel.y - gravity * DeltaTime()
    Entity.SetVelocity(entity, gVel)

    -- update sprint bar (UI bar must be refreshed each frame)
    UI.SetBar("SprintBar", stamina / maxStamina)
end

-- simple ground check using a short raycast downward
function IsGrounded(e)
    local pos = Entity.GetPosition(e)
    local rayPos = Vector(pos.x, 0.1, pos.z)
    local hit = Raycast(rayPos, Vector(0, -1, 0), 0.2)
    return hit
end