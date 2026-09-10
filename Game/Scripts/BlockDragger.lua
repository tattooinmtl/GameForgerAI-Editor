-- Hold G and move the mouse to slide this entity around the XZ plane.
--
-- Rewritten 2026-09-06. The previous version returned a table with an `init`
-- method and registered mouse callbacks (on_mouse_press / on_mouse_release /
-- on_mouse_move) on the entity, and read entity.position as a field. None
-- of that exists: the engine calls on_start/on_update/on_end, and position is
-- reached through self.entity:getPosition()/setPosition(). It could not run.
--
-- Mouse BUTTONS are not bridged to Lua (self.input exposes getAxis,
-- isKeyDown, isKeyPressed, getMouseDeltaX and getMouseDeltaY only), so the
-- drag is gated on holding a key rather than a mouse button - the same
-- grab-and-move idiom Blender uses for G.
local BlockDragger = {}

function BlockDragger:on_start()
    -- World units per pixel of mouse movement.
    self.drag_sensitivity = 0.01
    self.dragging = false
end

function BlockDragger:on_update(delta_time)
    -- delta_time is unused: mouse delta is already per-frame movement, so
    -- scaling it by frame time would make dragging slower on faster machines.
    local _ = delta_time

    self.dragging = self.input:isKeyDown("G")
    if not self.dragging then
        return
    end

    local dx = self.input:getMouseDeltaX()
    local dy = self.input:getMouseDeltaY()
    if dx == 0.0 and dy == 0.0 then
        return
    end

    local position = self.entity:getPosition()
    position.x = position.x + dx * self.drag_sensitivity
    -- Screen-down should move the block away from the camera, hence +Z.
    position.z = position.z + dy * self.drag_sensitivity
    self.entity:setPosition(position)
end

return BlockDragger
