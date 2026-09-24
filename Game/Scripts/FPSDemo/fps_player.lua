-- All-in-one first-person player: movement + camera + inventory + weapons,
-- with animated first-person hands.
--
-- Set it up in one click: Create > FPS Player (Hands + Weapons). That builds
-- the Player capsule with this script, and the "FPSRig" hands + 8 weapon
-- models this script drives (see FpsRigBuilder.hpp for the rig layout).
--
-- CONTROLS (during Play)
--   W A S D       move            Space      jump          Left Shift  sprint
--   Mouse         look            C          first/third person
--   E             pick up the nearest items.lua object
--   I             open/close the inventory grid (drag to rearrange,
--                 drag out of the window to drop, right-click for more)
--   1 - 8 / wheel choose the hotbar slot - its weapon is equipped
--   Left Mouse    attack / fire (hold to auto-fire the AK-47)
--   Right Mouse   aim down sights (pistol, AK-47)
--   R             reload
--
-- WEAPONS (an item's `weapon` value in items.lua picks one)
--   sword    fast slash             axe       heavy chop
--   hammer   slow, huge knockback   pickaxe   chop, x3 on "Mineable" rocks
--   gun      semi-auto pistol       ak47      full-auto rifle
--   taser    short-range stun
--   CASTERS (spell projectiles via projectiles.lua; charges refill by themselves)
--   lightning  Storm Caster - electric bolt that chains to up to 4 targets
--   fire       Fire Caster  - fireball that explodes and sets targets on fire
--   frost      Frost Caster - ice shard that freezes what it hits
--   heal       Life Caster  - heals you (needs health.lua on the player)
-- With nothing (or a non-weapon) in the selected slot you punch with both fists.
--
-- XP (xp_system.lua): every hit earns XP. Weapon levels raise damage, fire
-- rate, accuracy, magazine/charges and critical hits; player levels unlock
-- the Fire, Frost and Life Casters.
--
-- DAMAGE goes to anything with a script that handles on_damage - add
-- health.lua to enemies, props or rocks. It doesn't need a tag.
--
-- FPS DEMO KIT - these scripts work together, all on the player unless
-- noted: fps_player.lua, projectiles.lua, effects.lua, xp_system.lua,
-- health.lua (also on enemies), items.lua (on pickups), game_manager.lua (on
-- the Game Manager). Tick "Link all FPS Demo scripts" in the Inspector to
-- attach the player's set in one go.
--
-- @property walk_speed number 4.5
-- @property sprint_multiplier number 1.7
-- @property jump_speed number 6.2
-- @property gravity number -18
-- @property collider_radius number 0.35
-- @property collider_height number 1.8
-- @property rig_name string FPSRig
-- @property view_bob number 1
-- @property damage_multiplier number 1
-- @property infinite_ammo bool false
-- @property start_mode enum fps|third_person fps

local FpsPlayer = {}

-- ---------------------------------------------------------------- tuning

local WEAPONS = {
    fists = {
        kind = "melee", style = "punch", damage = 10, range = 1.7, cone = 0.55,
        cooldown = 0.38, swing = 0.32, knockback = 0.6, label = "Fists",
    },
    sword = {
        kind = "melee", style = "slash", damage = 34, range = 2.5, cone = 0.45,
        cooldown = 0.48, swing = 0.38, knockback = 1.2, cleave = true, label = "Sword",
    },
    axe = {
        kind = "melee", style = "chop", damage = 46, range = 2.3, cone = 0.55,
        cooldown = 0.7, swing = 0.5, knockback = 1.8, label = "Axe",
    },
    hammer = {
        kind = "melee", style = "smash", damage = 64, range = 2.2, cone = 0.45,
        cooldown = 1.0, swing = 0.68, knockback = 4.5, cleave = true, label = "War Hammer",
    },
    pickaxe = {
        kind = "melee", style = "chop", damage = 26, range = 2.3, cone = 0.55,
        cooldown = 0.62, swing = 0.48, knockback = 0.8, mine_bonus = 3.0, label = "Pickaxe",
    },
    gun = {
        kind = "hitscan", damage = 26, range = 80, cooldown = 0.2, auto = false,
        mag = 12, reload = 1.25, spread = 0.35, ads_spread = 0.05, recoil = 1.0,
        tracer = {r = 1.0, g = 0.9, b = 0.55}, label = "Pistol",
    },
    ak47 = {
        kind = "hitscan", damage = 19, range = 100, cooldown = 0.1, auto = true,
        mag = 30, reload = 2.2, spread = 1.8, ads_spread = 0.5, recoil = 0.55,
        tracer = {r = 1.0, g = 0.72, b = 0.3}, label = "AK-47",
    },
    taser = {
        kind = "taser", damage = 6, range = 9, cooldown = 1.3, stun = 2.5,
        recoil = 0.5, label = "Taser",
    },
    lightning = {
        kind = "spell", element = "electric", damage = 26, cooldown = 0.55, speed = 42,
        mag = 8, recharge = 1.1, jumps = 4, jump_range = 7.5, falloff = 0.75, recoil = 0.6,
        label = "Storm Caster",
    },
    fire = {
        kind = "spell", element = "fire", damage = 38, cooldown = 0.85, speed = 24,
        mag = 6, recharge = 1.6, radius = 3.2, burn_seconds = 3, burn_dps = 9, recoil = 0.9,
        label = "Fire Caster",
    },
    frost = {
        kind = "spell", element = "frost", damage = 24, cooldown = 0.4, speed = 38,
        mag = 10, recharge = 0.9, freeze_seconds = 1.6, splash_radius = 1.6, recoil = 0.5,
        label = "Frost Caster",
    },
    heal = {
        kind = "heal", element = "heal", heal = 32, cooldown = 1.2,
        mag = 4, recharge = 3.5, recoil = 0.4, label = "Life Caster",
    },
}

-- Used when xp_system.lua isn't attached.
local NO_BONUS = {level = 1, damage = 1, fire_rate = 1, accuracy = 1, ammo = 1, crit_chance = 0.05, crit_multiplier = 1.8}

-- Swing poses: HandR local rotation (degrees) / position offset at the top of
-- the wind-up and at the end of the strike.
local SWINGS = {
    punch = {wind_rot = {x = 0, y = 0, z = 0}, wind_pos = {x = 0.02, y = -0.03, z = -0.07},
             hit_rot = {x = -8, y = 5, z = 0}, hit_pos = {x = -0.09, y = 0.05, z = 0.2}},
    slash = {wind_rot = {x = -35, y = -25, z = 40}, wind_pos = {x = 0.07, y = 0.07, z = -0.05},
             hit_rot = {x = 55, y = 40, z = -50}, hit_pos = {x = -0.16, y = -0.04, z = 0.1}},
    chop  = {wind_rot = {x = -60, y = 0, z = 10}, wind_pos = {x = 0.02, y = 0.11, z = -0.07},
             hit_rot = {x = 75, y = 0, z = -5}, hit_pos = {x = -0.04, y = -0.1, z = 0.12}},
    smash = {wind_rot = {x = -80, y = 10, z = 15}, wind_pos = {x = 0.0, y = 0.16, z = -0.1},
             hit_rot = {x = 85, y = 0, z = 0}, hit_pos = {x = -0.06, y = -0.14, z = 0.15}},
}

local HAND_R_REST = {x = 0.19, y = -0.23, z = 0.42}
local HAND_L_REST = {x = -0.16, y = -0.24, z = 0.46}
local ADS_OFFSET = {
    gun  = {x = -0.19, y = 0.105, z = -0.05},
    ak47 = {x = -0.19, y = 0.115, z = -0.09},
}
local HOTBAR_KEYS = {"1", "2", "3", "4", "5", "6", "7", "8"}

-- ---------------------------------------------------------------- math helpers

local function v(x, y, z) return {x = x, y = y, z = z} end
local function add(a, b) return v(a.x + b.x, a.y + b.y, a.z + b.z) end
local function sub(a, b) return v(a.x - b.x, a.y - b.y, a.z - b.z) end
local function mul(a, s) return v(a.x * s, a.y * s, a.z * s) end
local function dot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end
local function len(a) return math.sqrt(dot(a, a)) end
local function norm(a)
    local l = len(a)
    if l < 1e-6 then return v(0, 0, 1) end
    return mul(a, 1 / l)
end
local function cross(a, b)
    return v(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x)
end
local function lerp(a, b, t) return a + (b - a) * t end
local function vlerp(a, b, t) return v(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t)) end
local function clamp(x, lo, hi) return math.max(lo, math.min(hi, x)) end
local function smooth(t) t = clamp(t, 0, 1) return t * t * (3 - 2 * t) end
-- Frame-rate independent approach toward a target.
local function approach(current, target, rate, dt) return lerp(current, target, 1 - math.exp(-rate * dt)) end

-- Poses below are written with +X = screen RIGHT. The game camera looks down
-- +Z and shows +X on screen-LEFT, so the rig (built mirrored the same way,
-- see FpsRigBuilder.cpp) takes them mirrored across X.
local function to_rig(p) return v(-p.x, p.y, p.z) end
local function to_rig_rot(r) return v(r.x, -r.y, -r.z) end

-- Random direction within `degrees` of `forward` (bullet spread).
local function spread(forward, degrees)
    if degrees <= 0 then return forward end
    local right = norm(cross(forward, v(0, 1, 0)))
    if len(right) < 0.5 then right = v(1, 0, 0) end
    local up = cross(right, forward)
    local r = math.rad(degrees) * math.sqrt(math.random())
    local a = math.random() * math.pi * 2
    return norm(add(forward, add(mul(right, math.cos(a) * r), mul(up, math.sin(a) * r))))
end

-- ---------------------------------------------------------------- lifecycle

function FpsPlayer:on_start()
    self.walk_speed = self.walk_speed or 4.5
    self.sprint_multiplier = self.sprint_multiplier or 1.7
    self.jump_speed = self.jump_speed or 6.2
    self.gravity = self.gravity or -18
    self.collider_radius = self.collider_radius or 0.35
    self.collider_height = self.collider_height or 1.8
    self.rig_name = self.rig_name or "FPSRig"
    self.view_bob = self.view_bob or 1
    self.damage_multiplier = self.damage_multiplier or 1
    if self.infinite_ammo == nil then self.infinite_ammo = false end

    -- Read by the engine: this script provides the E pickup / I inventory.
    self.inventory_enabled = true
    self.pickup_range = 3.0
    self.pickup_height_tolerance = 2.5

    self.velocity_y = 0
    self.grounded = true
    self.was_grounded = true
    self.time = 0

    self.camera_mode = (self.start_mode == "third_person") and "third_person" or "fps"
    self.camera:setMode(self.camera_mode)

    -- Rig node names.
    local rig = self.rig_name
    self.rig = rig
    self.rig_pitch = rig .. ".Pitch"
    self.rig_sway = rig .. ".Sway"
    self.hand_r = rig .. ".HandR"
    self.hand_l = rig .. ".HandL"
    self.has_rig = self.world:getEntityPosition(rig) ~= nil
    if not self.has_rig then
        print("fps_player: no viewmodel rig named '" .. rig .. "' - hands are off. " ..
              "Use Create > FPS Player (Hands + Weapons), or set rig_name.")
    end

    -- Weapon state.
    self.weapon_id = nil
    self.cooldown = 0
    self.attack_t = -1          -- melee swing progress 0..1, -1 = idle
    self.attack_hit_done = false
    self.punch_left = false
    self.reload_t = -1          -- reload progress 0..1, -1 = idle
    self.equip_t = 1
    self.recoil = 0
    self.ads = 0
    self.ammo = {}           -- filled lazily (max depends on the weapon's XP level)
    self.recharge_timer = {}
    self.warned = {}
    self.is_dead = false
    self.prev_fire = false
    self.hud_text = ""

    -- View motion.
    self.bob_phase = 0
    self.bob_amount = 0
    self.sway_x = 0
    self.sway_y = 0
    self.land_dip = 0
    self.sprint_blend = 0

    self:hide_all_weapons()
end

function FpsPlayer:on_update(delta_time)
    local dt = math.min(delta_time, 0.05)
    self.time = self.time + dt

    if self.input:isKeyPressed("C") then
        self.camera_mode = (self.camera_mode == "fps") and "third_person" or "fps"
        self.camera:setMode(self.camera_mode)
    end

    -- While health.lua says we're dead: no moving or attacking until respawn.
    local _, dead = self.entity:send("is_dead")
    self.is_dead = dead == true
    if self.is_dead then
        self.prev_fire = true
        self:update_view_motion(dt, false, false)
        self:update_rig()
        self:update_hud()
        return
    end

    local ui_open = self.inventory:isOpen()
    if not ui_open then
        self:update_hotbar_input()
    end

    local moving, sprinting = self:update_movement(dt)
    self:update_equipped()
    self:update_weapon(dt, ui_open, sprinting)
    self:update_view_motion(dt, moving, sprinting)
    self:update_rig()
    self:update_hud()
end

-- health.lua sends this when the player respawns.
function FpsPlayer:on_respawn()
    self.velocity_y = 0
    self.attack_t = -1
    self.reload_t = -1
end

-- ---------------------------------------------------------------- movement

function FpsPlayer:update_movement(dt)
    local blocked = self.world:isAimingCatapult()
    local forward_axis = blocked and 0 or self.input:getAxis("W", "S")
    local strafe_axis = blocked and 0 or self.input:getAxis("D", "A")
    local moving = forward_axis ~= 0 or strafe_axis ~= 0
    local sprinting = moving and forward_axis > 0 and self.input:isKeyDown("LeftShift")
        and self.ads < 0.5 and self.reload_t < 0 and not self.world:isHoldingItem()

    local speed = self.walk_speed * (sprinting and self.sprint_multiplier or 1)
    if self.ads > 0.5 then speed = speed * 0.6 end

    local forward = self.entity:getForward()
    local right = self.entity:getRight()
    if self.camera_mode == "third_person" then
        -- In third person the mouse turns only the camera, so move relative
        -- to where the camera looks - relative to the body, A/D and W/S went
        -- the wrong way on screen once the camera swung around.
        local _, aim = self.camera:getAim()
        local flat = aim and math.sqrt(aim.x * aim.x + aim.z * aim.z) or 0
        if flat > 1e-4 then
            forward = v(aim.x / flat, 0, aim.z / flat)
            right = norm(cross(forward, v(0, 1, 0)))
        end
    end
    local mx = forward.x * forward_axis + right.x * strafe_axis
    local mz = forward.z * forward_axis + right.z * strafe_axis
    local ml = math.sqrt(mx * mx + mz * mz)
    if ml > 1e-4 then mx, mz = mx / ml, mz / ml end

    if self.grounded and self.input:isKeyPressed("Space") then
        self.velocity_y = self.jump_speed
        self.grounded = false
    end
    self.velocity_y = self.velocity_y + self.gravity * dt

    local position = self.entity:getPosition()
    position.x = position.x + mx * speed * dt
    position.z = position.z + mz * speed * dt
    position.y = position.y + self.velocity_y * dt

    local resolved, grounded = self.physics:resolve(position, self.collider_radius, self.collider_height)
    position = resolved
    if position.y <= 0 then -- fallback floor for scenes without ground colliders
        position.y = 0
        grounded = true
    end
    if grounded then
        if not self.was_grounded and self.velocity_y < -4 then
            self.land_dip = math.min(0.06, -self.velocity_y * 0.004)
        end
        self.velocity_y = 0
    end
    self.grounded = grounded
    self.was_grounded = grounded
    self.entity:setPosition(position)
    return moving and grounded, sprinting
end

-- ---------------------------------------------------------------- inventory / equip

function FpsPlayer:update_hotbar_input()
    for index, key in ipairs(HOTBAR_KEYS) do
        if self.input:isKeyPressed(key) then
            self.inventory:select(index)
            return
        end
    end
    local scroll = self.input:getScrollDelta()
    if scroll ~= 0 and self.ads < 0.5 then
        local size = self.inventory:getHotbarSize()
        local slot = self.inventory:getSelectedSlot()
        if slot > size then slot = 1 end
        slot = slot + (scroll > 0 and -1 or 1)
        if slot < 1 then slot = size end
        if slot > size then slot = 1 end
        self.inventory:select(slot)
    end
end

-- Which weapon the selected slot holds ("fists" for empty / non-weapons).
function FpsPlayer:selected_weapon_id()
    local item = self.inventory:getSelected()
    if item ~= nil and item.weapon ~= nil and WEAPONS[item.weapon] ~= nil then
        return item.weapon, item
    end
    return "fists", item
end

function FpsPlayer:weapon_node(id)
    return self.rig .. ".W." .. id
end

function FpsPlayer:hide_all_weapons()
    if not self.has_rig then return end
    for id, _ in pairs(WEAPONS) do
        if id ~= "fists" then
            self.world:setEntityActive(self:weapon_node(id), false)
        end
    end
    self.world:setEntityActive(self.hand_l, false)
end

function FpsPlayer:update_equipped()
    local id = self:selected_weapon_id()
    if id == self.weapon_id then return end
    if self.has_rig then
        if self.weapon_id ~= nil and self.weapon_id ~= "fists" then
            self.world:setEntityActive(self:weapon_node(self.weapon_id), false)
        end
        if id ~= "fists" then
            self.world:setEntityActive(self:weapon_node(id), true)
        end
        -- The free left hand only shows for fists; two-handed weapons bring
        -- their own support hand as part of the weapon model.
        self.world:setEntityActive(self.hand_l, id == "fists")
    end
    self.weapon_id = id
    self.equip_t = 0
    self.attack_t = -1
    self.reload_t = -1
    self.cooldown = math.max(self.cooldown, 0.25)
end

-- ---------------------------------------------------------------- weapons

function FpsPlayer:muzzle_position(eye, forward)
    if self.has_rig and self.camera_mode == "fps" then
        local p = self.world:getEntityPosition(self:weapon_node(self.weapon_id) .. ".Muzzle")
        if p ~= nil then return p end
    end
    return add(eye, mul(forward, 0.6))
end

function FpsPlayer:update_weapon(dt, ui_open, sprinting)
    local id = self.weapon_id
    local w = WEAPONS[id]
    local bonus = self:bonus(id)
    self.cooldown = math.max(0, self.cooldown - dt)
    self.recoil = approach(self.recoil, 0, 10, dt)
    self.equip_t = math.min(1, self.equip_t + dt / 0.32)
    self:recharge_casters(dt)

    local fire_down = (not ui_open) and self.input:isMouseButtonDown("Left")
    local fire_pressed = fire_down and not self.prev_fire
    self.prev_fire = fire_down

    local wants_ads = (not ui_open) and ADS_OFFSET[id] ~= nil
        and self.input:isMouseButtonDown("Right") and self.reload_t < 0
    self.ads = approach(self.ads, wants_ads and 1 or 0, 14, dt)

    -- Reload (guns). Casters refill on their own, see recharge_casters.
    if w.mag ~= nil and w.reload ~= nil then
        local max_ammo = self:max_ammo(id)
        if self.reload_t >= 0 then
            self.reload_t = self.reload_t + dt / (w.reload / bonus.fire_rate)
            if self.reload_t >= 1 then
                self.reload_t = -1
                self.ammo[id] = max_ammo
            end
        elseif not ui_open and self:ammo_of(id) < max_ammo and
            (self.input:isKeyPressed("R") or (fire_pressed and self:ammo_of(id) <= 0)) then
            self.reload_t = 0
        end
    end

    -- Melee swing in progress.
    if self.attack_t >= 0 then
        self.attack_t = self.attack_t + dt / (w.swing / bonus.fire_rate)
        if not self.attack_hit_done and self.attack_t >= 0.45 then
            self.attack_hit_done = true
            self:melee_hit(w)
        end
        if self.attack_t >= 1 then self.attack_t = -1 end
    end

    -- A consumable in hand: left click uses one.
    local _, item = self:selected_weapon_id()
    if fire_pressed and item ~= nil and item.type == "consumable" and self.cooldown <= 0 then
        if self.inventory:consumeSelected(1) then
            print("Used " .. item.name .. ".")
        end
        self.cooldown = 0.5
        return
    end

    local ready = self.cooldown <= 0 and self.equip_t >= 0.7 and self.reload_t < 0 and self.attack_t < 0
    local trigger = w.auto and fire_down or fire_pressed
    if not (ready and trigger) or ui_open then return end

    local eye, forward = self.camera:getAim()
    if eye == nil then return end

    -- Anything with a magazine/charges needs one left.
    if w.mag ~= nil and not self.infinite_ammo then
        if self:ammo_of(id) <= 0 then
            if w.reload ~= nil then self.reload_t = 0 end
            return
        end
        self.ammo[id] = self:ammo_of(id) - 1
    end
    self.cooldown = w.cooldown / bonus.fire_rate

    if w.kind == "melee" then
        self.attack_t = 0
        self.attack_hit_done = false
        self.punch_left = not self.punch_left
        self.cooldown = w.cooldown / bonus.fire_rate
    elseif w.kind == "hitscan" then
        self:fire_hitscan(w, eye, forward, sprinting)
        self.recoil = math.min(1.5, self.recoil + w.recoil)
    elseif w.kind == "taser" then
        self:fire_taser(w, eye, forward)
        self.recoil = math.min(1.5, self.recoil + w.recoil)
    elseif w.kind == "spell" then
        self:fire_spell(w, eye, forward)
        self.recoil = math.min(1.5, self.recoil + w.recoil)
    elseif w.kind == "heal" then
        self:cast_heal(w)
        self.recoil = math.min(1.5, self.recoil + w.recoil)
    end
end

-- ---------------------------------------------------------------- XP / damage helpers

-- Per-weapon bonuses from xp_system.lua (neutral if it isn't attached).
function FpsPlayer:bonus(id)
    local _, bonus = self.entity:send("get_weapon_bonus", id)
    return bonus or NO_BONUS
end

function FpsPlayer:max_ammo(id)
    local w = WEAPONS[id]
    if w == nil or w.mag == nil then return 0 end
    return math.max(1, math.floor(w.mag * self:bonus(id).ammo + 0.5))
end

function FpsPlayer:ammo_of(id)
    if self.ammo[id] == nil then self.ammo[id] = self:max_ammo(id) end
    return self.ammo[id]
end

-- Casters refill one charge every `recharge` seconds, equipped or not.
function FpsPlayer:recharge_casters(dt)
    for id, w in pairs(WEAPONS) do
        if w.recharge ~= nil then
            local max_ammo = self:max_ammo(id)
            if self:ammo_of(id) < max_ammo then
                self.recharge_timer[id] = (self.recharge_timer[id] or 0) + dt * self:bonus(id).fire_rate
                if self.recharge_timer[id] >= w.recharge then
                    self.recharge_timer[id] = 0
                    self.ammo[id] = self.ammo[id] + 1
                end
            else
                self.recharge_timer[id] = 0
            end
        end
    end
end

-- Base damage -> final damage with XP bonus, player multiplier and a crit roll.
function FpsPlayer:roll_damage(base)
    local bonus = self:bonus(self.weapon_id)
    local damage = base * bonus.damage * self.damage_multiplier
    local crit = math.random() < bonus.crit_chance
    if crit then damage = damage * bonus.crit_multiplier end
    return damage, crit
end

-- Deal damage and report it to xp_system.lua. Returns handled, killed.
function FpsPlayer:deal(name, damage, crit, position, element)
    local handled, killed = self.world:damage(name, damage, {crit = crit, element = element or "physical"})
    if handled then
        self.entity:send("on_weapon_hit", self.weapon_id, damage, killed == true, crit, position)
        if crit then self:effect("crit", position) end
    end
    return handled, killed == true
end

function FpsPlayer:effect(name, position, extra)
    self.entity:send("play_effect", name, position, extra)
end

-- Once-per-session hint when a preset script is missing.
function FpsPlayer:warn_once(key, message)
    if not self.warned[key] then
        self.warned[key] = true
        self.world:showMessage(message, 4)
        print(message)
    end
end

-- ---------------------------------------------------------------- weapons

function FpsPlayer:melee_hit(w)
    local eye, forward = self.camera:getAim()
    if eye == nil then return end
    local mineable = {}
    if w.mine_bonus ~= nil then
        for _, t in ipairs(self.world:findAllWithTag("Mineable", eye, w.range + 2)) do
            mineable[t.name] = true
        end
    end

    local hit_any = false
    for _, target in ipairs(self.world:findDamageable(eye, w.range + 1.2)) do
        local to = sub(target.position, eye)
        local distance = len(to)
        if distance > 1e-3 and distance <= w.range + 0.8 and dot(to, forward) / distance >= w.cone then
            local damage, crit = self:roll_damage(w.damage)
            if mineable[target.name] then damage = damage * w.mine_bonus end
            local contact = add(eye, mul(norm(to), math.max(0.4, distance - 0.4)))
            self:deal(target.name, damage, crit, contact, "physical")
            -- Spark where the blade meets the target, and a shove.
            self.world:spawnFlash(contact, {r = 1, g = 0.85, b = 0.55}, 16, 0.1)
            self:effect("hit_physical", contact)
            if w.knockback > 0 then
                local push = norm(v(to.x, 0, to.z))
                local p = self.world:getEntityPosition(target.name)
                if p ~= nil then
                    self.world:setEntityPosition(target.name, add(p, mul(push, w.knockback * 0.25)))
                end
            end
            hit_any = true
            if not w.cleave then break end
        end
    end

    if not hit_any then
        -- Hit a wall/prop? Show a spark on the surface.
        local point = self.world:raycast(eye, forward, w.range)
        if point ~= nil then
            self.world:spawnFlash(point, {r = 0.9, g = 0.9, b = 0.9}, 9, 0.08)
            self:effect("hit_physical", point)
        end
    end
end

function FpsPlayer:fire_hitscan(w, eye, forward, sprinting)
    local bonus = self:bonus(self.weapon_id)
    local cone = lerp(w.spread, w.ads_spread, self.ads) * bonus.accuracy
    if sprinting then cone = cone * 2 end
    local direction = spread(forward, cone)
    local muzzle = self:muzzle_position(eye, forward)
    local point, _, name = self.world:raycast(eye, direction, w.range)
    local finish = point or add(eye, mul(direction, w.range))

    self.world:spawnFlash(muzzle, {r = 1, g = 0.8, b = 0.35}, 20, 0.05)
    self.world:spawnBeam(muzzle, finish, w.tracer, 0.06, 2, false)
    if point ~= nil then
        self.world:spawnFlash(point, {r = 1, g = 0.9, b = 0.6}, 10, 0.12)
        self:effect("hit_physical", point)
        if name ~= nil then
            local damage, crit = self:roll_damage(w.damage)
            self:deal(name, damage, crit, point, "physical")
        end
    end
end

function FpsPlayer:fire_taser(w, eye, forward)
    local muzzle = self:muzzle_position(eye, forward)
    local point, _, name = self.world:raycast(eye, forward, w.range)
    local finish = point or add(eye, mul(forward, w.range))
    local arc = {r = 0.55, g = 0.8, b = 1.0}
    -- Two wires + a crackling arc along them.
    self.world:spawnBeam(muzzle, finish, {r = 0.85, g = 0.85, b = 0.9}, 0.35, 1, false)
    self.world:spawnBeam(muzzle, finish, arc, 0.3, 2.5, true)
    self.world:spawnBeam(muzzle, finish, arc, 0.18, 1.5, true)
    self.world:spawnFlash(muzzle, arc, 14, 0.12)
    if point ~= nil then
        self.world:spawnFlash(point, arc, 26, 0.3)
        self:effect("impact_electric", point)
        if name ~= nil then
            self.world:stun(name, w.stun * self:bonus(self.weapon_id).damage, {element = "electric"})
            local damage, crit = self:roll_damage(w.damage)
            self:deal(name, damage, crit, point, "electric")
        end
    end
end

-- Casters: a visible projectile simulated by projectiles.lua. Aimed from the
-- orb at whatever the crosshair points at.
function FpsPlayer:fire_spell(w, eye, forward)
    local muzzle = self:muzzle_position(eye, forward)
    local point = self.world:raycast(eye, forward, 120)
    local target = point or add(eye, mul(forward, 120))
    local damage, crit = self:roll_damage(w.damage)
    local spec = {
        origin = muzzle, direction = norm(sub(target, muzzle)), element = w.element, speed = w.speed,
        damage = damage, crit = crit, weapon = self.weapon_id,
        radius = w.radius, burn_seconds = w.burn_seconds, burn_dps = w.burn_dps and w.burn_dps * self:bonus(self.weapon_id).damage,
        freeze_seconds = w.freeze_seconds, splash_radius = w.splash_radius,
        jumps = w.jumps, jump_range = w.jump_range, falloff = w.falloff,
    }
    local handled = self.entity:send("launch_projectile", spec)
    if not handled then
        -- Without projectiles.lua the Storm Caster falls back to its original
        -- instant chain lightning; other casters do a plain instant hit.
        self:warn_once("projectiles", "Attach projectiles.lua to the player (FPS Demo preset) for spell projectiles.")
        if w.element == "electric" then
            self:fire_chain(w, eye, forward)
        else
            local hit_point, _, name = self.world:raycast(eye, forward, 60)
            if name ~= nil then self:deal(name, damage, crit, hit_point, w.element) end
        end
    end
end

-- Life Caster: heal yourself.
function FpsPlayer:cast_heal(w)
    local amount = w.heal * self:bonus(self.weapon_id).damage
    local me = self.entity:getName()
    local handled, healed = self.world:heal(me, amount, {element = "heal"})
    local feet = self.entity:getPosition()
    self:effect("heal_self", feet)
    local eye, forward = self.camera:getAim()
    if eye ~= nil then self:effect("cast_heal", self:muzzle_position(eye, forward)) end
    if not handled then
        self:warn_once("health", "Attach health.lua to the player (tick is_player) so the Life Caster can heal you.")
    elseif (healed or 0) > 0 then
        -- Healing counts as using the weapon for its XP.
        self.entity:send("on_weapon_hit", self.weapon_id, healed, false, false, feet)
    end
end

-- Instant chain lightning (the Storm Caster's fallback without projectiles.lua).
function FpsPlayer:fire_chain(w, eye, forward)
    local muzzle = self:muzzle_position(eye, forward)
    local purple = {r = 0.62, g = 0.55, b = 1.0}
    local white = {r = 0.95, g = 0.95, b = 1.0}

    local first = nil
    local point, _, name = self.world:raycast(eye, forward, 24)
    if name ~= nil then
        for _, t in ipairs(self.world:findDamageable(point, 3.5)) do
            if t.name == name then first = t break end
        end
    end
    if first == nil then
        local best_dot = 0.94
        for _, t in ipairs(self.world:findDamageable(eye, 24)) do
            local to = sub(t.position, eye)
            local d = len(to)
            if d > 1e-3 then
                local alignment = dot(to, forward) / d
                if alignment > best_dot then best_dot = alignment first = t end
            end
        end
    end

    self.world:spawnFlash(muzzle, purple, 22, 0.15)
    if first == nil then
        local finish = point or add(eye, mul(forward, 12))
        self.world:spawnBeam(muzzle, finish, purple, 0.25, 3, true)
        return
    end

    local visited = {}
    local from = muzzle
    local current = first
    local damage, crit = self:roll_damage(w.damage)
    for _ = 0, (w.jumps or 4) do
        if current == nil then break end
        local to = current.position
        self.world:spawnBeam(from, to, purple, 0.35, 3.5, true)
        self.world:spawnBeam(from, to, white, 0.2, 1.2, true)
        self:deal(current.name, damage, crit, to, "electric")
        self.world:stun(current.name, 0.35)
        visited[current.name] = true
        damage = damage * (w.falloff or 0.75)
        from = to
        local next_target = nil
        for _, t in ipairs(self.world:findDamageable(to, w.jump_range or 7)) do
            if not visited[t.name] then next_target = t break end
        end
        current = next_target
    end
end

-- ---------------------------------------------------------------- viewmodel

function FpsPlayer:update_view_motion(dt, moving, sprinting)
    local bob_target = (moving and 1 or 0) * self.view_bob * (sprinting and 1.6 or 1) * (1 - self.ads * 0.8)
    self.bob_amount = approach(self.bob_amount, bob_target, 8, dt)
    if moving then
        self.bob_phase = self.bob_phase + dt * (sprinting and 13 or 9)
    end
    local mdx = self.input:getMouseDeltaX()
    local mdy = self.input:getMouseDeltaY()
    local sway_scale = 1 - self.ads * 0.85
    self.sway_x = approach(self.sway_x, clamp(-mdx * 0.00045, -0.035, 0.035) * sway_scale, 9, dt)
    self.sway_y = approach(self.sway_y, clamp(mdy * 0.00035, -0.03, 0.03) * sway_scale, 9, dt)
    self.land_dip = approach(self.land_dip, 0, 7, dt)
    self.sprint_blend = approach(self.sprint_blend, (sprinting and self.attack_t < 0) and 1 or 0, 8, dt)
end

-- HandR offset/rotation for the current melee swing.
function FpsPlayer:swing_pose()
    if self.attack_t < 0 then return v(0, 0, 0), v(0, 0, 0) end
    local pose = SWINGS[WEAPONS[self.weapon_id].style] or SWINGS.punch
    local t = self.attack_t
    local zero = v(0, 0, 0)
    if t < 0.3 then
        local k = smooth(t / 0.3)
        return vlerp(zero, pose.wind_pos, k), vlerp(zero, pose.wind_rot, k)
    elseif t < 0.55 then
        local k = smooth((t - 0.3) / 0.25)
        return vlerp(pose.wind_pos, pose.hit_pos, k), vlerp(pose.wind_rot, pose.hit_rot, k)
    end
    local k = smooth((t - 0.55) / 0.45)
    return vlerp(pose.hit_pos, zero, k), vlerp(pose.hit_rot, zero, k)
end

function FpsPlayer:update_rig()
    if not self.has_rig then return end
    local show = self.camera_mode == "fps"
    self.world:setEntityActive(self.rig, show)
    if not show then return end

    local eye = self.camera:getAim()
    if eye == nil then return end
    local rotation = self.entity:getRotation()
    self.world:setEntityPosition(self.rig, eye)
    self.world:setEntityRotation(self.rig, v(0, rotation.y, 0))
    self.world:setEntityRotation(self.rig_pitch, v(-self.camera:getPitch(), 0, 0))

    -- Whole-viewmodel motion: walk bob, mouse sway, breathing, landing dip.
    local bob = self.bob_amount
    local breathe = math.sin(self.time * 1.7) * 0.003 * (1 - self.ads)
    self.world:setEntityPosition(self.rig_sway, to_rig(v(
        math.sin(self.bob_phase) * 0.007 * bob + self.sway_x,
        -math.abs(math.cos(self.bob_phase)) * 0.009 * bob + self.sway_y + breathe - self.land_dip,
        0)))

    local id = self.weapon_id or "fists"
    local swing_pos, swing_rot = self:swing_pose()
    local equip = 1 - smooth(self.equip_t)
    local reload = 0
    if self.reload_t >= 0 then reload = math.sin(self.reload_t * math.pi) end
    local recoil = self.recoil

    -- Fists alternate: the swing plays on whichever hand is punching.
    local right_swings = not (id == "fists" and self.punch_left)
    local r_pos = HAND_R_REST
    local r_rot = v(0, 0, 0)
    if right_swings then
        r_pos = add(r_pos, swing_pos)
        r_rot = add(r_rot, swing_rot)
    end
    r_pos = add(r_pos, v(-0.02 * self.sprint_blend, -0.05 * self.sprint_blend - 0.28 * equip - 0.1 * reload,
        -0.03 * self.sprint_blend - 0.05 * recoil))
    r_rot = add(r_rot, v(10 * self.sprint_blend + 45 * equip - 14 * recoil + 20 * reload,
        -30 * self.sprint_blend, 12 * self.sprint_blend + 40 * reload))
    if ADS_OFFSET[id] ~= nil then
        r_pos = add(r_pos, mul(ADS_OFFSET[id], self.ads))
    end
    self.world:setEntityPosition(self.hand_r, to_rig(r_pos))
    self.world:setEntityRotation(self.hand_r, to_rig_rot(r_rot))

    if id == "fists" then
        local l_pos = add(HAND_L_REST, v(0, -0.28 * equip - 0.04 * self.sprint_blend, 0))
        local l_rot = v(10 * self.sprint_blend + 45 * equip, 30 * self.sprint_blend, 0)
        if not right_swings then
            -- Mirror the punch for the left hand.
            l_pos = add(l_pos, v(-swing_pos.x, swing_pos.y, swing_pos.z))
            l_rot = add(l_rot, v(swing_rot.x, -swing_rot.y, -swing_rot.z))
        end
        self.world:setEntityPosition(self.hand_l, to_rig(l_pos))
        self.world:setEntityRotation(self.hand_l, to_rig_rot(l_rot))
    end
end

-- ---------------------------------------------------------------- HUD

-- The engine draws self.hud_text in the Game view's bottom-right corner.
function FpsPlayer:update_hud()
    local w = WEAPONS[self.weapon_id]
    if w == nil then self.hud_text = "" return end
    local level = self:bonus(self.weapon_id).level or 1
    local text = w.label .. "  Lv " .. level
    if w.mag ~= nil then
        local ammo = self:ammo_of(self.weapon_id)
        local max_ammo = self:max_ammo(self.weapon_id)
        if w.recharge ~= nil then
            -- Casters: charge pips.
            text = text .. "   " .. string.rep("|", ammo) .. string.rep(".", math.max(0, max_ammo - ammo))
        else
            text = text .. "   " .. (self.infinite_ammo and "inf" or tostring(ammo)) .. " / " .. max_ammo
            if self.reload_t >= 0 then
                text = text .. "   RELOADING"
            elseif not self.infinite_ammo and ammo <= 0 then
                text = text .. "   [R] reload"
            end
        end
    end
    self.hud_text = text
end

return FpsPlayer

-- @preset FPS Demo | player
