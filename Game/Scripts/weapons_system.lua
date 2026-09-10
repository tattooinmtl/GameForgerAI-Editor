-- Weapons System - one pack: viewmodel, weapon switching, firing, melee.
--
-- HOW THE VIEWMODEL WORKS (read this before wiring a scene up):
--   The engine drives the scene's Main Camera entity from the live Play view
--   every frame, so ANYTHING parented to that camera rides the player's eyes.
--   A gun in front of the player is simply a child of the Main Camera. There
--   is no separate viewmodel system to configure.
--
-- SCENE SETUP:
--   1. Player capsule: FPS Controller + this script.
--   2. A Camera with "Main Camera" ticked.
--   3. Under that camera, one child per weapon - imported models or
--      primitives. Position them in the Inspector until they look right in
--      Play; what you see is what the player sees.
--   4. Name each weapon child below in `slots`.
--
-- Every tunable is a plain self.* field set in on_start, so it is editable
-- straight from the script editor - same convention as inventory_system.lua.

local WeaponsSystem = {}

function WeaponsSystem:on_start()
	-- ORDERED weapon list. Each entry names a child of the Main Camera.
	--   entity     - the scene entity to show while this weapon is selected
	--   kind       - "ranged" fires projectiles, "melee" swings
	--   cooldown   - seconds between attacks
	--   damage_tag - what this weapon's projectiles look for on hit
	--   muzzle_tag - optional: an entity tagged this fires from, instead of
	--                the camera. Tag a small Empty at the barrel tip.
	self.slots = {
		{ entity = "Gun",    kind = "ranged", cooldown = 0.15, damage_tag = "Enemy",
		  muzzle_tag = "gun_muzzle", speed = 60.0 },
		{ entity = "Sword",  kind = "melee",  cooldown = 0.55, damage_tag = "Enemy", reach = 2.4 },
		{ entity = "Axe",    kind = "melee",  cooldown = 0.75, damage_tag = "Enemy", reach = 2.2 },
		{ entity = "Hammer", kind = "melee",  cooldown = 0.90, damage_tag = "Enemy", reach = 2.0 },
	}

	self.hand_entity = "Hand"   -- always-visible arm; "" to disable
	self.selected = 1
	self.fire_button = "left"
	self.cooldown_remaining = 0.0

	-- Melee swing is a rotation nudge on the weapon entity itself, not an
	-- animation clip - these models have no rigs, and a short kick reads as
	-- a swing well enough to feel responsive.
	self.swing_time = 0.0
	self.swing_duration = 0.22
	self.swing_degrees = -70.0

	-- Cached so we only push a visibility change when it actually changes.
	-- Writing every frame would be harmless but noisy in the scene state.
	self.applied_selection = -1

	if self.hand_entity ~= "" then
		self.world:setEntityActive(self.hand_entity, true)
	end
end

-- Shows only the selected weapon. Called on start and on every switch, never
-- per-frame, so a weapon the player has manually toggled stays as they left it
-- until the next switch.
function WeaponsSystem:apply_selection()
	for index, slot in ipairs(self.slots) do
		self.world:setEntityActive(slot.entity, index == self.selected)
	end
	self.applied_selection = self.selected
end

function WeaponsSystem:current()
	return self.slots[self.selected]
end

function WeaponsSystem:attack()
	local slot = self:current()
	if slot == nil then return end

	if slot.kind == "ranged" then
		-- Fire from the muzzle marker if one exists, otherwise from the
		-- player's own eye line. Falling back rather than refusing to fire
		-- means a half-configured weapon still works.
		local from = nil
		if slot.muzzle_tag ~= nil and slot.muzzle_tag ~= "" then
			from = self.world:findPositionByTag(slot.muzzle_tag)
		end
		local origin = self.entity:getPosition()
		local forward = self.entity:getForward()
		local eye = { x = origin.x, y = origin.y + 0.75, z = origin.z }
		if from == nil then from = eye end
		local to = {
			x = eye.x + forward.x * 100.0,
			y = eye.y + forward.y * 100.0,
			z = eye.z + forward.z * 100.0,
		}
		self.world:fireProjectile(from, to, slot.speed or 50.0, slot.damage_tag or "Enemy")
	else
		-- Melee: start the swing, and damage whatever is in reach in front.
		self.swing_time = self.swing_duration
		local target, distance = self.world:findNearestWithTag(slot.damage_tag or "Enemy")
		if target ~= nil and distance ~= nil and distance <= (slot.reach or 2.0) then
			-- A very short, very fast projectile is the existing engine hit
			-- mechanism - reusing it keeps melee and ranged on one code path
			-- rather than inventing a second kind of hit test.
			local origin = self.entity:getPosition()
			self.world:fireProjectile(origin, target, 200.0, slot.damage_tag or "Enemy")
		end
	end
	self.cooldown_remaining = slot.cooldown or 0.3
end

function WeaponsSystem:on_update(dt)
	if self.applied_selection ~= self.selected then
		self:apply_selection()
	end

	-- Mouse wheel cycles weapons, wrapping both ways.
	local scroll = self.input:getScrollDelta()
	if scroll > 0.01 then
		self.selected = self.selected - 1
		if self.selected < 1 then self.selected = #self.slots end
	elseif scroll < -0.01 then
		self.selected = self.selected + 1
		if self.selected > #self.slots then self.selected = 1 end
	end

	-- Number keys jump straight to a slot, the way every FPS does.
	for index = 1, #self.slots do
		if index <= 9 and self.input:isKeyPressed(tostring(index)) then
			self.selected = index
		end
	end

	if self.cooldown_remaining > 0.0 then
		self.cooldown_remaining = self.cooldown_remaining - dt
	end
	if self.input:isMouseButtonDown(self.fire_button) and self.cooldown_remaining <= 0.0 then
		self:attack()
	end

	-- Drive the swing kick. Rotates the weapon entity's own local X, which
	-- is relative to the camera because the weapon is a child of it - so the
	-- swing reads correctly no matter which way the player is facing.
	local slot = self:current()
	if slot ~= nil and self.swing_time > 0.0 then
		self.swing_time = self.swing_time - dt
		local progress = 1.0 - (self.swing_time / self.swing_duration)
		if progress < 0.0 then progress = 0.0 end
		if progress > 1.0 then progress = 1.0 end
		-- Out and back within one swing.
		local arc = math.sin(progress * math.pi)
		self.world:setEntityRotation(slot.entity, { x = self.swing_degrees * arc, y = 0.0, z = 0.0 })
		if self.swing_time <= 0.0 then
			self.world:setEntityRotation(slot.entity, { x = 0.0, y = 0.0, z = 0.0 })
		end
	end
end

return WeaponsSystem
