#ifndef KAMEMIX_SOUND_BUFFER_H
#define KAMEMIX_SOUND_BUFFER_H

#include "KameMix.h"
#include <cstdint>
#include <cstddef>
#include <atomic>

namespace KameMix {

class SoundBuffer {
public:
  SoundBuffer()
    : buffer{nullptr}, buffer_size{0}, channels{0} { }

  SoundBuffer(const char *filename) 
    : buffer{nullptr}, buffer_size{0}, channels{0} 
  { load(filename); }

  ~SoundBuffer() { release(); }

  bool load(const char *filename);
  bool loadOGG(const char *filename);
  bool loadWAV(const char *filename);
  bool isLoaded() const { return buffer != nullptr; }
  // Frees loaded audio data. isLoaded() returns false after this.
  void release();

  // Returns pointer to currently loaded audio data, or nullptr if not loaded.
  uint8_t* data() { return buffer; }

  // Returns size in bytes of audio data in data() buffer, or 0 if not loaded.
  int size() const { return buffer_size; }

  // Returns 1 for mono, 2 for stereo, or 0 if not loaded.
  int numChannels() const { return channels; }

  // Returns size of audio format * number of channels in bytes, 
  // or 0 if not loaded.
  int sampleBlockSize() const { return channels * KameMix_getFormatSize(); }

private:
  SoundBuffer(const SoundBuffer &other) = delete;
  SoundBuffer& operator=(const SoundBuffer &other) = delete;

  uint8_t *buffer;
  int buffer_size;
  int channels;
};

} // end namespace KameMix
#endif
