/**
 * @file src/steam_state.h
 * @brief Steam library discovery and game-state reporting (installed games,
 *        running app, download/update progress) read from Steam's local
 *        appmanifest files. Powers console-style clients that need true
 *        game states without any extra host-side agent.
 */
#pragma once

#include <cstdint>

#include <nlohmann/json.hpp>

namespace steam_state {
  /**
   * @brief List installed Steam games across all library folders.
   * @return JSON: {"games":[{"id","name","installdir","size_on_disk"}]}
   */
  nlohmann::json games();

  /**
   * @brief Current state: the running Steam app and per-game update/download
   *        progress mirrored from appmanifest StateFlags and byte counters.
   * @return JSON: {"running":<appid|0>,
   *                "updates":[{"id","name","flags","state","done","todo","pct"}]}
   */
  nlohmann::json state();

  /**
   * @brief Launch a game via the steam:// protocol.
   */
  bool launch(std::uint64_t appid);

  /**
   * @brief Queue an update/verify for a game via steam://validate.
   */
  bool update(std::uint64_t appid);
}  // namespace steam_state
