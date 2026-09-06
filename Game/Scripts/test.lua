-- Minimal smoke-test script: moves the entity in a slow circle.
--
-- Rewritten 2026-09-06. The previous version was a LOVE (love2d) 2D sketch -
-- love.load / love.update / love.draw, love.keyboard.isDown,
-- love.graphics.rectangle - for a different engine entirely. None of those
-- exist here, and it returned no table, so attaching it did nothing.
--
-- Kept as the smallest script that proves the runtime works end to end:
-- lifecycle hooks fire, self.entity reads and writes, delta_time advances.
local Test = {}

function Test:on_start()
    self.elapsed = 0.0
    self.radius = 2.0
    self.speed = 1.0
    self.origin = self.entity:getPosition()
end

function Test:on_update(delta_time)
    self.elapsed = self.elapsed + delta_time * self.speed

    local position = self.entity:getPosition()
    position.x = self.origin.x + math.cos(self.elapsed) * self.radius
    position.z = self.origin.z + math.sin(self.elapsed) * self.radius
    self.entity:setPosition(position)
end

return Test
