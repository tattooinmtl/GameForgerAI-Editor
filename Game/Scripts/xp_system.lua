-- Experience and levels for the FPS Opus preset.
--
-- Every hit a weapon lands earns XP (fps_player.lua and projectiles.lua send
-- "on_weapon_hit"). XP goes to two places:
--   * the PLAYER level - reaching a level unlocks a new caster: the
--     unlock_*_item object (a hidden items.lua pickup placed in the scene by
--     Create > FPS Demo Arena) is put straight into your inventory.
--   * that WEAPON's own level - each weapon level raises its damage, fire
--     rate, accuracy, magazine/charges and critical-hit chance/damage.
-- fps_player.lua asks for the current bonuses with
--     local _, bonus = self.entity:send("get_weapon_bonus", weapon_id)
--
-- Shows an XP bar (bottom-left), "+N XP" numbers, and a LEVEL UP burst.
--
-- @property xp_per_damage number 0.6
-- @property xp_per_kill number 45
-- @property player_xp_base number 120
-- @property player_xp_growth number 1.35
-- @property weapon_xp_base number 70
-- @property weapon_xp_growth number 1.3
-- @property max_weapon_level number 10
-- @property damage_per_level number 0.08
-- @property fire_rate_per_level number 0.05
-- @property accuracy_per_level number 0.05
-- @property ammo_per_level number 0.15
-- @property base_crit_chance number 0.05
-- @property crit_chance_per_level number 0.02
-- @property base_crit_multiplier number 1.8
-- @property unlock_fire_level number 2
-- @property unlock_fire_item string Fire Caster Pickup
-- @property unlock_frost_level number 3
-- @property unlock_frost_item string Frost Caster Pickup
-- @property unlock_heal_level number 4
-- @property unlock_heal_item string Life Caster Pickup
-- @property show_xp_numbers bool true

local XpSystem = {}

function XpSystem:on_start()
    self.xp_per_damage = self.xp_per_damage or 0.6
    self.xp_per_kill = self.xp_per_kill or 45
    self.player_xp_base = self.player_xp_base or 120
    self.player_xp_growth = self.player_xp_growth or 1.35
    self.weapon_xp_base = self.weapon_xp_base or 70
    self.weapon_xp_growth = self.weapon_xp_growth or 1.3
    self.max_weapon_level = self.max_weapon_level or 10
    if self.show_xp_numbers == nil then self.show_xp_numbers = true end

    self.level = 1
    self.xp = 0
    self.weapons = {}
    self.unlocked = {}
    self.xp_system_ready = true

    -- Unlocks, lowest level first.
    self.unlocks = {
        {level = self.unlock_fire_level or 2, item = self.unlock_fire_item or "Fire Caster Pickup", name = "Fire Caster"},
        {level = self.unlock_frost_level or 3, item = self.unlock_frost_item or "Frost Caster Pickup", name = "Frost Caster"},
        {level = self.unlock_heal_level or 4, item = self.unlock_heal_item or "Life Caster Pickup", name = "Life Caster"},
    }
    table.sort(self.unlocks, function(a, b) return a.level < b.level end)
    self:update_bar()
end

function XpSystem:xp_for_player_level(level)
    return math.floor(self.player_xp_base * (self.player_xp_growth ^ (level - 1)))
end

function XpSystem:xp_for_weapon_level(level)
    return math.floor(self.weapon_xp_base * (self.weapon_xp_growth ^ (level - 1)))
end

function XpSystem:weapon_state(weapon)
    local state = self.weapons[weapon]
    if state == nil then
        state = {level = 1, xp = 0}
        self.weapons[weapon] = state
    end
    return state
end

function XpSystem:update_bar()
    local needed = self:xp_for_player_level(self.level)
    self.world:setHudBar("xp", "Lv " .. self.level .. "   XP " .. math.floor(self.xp) .. " / " .. needed,
        self.xp / needed, {r = 0.62, g = 0.42, b = 1.0}, 2)
end

-- Called for every hit: weapon id, damage dealt, whether it killed, crit, where.
function XpSystem:on_weapon_hit(weapon, damage, killed, crit, position)
    local gained = (damage or 0) * self.xp_per_damage + (killed and self.xp_per_kill or 0)
    if gained <= 0 then return end
    if self.show_xp_numbers and position ~= nil then
        self.world:spawnText({x = position.x, y = position.y + 1.2, z = position.z}, "+" .. math.floor(gained + 0.5) .. " XP",
            {r = 0.75, g = 0.6, b = 1.0}, 1.1, 0.85)
    end

    -- Weapon XP.
    local state = self:weapon_state(weapon or "fists")
    if state.level < self.max_weapon_level then
        state.xp = state.xp + gained
        while state.level < self.max_weapon_level and state.xp >= self:xp_for_weapon_level(state.level) do
            state.xp = state.xp - self:xp_for_weapon_level(state.level)
            state.level = state.level + 1
            self.world:showMessage(string.upper(weapon or "fists") .. " reached level " .. state.level .. "!", 2.5)
        end
    end

    -- Player XP / level-ups / unlocks.
    self.xp = self.xp + gained
    while self.xp >= self:xp_for_player_level(self.level) do
        self.xp = self.xp - self:xp_for_player_level(self.level)
        self.level = self.level + 1
        self:on_level_up()
    end
    self:update_bar()
end

function XpSystem:on_level_up()
    local feet = self.entity:getPosition()
    self.entity:send("play_effect", "level_up", feet)
    local message = "LEVEL " .. self.level .. "!"
    for _, unlock in ipairs(self.unlocks) do
        if self.level >= unlock.level and not self.unlocked[unlock.item] then
            self.unlocked[unlock.item] = true
            if self.inventory:addEntity(unlock.item) then
                message = message .. "   " .. unlock.name .. " unlocked - check your inventory (I)"
            end
        end
    end
    self.world:showMessage(message, 4)
    print(message)
end

-- The per-weapon bonuses fps_player.lua applies.
function XpSystem:get_weapon_bonus(weapon)
    local level = self:weapon_state(weapon or "fists").level
    local steps = level - 1
    return {
        level = level,
        damage = 1 + steps * (self.damage_per_level or 0.08),
        fire_rate = 1 + steps * (self.fire_rate_per_level or 0.05),
        accuracy = math.max(0.3, 1 - steps * (self.accuracy_per_level or 0.05)),
        ammo = 1 + steps * (self.ammo_per_level or 0.15),
        crit_chance = (self.base_crit_chance or 0.05) + steps * (self.crit_chance_per_level or 0.02),
        crit_multiplier = (self.base_crit_multiplier or 1.8) + steps * 0.05,
    }
end

function XpSystem:get_player_level()
    return self.level
end

function XpSystem:on_update(delta_time)
end

return XpSystem

-- @preset FPS Opus | player
