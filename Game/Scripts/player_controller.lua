local PlayerController = {}

function PlayerController:on_start()
    self.walk_speed = 2.2
    self.run_speed = 5.5
    -- Cursor lock moved off the per-entity Inspector checkbox onto
    -- whatever actually drives the player. Without this registration
    -- wantsCursorLock stays false and the mouse is never captured.
    self.gameManager:setCursorLock(true)
    self.managers:register("player_controller")
end

function PlayerController:on_update(delta_time)
    -- Engine input, character movement, and animation APIs will be exposed here.
end


-- Releasing the registration tells the engine nothing is driving the
-- player any more, so the cursor is not left captured.
function PlayerController:on_end()
    self.managers:unregister("player_controller")
    self.gameManager:setCursorLock(false)
end

return PlayerController
