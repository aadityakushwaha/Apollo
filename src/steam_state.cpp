/**
 * @file src/steam_state.cpp
 * @brief Steam library discovery and game-state reporting.
 */
#include "steam_state.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "logging.h"
#include "platform/common.h"

#ifdef _WIN32
  #include <windows.h>

// User-session helpers from platform/windows/misc.cpp (no public header).
namespace platf {
  HANDLE retrieve_users_token(bool elevated);
  std::error_code impersonate_current_user(HANDLE user_token, std::function<void()> callback);
  bool is_running_as_system();
}  // namespace platf
#endif

namespace fs = std::filesystem;

namespace steam_state {

  namespace {

    // Steam manifest StateFlags bits (per appmanifest_*.acf)
    constexpr std::uint32_t STATE_FULLY_INSTALLED = 4;
    constexpr std::uint32_t STATE_UPDATE_REQUIRED = 2;
    constexpr std::uint32_t STATE_UPDATE_RUNNING = 1024;

    /**
     * @brief Extract flat "key" "value" pairs from Valve KeyValues text.
     *        First occurrence of a key wins, matching the top-level manifest
     *        fields we care about.
     */
    std::unordered_map<std::string, std::string> parse_kv(const fs::path &file) {
      std::unordered_map<std::string, std::string> kv;
      std::ifstream in(file);
      if (!in) {
        return kv;
      }
      static const std::regex pair_re {R"regex("([^"]+)"\s+"([^"]*)")regex"};
      std::string line;
      while (std::getline(in, line)) {
        std::smatch m;
        if (std::regex_search(line, m, pair_re)) {
          kv.emplace(m[1].str(), m[2].str());  // emplace: keep first occurrence
        }
      }
      return kv;
    }

    std::uint64_t to_u64(const std::string &s) {
      try {
        return s.empty() ? 0 : std::stoull(s);
      } catch (...) {
        return 0;
      }
    }

    /**
     * @brief Locate the Steam install root for the current platform.
     */
    fs::path steam_root() {
#ifdef _WIN32
      // Per-user path is authoritative; machine-wide install path is the fallback.
      char buf[MAX_PATH] {};
      DWORD size = sizeof(buf);
      if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS && buf[0]) {
        return fs::path {buf};
      }
      size = sizeof(buf);
      if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS && buf[0]) {
        return fs::path {buf};
      }
      return fs::path {"C:\\Program Files (x86)\\Steam"};
#elif defined(__APPLE__)
      const char *home = std::getenv("HOME");
      return fs::path {home ? home : ""} / "Library/Application Support/Steam";
#else
      const char *home = std::getenv("HOME");
      fs::path h {home ? home : ""};
      if (fs::exists(h / ".steam/steam")) {
        return h / ".steam/steam";
      }
      return h / ".local/share/Steam";
#endif
    }

    /**
     * @brief All steamapps folders: the root one plus every extra library
     *        listed in libraryfolders.vdf.
     */
    std::vector<fs::path> steamapps_dirs() {
      std::vector<fs::path> dirs;
      auto root = steam_root() / "steamapps";
      std::error_code ec;
      if (fs::exists(root, ec)) {
        dirs.emplace_back(root);
      }

      std::ifstream in(root / "libraryfolders.vdf");
      if (in) {
        static const std::regex path_re {R"regex("path"\s+"([^"]+)")regex"};
        std::string line;
        while (std::getline(in, line)) {
          std::smatch m;
          if (std::regex_search(line, m, path_re)) {
            // VDF escapes backslashes
            std::string p = std::regex_replace(m[1].str(), std::regex {R"(\\\\)"}, R"(\)");
            fs::path lib = fs::path {p} / "steamapps";
            if (fs::exists(lib, ec) && lib != root) {
              dirs.emplace_back(lib);
            }
          }
        }
      }
      return dirs;
    }

    /**
     * @brief Parse every appmanifest_*.acf across all libraries.
     */
    std::vector<std::unordered_map<std::string, std::string>> manifests() {
      std::vector<std::unordered_map<std::string, std::string>> out;
      std::error_code ec;
      for (const auto &dir : steamapps_dirs()) {
        for (const auto &entry : fs::directory_iterator {dir, ec}) {
          const auto name = entry.path().filename().string();
          if (name.rfind("appmanifest_", 0) == 0 && entry.path().extension() == ".acf") {
            auto kv = parse_kv(entry.path());
            if (!kv["appid"].empty()) {
              out.emplace_back(std::move(kv));
            }
          }
        }
      }
      return out;
    }

    std::uint64_t running_appid() {
#ifdef _WIN32
      DWORD appid = 0;
      DWORD size = sizeof(appid);
      if (platf::is_running_as_system()) {
        // Service context: HKCU is SYSTEM's hive, where Steam never writes.
        // Read the console user's hive via an impersonation token instead.
        HANDLE token = platf::retrieve_users_token(false);
        if (!token) {
          return 0;
        }
        platf::impersonate_current_user(token, [&]() {
          HKEY hkcu = nullptr;
          if (RegOpenCurrentUser(KEY_READ, &hkcu) == ERROR_SUCCESS) {
            RegGetValueA(hkcu, "Software\\Valve\\Steam", "RunningAppID", RRF_RT_REG_DWORD, nullptr, &appid, &size);
            RegCloseKey(hkcu);
          }
        });
        CloseHandle(token);
        return appid;
      }
      if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "RunningAppID", RRF_RT_REG_DWORD, nullptr, &appid, &size) == ERROR_SUCCESS) {
        return appid;
      }
#endif
      // ponytail: no per-platform process scan; Windows registry covers the
      // shipped host. Add scanning if Linux/macOS hosts ever need it.
      return 0;
    }

  }  // namespace

  nlohmann::json games() {
    nlohmann::json out;
    out["games"] = nlohmann::json::array();
    for (auto &kv : manifests()) {
      nlohmann::json g;
      g["id"] = to_u64(kv["appid"]);
      g["name"] = kv["name"];
      g["installdir"] = kv["installdir"];
      g["size_on_disk"] = to_u64(kv["SizeOnDisk"]);
      out["games"].push_back(std::move(g));
    }
    return out;
  }

  nlohmann::json state() {
    nlohmann::json out;
    out["running"] = running_appid();
    out["updates"] = nlohmann::json::array();
    for (auto &kv : manifests()) {
      const auto flags = static_cast<std::uint32_t>(to_u64(kv["StateFlags"]));
      if (flags == STATE_FULLY_INSTALLED) {
        continue;  // current, nothing to report
      }
      nlohmann::json u;
      u["id"] = to_u64(kv["appid"]);
      u["name"] = kv["name"];
      u["flags"] = flags;
      u["downloading"] = (flags & STATE_UPDATE_RUNNING) != 0;
      u["update_required"] = (flags & STATE_UPDATE_REQUIRED) != 0;
      const auto todo = to_u64(kv["BytesToDownload"]);
      const auto done = to_u64(kv["BytesDownloaded"]);
      u["todo"] = todo;
      u["done"] = done;
      u["pct"] = todo > 0 ? static_cast<int>(done * 100 / todo) : 0;
      u["scheduled"] = to_u64(kv["ScheduledAutoUpdate"]);
      out["updates"].push_back(std::move(u));
    }
    return out;
  }

  // platf::open_url launches in the console user's session even when we run
  // as the service (raw ShellExecute would fire in session 0 and go nowhere).
  bool launch(std::uint64_t appid) {
    BOOST_LOG(info) << "steam_state: launching appid " << appid;
    platf::open_url("steam://rungameid/" + std::to_string(appid));
    return true;
  }

  bool update(std::uint64_t appid) {
    BOOST_LOG(info) << "steam_state: validating appid " << appid;
    platf::open_url("steam://validate/" + std::to_string(appid));
    return true;
  }

}  // namespace steam_state
