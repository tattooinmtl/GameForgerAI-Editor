-- Cube movement script
local speed = 200  -- movement speed in pixels per second

function love.load()
    cube = {x = 400, y = 300, size = 50}
end

function love.update(dt)
    -- accumulate movement from all directions before applying delta time
    local dx, dy = 0, 0
    if love.keyboard.isDown('a') then dx = dx - speed end
    if love.keyboard.isDown('d') then dx = dx + speed end
    if love.keyboard.isDown('w') then dy = dy - speed end
    if love.keyboard.isDown('s') then dy = dy + speed end
    cube.x = cube.x + dx * dt
    cube.y = cube.y + dy * dt
end

function love.draw()
    love.graphics.rectangle('fill', cube.x, cube.y, cube.size, cube.size)
end