local PlayerController = {}

function PlayerController:on_start()
    self.walk_speed = 2.2
    self.run_speed = 5.5
end

function PlayerController:on_update(delta_time)
    -- Engine input, character movement, and animation APIs will be exposed here.
end

return PlayerController
