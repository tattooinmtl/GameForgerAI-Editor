-- FPS Demo kit enemy: wanders near its spawn, chases whatever carries
-- target_tag (default "Player") once it's within search_radius, and attacks
-- it - anything with the kit's health.lua takes the damage. Put the kit's
-- health.lua on it too so the player's weapons can defeat it.
--
-- One script, set up per object in the Inspector:
--   attack_style  melee  - wind up (`windup` seconds - the player's parry
--                          window), then strike within attack_range
--                 ranged - keeps about keep_distance away and throws spears
--                          (projectile_speed) from up to attack_range away
--   has_shield    blocks shield_block of the damage from hits in front of it
--                 (not while attacking or stunned - hit it then, or from
--                 behind)
--   knockback     pushes the player away on a hit (the Orc Warlord)
--   is_boss       shows a big health bar (boss_title) at the top of the screen
--                 while it's fighting you
--   body_name     the humanoid body (built by GameObject > FPS Demo Arena)
--                 it animates: walk, run, attack, throw, block, stunned.
--                 Empty = no body (a plain capsule, like the Chasers).
--   model_scale   that body's size (1 = the player's size)
--
-- Stuns (the kit's taser, frost, lightning - and the player's shield PARRY)
-- stop it and cancel its attack. Shows the red "detected!" icon while
-- chasing (self.detected).
--
-- (This is the kit's own copy - Game/Scripts/enemy_ai.lua is the small
-- starter version to build your own enemies from.)
--
-- @property attack_style enum melee|ranged melee
-- @property damage number 8
-- @property attack_range number 1.5
-- @property attack_cooldown number 1.2
-- @property windup number 0.3
-- @property chase_speed number 3.5
-- @property wander_speed number 2
-- @property search_radius number 8
-- @property escape_radius number 14
-- @property keep_distance number 0
-- @property projectile_speed number 14
-- @property has_shield bool false
-- @property shield_block number 0.6
-- @property knockback number 0
-- @property is_boss bool false
-- @property boss_title string Boss
-- @property body_name string
-- @property model_scale number 1

local Enemy = {}

local function v(x, y, z) return {x = x, y = y, z = z} end
local function clamp(x, lo, hi) return math.max(lo, math.min(hi, x)) end
local function lerp(a, b, t) return a + (b - a) * t end
local function approach(current, target, rate, dt) return lerp(current, target, 1 - math.exp(-rate * dt)) end
local function smooth(t) t = clamp(t, 0, 1) return t * t * (3 - 2 * t) end

local function distance_xz(ax, az, bx, bz)
    local dx = ax - bx
    local dz = az - bz
    return math.sqrt(dx * dx + dz * dz)
end

-- Joint groups of the humanoid body (FpsRigBuilder.cpp buildHumanoid).
local JOINTS = {"Hips", "Spine", "Head", "ShoulderR", "ShoulderL", "ElbowR", "ElbowL", "HipR", "HipL", "KneeR", "KneeL"}
local HIPS_HEIGHT = 0.93
local RECOVER_SECONDS = 0.4
local SPEAR_GRAVITY = -9

function Enemy:on_start()
    self.target_tag = "Player"
    self.attack_style = self.attack_style or "melee"
    self.damage = self.damage or 8
    self.attack_range = self.attack_range or 1.5
    self.attack_cooldown = self.attack_cooldown or 1.2
    self.windup = math.max(0.05, self.windup or 0.3)
    self.chase_speed = self.chase_speed or 3.5
    self.wander_speed = self.wander_speed or 2
    self.search_radius = self.search_radius or 8
    self.escape_radius = math.max(self.escape_radius or 14, self.search_radius)
    self.keep_distance = self.keep_distance or 0
    self.projectile_speed = self.projectile_speed or 14
    if self.has_shield == nil then self.has_shield = false end
    self.shield_block = clamp(self.shield_block or 0.6, 0, 1)
    self.knockback = self.knockback or 0
    if self.is_boss == nil then self.is_boss = false end
    self.boss_title = self.boss_title or "Boss"
    self.body_name = self.body_name or ""
    self.model_scale = self.model_scale or 1
    self.wander_radius = 6.0
    self.gravity = -18.0

    local position = self.entity:getPosition()
    self.spawn_x = position.x
    self.spawn_z = position.z
    self.state = "wander"
    self.detected = 0
    self.velocity_y = 0.0
    self.wander_target_x = position.x
    self.wander_target_z = position.z
    self.wander_wait_remaining = 0.0
    self.attack_timer = 0.0
    self.attack_t = -1          -- -1 = not attacking; else seconds into the attack
    self.struck = false
    self.stunned_time = 0
    self.block_anim = 0
    self.spears = {}
    self.spear_hidden = 0
    self.boss_bar_shown = false
    self.time = 0

    local scale = self.entity:getScale()
    self.collider_radius = math.max(scale.x, scale.z)
    self.collider_height = scale.y * 2.0

    self.has_body = self.body_name ~= "" and self.world:getEntityPosition(self.body_name) ~= nil
    self.weapon_node = self.body_name .. ".Weapon"
    self.walk_phase = math.random() * 6
    self.move_speed = 0
    self.pose = {}
    self.sent_pose = {}
    self.drop = 0
end

function Enemy:pick_new_wander_target()
    local angle = math.random() * math.pi * 2.0
    local radius = math.random() * self.wander_radius
    self.wander_target_x = self.spawn_x + math.cos(angle) * radius
    self.wander_target_z = self.spawn_z + math.sin(angle) * radius
    self.wander_wait_remaining = 1.0 + math.random() * 2.0
end

-- Called by self.world:stun(name, seconds) - the taser, frost, lightning
-- and the player's shield parry. Stops it and cancels its attack.
function Enemy:on_stun(seconds, attacker_name)
    self.stunned_time = math.max(self.stunned_time or 0.0, seconds or 0)
    self.attack_t = -1
end

-- The goblins' shields: health.lua asks before a hit lands. Blocks part of
-- a hit from the front while guarding (not attacking, not stunned).
function Enemy:modify_incoming_damage(amount, attacker_name, info)
    if not self.has_shield or self.stunned_time > 0 or self.attack_t >= 0 then
        return nil
    end
    local source = (info and info.from) or (attacker_name and self.world:getEntityPosition(attacker_name))
    if source == nil then return nil end
    local position = self.entity:getPosition()
    local dx, dz = source.x - position.x, source.z - position.z
    local d = math.sqrt(dx * dx + dz * dz)
    if d < 1e-3 then return nil end
    local forward = self.entity:getForward()
    if (dx * forward.x + dz * forward.z) / d < 0.35 then
        return nil -- from the side or behind
    end
    self.block_anim = 0.45
    local shield = self.world:getEntityPosition(self.body_name .. ".Shield") or position
    self.world:spawnFlash(shield, {r = 0.95, g = 0.85, b = 0.55}, 16, 0.12)
    self.world:spawnText(v(shield.x, shield.y + 0.3, shield.z), "BLOCK", {r = 0.8, g = 0.85, b = 0.9}, 0.6, 0.8)
    return amount * (1 - self.shield_block)
end

-- health.lua calls this when it dies.
function Enemy:on_death(attacker_name)
    self:hide_boss_bar()
    if self.is_boss then
        self.world:showMessage(self.boss_title .. " is defeated!", 3)
    end
end

function Enemy:hide_boss_bar()
    if self.boss_bar_shown then
        self.world:clearHudBar("boss")
        self.boss_bar_shown = false
    end
end

function Enemy:update_boss_bar(fighting)
    if not self.is_boss then return end
    if not fighting then
        self:hide_boss_bar()
        return
    end
    local _, life = self.entity:send("get_health")
    if type(life) ~= "table" then return end
    self.world:setHudBar("boss", self.boss_title .. "   " .. math.ceil(life.health) .. " / " .. math.floor(life.max),
        life.health / math.max(life.max, 1), {r = 0.85, g = 0.32, b = 0.12}, 100)
    self.boss_bar_shown = true
end

-- ---------------------------------------------------------------- attacks

function Enemy:target_alive(name)
    local _, dead = self.world:send(name, "is_dead")
    return dead ~= true
end

function Enemy:facing_dot(target_position)
    local position = self.entity:getPosition()
    local dx, dz = target_position.x - position.x, target_position.z - position.z
    local d = math.sqrt(dx * dx + dz * dz)
    if d < 1e-3 then return 1 end
    local forward = self.entity:getForward()
    return (dx * forward.x + dz * forward.z) / d
end

function Enemy:strike(target_position, target_name)
    local position = self.entity:getPosition()
    local reach = distance_xz(position.x, position.z, target_position.x, target_position.z)
    if reach > self.attack_range * 1.25 + 0.3 or self:facing_dot(target_position) < 0.3 then
        return -- stepped out of reach / behind it: a miss
    end
    local handled = self.world:damage(target_name, self.damage, {element = "physical", from = position})
    if handled then
        local chest = v(target_position.x, target_position.y + 1.1, target_position.z)
        self.world:spawnFlash(chest, {r = 1.0, g = 0.2, b = 0.15}, self.is_boss and 34 or 22, 0.15)
        if self.knockback > 0 and reach > 1e-3 then
            local dx, dz = (target_position.x - position.x) / reach, (target_position.z - position.z) / reach
            self.world:send(target_name, "on_knockback", dx, dz, self.knockback)
        end
    end
    if self.is_boss then
        -- The club hits the ground: a ring of dust.
        local forward = self.entity:getForward()
        local s = self.model_scale
        local ground = v(position.x + forward.x * 1.6 * s, position.y - self.collider_height * 0.5 + 0.05,
            position.z + forward.z * 1.6 * s)
        for i = 1, 10 do
            local a = i / 10 * math.pi * 2
            self.world:particle(v(ground.x + math.cos(a) * 0.6, ground.y, ground.z + math.sin(a) * 0.6),
                {r = 0.55, g = 0.48, b = 0.38}, 0.18, 0.5)
        end
    end
end

function Enemy:throw_spear(target_position)
    local hand = self.world:getEntityPosition(self.weapon_node)
    local position = self.entity:getPosition()
    local from = hand or v(position.x, position.y + self.collider_height * 0.3, position.z)
    local aim = v(target_position.x, target_position.y + 1.1, target_position.z)
    local dx, dy, dz = aim.x - from.x, aim.y - from.y, aim.z - from.z
    local flat = math.sqrt(dx * dx + dz * dz)
    local t = math.max(flat / self.projectile_speed, 0.05)
    -- Lob it: enough upward speed to land on the target under gravity.
    self.spears[#self.spears + 1] = {
        pos = from,
        vel = v(dx / t, dy / t - 0.5 * SPEAR_GRAVITY * t, dz / t),
        age = 0,
    }
    self.spear_hidden = 0.7
    if self.has_body then self.world:setEntityActive(self.weapon_node, false) end
end

function Enemy:update_spears(dt, target_position, target_name)
    local still = {}
    for _, s in ipairs(self.spears) do
        s.age = s.age + dt
        local old = s.pos
        s.vel.y = s.vel.y + SPEAR_GRAVITY * dt
        local new = v(old.x + s.vel.x * dt, old.y + s.vel.y * dt, old.z + s.vel.z * dt)
        local speed = math.sqrt(s.vel.x * s.vel.x + s.vel.y * s.vel.y + s.vel.z * s.vel.z)
        local hit_target = false
        if target_position ~= nil and target_name ~= nil then
            -- The target as a standing capsule: feet to head, 0.45 wide.
            for k = 1, 3 do
                local q = v(lerp(old.x, new.x, k / 3), lerp(old.y, new.y, k / 3), lerp(old.z, new.z, k / 3))
                if q.y > target_position.y - 0.1 and q.y < target_position.y + 1.9
                    and distance_xz(q.x, q.z, target_position.x, target_position.z) < 0.45 then
                    self.world:damage(target_name, self.damage, {element = "physical", from = q})
                    self.world:spawnFlash(q, {r = 1.0, g = 0.3, b = 0.2}, 18, 0.12)
                    hit_target = true
                    break
                end
            end
        end
        local blocked = false
        if not hit_target and speed > 1e-3 then
            local dir = v(s.vel.x / speed, s.vel.y / speed, s.vel.z / speed)
            local point = self.world:raycast(old, dir, speed * dt + 0.05)
            if point ~= nil then
                blocked = true
                self.world:spawnFlash(point, {r = 0.7, g = 0.6, b = 0.45}, 12, 0.15)
            end
        end
        if not hit_target and not blocked and s.age < 3 and new.y > -1 then
            s.pos = new
            -- The spear in flight: a wooden shaft line + steel tip.
            local tail = v(new.x - s.vel.x / speed * 0.7, new.y - s.vel.y / speed * 0.7, new.z - s.vel.z / speed * 0.7)
            self.world:spawnBeam(tail, new, {r = 0.55, g = 0.38, b = 0.2}, 0.03, 2.5, false)
            self.world:particle(new, {r = 0.85, g = 0.86, b = 0.9}, 0.04, 0.05)
            still[#still + 1] = s
        end
    end
    self.spears = still
end

-- ---------------------------------------------------------------- update

function Enemy:on_update(delta_time)
    local dt = math.min(delta_time, 0.05)
    self.time = self.time + dt
    local position = self.entity:getPosition()
    local stunned = self.stunned_time > 0.0
    if stunned then
        self.stunned_time = math.max(0, self.stunned_time - dt)
    end
    self.block_anim = math.max(0, self.block_anim - dt)
    self.attack_timer = math.max(0.0, self.attack_timer - delta_time)
    if self.spear_hidden > 0 then
        self.spear_hidden = self.spear_hidden - dt
        if self.spear_hidden <= 0 and self.has_body then self.world:setEntityActive(self.weapon_node, true) end
    end

    local target_position, target_distance, target_name = self.world:findNearestWithTag(self.target_tag)
    if target_name ~= nil and not self:target_alive(target_name) then
        target_position, target_name = nil, nil
    end
    self:update_spears(dt, target_position, target_name)
    -- Boss bar: shown while the player is within its escape radius and it has noticed them.
    local in_fight = target_position ~= nil and target_distance <= self.escape_radius
        and (self.state == "chase" or stunned)
    self:update_boss_bar(in_fight)

    local move_x, move_z, speed = 0.0, 0.0, 0.0
    local face_x, face_z = nil, nil
    if stunned then
        target_position = nil
        self.state = "wander"
        self.wander_wait_remaining = math.max(self.wander_wait_remaining, 0.5)
    end

    if self.state == "wander" then
        self.detected = 0
        if target_position ~= nil and target_distance <= self.search_radius then
            self.state = "chase"
        else
            local to_target_dist = distance_xz(position.x, position.z, self.wander_target_x, self.wander_target_z)
            if stunned then
                -- frozen in place
            elseif to_target_dist < 0.3 then
                if self.wander_wait_remaining > 0.0 then
                    self.wander_wait_remaining = self.wander_wait_remaining - delta_time
                else
                    self:pick_new_wander_target()
                end
            else
                move_x = (self.wander_target_x - position.x) / to_target_dist
                move_z = (self.wander_target_z - position.z) / to_target_dist
                speed = self.wander_speed
            end
        end
    end

    local to_target = nil
    if self.state == "chase" then
        if target_position == nil or target_distance > self.escape_radius then
            self.state = "wander"
            self.detected = 0
            self.wander_wait_remaining = 0.0
            self.attack_t = -1
        else
            self.detected = 1
            to_target = distance_xz(position.x, position.z, target_position.x, target_position.z)
            local dir_x, dir_z = 0, 0
            if to_target > 1e-3 then
                dir_x = (target_position.x - position.x) / to_target
                dir_z = (target_position.z - position.z) / to_target
            end
            face_x, face_z = dir_x, dir_z

            if self.attack_t >= 0 then
                -- Mid-attack: stand, keep turning to the target during the wind-up.
                self.attack_t = self.attack_t + dt
                if self.attack_t > self.windup then face_x, face_z = nil, nil end
                if not self.struck and self.attack_t >= self.windup then
                    self.struck = true
                    if self.attack_style == "ranged" then
                        self:throw_spear(target_position)
                    else
                        self:strike(target_position, target_name)
                    end
                end
                if self.attack_t >= self.windup + RECOVER_SECONDS then
                    self.attack_t = -1
                    self.attack_timer = self.attack_cooldown
                end
            else
                if self.attack_style == "ranged" and self.keep_distance > 0 then
                    -- Keep its distance: back off if too close, close in if too far.
                    if to_target < self.keep_distance - 1.0 then
                        move_x, move_z, speed = -dir_x, -dir_z, self.chase_speed * 0.8
                    elseif to_target > self.keep_distance + 1.5 then
                        move_x, move_z, speed = dir_x, dir_z, self.chase_speed
                    end
                elseif to_target > self.attack_range * 0.8 then
                    move_x, move_z, speed = dir_x, dir_z, self.chase_speed
                end
                if to_target <= self.attack_range and self.attack_timer <= 0.0
                    and self:facing_dot(target_position) > 0.6 then
                    self.attack_t = 0
                    self.struck = false
                    move_x, move_z, speed = 0, 0, 0
                end
            end
        end
    end

    -- Face where it's going (or its target while fighting).
    if face_x == nil and self.attack_t < 0 and (move_x ~= 0.0 or move_z ~= 0.0) then
        face_x, face_z = move_x, move_z
    end
    if face_x ~= nil and (face_x ~= 0 or face_z ~= 0) then
        local target_yaw = math.deg(math.atan(face_x, face_z))
        local yaw = self.entity:getRotation().y
        local delta = (target_yaw - yaw + 180) % 360 - 180
        local turn = (self.is_boss and 240 or 540) * dt
        yaw = yaw + clamp(delta, -turn, turn)
        self.entity:setRotation({x = 0.0, y = yaw, z = 0.0})
    end

    self.velocity_y = self.velocity_y + self.gravity * delta_time

    -- self.entity:getPosition()/setPosition() is this object's center,
    -- self.physics:resolve() expects a feet/base position.
    local half_height = self.collider_height * 0.5
    position.x = position.x + move_x * speed * delta_time
    position.z = position.z + move_z * speed * delta_time
    position.y = position.y + self.velocity_y * delta_time

    local feet = {x = position.x, y = position.y - half_height, z = position.z}
    local resolved, grounded = self.physics:resolve(feet, self.collider_radius, self.collider_height)
    position.x = resolved.x
    position.z = resolved.z
    position.y = resolved.y + half_height

    if grounded then
        self.velocity_y = 0.0
    end
    -- Fallback world floor for scenes with no other Collider-enabled objects.
    if position.y <= half_height then
        position.y = half_height
        self.velocity_y = 0.0
    end

    self.entity:setPosition(position)
    self.move_speed = speed
    self.guarding = self.has_shield and to_target ~= nil and to_target < 3.5 and self.attack_t < 0 and not stunned
    self:animate(dt, stunned)
end

-- ---------------------------------------------------------------- body animation

-- Target pose (degrees per joint + how far the hips drop). A hanging limb
-- swings BACK with +X, forward with -X; the spine and head tilt forward
-- with +X. The body's right side is -X.
function Enemy:body_pose(stunned)
    local pose = {}
    for _, joint in ipairs(JOINTS) do pose[joint] = v(0, 0, 0) end
    local drop = 0
    local speed = self.move_speed / math.max(self.model_scale, 0.3)
    local gait = clamp(speed / 2.5, 0, 1.6)
    local run = clamp((gait - 1) / 0.6, 0, 1)
    local s = math.sin(self.walk_phase)

    if stunned then
        local wobble = math.sin(self.time * 9) * 4
        pose.Head = v(30, wobble, 0)
        pose.Spine = v(22, 0, wobble)
        pose.ShoulderR = v(8, 0, -4)
        pose.ShoulderL = v(8, 0, 4)
        pose.KneeR = v(20, 0, 0)
        pose.KneeL = v(20, 0, 0)
        pose.HipR = v(-12, 0, 0)
        pose.HipL = v(-12, 0, 0)
        return pose, 0.05
    end

    if gait > 0.05 then
        local leg = 30 * math.min(gait, 1) + 18 * run
        pose.HipR = v(-leg * s, 0, 0)
        pose.HipL = v(leg * s, 0, 0)
        pose.KneeR = v(6 + (25 + 40 * run) * math.max(0, -s), 0, 0)
        pose.KneeL = v(6 + (25 + 40 * run) * math.max(0, s), 0, 0)
        pose.ShoulderR = v(leg * 0.7 * s, 0, -6)
        pose.ShoulderL = v(-leg * 0.7 * s, 0, 6)
        pose.ElbowR = v(-15 - 45 * run, 0, 0)
        pose.ElbowL = v(-15 - 45 * run, 0, 0)
        pose.Spine = v(5 + 10 * run, 0, 0)
        drop = (0.02 + 0.03 * run) * math.abs(math.cos(self.walk_phase))
    else
        local breathe = math.sin(self.time * 1.6)
        pose.Spine = v(2 + 1.5 * breathe, 0, 0)
        pose.ShoulderR = v(2 * breathe, 0, -8)
        pose.ShoulderL = v(2 * breathe, 0, 8)
        pose.ElbowR = v(-12, 0, 0)
        pose.ElbowL = v(-12, 0, 0)
        pose.KneeR = v(8, 0, 0)
        pose.KneeL = v(8, 0, 0)
        pose.HipR = v(-5, 0, 0)
        pose.HipL = v(-5, 0, 0)
    end
    if self.is_boss then
        -- Heavy: hunched, knees bent.
        pose.Spine = v(pose.Spine.x + 12, 0, 0)
        pose.Head = v(-10, 0, 0)
    end

    -- Weapon arm held ready.
    pose.ShoulderR = v(pose.ShoulderR.x - 15, 0, -10)
    pose.ElbowR = v(-35, 0, 0)

    -- Shield arm: carried in front, raised to guard / block.
    if self.has_shield then
        if self.guarding or self.block_anim > 0 then
            pose.ShoulderL = v(-55, 0, 12)
            pose.ElbowL = v(-80, 0, 0)
        else
            pose.ShoulderL = v(-20, 0, 8)
            pose.ElbowL = v(-60, 0, 0)
        end
    end

    -- Attacks.
    if self.attack_t >= 0 then
        local wind = smooth(self.attack_t / self.windup)
        local after = self.attack_t - self.windup
        if self.attack_style == "ranged" then
            -- Throw: arm cocked back and up, then whips forward.
            if after < 0 then
                pose.ShoulderR = v(lerp(-15, -150, wind), 0, lerp(-10, -25, wind))
                pose.ElbowR = v(lerp(-35, -75, wind), 0, 0)
                pose.Spine = v(lerp(pose.Spine.x, -10, wind), 0, 0)
            else
                local k = smooth(after / 0.15)
                pose.ShoulderR = v(lerp(-150, -55, k), 0, -10)
                pose.ElbowR = v(lerp(-75, -5, k), 0, 0)
                pose.Spine = v(lerp(-10, 15, k), 0, 0)
            end
        else
            -- Overhead chop / club smash.
            if after < 0 then
                pose.ShoulderR = v(lerp(-15, -165, wind), 0, -10)
                pose.ElbowR = v(lerp(-35, -45, wind), 0, 0)
                pose.Spine = v(lerp(pose.Spine.x, -10, wind), 0, 0)
                if self.is_boss then pose.ShoulderL = v(lerp(0, -140, wind), 0, 20) end
            else
                local k = smooth(after / 0.14)
                pose.ShoulderR = v(lerp(-165, -30, k), 0, -10)
                pose.ElbowR = v(lerp(-45, -8, k), 0, 0)
                pose.Spine = v(lerp(-10, 22, k), 0, 0)
                if self.is_boss then
                    pose.ShoulderL = v(lerp(-140, -40, k), 0, 20)
                    drop = drop + 0.08 * k
                end
            end
        end
    end
    return pose, drop
end

function Enemy:animate(dt, stunned)
    if not self.has_body then return end
    if self.move_speed > 0 then
        self.walk_phase = self.walk_phase + dt * (4 + 1.3 * self.move_speed) / math.sqrt(self.model_scale)
    end
    local target_pose, drop = self:body_pose(stunned)
    self.drop = approach(self.drop, drop, 14, dt)
    self.world:setEntityPosition(self.body_name .. ".Hips",
        v(0, (HIPS_HEIGHT - self.drop) * self.model_scale, 0))
    -- Attacks snap faster than walking.
    local rate = self.attack_t >= 0 and 26 or 12
    for _, joint in ipairs(JOINTS) do
        local current = self.pose[joint] or v(0, 0, 0)
        local goal = target_pose[joint]
        current = v(approach(current.x, goal.x, rate, dt), approach(current.y, goal.y, rate, dt),
            approach(current.z, goal.z, rate, dt))
        self.pose[joint] = current
        local sent = self.sent_pose[joint]
        if sent == nil or math.abs(sent.x - current.x) + math.abs(sent.y - current.y)
            + math.abs(sent.z - current.z) > 0.05 then
            self.world:setEntityRotation(self.body_name .. "." .. joint, current)
            self.sent_pose[joint] = current
        end
    end
end

return Enemy

-- @preset FPS Demo | enemy
