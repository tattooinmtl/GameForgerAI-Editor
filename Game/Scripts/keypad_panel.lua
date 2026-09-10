-- Keypad - walk up, press E, type a code, unlock whatever doors use it.
--
-- Attach to a panel object beside a locked door. While the player is in range
-- and has activated it, number keys append digits, Backspace deletes one, and
-- Enter submits. A correct code registers a manager named "keypad:<code>",
-- which is what door_interaction.lua checks - so one keypad can unlock any
-- number of doors sharing that code, and no direct link between the two
-- objects is needed.
--
-- The registration deliberately persists for the rest of the Play session: a
-- door you have unlocked stays unlocked, which is what players expect and
-- avoids re-entering a code every time they walk through.
--
-- Progress is printed to the Console rather than drawn on screen. Rendering
-- the entry as a HUD element would mean this script owning a UI object, and
-- authored UI elements are placed in the scene, not spawned - so the honest
-- version of on-screen feedback is a Text UI element parented to the Main
-- Camera whose content a later engine binding can drive. Flagged rather than
-- faked.

local Keypad = {}

function Keypad:on_start()
	self.correct_code = "1234"
	self.interact_range = 2.4
	self.player_tag = "Player"
	self.max_length = 8

	self.active = false
	self.entry = ""
	self.unlocked = false
	self.feedback = ""
	self.feedback_timer = 0.0
end

function Keypad:submit()
	if self.entry == self.correct_code then
		self.unlocked = true
		-- The name every door with this code is watching for.
		self.managers:register("keypad:" .. self.correct_code)
		self.feedback = "Accepted."
		print("Keypad accepted " .. self.entry .. " - doors using this code are unlocked.")
	else
		self.feedback = "Rejected."
		print("Keypad rejected " .. self.entry .. ".")
	end
	self.feedback_timer = 2.0
	self.entry = ""
end

function Keypad:on_update(dt)
	if self.feedback_timer > 0.0 then
		self.feedback_timer = self.feedback_timer - dt
	end

	local player, distance = self.world:findNearestWithTag(self.player_tag)
	local in_range = player ~= nil and distance ~= nil and distance <= self.interact_range

	-- Walking away closes the panel, so the player never keeps typing into a
	-- keypad they have left behind.
	if not in_range then
		if self.active then
			self.active = false
			self.entry = ""
		end
		return
	end

	if self.input:isKeyPressed("E") then
		self.active = not self.active
		self.entry = ""
		if self.active then
			print("Keypad active - type the code, Enter to submit, Backspace to correct.")
		end
	end

	if not self.active or self.unlocked then
		return
	end

	for digit = 0, 9 do
		if self.input:isKeyPressed(tostring(digit)) and #self.entry < self.max_length then
			self.entry = self.entry .. tostring(digit)
			print("Keypad: " .. string.rep("*", #self.entry))
		end
	end
	if self.input:isKeyPressed("Backspace") and #self.entry > 0 then
		self.entry = string.sub(self.entry, 1, #self.entry - 1)
	end
	if self.input:isKeyPressed("Enter") and #self.entry > 0 then
		self:submit()
	end
end

return Keypad
