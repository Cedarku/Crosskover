#include "HardcoverSyncClient.h"

#include <ArduinoJson.h>
#ifdef SIMULATOR
#include <ArduinoJsonStringCompat.h>
#endif
#include <HTTPClient.h>
#include <I18n.h>
#include <Logging.h>
#ifdef SIMULATOR
#include <WiFi.h>
#include <WiFiClientSecure.h>
#else
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

#include "HardcoverCredentialStore.h"

int HardcoverSyncClient::lastHttpCode = 0;
int HardcoverSyncClient::lastTransportError = 0;

namespace {
constexpr char GRAPHQL_URL[] = "https://api.hardcover.app/v1/graphql";

// See the note in KOReaderSyncClient.cpp: TLS handshakes on the ESP32-C3
// consume a large chunk of heap, so refuse to even try below this threshold.
constexpr uint32_t MIN_HEAP_FOR_TLS = 55000;
constexpr int HTTP_BUF_SIZE = 4096;  // GraphQL responses are bigger than KOSync's tiny JSON

std::string toLower(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

// Response buffer shared by both transport implementations below.
struct ResponseBuffer {
  char* data = nullptr;
  int len = 0;
  int capacity = 0;

  ~ResponseBuffer() { free(data); }

  bool ensure(int size) {
    if (size <= capacity) return true;
    char* newData = (char*)realloc(data, size);
    if (!newData) return false;
    data = newData;
    capacity = size;
    return true;
  }

  bool append(const char* chunk, int chunkLen) {
    if (!ensure(len + chunkLen + 1)) return false;
    memcpy(data + len, chunk, chunkLen);
    len += chunkLen;
    data[len] = '\0';
    return true;
  }
};

#ifdef SIMULATOR
// POST `body` to the GraphQL endpoint. Returns the HTTP status code, or a
// negative value on transport failure. On success, `outBuf` holds the body.
int postGraphQL(const std::string& body, ResponseBuffer& outBuf) {
  HTTPClient http;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  http.begin(secureClient, GRAPHQL_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", HARDCOVER_STORE.getAuthorizationHeader().c_str());

  const int httpCode = http.POST(body.c_str());
  HardcoverSyncClient::lastHttpCode = httpCode;
  HardcoverSyncClient::lastTransportError = (httpCode < 0) ? httpCode : 0;

  if (httpCode > 0) {
    String responseBody = http.getString();
    if (!outBuf.append(responseBody.c_str(), responseBody.length())) {
      LOG_ERR("HCSync", "Response buffer allocation failed (%u bytes)", responseBody.length());
      http.end();
      return -1;
    }
  }
  http.end();
  return httpCode;
}
#else
esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
  auto* buf = static_cast<ResponseBuffer*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && buf) {
    if (!buf->append(static_cast<const char*>(evt->data), evt->data_len)) {
      LOG_ERR("HCSync", "Response buffer allocation failed (%d bytes)", evt->data_len);
    }
  }
  return ESP_OK;
}

int postGraphQL(const std::string& body, ResponseBuffer& outBuf) {
  esp_http_client_config_t config = {};
  config.url = GRAPHQL_URL;
  config.method = HTTP_METHOD_POST;
  config.event_handler = httpEventHandler;
  config.user_data = &outBuf;
  config.timeout_ms = 15000;
  config.buffer_size = HTTP_BUF_SIZE;
  config.buffer_size_tx = HTTP_BUF_SIZE;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    HardcoverSyncClient::lastTransportError = ESP_ERR_NO_MEM;
    return -1;
  }

  const std::string authHeader = HARDCOVER_STORE.getAuthorizationHeader();
  if (esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK ||
      esp_http_client_set_header(client, "Authorization", authHeader.c_str()) != ESP_OK ||
      esp_http_client_set_post_field(client, body.c_str(), body.length()) != ESP_OK) {
    LOG_ERR("HCSync", "Failed to set request headers/body");
    esp_http_client_cleanup(client);
    HardcoverSyncClient::lastTransportError = ESP_ERR_INVALID_STATE;
    return -1;
  }

  const esp_err_t err = esp_http_client_perform(client);
  const int httpCode = esp_http_client_get_status_code(client);
  HardcoverSyncClient::lastHttpCode = httpCode;
  HardcoverSyncClient::lastTransportError = static_cast<int>(err);
  esp_http_client_cleanup(client);

  return (err == ESP_OK) ? httpCode : -1;
}
#endif

// Wraps the transport call with the shared preconditions (token present,
// enough heap for TLS) and GraphQL-level error checking. On success, `outDoc`
// contains the parsed `data` object (or is null if the server returned only
// `errors`, which is reported as GRAPHQL_ERROR).
HardcoverSyncClient::Error runGraphQL(const std::string& query, JsonDocument& variables, JsonDocument& outDoc) {
  HardcoverSyncClient::lastHttpCode = 0;
  HardcoverSyncClient::lastTransportError = 0;

  if (!HARDCOVER_STORE.hasToken()) {
    LOG_DBG("HCSync", "No Hardcover API token configured");
    return HardcoverSyncClient::NO_TOKEN;
  }

  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_HEAP_FOR_TLS) {
    LOG_ERR("HCSync", "Insufficient heap for TLS handshake: %u bytes free (need %u)", freeHeap, MIN_HEAP_FOR_TLS);
    return HardcoverSyncClient::LOW_MEMORY;
  }

  JsonDocument reqDoc;
  reqDoc["query"] = query;
  if (!variables.isNull()) reqDoc["variables"] = variables;
  std::string body;
  serializeJson(reqDoc, body);

  ResponseBuffer buf;
  const int httpCode = postGraphQL(body, buf);
LOG_DBG("HCSync", "GraphQL response: %d (transportErr=%d)", httpCode, HardcoverSyncClient::lastTransportError);

  if (httpCode < 0) return HardcoverSyncClient::NETWORK_ERROR;
  if (httpCode == 401 || httpCode == 403) return HardcoverSyncClient::AUTH_FAILED;
  if (httpCode != 200 || !buf.data) return HardcoverSyncClient::SERVER_ERROR;

  const DeserializationError parseErr = deserializeJson(outDoc, buf.data);
  if (parseErr) {
    LOG_ERR("HCSync", "JSON parse failed: %s", parseErr.c_str());
    return HardcoverSyncClient::JSON_ERROR;
  }

  if (outDoc["errors"].is<JsonArray>() && outDoc["errors"].size() > 0) {
    const char* message = outDoc["errors"][0]["message"] | "unknown error";
    LOG_ERR("HCSync", "GraphQL error: %s", message);
    return HardcoverSyncClient::GRAPHQL_ERROR;
  }

  return HardcoverSyncClient::OK;
}
}  // namespace

HardcoverSyncClient::Error HardcoverSyncClient::authenticate(int64_t& outUserId, std::string& outUsername) {
  outUserId = 0;
  outUsername.clear();

  JsonDocument noVars;
  JsonDocument resp;
  const Error err = runGraphQL("query { me { id username } }", noVars, resp);
  if (err != OK) return err;

  // `me` is documented as returning the current user wrapped in an array.
  JsonVariant meNode = resp["data"]["me"];
  JsonObject me = meNode.is<JsonArray>() ? meNode[0].as<JsonObject>() : meNode.as<JsonObject>();
  if (me.isNull() || !me["id"].is<int64_t>()) {
    LOG_ERR("HCSync", "Unexpected /me response shape");
    return JSON_ERROR;
  }

  outUserId = me["id"].as<int64_t>();
  outUsername = me["username"] | std::string("");
  return OK;
}

HardcoverSyncClient::Error HardcoverSyncClient::findBookId(const std::string& title, const std::string& author,
                                                           int64_t& outBookId) {
  outBookId = 0;
  if (title.empty()) return NOT_FOUND;

  constexpr char query[] =
      "query FindBook($title: String!) {"
      "  books(where: {title: {_ilike: $title}}, limit: 10, order_by: {users_count: desc}) {"
      "    id title contributions { author { name } }"
      "  }"
      "}";

  JsonDocument vars;
  vars["title"] = "%" + title + "%";

  JsonDocument resp;
  const Error err = runGraphQL(query, vars, resp);
  if (err != OK) return err;

  JsonArray books = resp["data"]["books"].as<JsonArray>();
  if (books.isNull() || books.size() == 0) return NOT_FOUND;

  const std::string authorLower = toLower(author);
  if (!authorLower.empty()) {
    for (JsonObject book : books) {
      for (JsonObject contribution : book["contributions"].as<JsonArray>()) {
        const char* name = contribution["author"]["name"] | "";
        if (toLower(name).find(authorLower) != std::string::npos) {
          outBookId = book["id"].as<int64_t>();
          return OK;
        }
      }
    }
  }

  // No author match (or no author supplied) - fall back to the most-read
  // title match, which `order_by: users_count desc` already put first.
  outBookId = books[0]["id"].as<int64_t>();
  return OK;
}

HardcoverSyncClient::Error HardcoverSyncClient::addQuoteJournalEntry(int64_t bookId, const std::string& quoteText,
                                                                     uint32_t timestampUnix) {
  if (bookId == 0) return NOT_FOUND;

  // NOTE: verify `ReadingJournalCreateInput` and `insert_reading_journal`
  // against https://hardcover.app/api-explorer if Hardcover changes its
  // (currently beta) schema - see the header comment for context.
  constexpr char mutation[] =
      "mutation AddQuote($object: ReadingJournalCreateInput!) {"
      "  insert_reading_journal(object: $object) { id error }"
      "}";

  JsonDocument vars;
  JsonObject object = vars["object"].to<JsonObject>();
  object["book_id"] = bookId;
  object["event"] = "quote";
  object["entry"] = quoteText;
  object["privacy_setting_id"] = 3;  // 1=Public, 2=Followers, 3=Private (default: keep highlights private)
  char actionAt[24];
  time_t t = static_cast<time_t>(timestampUnix);
  struct tm* utc = gmtime(&t);
  strftime(actionAt, sizeof(actionAt), "%Y-%m-%dT%H:%M:%SZ", utc);
  object["action_at"] = actionAt;

  JsonDocument resp;
  const Error err = runGraphQL(mutation, vars, resp);
  if (err != OK) return err;

  const char* mutationError = resp["data"]["insert_reading_journal"]["error"] | nullptr;
  if (mutationError) {
    LOG_ERR("HCSync", "insert_reading_journal rejected: %s", mutationError);
    return GRAPHQL_ERROR;
  }
  if (resp["data"]["insert_reading_journal"]["id"].isNull()) {
    LOG_ERR("HCSync", "insert_reading_journal returned no id and no error");
    return GRAPHQL_ERROR;
  }

  return OK;
}

std::string HardcoverSyncClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_TOKEN:
      return tr(STR_HARDCOVER_NO_TOKEN);
    case NETWORK_ERROR:
      return tr(STR_HARDCOVER_NETWORK_ERROR);
    case AUTH_FAILED:
      return tr(STR_HARDCOVER_AUTH_FAILED);
    case SERVER_ERROR:
      return tr(STR_HARDCOVER_SERVER_ERROR);
    case JSON_ERROR:
    case GRAPHQL_ERROR:
      return tr(STR_HARDCOVER_BAD_RESPONSE);
    case NOT_FOUND:
      return tr(STR_HARDCOVER_BOOK_NOT_FOUND);
    case LOW_MEMORY:
      return tr(STR_HARDCOVER_LOW_MEMORY);
    default:
      return tr(STR_UNKNOWN_ERROR);
  }
}
