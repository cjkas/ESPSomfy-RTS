// Minimal stand-in for the ESP32 Preferences library (NVS) so firmware sources
// can be built for the native test env (pio test -e native).  Every handle
// shares one store that outlives it, as NVS does, so a test can seed what is
// stored and read back what the code under test wrote.  Only add what tested
// code uses.
#ifndef preferences_stub_h
#define preferences_stub_h
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

class Preferences {
  public:
    // "namespace/key" -> value.  Tests clear it in setUp().
    static std::map<std::string, uint16_t> &stubStore() {
      static std::map<std::string, uint16_t> store;
      return store;
    }
    bool begin(const char *name, bool readOnly = false) {
      this->ns = name;
      this->readOnly = readOnly;
      this->opened = true;
      return true;
    }
    void end() { this->opened = false; }
    uint16_t getUShort(const char *key, uint16_t defaultValue = 0) {
      if(!this->opened) return defaultValue;
      auto it = stubStore().find(this->path(key));
      return it == stubStore().end() ? defaultValue : it->second;
    }
    size_t putUShort(const char *key, uint16_t value) {
      if(!this->opened || this->readOnly) return 0;
      stubStore()[this->path(key)] = value;
      return sizeof(value);
    }
  private:
    std::string ns;
    bool readOnly = false;
    bool opened = false;
    std::string path(const char *key) const { return this->ns + "/" + key; }
};
#endif
