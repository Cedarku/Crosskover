#pragma once

#include <string>
#include <vector>

#include "ClippingStore.h"
#include "HardcoverSyncClient.h"
#include "activities/Activity.h"

/**
 * Pushes a book's highlights (Clippings) to the user's Hardcover reading
 * journal as "quote" entries.
 *
 * Flow:
 * 1. Connect to WiFi (if not connected)
 * 2. Resolve (and cache) the Hardcover book id for this title/author
 * 3. Post any highlight newer than the last successful sync as a quote entry
 * 4. Show a result summary
 *
 * Only ever sends highlights that haven't been sent before (tracked via
 * HardcoverSyncState, keyed by the local book file path), so re-running sync
 * after adding new highlights only uploads the new ones.
 */
class HardcoverSyncActivity final : public Activity {
 public:
  HardcoverSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookFilePath,
                        std::string bookTitle, std::string bookAuthor, std::vector<Clipping> clippings)
      : Activity("HardcoverSync", renderer, mappedInput),
        bookFilePath(std::move(bookFilePath)),
        bookTitle(std::move(bookTitle)),
        bookAuthor(std::move(bookAuthor)),
        clippings(std::move(clippings)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == SYNCING; }

 private:
  enum State { WIFI_SELECTION, CONNECTING, SYNCING, DONE, FAILED, NO_CREDENTIALS, NOTHING_TO_SYNC };

  std::string bookFilePath;
  std::string bookTitle;
  std::string bookAuthor;
  std::vector<Clipping> clippings;

  State state = WIFI_SELECTION;
  std::string errorMessage;
  int totalToSync = 0;
  int syncedCount = 0;

  void onWifiSelectionComplete(bool success);
  void performSync();
};
