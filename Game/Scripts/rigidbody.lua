-- Rigidbody: makes this object fall under gravity and land on/collide with
-- Collider-enabled objects, using the same self.physics:resolve() AABB
-- system as the FPS/Third-Person controllers. Attaching this also marks the
-- object itself as a Collider (done by the Add Script preset, not this
-- script), so once it lands, other things - the player, other rigidbodies -
-- collide with it too.
--
-- Unlike the player capsule, this can be any primitive at any scale, so its
-- collision box is derived from the object's own scale via self.entity:
-- getScale() rather than a fixed size.
--
-- Matches this project's script convention (see player_controller.lua):
-- on_start() runs once when Play starts, on_update(delta_time) runs every
-- frame. self.entity, self.input, self.camera, and self.physics are attached
-- by the engine before on_start() runs.

local Rigidbody = {}

function Rigidbody:on_start()
    self.gravity = -18.0
    self.velocity_y = 0.0

    local scale = self.entity:getScale()
    self.collider_radius = math.max(scale.x, scale.z)
    self.collider_height = scale.y * 2.0
end

function Rigidbody:on_update(delta_time)
    self.velocity_y = self.velocity_y + self.gravity * delta_time

    -- self.entity:getPosition()/setPosition() is this object's center (how
    -- primitives are rendered), but self.physics:resolve() expects a feet/
    -- base position (the bottom of the box) - convert down before resolving
    -- and back up after, so the object doesn't render sunk into the ground.
    local half_height = self.collider_height * 0.5
    local position = self.entity:getPosition()
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
end

return Rigidbody
