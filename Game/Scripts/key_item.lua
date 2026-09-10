-- Key Item - a pickup that unlocks doors once it is in the player's hands.
--
-- Attach to any object that is ALSO marked "Is Pickup Item" in the Inspector,
-- with its Item Name set to the same string a locked door expects. Picking it
-- up registers a manager named "key:<Item Name>", which door_interaction.lua
-- checks in "key" lock mode.
--
-- The engine deletes an entity the moment it is picked up, which means this
-- script stops running at exactly the point it needs to report the pickup. So
-- the registration happens while the key is still in the world, the instant
-- the player is close enough to take it - which lines up with the pickup
-- itself because both use the same proximity rule. The alternative (an engine
-- callback on pickup) is a real improvement worth making later; this works
-- today without new engine support.
--
-- Set pickup_range to match inventory_system.lua's own pickup_range so the two
-- agree about "close enough". They are separate scripts on separate objects,
-- so nothing enforces that for you.

local KeyItem = {}

function KeyItem:on_start()
	self.item_name = "Brass Key"   -- must match the door's key_item_name
	self.player_tag = "Player"
	self.pickup_range = 2.2        -- keep in step with inventory_system.lua
	self.pickup_height_tolerance = 2.5
	self.registered = false
end

function KeyItem:on_update(dt)
	if self.registered then
		return
	end
	local player, distance = self.world:findNearestWithTag(self.player_tag)
	if player == nil or distance == nil then
		return
	end
	-- Horizontal distance only, matching how pickup detection actually works
	-- (see inventory_system.lua) - a key on the floor is below the player's
	-- own origin, and a straight 3D distance would fail the common case.
	local here = self.entity:getPosition()
	if math.abs(player.y - here.y) > self.pickup_height_tolerance then
		return
	end
	local dx = player.x - here.x
	local dz = player.z - here.z
	if math.sqrt(dx * dx + dz * dz) > self.pickup_range then
		return
	end

	self.managers:register("key:" .. self.item_name)
	self.registered = true
	print("Picked up " .. self.item_name .. " - doors needing it are now unlocked.")
end

return KeyItem
