#pragma once
#include <cstdint>
#include <string>
#include <vector>

/**
 * HTTP client for the Hardcover GraphQL API.
 *
 * Endpoint: https://api.hardcover.app/v1/graphql
 * Auth: Authorization: Bearer <personal API token from hardcover.app/account/api>
 *
 * NOTE: Hardcover's API is officially in beta and the exact mutation/type
 * names documented here (insert_reading_journal / ReadingJournalCreateInput)
 * are the ones published in their GraphQL schema at the time this was
 * written. If Hardcover changes their schema, only the query strings in
 * HardcoverSyncClient.cpp need to be updated - nothing else in the firmware
 * depends on them.
 *
 * Mirrors the transport-layer plumbing of KOReaderSyncClient (lib/KOReaderSync),
 * but all requests share a single POST /v1/graphql endpoint, so the low-level
 * HTTP handling is factored into one helper instead of being duplicated per call.
 */
class HardcoverSyncClient {
 public:
  enum Error {
    OK = 0,
    NO_TOKEN,
    NETWORK_ERROR,
    AUTH_FAILED,
    SERVER_ERROR,
    JSON_ERROR,
    GRAPHQL_ERROR,
    NOT_FOUND,
    LOW_MEMORY,
  };

  /** Validates the stored token and returns the account's Hardcover user id / username. */
  static Error authenticate(int64_t& outUserId, std::string& outUsername);

  /**
   * Looks up a Hardcover book id for the given title/author. Picks the
   * highest-`users_count` match; if `author` is non-empty and matches one of
   * the returned contributors that candidate is preferred over a pure title match.
   */
  static Error findBookId(const std::string& title, const std::string& author, int64_t& outBookId);

  /**
   * Adds a single highlighted passage to the user's Hardcover reading journal
   * as a "quote" entry, associated with the given book.
   */
  static Error addQuoteJournalEntry(int64_t bookId, const std::string& quoteText, uint32_t timestampUnix);

  static std::string errorString(Error error);

  static int lastHttpCode;
  static int lastTransportError;
};
