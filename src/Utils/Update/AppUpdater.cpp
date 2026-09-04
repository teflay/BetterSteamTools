#include "AppUpdater.h"

#include "OpenSteamToolBuildInfo.h"
#include "OSTPlatform/include/DynamicLibrary.h"
#include "OSTPlatform/include/Hash.h"
#include "OSTPlatform/include/Process.h"
#include "Utils/Logging/Log.h"
#include "Utils/SteamMetadata/Mirror.h"

#include <toml++/toml.hpp>

#include <windows.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace AppUpdater {

namespace {
    // Bounds for a sane framework DLL payload — guards against a truncated or hostile
    // response being written to disk. OpenSteamTool.dll is ~1 MB.
    constexpr size_t kMinDllBytes = 200 * 1024;
    constexpr size_t kMaxDllBytes = 8 * 1024 * 1024;

    constexpr const char* kPointerPath = "opensteamtool/latest.toml";

    bool EqualsIgnoreCase(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    std::string EscapeForPowerShellSingleQuoted(std::string s)
    {
        // Inside a PowerShell single-quoted string, ' is escaped by doubling it.
        for (size_t p = s.find('\''); p != std::string::npos; p = s.find('\'', p + 2))
            s.insert(p, 1, '\'');
        return s;
    }
} // namespace

CheckResult Check()
{
    CheckResult r;
    r.oldVersion = OPENSTEAMTOOL_VERSION;
    r.newVersion = OPENSTEAMTOOL_VERSION;
    r.updateAvailable = false;
    r.dllRelPath = "";
    r.sha256 = "";
    
    LOG_INFO("AppUpdater: disabled - update checks are bypassed");
    return r;
}

bool DownloadAndStage(const CheckResult& /*result*/, const std::string& /*selfDllPath*/)
{
    LOG_WARN("AppUpdater: downloads disabled");
    return false;
}

void CleanupStagedBackup(const std::string& selfDllPath)
{
    const std::string backup = selfDllPath + ".old";
    std::error_code ec;
    if (std::filesystem::exists(backup, ec) && DeleteFileA(backup.c_str()))
        LOG_INFO("AppUpdater: removed stale backup {}", backup);
}

void RestartSteam()
{
    const std::filesystem::path steamExe = OSTPlatform::DynamicLibrary::GetMainExecutablePath();
    if (steamExe.empty()) {
        LOG_WARN("AppUpdater: steam.exe path unknown; cannot auto-restart");
        return;
    }

    const std::string exe = EscapeForPowerShellSingleQuoted(steamExe.string());
    // Detached, hidden PowerShell: graceful -shutdown, wait for every steam process to
    // exit (covers steamwebhelper), then relaunch.
    const std::string command =
        "powershell -NoProfile -WindowStyle Hidden -Command "
        "\"& '" + exe + "' -shutdown; "
        "Wait-Process -Name steam -Timeout 30 -ErrorAction SilentlyContinue; "
        "Start-Process '" + exe + "'\"";

    if (OSTPlatform::Process::LaunchDetachedHidden(command))
        LOG_INFO("AppUpdater: restart helper launched");
    else
        LOG_WARN("AppUpdater: restart helper failed to launch; "
                 "update applies on next manual Steam start");
}

} // namespace AppUpdater
