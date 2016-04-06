#ifndef SOUND_BUFFER_H
#define SOUND_BUFFER_H

#include <stdint.h>

namespace KameMix {

class SoundBuffer {
public:
  SoundBuffer() : buffer{nullptr}, refcount{nullptr}, buffer_size{0} { }

  SoundBuffer(const char *filename)
    : buffer{nullptr}, refcount{nullptr}, buffer_size{0} { load(filename); }

  SoundBuffer::SoundBuffer(const SoundBuffer &other) 
    : refcount{other.refcount},buffer{other.buffer}, 
      buffer_size{other.buffer_size}
  {
    if (refcount) {
      *refcount += 1;
    }
  }

  SoundBuffer::SoundBuffer(SoundBuffer &&other) 
    : refcount{other.refcount},buffer{other.buffer}, 
      buffer_size{other.buffer_size}
  {
    other.refcount = nullptr;
    other.buffer = nullptr;
    other.buffer_size = 0;
  }

  SoundBuffer& operator=(const SoundBuffer &other)
  {
    if (this != &other) {
      release();
      refcount = other.refcount;
      buffer = other.buffer;
      buffer_size = other.buffer_size;
      if (refcount) {
        (*refcount)++;
      }
    }
    return *this;
  }

  SoundBuffer& operator=(SoundBuffer &&other)
  {
    if (this != &other) {
      release();
      refcount = other.refcount;
      buffer = other.buffer;
      buffer_size = other.buffer_size;
      other.refcount = nullptr;
      other.buffer = nullptr;
      other.buffer_size = 0;
    }
    return *this;
  }


  ~SoundBuffer() { release(); }

  bool load(const char *filename);
  bool loadOGG(const char *filename);
  bool loadWAV(const char *filename);
  void release();
  bool isLoaded() const { return refcount != nullptr; }

  uint8_t* data() { return buffer; }
  int useCount() const { return refcount == nullptr ? 0 : *refcount; }
  int size() const { return buffer_size; }

private:
  int *refcount;
  uint8_t *buffer;
  int buffer_size;
};

} // end namespace KameMix
#endif
