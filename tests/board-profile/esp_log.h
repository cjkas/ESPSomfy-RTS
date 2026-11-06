#pragma once
// Keep the test logger a function call so unused firmware format arguments are
// still type-checked and the production TAG is referenced.
template <typename... Args> inline void testLog(const char *, const char *, Args...) {}
#define ESP_LOGD(...) testLog(__VA_ARGS__)

#define ESP_LOGE(...) testLog(__VA_ARGS__)
#define ESP_LOGW(...) testLog(__VA_ARGS__)
#define ESP_LOGI(...) testLog(__VA_ARGS__)
