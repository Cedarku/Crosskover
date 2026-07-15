#pragma once
#include <cstdint>
#include <string>

/**
 * Tracks, per local book file, which Hardcover book it was matched to and the
 * timestamp of the newest highlight already pushed to Hardcover, so repeated
 * syncs only send new highlights.
 *
 * Stored at /.crosspoint/hardcover/<crc-of-book-path>.json - same key derivation
 * as ClippingStore uses for its own per-book file, so state naturally follows
 * the book the same way clippings do.
 */
class HardcoverSyncState {
 public:
  static bool load(const std::string& bookFilePath, int64_t& outBookId, uint32_t& outLastSyncedTimestamp);
  static bool save(const std::string& bookFilePath, int64_t bookId, uint32_t lastSyncedTimestamp);
};
