-- Audio Manager
--
-- Background music plus a shared handle on the audio engine for every other
-- script. Attach to ONE entity per scene.
--
-- self.audio is available in EVERY script, not just this one:
--   self.audio:play(clipPath, volume, loop)   -- clipPath relative to the project, e.g. "Game/Audio/hit.wav"
--   self.audio:stop()                          -- stops everything
--   self.audio:setMasterVolume(0.0 .. 1.0)
--   self.audio:isPlaying()
--
-- Put sound files in Game/Audio - the Audio panel's "Import Sound from PC..."
-- copies them there for you.

local AudioManager = {}

function AudioManager:on_start()
    -- Tunables. Edit them here; they are read fresh each Play session.
    self.music_clip = "Game/Audio/music.wav"
    self.music_volume = 0.5
    self.music_loops = true
    self.master_volume = 1.0

    self.managers:register("audio_manager")

    self.audio:setMasterVolume(self.master_volume)

    -- A missing file is not an error worth stopping Play for - the engine
    -- logs it once and stays silent, so a scene without music still runs.
    if self.music_clip ~= "" then
        self.audio:play(self.music_clip, self.music_volume, self.music_loops)
    end
end

function AudioManager:on_update(delta_time)
    -- Nothing per-frame. Looping is handled by the audio engine itself, so
    -- there is no need to poll isPlaying() and restart.
end

-- Stop the music when Play stops. Without this the track would keep going
-- after the session ended, since the engine outlives a single Play run.
function AudioManager:on_end()
    self.managers:unregister("audio_manager")
    self.audio:stop()
end

return AudioManager
