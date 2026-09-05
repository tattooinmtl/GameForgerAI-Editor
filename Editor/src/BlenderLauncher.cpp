#include "GameForger/Editor/BlenderLauncher.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "Advapi32.lib")   // Registry
#pragma comment(lib, "Shell32.lib")    // ShellExecute

namespace gameforger::editor
{
	namespace
	{
		std::wstring widen(const std::string& value)
		{
			if (value.empty())
			{
				return {};
			}
			const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
			std::wstring result(static_cast<std::size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
			return result;
		}

		std::string narrow(const std::wstring& value)
		{
			if (value.empty())
			{
				return {};
			}
			const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
			std::string result(static_cast<std::size_t>(size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
			return result;
		}

		std::optional<std::filesystem::path> detectFromEnv()
		{
			wchar_t buffer[MAX_PATH];
			const DWORD size = GetEnvironmentVariableW(L"GAMEFORGER_BLENDER_EXE", buffer, MAX_PATH);
			if (size == 0 || size >= MAX_PATH)
			{
				return std::nullopt;
			}
			std::filesystem::path path(buffer);
			std::error_code ec;
			if (std::filesystem::exists(path, ec))
			{
				return path;
			}
			return std::nullopt;
		}

		// The blendfile ShellOpen command usually looks like:
		//   "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" "%1"
		// We need to strip the arg and unquote the exe path.
		std::optional<std::filesystem::path> parseShellOpenCommand(const std::wstring& raw)
		{
			std::wstring exe;
			if (!raw.empty() && raw.front() == L'"')
			{
				const std::size_t end = raw.find(L'"', 1);
				if (end == std::wstring::npos)
				{
					return std::nullopt;
				}
				exe = raw.substr(1, end - 1);
			}
			else
			{
				const std::size_t space = raw.find(L' ');
				exe = space == std::wstring::npos ? raw : raw.substr(0, space);
			}
			std::error_code ec;
			std::filesystem::path path(exe);
			if (std::filesystem::exists(path, ec))
			{
				return path;
			}
			return std::nullopt;
		}

		std::optional<std::filesystem::path> detectFromRegistry()
		{
			HKEY key = nullptr;
			LONG openResult = RegOpenKeyExW(HKEY_CLASSES_ROOT, L"blendfile\\shell\\open\\command",
				0, KEY_READ | KEY_WOW64_64KEY, &key);
			if (openResult != ERROR_SUCCESS)
			{
				openResult = RegOpenKeyExW(HKEY_CLASSES_ROOT, L"blendfile\\shell\\open\\command",
					0, KEY_READ, &key);
			}
			if (openResult != ERROR_SUCCESS)
			{
				return std::nullopt;
			}
			DWORD type = 0;
			DWORD sizeBytes = 0;
			LONG queryResult = RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &sizeBytes);
			if (queryResult != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || sizeBytes == 0)
			{
				RegCloseKey(key);
				return std::nullopt;
			}
			std::wstring raw(sizeBytes / sizeof(wchar_t), L'\0');
			queryResult = RegQueryValueExW(key, nullptr, nullptr, &type,
				reinterpret_cast<LPBYTE>(raw.data()), &sizeBytes);
			RegCloseKey(key);
			if (queryResult != ERROR_SUCCESS)
			{
				return std::nullopt;
			}
			// Trim trailing NUL(s) that REG_SZ often leaves in place.
			while (!raw.empty() && raw.back() == L'\0')
			{
				raw.pop_back();
			}
			return parseShellOpenCommand(raw);
		}

		std::optional<std::filesystem::path> detectFromProgramFilesGlob()
		{
			wchar_t buffer[MAX_PATH];
			const DWORD size = GetEnvironmentVariableW(L"ProgramFiles", buffer, MAX_PATH);
			if (size == 0 || size >= MAX_PATH)
			{
				return std::nullopt;
			}
			const std::filesystem::path root = std::filesystem::path(buffer) / L"Blender Foundation";
			std::error_code ec;
			if (!std::filesystem::is_directory(root, ec))
			{
				return std::nullopt;
			}
			// Collect every ".../Blender X.Y/blender.exe" and pick the highest-versioned name.
			std::vector<std::filesystem::path> candidates;
			for (const auto& entry : std::filesystem::directory_iterator(root, ec))
			{
				if (ec) break;
				if (!entry.is_directory(ec)) continue;
				std::filesystem::path exe = entry.path() / L"blender.exe";
				if (std::filesystem::exists(exe, ec))
				{
					candidates.push_back(std::move(exe));
				}
			}
			if (candidates.empty())
			{
				return std::nullopt;
			}
			// Descending lexicographic sort - Blender's own version directory
			// naming ("Blender 4.2", "Blender 5.2") sorts correctly this way
			// until they hit "Blender 10.x", at which point this needs a real
			// version parser. Deferred until it actually matters.
			std::sort(candidates.begin(), candidates.end(),
				[](const auto& a, const auto& b) { return a.wstring() > b.wstring(); });
			return candidates.front();
		}

		std::optional<std::filesystem::path> detectFromPath()
		{
			wchar_t buffer[MAX_PATH];
			const DWORD size = SearchPathW(nullptr, L"blender.exe", nullptr, MAX_PATH, buffer, nullptr);
			if (size == 0 || size >= MAX_PATH)
			{
				return std::nullopt;
			}
			return std::filesystem::path(buffer);
		}

		// Escape a string so it survives passing through Python's parser as a
		// string literal (used inside --python-expr). We quote with double
		// quotes and escape only the characters that would break out of a
		// double-quoted Python string literal.
		std::string escapePythonStringLiteral(const std::string& value)
		{
			std::string out;
			out.reserve(value.size() + 4);
			for (const char c : value)
			{
				switch (c)
				{
					case '\\': out += "\\\\"; break;
					case '"':  out += "\\\""; break;
					case '\n': out += "\\n"; break;
					case '\r': out += "\\r"; break;
					default:   out += c; break;
				}
			}
			return out;
		}

		// CreateProcessW wants the command-line inside a MUTABLE buffer.
		// Callers wrap the string into a std::wstring and pass .data().
		std::wstring buildCommandLine(const std::filesystem::path& exe,
		                              const BlenderLauncher::LaunchOptions& options)
		{
			// The Python one-liner:
			//   import bpy
			//   prefs = bpy.context.preferences.addons.get('blender_mcp_addon')
			//   if prefs is None:
			//     bpy.ops.preferences.addon_enable(module='blender_mcp_addon')
			//     prefs = bpy.context.preferences.addons.get('blender_mcp_addon')
			//   prefs.preferences.port = <port>
			//   prefs.preferences.auth_token = "<token>"
			//   bpy.ops.blender_mcp.start_server()
			//
			// Compressed onto one line with semicolons (Python allows this for
			// simple statements). No triple-quoted strings needed since we
			// escape any embedded double quotes.
			std::ostringstream python;
			python << "import bpy; "
			       << "p = bpy.context.preferences.addons.get('blender_mcp_addon'); "
			       << "bpy.ops.preferences.addon_enable(module='blender_mcp_addon') if p is None else None; "
			       << "p = bpy.context.preferences.addons.get('blender_mcp_addon'); ";
			if (options.port > 0)
			{
				python << "p.preferences.port = " << options.port << "; ";
			}
			if (!options.authToken.empty())
			{
				python << "p.preferences.auth_token = \"" << escapePythonStringLiteral(options.authToken) << "\"; ";
			}
			python << "bpy.ops.blender_mcp.start_server()";

			// Build "exe" --python-expr "python..." [extras...]
			std::wstring cmd;
			cmd.reserve(exe.wstring().size() + 256);
			cmd += L"\"";
			cmd += exe.wstring();
			cmd += L"\" --python-expr \"";
			cmd += widen(python.str());
			cmd += L"\"";
			for (const std::string& extra : options.extraArgs)
			{
				cmd += L" ";
				cmd += widen(extra);
			}
			return cmd;
		}
	}

	struct BlenderLauncher::ProcessRecord
	{
		HANDLE processHandle = nullptr;
		HANDLE threadHandle = nullptr;
		DWORD  processId = 0;

		~ProcessRecord()
		{
			if (threadHandle != nullptr) CloseHandle(threadHandle);
			if (processHandle != nullptr) CloseHandle(processHandle);
		}
	};

	BlenderLauncher::BlenderLauncher() = default;
	BlenderLauncher::~BlenderLauncher() = default;

	std::optional<std::filesystem::path> BlenderLauncher::detectBlenderExe()
	{
		if (cachedBlenderExe_.has_value())
		{
			return cachedBlenderExe_;
		}
		if (auto path = detectFromEnv())                { cachedBlenderExe_ = *path; return cachedBlenderExe_; }
		if (auto path = detectFromRegistry())           { cachedBlenderExe_ = *path; return cachedBlenderExe_; }
		if (auto path = detectFromProgramFilesGlob())   { cachedBlenderExe_ = *path; return cachedBlenderExe_; }
		if (auto path = detectFromPath())               { cachedBlenderExe_ = *path; return cachedBlenderExe_; }
		return std::nullopt;
	}

	void BlenderLauncher::refreshDetection()
	{
		cachedBlenderExe_.reset();
	}

	bool BlenderLauncher::start(const LaunchOptions& options, std::string& error)
	{
		error.clear();
		if (isRunning())
		{
			error = "Blender is already running (via this launcher).";
			return false;
		}

		std::filesystem::path exe = options.blenderExeOverride;
		if (exe.empty())
		{
			auto detected = detectBlenderExe();
			if (!detected.has_value())
			{
				error = "blender.exe was not found. Install Blender or set GAMEFORGER_BLENDER_EXE.";
				return false;
			}
			exe = *detected;
		}
		std::error_code ec;
		if (!std::filesystem::exists(exe, ec))
		{
			error = "Selected blender.exe does not exist: " + exe.string();
			return false;
		}

		std::wstring cmdLine = buildCommandLine(exe, options);

		STARTUPINFOW si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};

		// CREATE_NEW_PROCESS_GROUP so a Ctrl-Break here doesn't cascade into Blender.
		const BOOL ok = CreateProcessW(
			exe.wstring().c_str(),   // lpApplicationName
			cmdLine.data(),          // lpCommandLine (must be mutable)
			nullptr, nullptr,
			FALSE,                   // bInheritHandles
			CREATE_NEW_PROCESS_GROUP,
			nullptr,                 // lpEnvironment
			exe.parent_path().empty() ? nullptr : exe.parent_path().wstring().c_str(),
			&si,
			&pi);
		if (!ok)
		{
			std::ostringstream err;
			err << "CreateProcessW failed (GetLastError=" << GetLastError() << ").";
			error = err.str();
			return false;
		}
		process_ = std::make_unique<ProcessRecord>();
		process_->processHandle = pi.hProcess;
		process_->threadHandle  = pi.hThread;
		process_->processId     = pi.dwProcessId;
		return true;
	}

	void BlenderLauncher::terminate()
	{
		if (!process_ || process_->processHandle == nullptr)
		{
			return;
		}
		TerminateProcess(process_->processHandle, 0);
		WaitForSingleObject(process_->processHandle, 2000);
		process_.reset();
	}

	bool BlenderLauncher::isRunning()
	{
		if (!process_ || process_->processHandle == nullptr)
		{
			return false;
		}
		const DWORD wait = WaitForSingleObject(process_->processHandle, 0);
		if (wait == WAIT_TIMEOUT)
		{
			return true;
		}
		// Process exited (or the wait failed). Drop the handle so the next
		// isRunning() query is cheap.
		process_.reset();
		return false;
	}

	bool BlenderLauncher::launchWingetInstall()
	{
		// ShellExecuteW returns > 32 on success. The user may be prompted for
		// consent - fine; that's the whole point of this flow.
		const HINSTANCE result = ShellExecuteW(
			nullptr, L"open", L"winget.exe",
			L"install --id BlenderFoundation.Blender -e --accept-source-agreements --accept-package-agreements",
			nullptr, SW_SHOW);
		return reinterpret_cast<INT_PTR>(result) > 32;
	}

	void BlenderLauncher::openDownloadPageInBrowser()
	{
		ShellExecuteW(nullptr, L"open", L"https://www.blender.org/download/", nullptr, nullptr, SW_SHOWNORMAL);
	}
}
