-- Door - open/close on E, optionally locked behind a key item or a keycode.
--
-- Attach to the door object itself. The door SWINGS by rotating around its own
-- pivot, so set the Inspector's Pivot to the hinge edge ("left" or "right")
-- first - otherwise it spins around its middle like a revolving door. That
-- pivot control already exists; this script deliberately does not reimplement
-- hinge maths on top of it.
--
-- LOCK MODES (self.lock_mode):
--   "none"    - opens on E.
--   "key"     - needs an inventory item whose name matches self.key_item_name.
--   "keycode" - needs self.keycode to have been entered on a keypad. The
--               keypad is a separate object running keypad_panel.lua that
--               registers itself as a manager named "keypad:<code>" once
--               unlocked; this door just asks whether that manager exists.
--
-- Why a manager name rather than a shared variable: scripts here have no
-- cross-script storage, but the manager registry (self.managers) is exactly a
-- global set of names any script can register and any other can query. Using
-- it for "this code has been entered" needs no new engine support.

local Door = {}

function Door:on_start()
	self.lock_mode = "none"          -- "none" | "key" | "keycode"
	self.key_item_name = "Brass Key" -- must match the pickup item's Item Name
	self.keycode = "1234"

	self.open_angle = 95.0           -- degrees the door swings
	self.open_speed = 220.0          -- degrees per second
	self.interact_range = 2.6        -- how close the player must be
	self.player_tag = "Player"
	self.auto_close_seconds = 0.0    -- 0 = stays open until E again

	-- Runtime state.
	self.is_open = false
	self.current_angle = 0.0
	self.closed_yaw = self.entity:getRotation().y
	self.open_timer = 0.0
	self.message = ""
	self.message_timer = 0.0
end

-- True when the door's own lock condition is satisfied right now.
function Door:can_open()
	if self.lock_mode == "none" then
		return true, ""
	end
	if self.lock_mode == "key" then
		if self.managers:has("key:" .. self.key_item_name) then
			return true, ""
		end
		return false, "Locked - needs the " .. self.key_item_name .. "."
	end
	if self.lock_mode == "keycode" then
		if self.managers:has("keypad:" .. self.keycode) then
			return true, ""
		end
		return false, "Locked - enter the code on the keypad."
	end
	return true, ""
end

function Door:on_update(dt)
	local player, distance = self.world:findNearestWithTag(self.player_tag)
	local in_range = player ~= nil and distance ~= nil and distance <= self.interact_range

	if in_range and self.input:isKeyPressed("E") then
		if self.is_open then
			self.is_open = false
		else
			local allowed, why = self:can_open()
			if allowed then
				self.is_open = true
				self.open_timer = self.auto_close_seconds
			else
				self.message = why
				self.message_timer = 2.0
				print(why)
			end
		end
	end

	if self.auto_close_seconds > 0.0 and self.is_open then
		self.open_timer = self.open_timer - dt
		if self.open_timer <= 0.0 then
			self.is_open = false
		end
	end

	-- Ease toward the target angle rather than snapping, so the door reads as
	-- a door and not a teleporting wall.
	local target = self.is_open and self.open_angle or 0.0
	if math.abs(self.current_angle - target) > 0.01 then
		local step = self.open_speed * dt
		if self.current_angle < target then
			self.current_angle = math.min(self.current_angle + step, target)
		else
			self.current_angle = math.max(self.current_angle - step, target)
		end
		self.entity:setRotation({ x = 0.0, y = self.closed_yaw + self.current_angle, z = 0.0 })
	end

	if self.message_timer > 0.0 then
		self.message_timer = self.message_timer - dt
	end

	-- A door that is open should not block the player. hasCollider is an
	-- authored property rather than a script one, so instead of trying to
	-- toggle it, keep the door thin and rely on it swinging out of the
	-- doorway - which is what the pivot setup above is for.
end

return Door
