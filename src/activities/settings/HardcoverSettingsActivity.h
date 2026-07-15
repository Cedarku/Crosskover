#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Submenu for Hardcover account linking. Shows the API token field and a
 * "Link account" action. Mirrors KOReaderSettingsActivity.
 */
class HardcoverSettingsActivity final : public Activity {
 public:
  explicit HardcoverSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HardcoverSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  size_t selectedIndex = 0;

  void handleSelection();
};
