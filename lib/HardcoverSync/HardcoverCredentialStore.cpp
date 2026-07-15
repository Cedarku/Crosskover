#include "HardcoverCredentialStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cctype>

#include "HardcoverJsonIO.h"

HardcoverCredentialStore HardcoverCredentialStore::instance;

namespace {
constexpr char HARDCOVER_FILE_JSON[] = "/.crosspoint/hardcover.json";

std::string trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
  return s.substr(start, end - start);
}
}  // namespace

bool HardcoverCredentialStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return HardcoverJsonIO::save(*this, HARDCOVER_FILE_JSON);
}

bool HardcoverCredentialStore::loadFromFile() {
  if (!Storage.exists(HARDCOVER_FILE_JSON)) {
    LOG_DBG("HCS", "Creating default Hardcover configuration");
    saveToFile();
    return false;
}

  String json = Storage.readFile(HARDCOVER_FILE_JSON);
  if (json.isEmpty()) return false;

  return HardcoverJsonIO::load(*this, json.c_str());
}

void HardcoverCredentialStore::setApiToken(const std::string& token) {
  apiToken = trim(token);
  LOG_DBG("HCS", "Set Hardcover API token (len=%u)", (unsigned)apiToken.size());
}

std::string HardcoverCredentialStore::getAuthorizationHeader() const {
  if (apiToken.empty()) return "";
  // Users copy the token straight from https://hardcover.app/account/api,
  // which already includes the "Bearer " prefix - but accept either form.
  if (apiToken.rfind("Bearer ", 0) == 0) return apiToken;
  return "Bearer " + apiToken;
}

void HardcoverCredentialStore::clearToken() {
  apiToken.clear();
  userId = 0;
  username.clear();
  saveToFile();
  LOG_DBG("HCS", "Cleared Hardcover credentials");
}

void HardcoverCredentialStore::setIdentity(int64_t id, const std::string& name) {
  userId = id;
  username = name;
}
