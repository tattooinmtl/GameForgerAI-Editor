-- Makes this object an inventory item. Attach it to ANY object (a primitive,
-- an imported model, a whole parented group) and during Play the player can
-- walk up to it and press E to put it in the inventory (I opens the grid,
-- 1-8 / mouse wheel pick the hotbar slot). Dropping it from the inventory
-- puts THIS same object back in the world - its mesh, material, children
-- and scripts all come back with it.
--
-- Every value below is per object: select the object and edit them in the
-- Inspector's Scripts section. The icon shows a thumbnail there with a
-- "Change Icon..." button that picks a new image from your PC (it's copied
-- into Game/Icons/ and saved with this object). The kit's own icons are in
-- Game/Icons/FPSDemo/.
--
-- To make it a weapon for fps_player.lua, set item_type = weapon and pick
-- which weapon it is (lightning/fire/frost/heal are the Storm, Fire, Frost
-- and Life Casters).
--
-- @property item_name string Item
-- @property icon icon Game/Icons/FPSDemo/Item.png
-- @property item_type enum weapon|consumable|ammo|misc misc
-- @property weapon enum none|sword|axe|hammer|pickaxe|gun|ak47|taser|lightning|fire|frost|heal none
-- @property stackable bool false
-- @property max_stack number 99
-- @property spin bool true
-- @property spin_speed number 45

local Item = {}

function Item:on_start()
    -- The engine sets the @property values above on self before this runs;
    -- the fallbacks only matter if this file is used some other way.
    self.item_name = self.item_name or "Item"
    self.icon = self.icon or ""
    self.item_type = self.item_type or "misc"
    self.weapon = self.weapon or "none"
    if self.stackable == nil then self.stackable = false end
    self.max_stack = self.max_stack or 99
    if self.spin == nil then self.spin = true end
    self.spin_speed = self.spin_speed or 45
end

function Item:on_update(delta_time)
    -- A slow turn while lying in the world so pickups read as pickups.
    -- (Hidden while in the inventory - the engine stops ticking it then.)
    if self.spin then
        local rotation = self.entity:getRotation()
        rotation.y = (rotation.y + self.spin_speed * delta_time) % 360
        self.entity:setRotation(rotation)
    end
end

return Item

-- @preset FPS Demo | item
