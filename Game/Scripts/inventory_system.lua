-- Enables pickup + inventory for whichever entity this is attached to -
-- normally the player, alongside FPS Controller or Third-Person
-- Controller. While attached and this entity's camera is active during
-- Play: E picks up the nearest "Is Pickup Item" object (Inspector) within
-- pickup_range/pickup_height_tolerance of THIS entity's own position, I
-- opens/closes the inventory grid (drag to reorder, drag out of the
-- window to drop an item back into the world).
--
-- Deliberately proximity-based ("walk up and press E"), not aim-based -
-- an earlier version required the camera's own eye/forward ray to pass
-- close to the item, which failed for small/floor-level objects (e.g. a
-- potion) sitting below a standing FPS camera's roughly-level eye line.
--
-- The detection, inventory window, and world-drop logic all run in the
-- engine, not here - there are no ImGui bindings exposed to Lua scripts,
-- so the grid/drag-drop itself can't be authored as a script. This file
-- exists so the feature can be added to or removed from an entity the
-- same way as any other script (Presets dropdown, Inspector Scripts
-- list), and so the two numbers below are actually tunable - the engine
-- reads them fresh off this instance every frame, no rebuild needed.
local InventorySystem = {}

function InventorySystem:on_start()
    self.pickup_range = 4.0            -- max horizontal distance (world units, X/Z) an item can be picked up from
    self.pickup_height_tolerance = 2.5 -- max vertical distance (world units) - generous, so floor-level items work
end

function InventorySystem:on_update(delta_time)
    -- Intentionally empty - see comment above.
end

return InventorySystem
