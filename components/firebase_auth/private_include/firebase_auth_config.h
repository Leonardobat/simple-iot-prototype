/**
 * @file firebase_auth_config.h
 * @brief Private configuration of the firebase_auth component.
 */
#ifndef FIREBASE_AUTH_CONFIG_H
#define FIREBASE_AUTH_CONFIG_H

#include "sdkconfig.h"

/** Firebase Auth REST API: email/password sign-in. */
#define FIREBASE_AUTH_SIGN_IN_URL                                                                  \
    "https://identitytoolkit.googleapis.com/v1/"                                                   \
    "accounts:signInWithPassword?key=" CONFIG_FIREBASE_API_KEY

/** Sign-in request body: {"email":...,"password":...,"returnSecureToken":true}. */
#define FIREBASE_AUTH_REQUEST_BODY_MAX_LEN 256U

/** Sign-in response (~1.5 KB: idToken, refreshToken, ids). */
#define FIREBASE_AUTH_RESPONSE_MAX_LEN 4096U

/** JSON tokens of the sign-in response (~10 fields, key + value each). */
#define FIREBASE_AUTH_JSON_MAX_TOKENS 32

/** "expiresIn" is a decimal string ("3600"). */
#define FIREBASE_AUTH_EXPIRES_IN_MAX_LEN 16U

/** Longest token lifetime accepted from the server (Firebase issues 1 h tokens). */
#define FIREBASE_AUTH_EXPIRES_IN_MAX_S 86400LL

/** Longest wait for the token lock: a sign-in may wait for the HTTP client, then run. */
#define FIREBASE_AUTH_LOCK_TIMEOUT_MS 45000

#define FIREBASE_AUTH_US_PER_S 1000000LL

#endif /* FIREBASE_AUTH_CONFIG_H */
