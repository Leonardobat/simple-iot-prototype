/**
 * @file firebase_auth.h
 * @brief Firebase Authentication (email/password) and ID token cache.
 *
 * The device signs in with CONFIG_FIREBASE_USER_EMAIL / CONFIG_FIREBASE_USER_PASSWORD and keeps the
 * returned ID token, which the Realtime Database accepts as ?auth=<token>. The token is renewed by
 * signing in again shortly before it expires (Firebase tokens last 1 h).
 */
#ifndef FIREBASE_AUTH_H
#define FIREBASE_AUTH_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/** Longest ID token (JWT, ~1 KB) including the terminating NUL; leaves room for custom claims. */
#define FIREBASE_ID_TOKEN_MAX_LEN 2048U

/** The token is renewed this long before it expires, so in-flight requests never use a stale one.
 */
#define FIREBASE_AUTH_RENEW_MARGIN_S 300

/**
 * @brief Prepares the token cache.
 *
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NO_MEM if the mutex cannot be allocated.
 * @pre Not initialized (asserted). http_client is initialized before the first token request.
 * @post No token is cached: the first firebase_auth_get_token() signs in.
 * @note Not thread-safe: call once at start-up.
 */
esp_err_t firebase_auth_init(void);

/**
 * @brief Erases the cached token and releases the mutex.
 *
 * @pre No firebase_auth_get_token() call is in progress.
 * @post The component can be initialized again.
 * @note Not thread-safe. Safe to call when not initialized.
 */
void firebase_auth_deinit(void);

/**
 * @brief Tells whether requests must carry a token.
 *
 * @return false when CONFIG_FIREBASE_USER_EMAIL is empty (open database rules), true otherwise.
 * @note Thread-safe, non-blocking, ISR-safe.
 */
bool firebase_auth_is_enabled(void);

/**
 * @brief Copies a valid ID token into @p token, signing in first when needed.
 *
 * @param[out] token Receives the NUL-terminated token.
 * @param[in]  size  Size of @p token in bytes.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_SIZE if the token does not fit in @p size bytes
 *  - ESP_ERR_INVALID_STATE if the API key or password is not configured
 *  - ESP_ERR_INVALID_ARG if the email or password contains '"' or '\'
 *  - ESP_ERR_INVALID_RESPONSE if the sign-in response lacks a valid idToken/expiresIn
 *  - ESP_ERR_TIMEOUT if another task held the token lock for too long
 *  - errors of http_client_post() (e.g. ESP_FAIL for wrong credentials, ESP_ERR_HTTP_CONNECT)
 * @pre Initialized; @p token is not NULL (asserted).
 * @post After a failure the next call signs in again.
 * @note Thread-safe: concurrent callers wait for a single sign-in. Blocking while signing in
 *       (network, up to ~10 s); otherwise only copies the cached token.
 */
esp_err_t firebase_auth_get_token(char *token, size_t size);

#endif /* FIREBASE_AUTH_H */
