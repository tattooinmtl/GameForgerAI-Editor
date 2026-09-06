-- Game Manager
--
-- Owns session-wide state that is not any single object's business. Attach it
-- to ONE entity in the scene (an empty cube is fine - it is never rendered
-- meaningfully, it just needs somewhere to live).
--
-- Why this exists: cursor lock used to be a "Lock Cursor" checkbox on EVERY
-- entity's Inspector, which made no sense - the cursor is a property of the
-- session, not of a crate. Now whoever is driving the player asks for it.
--
-- Available here:
--   self.gameManager:setCursorLock(true|false)
--   self.managers:register(name) / :unregister(name) / :has(name) / :list()
--   self.audio:play(clip, volume, loop) / :stop() / :setMasterVolume(v) / :isPlaying()

local GameManager = {}

function GameManager:on_start()
    -- Tunables, editable in the script editor like every other preset.
    self.lock_cursor_on_start = true

    self.managers:register("game_manager")

    if self.lock_cursor_on_start then
        self.gameManager:setCursorLock(true)
    end
end

function GameManager:on_update(delta_time)
    -- Nothing per-frame yet. The controllers ask for cursor lock themselves
    -- when they start, so this stays empty until there is real session state
    -- to tick (score, game phase, and so on).
end

-- Called when Play stops, the script is detached, or the scene is torn down.
-- Releasing here matters: without it the registry would still list this
-- manager on the next Play session and the cursor would lock with nothing
-- driving it.
function GameManager:on_end()
    self.managers:unregister("game_manager")
    self.gameManager:setCursorLock(false)
end

return GameManager
