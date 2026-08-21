-- BlockDragger.lua
-- Simple script to allow dragging of entities
local function on_mouse_press(entity, x, y, button)
    if button == 1 then
        entity.drag = true
    end
end

local function on_mouse_release(entity, x, y, button)
    if button == 1 then
        entity.drag = false
    end
end

local function on_mouse_move(entity, x, y, dx, dy)
    if entity.drag then
        local pos = entity.position
        pos[1] = pos[1] + dx * 0.01
        pos[3] = pos[3] + dy * 0.01
        entity.position = pos
    end
end

return {
    init = function(self)
        self.entity:on_mouse_press(on_mouse_press)
        self.entity:on_mouse_release(on_mouse_release)
        self.entity:on_mouse_move(on_mouse_move)
    end
}