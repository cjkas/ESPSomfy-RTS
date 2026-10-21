#ifndef json_formatter_h
#define json_formatter_h
#include <Arduino.h>

// Streams JSON into a fixed buffer.  Free of the web server so it can be unit
// tested on the PC (pio test -e native).
class JsonFormatter {
  protected:
    char *buff;
    size_t buffSize;
    // What is in buff, kept so appending never has to strlen() the buffer.
    size_t _len = 0;
    bool _headersSent = false;
    uint8_t _objects = 0;
    uint8_t _arrays = 0;
    bool _nocomma = true;
    // Something didn't fit and was left out so the JSON is incomplete.
    bool _overflow = false;
    char _numbuff[25] = {0};
    virtual void _safecat(const char *val, bool escape = false);
    // Appends val at buff + _len.  vlen is what it takes, including the quotes
    // when escaped; the caller has made sure it fits.
    void _appendAt(const char *val, bool escape, size_t vlen);
    void _appendNumber(const char *name);
  public:
    bool overflowed() const { return _overflow; }
    // Drops whatever is written until a buffer is set again.  Nothing is
    // written anywhere, so tasks can share one discarding formatter.
    void discard();
    // Writes c as it must appear inside a JSON string (out needs 7 bytes) and
    // returns its length.
    static size_t escapeChar(char c, char *out);
    void escapeString(const char *raw, char *escaped);
    uint32_t calcEscapedLength(const char *raw);
    void beginObject(const char *name = nullptr);
    void endObject();
    void beginArray(const char *name = nullptr);
    void endArray();
    void appendElem(const char *name = nullptr);

    void addElem(const char* val);
    void addElem(float fval);
    void addElem(int8_t nval);
    void addElem(uint8_t nval);
    /*
    void addElem(int32_t nval);
    void addElem(int16_t nval);
    void addElem(uint16_t nval);
    void addElem(unsigned int nval);
    */
    void addElem(int32_t lval);
    void addElem(uint32_t lval);
    void addElem(bool bval);
    
    void addElem(const char* name, float fval);
    void addElem(const char* name, int8_t nval);
    void addElem(const char* name, uint8_t nval);
    /*
    void addElem(const char* name, int nval);
    void addElem(const char* name, int16_t nval);
    void addElem(const char* name, uint16_t nval);
    void addElem(const char* name, unsigned int nval);
    */
    void addElem(const char* name, int32_t lval);
    void addElem(const char* name, uint32_t lval);
    void addElem(const char* name, bool bval);
    void addElem(const char *name, const char *val);
    void addElem(const char* name, uint64_t lval);
};
class BufferedJsonFormatter : public JsonFormatter {
  public:
    void setBuffer(char *b, size_t sz);
    size_t length() const;
};
#endif
