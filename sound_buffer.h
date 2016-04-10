#ifndef SOUND_BUFFER_H
#define SOUND_BUFFER_H

#include "audio_system.h"
#include <cstdint>
#include <cstddef>

namespace KameMix {

class SoundBuffer {
public:
  SoundBuffer() : buffer{nullptr}, mdata{nullptr}, buffer_size{0} { }

  SoundBuffer(const char *filename)
    : buffer{nullptr}, mdata{nullptr}, buffer_size{0} { load(filename); }

  SoundBuffer::SoundBuffer(const SoundBuffer &other) 
    : mdata{other.mdata},buffer{other.buffer}, 
      buffer_size{other.buffer_size}
  {
    if (mdata) {
      mdata->refcount += 1;
    }
  }

  SoundBuffer::SoundBuffer(SoundBuffer &&other) 
    : mdata{other.mdata},buffer{other.buffer}, 
      buffer_size{other.buffer_size}
  {
    other.mdata = nullptr;
    other.buffer = nullptr;
    other.buffer_size = 0;
  }

  SoundBuffer& operator=(const SoundBuffer &other)
  {
    if (this != &other) {
      release();
      mdata = other.mdata;
      buffer = other.buffer;
      buffer_size = other.buffer_size;
      if (mdata) {
        mdata->refcount++;
      }
    }
    return *this;
  }

  SoundBuffer& operator=(SoundBuffer &&other)
  {
    if (this != &other) {
      release();
      mdata = other.mdata;
      buffer = other.buffer;
      buffer_size = other.buffer_size;
      other.mdata = nullptr;
      other.buffer = nullptr;
      other.buffer_size = 0;
    }
    return *this;
  }

  ~SoundBuffer() { release(); }

  bool load(const char *filename);
  bool loadOGG(const char *filename);
  bool loadWAV(const char *filename);
  bool isLoaded() const { return mdata != nullptr; }
  void release()
  {
    if (mdata != nullptr) {
      if (mdata->refcount == 1) {
        AudioSystem::getFree()(mdata); // includes audio buffer
        mdata = nullptr;
        buffer = nullptr;
        buffer_size = 0;
      } else {
        mdata->refcount -= 1;
      }
    }
  }

  uint8_t* data() { return buffer; }
  int useCount() const { return mdata ? mdata->refcount : 0; }
  int size() const { return buffer_size; }
  int numChannels() const { return mdata ? mdata->channels : 0; }

private:
  struct alignas(std::max_align_t) MiscData {
    int refcount;
    int channels;
  };

  MiscData *mdata;
  uint8_t *buffer;
  int buffer_size;
};

} // end namespace KameMix
#endif
