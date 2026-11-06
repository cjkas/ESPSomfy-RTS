#pragma once
extern bool testRadioInit;
struct Preferences {
  void begin(const char *) {}
  bool getBool(const char *, bool) { return testRadioInit; }
  void putBool(const char *, bool value) { testRadioInit = value; }
  void end() {}
};
