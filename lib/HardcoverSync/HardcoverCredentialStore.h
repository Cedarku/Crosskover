#pragma once
#include <string>

/**
 * Singleton class for storing Hardcover credentials on the SD card.
 *
 * Credentials are stored in a JSON file located at
 * /.crosspoint/hardcover.json.
 *
 * The API token is stored in plain text so users can easily configure it
 * by editing the file from a computer.
 */
class HardcoverCredentialStore {
 private:
  static HardcoverCredentialStore instance;
  std::string apiToken;
  int64_t userId = 0;
  std::string username;

  HardcoverCredentialStore() = default;

 public:
  HardcoverCredentialStore(const HardcoverCredentialStore&) = delete;
  HardcoverCredentialStore& operator=(const HardcoverCredentialStore&) = delete;

  static HardcoverCredentialStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();

  // Token management. Accepts the value exactly as copied from the Hardcover
  // account settings page (with or without the "Bearer " prefix).
  void setApiToken(const std::string& token);
  const std::string& getApiToken() const { return apiToken; }

  // Returns the token normalised for use in the Authorization header
  // (always prefixed with "Bearer ").
  std::string getAuthorizationHeader() const;

  bool hasToken() const { return !apiToken.empty(); }

  void clearToken();

  // Cached identity, populated after a successful authenticate() call so the
  // settings screen can show which account is linked without another request.
  void setIdentity(int64_t id, const std::string& name);
  int64_t getUserId() const { return userId; }
  const std::string& getUsername() const { return username; }
  bool hasIdentity() const { return userId != 0; }
};

#define HARDCOVER_STORE HardcoverCredentialStore::getInstance()
