#ifndef KAME_MIX_SOUND_BUFFER_H
#define KAME_MIX_SOUND_BUFFER_H

#include "audio_system.h"
#include "declspec.h"
#include <cstdint>
#include <cstddef>
#include <atomic>

namespace KameMix {

class DECLSPEC SoundBuffer {
public:
  SoundBuffer() : sdata{nullptr} { }

  SoundBuffer(const char *filename) : sdata{nullptr} { load(filename); }

  SoundBuffer(const SoundBuffer &other) : sdata{other.sdata}
  {
    incRefCount();
  }

  SoundBuffer(SoundBuffer &&other) : sdata{other.sdata}
  {
    other.sdata = nullptr;
  }

  SoundBuffer& operator=(const SoundBuffer &other)
  {
    if (this != &other) {
      release();
      sdata = other.sdata;
      incRefCount();
    }
    return *this;
  }

  SoundBuffer& operator=(SoundBuffer &&other)
  {
    if (this != &other) {
      release();
      sdata = other.sdata;
      other.sdata = nullptr;
    }
    return *this;
  }

  ~SoundBuffer() { release(); }

  bool load(const char *filename);
  bool loadOGG(const char *filename);
  bool loadWAV(const char *filename);
  bool isLoaded() const { return sdata != nullptr; }
  // Release loaded file and free allocated data. isLoaded() is false after.
  void release();
  int useCount() const { return sdata ? sdata->refcount.load() : 0; }

  // Pointer to currently loaded audio data.
  uint8_t* data() 
  { 
    assert(sdata && "SoundBuffer must be loaded before calling data()");
    return sdata->buffer; 
  }

  // Size in bytes of audio data in data() buffer.
  int size() const 
  { 
    assert(sdata && "SoundBuffer must be loaded before calling size()");
    return sdata->buffer_size; 
  }

  // Returns 1 for mono, 2 for stereo.
  int numChannels() const 
  { 
    assert(sdata && 
           "SoundBuffer must be loaded before calling numChannels()");
    return sdata->channels; 
  }

  // Returns size of audio format * number of channels in bytes.
  int sampleBlockSize() const 
  { 
    assert(sdata && 
           "SoundBuffer must be loaded before calling sampleBlockSize()");
    return sdata->channels * AudioSystem::getFormatSize(); 
  }

private:
  struct alignas(std::max_align_t) SharedData {
    SharedData(uint8_t *buf, int buf_len, int channels) 
      : buffer{buf}, refcount{1}, buffer_size{buf_len}, channels{channels} 
    { }
    uint8_t *buffer;
    std::atomic<int> refcount;
    int buffer_size;
    int channels;
  };

  static const size_t HEADER_SIZE = sizeof(SharedData);

  void incRefCount() 
  { 
    if (sdata) {
      sdata->refcount.fetch_add(1, std::memory_order_relaxed);
    }
  }

  SharedData *sdata;
};

} // end namespace KameMix
#endif
