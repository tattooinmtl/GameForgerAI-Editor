-- The Game Manager: one per scene, holds the settings of the built game.
-- Select the "Game Manager" object in the Hierarchy and edit these in the
-- Inspector - GameForgerRuntime reads them when it starts the game:
--
--   game_title      the game window's title
--   splash_logo     the picture shown on the boot splash screen. Click
--                   "Change Image..." to pick one from your PC - it's copied
--                   into the game's own Game/Branding/ folder, so it ships
--                   with the game.
--   splash_seconds  how long the splash stays up (0 = no splash)
--   intro_message   shown on screen when the game starts (empty = none)
--
-- Create one with GameObject > Game Manager (Create > FPS Demo Arena adds
-- one for you). File > Build Game makes this scene the one Runtime loads.
--
-- @property game_title string GameForgerAI - Opus FPS Demo
-- @property splash_logo image Game/Branding/logo.jpg
-- @property splash_seconds number 2.5
-- @property intro_message string Grab a weapon (E), open your bag (I), level up to unlock the Fire, Frost and Life Casters!
-- @property intro_seconds number 7

local GameManager = {}

function GameManager:on_start()
    self.game_title = self.game_title or "GameForgerAI Game"
    self.intro_message = self.intro_message or ""
    self.intro_seconds = self.intro_seconds or 6
    if self.intro_message ~= "" then
        self.world:showMessage(self.intro_message, self.intro_seconds)
    end
end

function GameManager:on_update(delta_time)
end

return GameManager

-- @preset FPS Opus | manager
