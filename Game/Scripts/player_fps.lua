local Player = {}
function Player:on_start()
    self.camera:setMode("fps")
    -- Cursor lock moved off the per-entity Inspector checkbox onto
    -- whatever actually drives the player. Without this registration
    -- wantsCursorLock stays false and the mouse is never captured.
    self.gameManager:setCursorLock(true)
    self.managers:register("player_fps")
end;
function Player:on_update(dt)
    -- movement handled by engine input
end;

-- Releasing the registration tells the engine nothing is driving the
-- player any more, so the cursor is not left captured.
function Player:on_end()
    self.managers:unregister("player_fps")
    self.gameManager:setCursorLock(false)
end

return Player