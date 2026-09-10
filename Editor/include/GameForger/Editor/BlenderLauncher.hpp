#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gameforger::editor
{
	// Locates blender.exe on Windows and launches it with the MCP addon
	// enabled + its HTTP server started, so BlenderClient can immediately
	// connect on 127.0.0.1:8765. Windows-only (Inno Setup + winget flow is
	// also Windows-only).
	//
	// Detection order (first hit wins):
	//   1. Environment variable GAMEFORGER_BLENDER_EXE (test/override hook).
	//   2. Registry: HKLM\SOFTWARE\Classes\blendfile\shell\open\command
	//      default value - the shell open verb Blender itself registers.
	//   3. Glob: %ProgramFiles%\Blender Foundation\Blender *\blender.exe
	//      sorted descending so a 5.2 install beats a 4.2 install.
	//   4. %PATH% search.
	//
	// Launch model: we always spawn a fresh Blender process (v1 does not try
	// to attach to a Blender the user already had open). Duplicate-launch
	// prevention is the UI's job - the top-menu "Start Blender" item is
	// greyed out while isRunning() reports true.
	class BlenderLauncher final
	{
	public:
		struct LaunchOptions
		{
			// Port passed to bpy.ops.blender_mcp.start_server via prefs. If 0,
			// the addon uses its own default (8765). Kept explicit so the
			// panel can drive the same value that BlenderClient::Config uses.
			int port = 8765;
			// Optional auth token to pre-write into addon prefs. Empty = no auth.
			std::string authToken;
			// If non-empty, override the detected blender.exe (for the UI's
			// "Browse..." field).
			std::filesystem::path blenderExeOverride;
			// Extra CLI args appended after --python-expr, e.g. a scene to open.
			std::vector<std::string> extraArgs;
		};

		BlenderLauncher();
		~BlenderLauncher();

		BlenderLauncher(const BlenderLauncher&) = delete;
		BlenderLauncher& operator=(const BlenderLauncher&) = delete;

		// Returns the first blender.exe found by the detection order above,
		// or nullopt if none. Cached; call refreshDetection() to re-scan.
		[[nodiscard]] std::optional<std::filesystem::path> detectBlenderExe();

		[[nodiscard]] bool isBlenderInstalled() { return detectBlenderExe().has_value(); }

		// Force re-run of the detection order (e.g. after the user installs
		// Blender via winget from inside the panel).
		void refreshDetection();

		// Spawns blender.exe. Returns true on success; on failure, `error`
		// holds a human-readable reason (path not found, CreateProcess failed).
		bool start(const LaunchOptions& options, std::string& error);

		// Best-effort terminate of the last-launched process. Not a graceful
		// shutdown - if the user has unsaved work in Blender, they lose it.
		// The panel should confirm before calling this.
		void terminate();

		// True while the last-launched process handle is still open and the
		// process has not exited.
		[[nodiscard]] bool isRunning();

		// Fire-and-forget: shell to `winget install --id BlenderFoundation.Blender`.
		// Returns true if the shell request was accepted (not that install
		// actually succeeded). Called by the panel's "Install Blender..." button.
		static bool launchWingetInstall();

		// Opens the Blender download page in the default browser as a fallback
		// for machines without winget.
		static void openDownloadPageInBrowser();

	private:
		// Cached most-recently-detected path (may be stale if user just installed).
		std::optional<std::filesystem::path> cachedBlenderExe_;

		// Opaque per-platform handle so we don't leak <windows.h> types
		// through the public header.
		struct ProcessRecord;
		std::unique_ptr<ProcessRecord> process_;
	};
}
