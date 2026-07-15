#include "HardcoverJsonIO.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include "HardcoverCredentialStore.h"

namespace HardcoverJsonIO {

bool save(const HardcoverCredentialStore& store, const char* path) {
  JsonDocument doc;
  doc["token_obf"] = obfuscation::obfuscateToBase64(store.getApiToken());
  doc["userId"] = store.getUserId();
  doc["username"] = store.getUsername();

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool load(HardcoverCredentialStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("HCS", "JSON parse error: %s", error.c_str());
    return false;
  }

  obfuscation::DecodeStatus status = obfuscation::DecodeStatus::INVALID;
  std::string token = obfuscation::deobfuscateFromBase64(doc["token_obf"] | "", &status);
  if (status == obfuscation::DecodeStatus::INVALID) {
    LOG_ERR("HCS", "Ignoring unreadable Hardcover token");
    token.clear();
  }

  store.setApiToken(token);
  store.setIdentity(doc["userId"] | (int64_t)0, doc["username"] | std::string(""));
  return true;
}

}  // namespace HardcoverJsonIO
