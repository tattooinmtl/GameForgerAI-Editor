local Player = {}
function Player:on_start()
    self.camera:setMode("fps")
end;
function Player:on_update(dt)
    -- movement handled by engine input
end;
return Player