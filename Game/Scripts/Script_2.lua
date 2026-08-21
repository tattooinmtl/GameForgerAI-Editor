local Projectile = {}
function Projectile:on_start()
    -- initialize speed and store forward direction of the sphere
    self.speed = 30
    self.direction = self.entity:getForward()
    self.lifetime = 0
end

function Projectile:on_update(dt)
    -- move the sphere forward each frame
    local pos = self.entity:getPosition()
    local move = self.direction * self.speed * dt
    self.entity:setPosition(pos[1] + move[1], pos[2] + move[2], pos[3] + move[3])
    -- track lifetime and destroy after a few seconds
    self.lifetime = self.lifetime + dt
    if self.lifetime > 5 then
        self.entity:destroy()
    end
end

return Projectile