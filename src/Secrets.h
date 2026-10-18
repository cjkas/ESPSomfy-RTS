#ifndef secrets_h
#define secrets_h
#include <string.h>

// Stored secrets (WiFi passphrase, MQTT password, login password/PIN) are
// write-only: responses carry hasSecret() flags and logs carry maskSecret().

inline bool hasSecret(const char *s) { return s && s[0] != '\0'; }

inline const char *maskSecret(const char *s) { return hasSecret(s) ? "<set>" : "<empty>"; }

// Updates dest from an incoming value, truncating to size.  A null incoming
// value means the client left the field out, so the stored secret is kept.
// Pass obj["key"].as<const char *>(), which is null when the key is absent.
// Returns true when dest changed.
inline bool applySecret(const char *incoming, char *dest, size_t size) {
  if(!incoming || size == 0) return false;
  size_t len = strlen(incoming);
  if(len >= size) len = size - 1;
  if(strncmp(dest, incoming, len) == 0 && dest[len] == '\0') return false;
  memcpy(dest, incoming, len);
  dest[len] = '\0';
  return true;
}
#endif
