#include "HardcoverSyncState.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <uzlib.h>

namespace {
constexpr char HARDCOVER_STATE_DIR[] = "/.crosspoint/hardcover";

std::string statePathForBook(const std::string& filePath) {
  const uint32_t crc = uzlib_crc32(filePath.data(), static_cast<unsigned int>(filePath.size()), 0);
  return std::string(HARDCOVER_STATE_DIR) + "/" + std::to_string(crc) + ".json";
}
}  // namespace

bool HardcoverSyncState::load(const std::string& bookFilePath, int64_t& outBookId,
                              uint32_t& outLastSyncedTimestamp) {
  outBookId = 0;
  outLastSyncedTimestamp = 0;

  const std::string path = statePathForBook(bookFilePath);
  if (!Storage.exists(path.c_str())) return false;

  String json = Storage.readFile(path.c_str());
  if (json.isEmpty()) return false;

  JsonDocument doc;
  if (deserializeJson(doc, json.c_str())) {
    LOG_ERR("HCS", "Failed to parse Hardcover sync state: %s", path.c_str());
    return false;
  }

  outBookId = doc["bookId"] | (int64_t)0;
  outLastSyncedTimestamp = doc["lastSyncedTimestamp"] | (uint32_t)0;
  return true;
}

bool HardcoverSyncState::save(const std::string& bookFilePath, int64_t bookId, uint32_t lastSyncedTimestamp) {
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(HARDCOVER_STATE_DIR);

  JsonDocument doc;
  doc["bookId"] = bookId;
  doc["lastSyncedTimestamp"] = lastSyncedTimestamp;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(statePathForBook(bookFilePath).c_str(), json);
}
