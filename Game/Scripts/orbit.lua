-- Circular orbit around the player
local X = {}
function X:on_start()
   -- Capture player position as orbit center
   self.center = player:getPosition()
   self.radius = 5
   self.angle = 0
end

function X:on_update(dt)
   -- Increase angle to create circular motion (1 radian per second)
   self.angle = self.angle + dt * 1
   local cx, cy, cz = self.center.x, self.center.y, self.center.z
   local nx = cx + self.radius * math.cos(self.angle)
   local nz = cz + self.radius * math.sin(self.angle)
   self.entity:setPosition({x=nx, y=cy, z=nz})
end

return X