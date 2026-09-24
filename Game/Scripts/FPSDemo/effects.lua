-- Visual effects for the FPS Demo preset: a small particle system plus a
-- library of named effects (spell casts, trails, elemental impacts, heals,
-- level-ups, critical hits). Pure visuals - no damage happens here.
--
-- Other scripts on the same object play an effect with:
--     self.entity:send("play_effect", "impact_fire", position)
-- Names: cast_<element>, trail_<element>, impact_<element>, heal_self,
-- level_up, crit, hit_physical, fizzle_<element>, burn_tick, frost_tick
-- where <element> is fire, frost, electric or heal.
--
-- Particles are drawn with self.world:particle (one frame each), so this
-- script moves them itself every frame: position, velocity, gravity, drag,
-- shrinking and fading. Tweak the look in the ELEMENTS table below.
--
-- @property max_particles number 500
-- @property quality enum low|medium|high high
-- @property intensity number 1

local Effects = {}

local ELEMENTS = {
    fire     = {core = {r = 1.0, g = 0.62, b = 0.18}, glow = {r = 1.0, g = 0.28, b = 0.04}, spark = {r = 1.0, g = 0.9, b = 0.5}},
    frost    = {core = {r = 0.72, g = 0.9, b = 1.0},  glow = {r = 0.3, g = 0.62, b = 1.0},  spark = {r = 0.95, g = 1.0, b = 1.0}},
    electric = {core = {r = 0.78, g = 0.82, b = 1.0}, glow = {r = 0.55, g = 0.45, b = 1.0}, spark = {r = 1.0, g = 1.0, b = 1.0}},
    heal     = {core = {r = 0.5, g = 1.0, b = 0.6},   glow = {r = 0.15, g = 0.85, b = 0.35}, spark = {r = 0.9, g = 1.0, b = 0.8}},
    physical = {core = {r = 1.0, g = 0.85, b = 0.55}, glow = {r = 0.9, g = 0.6, b = 0.3},  spark = {r = 1.0, g = 1.0, b = 0.9}},
    gold     = {core = {r = 1.0, g = 0.85, b = 0.3},  glow = {r = 1.0, g = 0.6, b = 0.1},  spark = {r = 1.0, g = 1.0, b = 0.7}},
}

local QUALITY_SCALE = {low = 0.35, medium = 0.65, high = 1.0}

local function v(x, y, z) return {x = x, y = y, z = z} end
local function rnd(a, b) return a + (b - a) * math.random() end
local function random_dir()
    local z = rnd(-1, 1)
    local a = rnd(0, math.pi * 2)
    local r = math.sqrt(1 - z * z)
    return v(r * math.cos(a), z, r * math.sin(a))
end
local function mix(a, b, t) return {r = a.r + (b.r - a.r) * t, g = a.g + (b.g - a.g) * t, b = a.b + (b.b - a.b) * t} end

function Effects:on_start()
    self.max_particles = self.max_particles or 500
    self.quality = self.quality or "high"
    self.intensity = self.intensity or 1
    self.particles = {}
    self.effects_ready = true -- other scripts can check the preset is present
end

-- One particle. p = {pos, vel, color, color_end, size, life, gravity, drag, grow}
function Effects:emit(p)
    if #self.particles >= self.max_particles then return end
    p.age = 0
    p.life = p.life or 0.6
    p.gravity = p.gravity or 0
    p.drag = p.drag or 0
    p.grow = p.grow or -0.6
    p.color_end = p.color_end or p.color
    self.particles[#self.particles + 1] = p
end

-- Many particles flying out from `center`. `alpha` (optional) makes them
-- translucent - smoke and mist use ~0.3.
function Effects:burst(center, count, speed, color, color_end, size, life, gravity, drag, upward, alpha)
    local n = math.floor(count * (QUALITY_SCALE[self.quality] or 1) * self.intensity + 0.5)
    for _ = 1, n do
        local d = random_dir()
        if upward then d.y = math.abs(d.y) * upward end
        local s = speed * rnd(0.35, 1.0)
        self:emit({
            pos = v(center.x, center.y, center.z),
            vel = v(d.x * s, d.y * s, d.z * s),
            color = color, color_end = color_end,
            size = size * rnd(0.6, 1.3), life = life * rnd(0.6, 1.2),
            gravity = gravity, drag = drag, alpha = alpha,
        })
    end
end

-- A flat expanding ring of particles on the XZ plane.
function Effects:ring(center, radius, color, count, speed, life, size)
    local n = math.floor(count * (QUALITY_SCALE[self.quality] or 1) + 0.5)
    for i = 1, n do
        local a = (i / n) * math.pi * 2
        self:emit({
            pos = v(center.x + math.cos(a) * radius * 0.2, center.y + 0.05, center.z + math.sin(a) * radius * 0.2),
            vel = v(math.cos(a) * speed, 0.2, math.sin(a) * speed),
            color = color, size = size or 0.05, life = life or 0.45, drag = 3.5, grow = -0.8,
        })
    end
end

-- Radial electric arcs.
function Effects:arcs(center, count, length, color, seconds)
    for _ = 1, count do
        local d = random_dir()
        local len = length * rnd(0.5, 1.0)
        self.world:spawnBeam(center, v(center.x + d.x * len, center.y + d.y * len, center.z + d.z * len), color,
            seconds or 0.15, 1.6, true)
    end
end

function Effects:play_effect(name, position, extra)
    if position == nil then return end
    local p = position
    local e = function(key) return ELEMENTS[key] or ELEMENTS.physical end

    -- casting (at the weapon's muzzle / orb)
    if name == "cast_fire" then
        local c = e("fire")
        self.world:spawnFlash(p, c.glow, 26, 0.12)
        self:burst(p, 12, 2.2, c.core, c.glow, 0.012, 0.35, 1.5, 1.5)
    elseif name == "cast_frost" then
        local c = e("frost")
        self.world:spawnFlash(p, c.glow, 22, 0.1)
        self:burst(p, 10, 2.5, c.spark, c.glow, 0.015, 0.3, 0, 2)
    elseif name == "cast_electric" then
        local c = e("electric")
        self.world:spawnFlash(p, c.glow, 24, 0.1)
        self:arcs(p, 3, 0.35, c.core, 0.1)
    elseif name == "cast_heal" then
        local c = e("heal")
        self.world:spawnFlash(p, c.glow, 24, 0.2)
        self:burst(p, 10, 1.2, c.core, c.glow, 0.02, 0.5, 1.0, 1.0)

    -- trails (called every frame per projectile)
    elseif name == "trail_fire" then
        local c = e("fire")
        self:emit({pos = v(p.x, p.y, p.z), vel = v(rnd(-0.4, 0.4), rnd(0.2, 0.9), rnd(-0.4, 0.4)),
            color = c.core, color_end = {r = 0.3, g = 0.3, b = 0.3}, size = rnd(0.022, 0.04), life = rnd(0.25, 0.45), drag = 1.5, alpha = 0.8})
    elseif name == "trail_frost" then
        local c = e("frost")
        self:emit({pos = v(p.x, p.y, p.z), vel = v(rnd(-0.3, 0.3), rnd(-0.3, 0.3), rnd(-0.3, 0.3)),
            color = c.spark, color_end = c.glow, size = rnd(0.02, 0.04), life = rnd(0.2, 0.4), drag = 2})
    elseif name == "trail_electric" then
        local c = e("electric")
        self:emit({pos = v(p.x, p.y, p.z), vel = v(rnd(-1.5, 1.5), rnd(-1.5, 1.5), rnd(-1.5, 1.5)),
            color = c.spark, color_end = c.glow, size = rnd(0.015, 0.03), life = rnd(0.08, 0.18), drag = 3})
    elseif name == "trail_heal" then
        local c = e("heal")
        self:emit({pos = v(p.x, p.y, p.z), vel = v(0, rnd(0.2, 0.6), 0), color = c.core, size = 0.03, life = 0.4})

    -- impacts
    elseif name == "impact_fire" then
        local c = e("fire")
        local radius = (extra and extra.radius) or 3
        self.world:spawnFlash(p, c.glow, 70, 0.28)
        self.world:spawnFlash(p, c.core, 34, 0.18)
        self:burst(p, 55, 6.5, c.spark, c.glow, 0.028, 0.65, -4, 1.4)                       -- fireball
        self:burst(p, 22, 1.6, {r = 0.35, g = 0.33, b = 0.32}, {r = 0.15, g = 0.15, b = 0.15},
            0.075, 1.4, 1.2, 1.0, 1, 0.3)                                                    -- smoke rising
        self:ring(p, radius, c.glow, 32, radius * 2.2, 0.45, 0.03)
    elseif name == "impact_frost" then
        local c = e("frost")
        self.world:spawnFlash(p, c.glow, 50, 0.25)
        self:burst(p, 40, 7, c.spark, c.glow, 0.02, 0.55, -12, 0.5)                      -- shards
        self:burst(p, 18, 0.8, {r = 0.85, g = 0.95, b = 1.0}, {r = 0.6, g = 0.8, b = 1.0}, 0.07, 1.3, 0.3, 1.2, nil, 0.3) -- mist
        self:ring(p, 2, c.core, 28, 3.5, 0.5, 0.028)
    elseif name == "impact_electric" then
        local c = e("electric")
        self.world:spawnFlash(p, c.glow, 56, 0.2)
        self.world:spawnFlash(p, c.spark, 22, 0.12)
        self:burst(p, 45, 9, c.spark, c.glow, 0.013, 0.32, -14, 0.8)                       -- sparks
        self:arcs(p, 6, 1.2, c.core, 0.2)
    elseif name == "impact_heal" or name == "heal_self" then
        local c = e("heal")
        self.world:spawnFlash(v(p.x, p.y + 1, p.z), c.glow, 40, 0.3)
        self:ring(p, 1.5, c.core, 30, 2.5, 0.6, 0.028)
        for i = 1, math.floor(40 * (QUALITY_SCALE[self.quality] or 1)) do                  -- rising spiral
            local a = i * 0.9
            local r = rnd(0.3, 0.6)
            self:emit({pos = v(p.x + math.cos(a) * r, p.y + rnd(0, 0.6), p.z + math.sin(a) * r),
                vel = v(-math.sin(a) * 0.6, rnd(1.2, 2.4), math.cos(a) * 0.6),
                color = c.core, color_end = c.spark, size = rnd(0.018, 0.032), life = rnd(0.8, 1.3), drag = 0.5})
        end
    elseif name == "fizzle_fire" or name == "fizzle_frost" or name == "fizzle_electric" then
        local c = e(string.sub(name, 8))
        self:burst(p, 12, 1.5, c.core, c.glow, 0.03, 0.35, 0, 2)

    -- feedback
    elseif name == "hit_physical" then
        local c = e("physical")
        self:burst(p, 10, 4, c.spark, c.glow, 0.015, 0.25, -12, 0.5)
    elseif name == "crit" then
        local c = e("gold")
        self.world:spawnFlash(p, c.core, 30, 0.2)
        self:burst(p, 18, 3.5, c.spark, c.core, 0.015, 0.45, -3, 1)
    elseif name == "level_up" then
        local c = e("gold")
        self.world:spawnFlash(v(p.x, p.y + 1, p.z), c.core, 60, 0.4)
        self:ring(p, 2.5, c.core, 48, 4, 0.7, 0.032)
        for _ = 1, math.floor(60 * (QUALITY_SCALE[self.quality] or 1)) do
            self:emit({pos = v(p.x + rnd(-0.5, 0.5), p.y + rnd(0, 0.3), p.z + rnd(-0.5, 0.5)),
                vel = v(rnd(-0.3, 0.3), rnd(2.5, 5), rnd(-0.3, 0.3)),
                color = c.spark, color_end = c.glow, size = rnd(0.018, 0.035), life = rnd(0.8, 1.4), drag = 0.8})
        end
    elseif name == "burn_tick" then
        local c = e("fire")
        self:burst(p, 6, 1.2, c.core, {r = 0.3, g = 0.3, b = 0.3}, 0.03, 0.5, 2, 1, 1, 0.7)
    elseif name == "frost_tick" then
        local c = e("frost")
        self:burst(p, 4, 0.6, c.spark, c.glow, 0.03, 0.5, -1, 1)
    end
end

function Effects:on_update(delta_time)
    local dt = math.min(delta_time, 0.05)
    local alive = {}
    for _, p in ipairs(self.particles) do
        p.age = p.age + dt
        if p.age < p.life then
            p.vel.y = p.vel.y + p.gravity * dt
            local damp = math.max(0, 1 - p.drag * dt)
            p.vel.x, p.vel.y, p.vel.z = p.vel.x * damp, p.vel.y * damp, p.vel.z * damp
            p.pos.x = p.pos.x + p.vel.x * dt
            p.pos.y = p.pos.y + p.vel.y * dt
            p.pos.z = p.pos.z + p.vel.z * dt
            local t = p.age / p.life
            local size = p.size * math.max(0.05, 1 + p.grow * t)
            local fade = (t < 0.1) and (t / 0.1) or (1 - (t - 0.1) / 0.9)
            self.world:particle(p.pos, mix(p.color, p.color_end, t), size, fade * (p.alpha or 1))
            alive[#alive + 1] = p
        end
    end
    self.particles = alive
end

return Effects

-- @preset FPS Demo | player
