#pragma once

#include "activities/Activity.h"

/**
 * Activity for validating a Hardcover API token.
 * Connects to WiFi and calls HardcoverSyncClient::authenticate().
 * Mirrors KOReaderAuthActivity.
 */
class HardcoverAuthActivity final : public Activity {
 public:
  explicit HardcoverAuthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HardcoverAuth", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == AUTHENTICATING; }

 private:
  enum State { WIFI_SELECTION, CONNECTING, AUTHENTICATING, SUCCESS, FAILED };

  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string errorMessage;

  void onWifiSelectionComplete(bool success);
  void performAuthentication();
};
