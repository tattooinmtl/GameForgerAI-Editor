-- Makes this object climbable by the FPS Demo player (fps_player.lua): a
-- ladder, a wall, a rock cliff - any object. Add it to as many objects as
-- you like.
--
-- CLIMBING (during Play)
--   Walk into the climbable side with W to grab on.
--   W / S   climb up / down          A / D   move sideways along it
--   Space   jump off                 Climb past the top to pull yourself up.
--   S at the bottom lets go.
--
-- climb_angle (Inspector slider, 0-360) locks WHICH SIDE of the object is
-- climbed - and so which way the player faces while climbing. It turns with
-- the object:
--     0 = its front (+Z side)    90 = its +X side
--   180 = its back  (-Z side)   270 = its -X side
-- Anything in between works too (a slanted side, a round rock).
--
-- The climbable area is the object's box: its scale (the primitive shapes
-- are 2 x 2 x 2 at scale 1), position, pivot and Y rotation.
-- NOTE: colliders ignore rotation (they are position +/- scale boxes), so a
-- TURNED object that is also a Collider can stop the player before its real
-- face. Keep solid climbables unturned, or turn their Collider off.
--
-- @property climb_angle slider 0|360 0
-- @property reach number 0.9

local Climbable = {}

function Climbable:on_start()
    self.climb_angle = self.climb_angle or 0
    self.reach = self.reach or 0.9
end

-- The climbable face, in world space.
function Climbable:face()
    local position = self.entity:getPosition()
    local scale = self.entity:getScale()
    local pivot = self.entity:getPivot()
    local yaw = math.rad(self.entity:getRotation().y)
    local cy, sy = math.cos(yaw), math.sin(yaw)
    -- Local (object) -> world for a horizontal vector, Y rotation only.
    local function turn(x, z) return x * cy + z * sy, -x * sy + z * cy end

    local hx, hy, hz = math.abs(scale.x), math.abs(scale.y), math.abs(scale.z)
    -- The pivot is where `position` sits inside the box.
    local ox, oz = turn(pivot.x * hx, pivot.z * hz)
    local center = {x = position.x - ox, y = position.y - pivot.y * hy, z = position.z - oz}

    local a = math.rad(self.climb_angle % 360)
    local ax, az = math.sin(a), math.cos(a)                 -- outward, object space
    local depth = math.abs(hx * ax) + math.abs(hz * az)     -- center -> face
    local half_width = math.abs(hx * az) + math.abs(hz * ax)
    local nx, nz = turn(ax, az)                             -- outward, world
    return {
        name = self.entity:getName(),
        nx = nx, nz = nz,
        -- The player's right while facing the face (outward normal reversed).
        sx = nz, sz = -nx,
        px = center.x + nx * depth, pz = center.z + nz * depth,
        half_width = half_width,
        bottom = center.y - hy,
        top = center.y + hy,
    }
end

function Climbable:on_update(dt)
    local face = self:face()
    local scale = self.entity:getScale()
    local radius = math.sqrt(scale.x * scale.x + scale.z * scale.z) + math.abs(scale.y) + 3
    local players = self.world:findAllWithTag("Player", self.entity:getPosition(), radius)
    for _, player in ipairs(players) do
        local p = player.position
        local rx, rz = p.x - face.px, p.z - face.pz
        local out = rx * face.nx + rz * face.nz      -- distance in front of the face
        local side = rx * face.sx + rz * face.sz     -- along the face
        if out > -0.35 and out < self.reach and math.abs(side) < face.half_width + 0.3
            and p.y > face.bottom - 0.6 and p.y < face.top + 0.25 then
            self.world:send(player.name, "on_climbable_near", face)
        end
    end
end

return Climbable

-- @preset FPS Demo | climbable
