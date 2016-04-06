#ifndef AUDIO_SYSTEM_H
#define AUDIO_SYSTEM_H

#include <cstdint>

namespace KameMix {

typedef void* (*MallocFunc)(size_t len);
typedef void (*FreeFunc)(void *ptr);
typedef void* (*ReallocFunc)(void *ptr, size_t len);

class Sound;

class Group {
public:
  Group(float volume = 1.0f) : volume{volume} { }
  float getVolume() const { return volume; }
  void setVolume(float v) { volume = v; }

private:
  float volume;
};

class Listener {
public:
  Listener(float x, float y) : x{x}, y{y} { }
  void setPos(float x_, float y_) { x = x_; y = y_; }
  void moveBy(float dx, float dy) { x += dx; y += dy; }
  float getX() const { return x; }
  float getY() const { return y; }

private:
  float x, y;
};

class AudioSystem {
public:
  // Initializes AudioSystem. Must be called before all other AudioSystem,
  // Sound, and SoundBuffer functions. Only call once in application, even 
  // if shutdown is called. 
  static bool init(int num_sounds = 128, int freq = 44100, 
                   int sample_buf_size = 2048, 
                   MallocFunc custom_malloc = nullptr,
                   FreeFunc custom_free = nullptr,
                   ReallocFunc custom_realloc = nullptr);
  static void shutdown();
  // Updates the audio callback thread with new and changed data, such as
  // Sounds played and stopped, and changes in volume. Should be called
  // after every game frame.
  static void update();

  static int numberPlaying(); // number of sounds playing including paused
  static float getMastervolume() { return master_volume; }
  static void setMasterVolume(float volume);

  static int getFrequency() { return frequency; }
  static int getChannels() { return channels; }

  static MallocFunc getMalloc() { return km_malloc; }
  static FreeFunc getFree() { return km_free; }
  static ReallocFunc getRealloc() { return km_realloc; }

  // Returns description string of last error. Do NOT delete string.
  static const char* getError() { return error_string; }
  static void setError(const char *error) { error_string = error; }

private:
  static void addSound(Sound *sound, int loops);
  static void removeSound(int idx);
  static void setSoundPos(int idx, int pos);
  static void replaySound(int idx, int loops);
  static void pauseSound(int idx, bool paused);
  static bool isSoundPaused(int idx);

  static int channels;
  static int frequency;
  static float master_volume;
  static const char *error_string;
  static MallocFunc km_malloc;
  static FreeFunc km_free;
  static ReallocFunc km_realloc;

  friend class SoundBuffer;
  friend class Sound;
};

} // end namespace KameMix

#endif