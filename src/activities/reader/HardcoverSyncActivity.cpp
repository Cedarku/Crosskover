#include "HardcoverSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>

#include "HardcoverCredentialStore.h"
#include "HardcoverSyncState.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void HardcoverSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    {
      RenderLock lock(*this);
      state = FAILED;
      errorMessage = tr(STR_WIFI_CONN_FAILED);
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = SYNCING;
  }
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
    LOG_ERR("HCSync", "Sync screen could not be rendered synchronously; aborting sync");
    RenderLock lock(*this);
    state = FAILED;
    errorMessage = tr(STR_HARDCOVER_SERVER_ERROR);
    requestUpdate(true);
    return;
  }

  performSync();
}

void HardcoverSyncActivity::performSync() {
  if (!HARDCOVER_STORE.hasToken()) {
    RenderLock lock(*this);
    state = NO_CREDENTIALS;
    requestUpdate();
    return;
  }

  int64_t bookId = 0;
  uint32_t lastSyncedTimestamp = 0;
  HardcoverSyncState::load(bookFilePath, bookId, lastSyncedTimestamp);

  if (bookId == 0) {
    const auto err = HardcoverSyncClient::findBookId(bookTitle, bookAuthor, bookId);
    if (err != HardcoverSyncClient::OK || bookId == 0) {
      RenderLock lock(*this);
      state = FAILED;
      errorMessage = HardcoverSyncClient::errorString(err == HardcoverSyncClient::OK ? HardcoverSyncClient::NOT_FOUND
                                                                                     : err);
      requestUpdate();
      return;
    }
  }

  // Sort so we save progress (and can resume) in chronological order.
  std::vector<Clipping> pending;
  pending.reserve(clippings.size());
  for (const auto& clipping : clippings) {
    if (clipping.timestamp > lastSyncedTimestamp) pending.push_back(clipping);
  }
  std::sort(pending.begin(), pending.end(),
           [](const Clipping& a, const Clipping& b) { return a.timestamp < b.timestamp; });

  totalToSync = static_cast<int>(pending.size());
  if (totalToSync == 0) {
    // Book id is now known even if there's nothing new to send - persist it
    // so the next sync skips the lookup.
    HardcoverSyncState::save(bookFilePath, bookId, lastSyncedTimestamp);
    RenderLock lock(*this);
    state = NOTHING_TO_SYNC;
    requestUpdate();
    return;
  }

  syncedCount = 0;
  for (const auto& clipping : pending) {
    const auto err = HardcoverSyncClient::addQuoteJournalEntry(bookId, clipping.text, clipping.timestamp);
    if (err != HardcoverSyncClient::OK) {
      // Persist everything sent so far so a retry doesn't resend it.
      HardcoverSyncState::save(bookFilePath, bookId, lastSyncedTimestamp);
      RenderLock lock(*this);
      state = FAILED;
      errorMessage = HardcoverSyncClient::errorString(err);
      requestUpdate();
      return;
    }
    syncedCount++;
    lastSyncedTimestamp = clipping.timestamp;
  }

  HardcoverSyncState::save(bookFilePath, bookId, lastSyncedTimestamp);

  RenderLock lock(*this);
  state = DONE;
  requestUpdate();
}

void HardcoverSyncActivity::onEnter() {
  Activity::onEnter();
  sdFontSystem.releaseLoadedFont(renderer);

  if (clippings.empty()) {
    state = NOTHING_TO_SYNC;
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void HardcoverSyncActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void HardcoverSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_HARDCOVER_SYNC));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  switch (state) {
    case SYNCING:
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_HARDCOVER_SYNCING));
      break;
    case DONE: {
      char buf[64];
      snprintf(buf, sizeof(buf), tr(STR_HARDCOVER_SYNC_DONE_FORMAT), syncedCount);
      renderer.drawCenteredText(UI_10_FONT_ID, top, buf, true, EpdFontFamily::BOLD);
      break;
    }
    case NOTHING_TO_SYNC:
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_HARDCOVER_NOTHING_TO_SYNC));
      break;
    case NO_CREDENTIALS:
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_HARDCOVER_NO_TOKEN), true, EpdFontFamily::BOLD);
      break;
    case FAILED:
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_AUTH_FAILED), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, top + height + 10, errorMessage.c_str());
      break;
    default:
      break;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void HardcoverSyncActivity::loop() {
  if (state == DONE || state == FAILED || state == NOTHING_TO_SYNC || state == NO_CREDENTIALS) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finishAfterBackPress();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
  }
}
