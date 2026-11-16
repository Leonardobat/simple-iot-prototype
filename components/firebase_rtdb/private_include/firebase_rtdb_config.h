/**
 * @file firebase_rtdb_config.h
 * @brief Private configuration of the firebase_rtdb component.
 */
#ifndef FIREBASE_RTDB_CONFIG_H
#define FIREBASE_RTDB_CONFIG_H

/** "<CONFIG_FIREBASE_DB_URL>/<path>.json" before the auth query. */
#define FIREBASE_RTDB_URL_BASE_MAX_LEN 256U

#define FIREBASE_RTDB_AUTH_QUERY "?auth="

/** Longest wait for the write buffer: the holder may be signing in and then sending (both bounded).
 */
#define FIREBASE_RTDB_LOCK_TIMEOUT_MS 120000

#endif /* FIREBASE_RTDB_CONFIG_H */
