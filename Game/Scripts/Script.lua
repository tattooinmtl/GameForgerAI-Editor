-- create projectile on left mouse click, apply gravity, add crosshair at screen center
local Capsule = {}
function Capsule:on_start()
    self.projectiles = {}
    self.projectile_speed = 25
    self.gravity = 9.8
    self.crosshair = true
    self.camera:setCrosshair(true)  -- enable center crosshair
end

function Capsule:on_update(dt)
    -- shoot projectile
    if self.input:isKeyPressed("LeftMouse") then
        self:launchProjectile()
    end

    -- update projectiles with gravity
    for i = #self.projectiles, 1, -1 do
        local p = self.projectiles[i]
        p.velocity.y = p.velocity.y - self.gravity * dt
        p.position.y = p.position.y + p.velocity.y * dt
        if p.position.y < 0 then
            table.remove(self.projectiles, i)  -- remove when hits ground
        end
    end
end

function Capsule:launchProjectile()
    local pos = self.entity:getPosition()
    local forward = self.entity:getForward()
    local spawn = {x = pos.x + forward.x * 0.5, y = pos.y + 0.5, z = pos.z + forward.z * 0.5}
    local proj = {
        position = spawn,
        velocity = {
            x = forward.x * self.projectile_speed,
            y = 0,
            z = forward.z * self.projectile_speed
        }
    }
    table.insert(self.projectiles, proj)
    -- optionally create visual sphere entity here
end

return Capsule