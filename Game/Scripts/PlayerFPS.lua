-- Minimal FPS camera script. Registers as a manager so the engine knows
-- something is driving the player - wantsCursorLock requires that, so without
-- it the mouse is never captured.
local X = {}

function X:on_start()
    self.camera:setMode("fps")
    self.gameManager:setCursorLock(true)
    self.managers:register("PlayerFPS")
end

function X:on_end()
    self.managers:unregister("PlayerFPS")
    self.gameManager:setCursorLock(false)
end

return X
