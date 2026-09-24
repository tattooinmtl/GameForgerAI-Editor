-- Gives this object health so weapons can hurt it (fps_player.lua's melee,
-- guns, taser and casters; projectiles.lua's fire/frost/electric spells;
-- enemy_ai.lua's attacks). Put it on enemies, props, rocks - and on the
-- player (tick is_player) for a health bar, the Life Caster's healing and
-- respawning.
--
-- Hooks other scripts call (through self.world:damage / heal / stun / send):
--   on_damage(amount, attacker, info)  info = {crit=, element=} ; returns true if this hit killed it
--   on_heal(amount, healer, info)      returns how much was actually healed
--   on_stun(seconds, attacker, info)
--   on_status(kind, seconds, strength, source)   kind = "burn" (strength = damage/sec) or "frost"
--   is_dead()                           true while dead (the player controller freezes)
--
-- @property max_health number 100
-- @property destroy_on_death bool true
-- @property hit_flash bool true
-- @property show_damage_numbers bool true
-- @property is_player bool false
-- @property respawn_seconds number 3
-- @property regen_per_second number 0

local Health = {}

local ELEMENT_COLORS = {
    fire = {r = 1.0, g = 0.55, b = 0.15},
    frost = {r = 0.55, g = 0.85, b = 1.0},
    electric = {r = 0.75, g = 0.65, b = 1.0},
    heal = {r = 0.45, g = 1.0, b = 0.5},
}

function Health:on_start()
    self.max_health = self.max_health or 100
    if self.destroy_on_death == nil then self.destroy_on_death = true end
    if self.hit_flash == nil then self.hit_flash = true end
    if self.show_damage_numbers == nil then self.show_damage_numbers = true end
    if self.is_player == nil then self.is_player = false end
    self.respawn_seconds = self.respawn_seconds or 3
    self.regen_per_second = self.regen_per_second or 0

    self.health = self.max_health
    self.dead = 0          -- numbers (0/1) so the engine's HUD can read them
    self.burning = 0
    self.frozen = 0
    self.stunned_time = 0
    self.burn_time = 0
    self.burn_dps = 0
    self.burn_source = ""
    self.burn_timer = 0
    self.frost_time = 0
    self.spark_timer = 0
    self.respawn_timer = 0
    self.spawn_position = self.entity:getPosition()
    self:update_player_bar()
end

function Health:top_position()
    local position = self.entity:getPosition()
    local scale = self.entity:getScale()
    local height = self.is_player and 1.9 or math.abs(scale.y)
    return {x = position.x, y = position.y + height, z = position.z}
end

function Health:update_player_bar()
    if self.is_player then
        self.world:setHudBar("health", "HP " .. math.ceil(self.health) .. " / " .. math.floor(self.max_health),
            self.health / self.max_health, {r = 0.85, g = 0.2, b = 0.22}, 1)
    end
end

function Health:is_dead()
    return self.dead == 1
end

function Health:on_damage(amount, attacker_name, info)
    if self.dead == 1 or amount == nil or amount <= 0 then
        return false
    end
    info = info or {}
    self.health = math.max(0, self.health - amount)

    local top = self:top_position()
    if self.hit_flash then
        self.world:spawnFlash(top, {r = 1.0, g = 0.25, b = 0.2}, 18, 0.12)
    end
    if self.show_damage_numbers and not self.is_player then
        local color = ELEMENT_COLORS[info.element] or {r = 1, g = 1, b = 1}
        local text = tostring(math.floor(amount + 0.5))
        local scale = 1.0
        if info.crit then
            color = {r = 1.0, g = 0.85, b = 0.2}
            text = text .. "!"
            scale = 1.45
        end
        local jitter = (math.random() - 0.5) * 0.5
        self.world:spawnText({x = top.x + jitter, y = top.y + 0.3, z = top.z}, text, color, 0.9, scale)
    end
    self:update_player_bar()

    if self.health <= 0 then
        self.dead = 1
        self.burning = 0
        self.frozen = 0
        if self.is_player then
            self.respawn_timer = self.respawn_seconds
            self.world:showMessage("You were defeated - respawning...", self.respawn_seconds)
        else
            print(self.entity:getName() .. " was defeated" ..
                ((attacker_name ~= nil and attacker_name ~= "") and (" by " .. attacker_name) or "") .. ".")
            if self.destroy_on_death then
                self.entity:setActive(false)
            end
        end
        return true
    end
    return false
end

function Health:on_heal(amount, healer_name, info)
    if self.dead == 1 or amount == nil or amount <= 0 then
        return 0
    end
    local before = self.health
    self.health = math.min(self.max_health, self.health + amount)
    local healed = self.health - before
    if healed > 0 and self.show_damage_numbers then
        local top = self:top_position()
        self.world:spawnText({x = top.x, y = top.y + 0.2, z = top.z}, "+" .. math.floor(healed + 0.5),
            ELEMENT_COLORS.heal, 1.0, 1.2)
    end
    self:update_player_bar()
    return healed
end

function Health:on_stun(seconds, attacker_name, info)
    self.stunned_time = math.max(self.stunned_time, seconds or 0)
end

function Health:on_status(kind, seconds, strength, source)
    if self.dead == 1 then return end
    if kind == "burn" then
        self.burn_time = math.max(self.burn_time, seconds or 3)
        self.burn_dps = math.max(self.burn_dps, strength or 5)
        self.burn_source = source or "fire"
    elseif kind == "frost" then
        self.frost_time = math.max(self.frost_time, seconds or 1.5)
        -- Frost puts out fire.
        self.burn_time = 0
    end
end

function Health:on_update(delta_time)
    local dt = math.min(delta_time, 0.05)

    if self.dead == 1 then
        if self.is_player then
            self.respawn_timer = self.respawn_timer - dt
            if self.respawn_timer <= 0 then
                self.dead = 0
                self.health = self.max_health
                self.entity:setPosition(self.spawn_position)
                self.entity:send("on_respawn")
                self.world:showMessage("Back in the fight!", 1.5)
                self:update_player_bar()
            end
        end
        return
    end

    if self.regen_per_second > 0 and self.health < self.max_health then
        self.health = math.min(self.max_health, self.health + self.regen_per_second * dt)
        self:update_player_bar()
    end

    -- Burning: damage over time + flames.
    if self.burn_time > 0 then
        self.burn_time = self.burn_time - dt
        self.burning = 1
        self.burn_timer = self.burn_timer - dt
        if self.burn_timer <= 0 then
            self.burn_timer = 0.5
            local position = self.entity:getPosition()
            self.entity:send("play_effect", "burn_tick", position)
            -- Other objects have no effects.lua, so draw flames here too.
            local scale = self.entity:getScale()
            for _ = 1, 3 do
                self.world:spawnFlash({x = position.x + (math.random() - 0.5) * scale.x,
                    y = position.y + math.random() * math.abs(scale.y), z = position.z + (math.random() - 0.5) * scale.z},
                    {r = 1.0, g = 0.45, b = 0.1}, 14, 0.35)
            end
            self:on_damage(self.burn_dps * 0.5, self.burn_source, {element = "fire"})
        end
    else
        self.burning = 0
    end

    -- Frozen: blue shimmer while it lasts.
    if self.frost_time > 0 then
        self.frost_time = self.frost_time - dt
        self.frozen = 1
        self.spark_timer = self.spark_timer - dt
        if self.spark_timer <= 0 then
            self.spark_timer = 0.15
            local position = self.entity:getPosition()
            local scale = self.entity:getScale()
            self.world:spawnFlash({x = position.x + (math.random() - 0.5) * scale.x * 2,
                y = position.y + (math.random() - 0.2) * math.abs(scale.y), z = position.z + (math.random() - 0.5) * scale.z * 2},
                {r = 0.7, g = 0.9, b = 1.0}, 10, 0.4)
        end
    else
        self.frozen = 0
    end

    -- Stunned (taser): crackle.
    if self.stunned_time > 0 then
        self.stunned_time = math.max(0, self.stunned_time - dt)
        self.spark_timer = self.spark_timer - dt
        if self.spark_timer <= 0 and self.frost_time <= 0 then
            self.spark_timer = 0.12
            local position = self.entity:getPosition()
            local scale = self.entity:getScale()
            local top = {x = position.x, y = position.y + math.abs(scale.y), z = position.z}
            local offset = {
                x = position.x + (math.random() - 0.5) * scale.x * 2,
                y = position.y + math.random() * math.abs(scale.y),
                z = position.z + (math.random() - 0.5) * scale.z * 2,
            }
            self.world:spawnBeam(top, offset, {r = 0.6, g = 0.85, b = 1.0}, 0.1, 1.5, true)
        end
    end
end

return Health

-- @preset FPS Opus | damageable
