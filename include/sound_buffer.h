#ifndef KAME_MIX_SOUND_BUFFER_H
#define KAME_MIX_SOUND_BUFFER_H

#include "audio_system.h"
#include "declspec.h"
#include <cstdint>
#include <cstddef>
#include <atomic>

namespace KameMix {

struct SoundSharedData;

class KAMEMIX_DECLSPEC SoundBuffer {
public:
  SoundBuffer();
  explicit SoundBuffer(const char *filename);
  SoundBuffer(const SoundBuffer &other);
  SoundBuffer(SoundBuffer &&other);
  SoundBuffer& operator=(const SoundBuffer &other);
  SoundBuffer& operator=(SoundBuffer &&other);
  ~SoundBuffer();

  bool load(const char *filename);
  bool loadOGG(const char *filename);
  bool loadWAV(const char *filename);
  bool isLoaded() const;
  // Release loaded file and free allocated data. isLoaded() is false after.
  void release();
  int useCount() const;

  // Pointer to currently loaded audio data.
  uint8_t* data();
  // Size in bytes of audio data in data() buffer.
  int size() const; 
  // Returns 1 for mono, 2 for stereo.
  int numChannels() const; 
  // Returns size of audio format * number of channels in bytes.
  int sampleBlockSize() const;

private:
  void incRefCount();

  SoundSharedData *sdata;
};

} // end namespace KameMix
#endif
