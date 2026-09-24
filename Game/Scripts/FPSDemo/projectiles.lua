-- Spell projectiles for the FPS Demo preset (the Storm, Fire and Frost
-- Casters). fps_player.lua launches one with:
--     self.entity:send("launch_projectile", spec)
-- spec = {origin=, direction=, element="fire"|"frost"|"electric",
--         damage=, speed=, crit=bool, weapon="fire", ...element extras}
--
-- Each projectile is simulated here: it flies, leaves a trail, and on
-- contact (a ray swept along its path each frame, so fast bolts never skip
-- through thin targets) applies its element:
--   electric  direct hit + chain lightning to up to `jumps` nearby targets
--   fire      explosion: damage falls off over `radius`, targets burn over time
--   frost     direct hit + freeze (stun) + chill visuals
-- Visuals go through effects.lua ("cast_/trail_/impact_" effects) and every
-- hit is reported to xp_system.lua ("on_weapon_hit").
--
-- @property max_projectiles number 48
-- @property max_lifetime number 3.5
-- @property orb_size number 0.06

local Projectiles = {}

local ORB_COLORS = {
    fire     = {core = {r = 1.0, g = 0.75, b = 0.3},  glow = {r = 1.0, g = 0.35, b = 0.05}},
    frost    = {core = {r = 0.9, g = 0.97, b = 1.0},  glow = {r = 0.35, g = 0.7, b = 1.0}},
    electric = {core = {r = 0.95, g = 0.95, b = 1.0}, glow = {r = 0.55, g = 0.45, b = 1.0}},
}

local function v(x, y, z) return {x = x, y = y, z = z} end
local function add(a, b) return v(a.x + b.x, a.y + b.y, a.z + b.z) end
local function sub(a, b) return v(a.x - b.x, a.y - b.y, a.z - b.z) end
local function mul(a, s) return v(a.x * s, a.y * s, a.z * s) end
local function len(a) return math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z) end
local function norm(a)
    local l = len(a)
    if l < 1e-6 then return v(0, 0, 1) end
    return mul(a, 1 / l)
end

function Projectiles:on_start()
    self.max_projectiles = self.max_projectiles or 48
    self.max_lifetime = self.max_lifetime or 3.5
    self.orb_size = self.orb_size or 0.06
    self.live = {}
    self.projectiles_ready = true
end

function Projectiles:effect(name, position, extra)
    self.entity:send("play_effect", name, position, extra)
end

-- Report damage dealt to xp_system.lua (and anything else listening).
function Projectiles:report(weapon, damage, killed, crit, position)
    self.entity:send("on_weapon_hit", weapon, damage, killed, crit, position)
end

function Projectiles:launch_projectile(spec)
    if #self.live >= self.max_projectiles or spec == nil or spec.origin == nil then
        return false
    end
    local p = {
        pos = v(spec.origin.x, spec.origin.y, spec.origin.z),
        dir = norm(spec.direction or v(0, 0, 1)),
        speed = spec.speed or 30,
        element = spec.element or "electric",
        damage = spec.damage or 20,
        crit = spec.crit or false,
        weapon = spec.weapon or spec.element,
        age = 0,
        spec = spec,
    }
    self.live[#self.live + 1] = p
    self:effect("cast_" .. p.element, p.pos)
    return true
end

-- Damage one target and tell the XP system. Returns true if it died.
function Projectiles:hit(p, name, amount, position, extra_info)
    local info = {element = p.element, crit = p.crit}
    if extra_info then
        for k, value in pairs(extra_info) do info[k] = value end
    end
    local handled, killed = self.world:damage(name, amount, info)
    if handled then
        self:report(p.weapon, amount, killed == true, p.crit, position)
        if p.crit then self:effect("crit", position) end
    end
    return killed == true
end

function Projectiles:impact(p, point, target_name)
    local s = p.spec
    self:effect("impact_" .. p.element, point, {radius = s.radius})

    if p.element == "fire" then
        -- Explosion: everything damageable in the radius, less at the edge,
        -- and set on fire.
        local radius = s.radius or 3
        for _, t in ipairs(self.world:findDamageable(point, radius)) do
            local falloff = 1 - 0.6 * math.min(1, t.distance / radius)
            local amount = p.damage * falloff
            self:hit(p, t.name, amount, t.position)
            self.world:send(t.name, "on_status", "burn", s.burn_seconds or 3, s.burn_dps or 8, "fire")
        end
    elseif p.element == "frost" then
        if target_name ~= nil then
            self:hit(p, target_name, p.damage, point)
            self.world:stun(target_name, s.freeze_seconds or 1.5, {element = "frost"})
            self.world:send(target_name, "on_status", "frost", s.freeze_seconds or 1.5, 0, "frost")
        end
        -- A little splash chill around the impact.
        for _, t in ipairs(self.world:findDamageable(point, s.splash_radius or 1.5)) do
            if t.name ~= target_name then
                self:hit(p, t.name, p.damage * 0.35, t.position)
                self.world:send(t.name, "on_status", "frost", (s.freeze_seconds or 1.5) * 0.5, 0, "frost")
            end
        end
    elseif p.element == "electric" then
        -- Direct hit, then the bolt jumps to the nearest un-hit target.
        local visited = {}
        local from = point
        local current = nil
        if target_name ~= nil then
            local center = self.world:getEntityPosition(target_name) or point
            current = {name = target_name, position = center}
        end
        local amount = p.damage
        local jumps = s.jumps or 4
        local purple = ORB_COLORS.electric.glow
        local white = ORB_COLORS.electric.core
        for jump = 0, jumps do
            if current == nil then break end
            if jump > 0 then
                self.world:spawnBeam(from, current.position, purple, 0.35, 3.5, true)
                self.world:spawnBeam(from, current.position, white, 0.2, 1.2, true)
                self:effect("impact_electric", current.position)
            end
            self:hit(p, current.name, amount, current.position)
            self.world:stun(current.name, 0.35, {element = "electric"})
            visited[current.name] = true
            amount = amount * (s.falloff or 0.75)
            from = current.position
            local next_target = nil
            for _, t in ipairs(self.world:findDamageable(from, s.jump_range or 7)) do
                if not visited[t.name] then next_target = t break end
            end
            current = next_target
        end
    end
end

function Projectiles:on_update(delta_time)
    local dt = math.min(delta_time, 0.05)
    local still = {}
    for _, p in ipairs(self.live) do
        p.age = p.age + dt
        local step = p.speed * dt
        -- Sweep the path covered this frame.
        local point, distance, name = self.world:raycast(p.pos, p.dir, step + 0.05)
        if point ~= nil then
            self:impact(p, point, name)
        elseif p.age >= self.max_lifetime then
            self:effect("fizzle_" .. p.element, p.pos)
        else
            p.pos = add(p.pos, mul(p.dir, step))
            -- Orb: glowing core + halo, plus a trail.
            local colors = ORB_COLORS[p.element] or ORB_COLORS.electric
            local pulse = 1 + 0.15 * math.sin(p.age * 30)
            self.world:particle(p.pos, colors.glow, self.orb_size * 1.7 * pulse, 0.4)
            self.world:particle(p.pos, colors.core, self.orb_size * pulse, 1)
            self:effect("trail_" .. p.element, p.pos)
            if p.element == "electric" and math.random() < 0.35 then
                local jitter = v(math.random() - 0.5, math.random() - 0.5, math.random() - 0.5)
                self.world:spawnBeam(p.pos, add(p.pos, mul(jitter, 0.6)), colors.core, 0.05, 1.2, true)
            end
            still[#still + 1] = p
        end
    end
    self.live = still
end

return Projectiles

-- @preset FPS Demo | player
