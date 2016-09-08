#ifndef AUDIO_SYSTEM_H
#define AUDIO_SYSTEM_H

#include <cstdint>
#include <cassert>

namespace KameMix {

typedef void* (*MallocFunc)(size_t len);
typedef void (*FreeFunc)(void *ptr);
typedef void* (*ReallocFunc)(void *ptr, size_t len);

class Sound;
class Stream;

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

// Only float supported for now
enum AudioFormat {
  FloatFormat
};

class AudioSystem {
public:
  // Initializes AudioSystem. Must be called before all other KameMix 
  // functions. Only call once in application, even if shutdown is called. 
  static bool init(int freq = 44100, 
                   int sample_buf_size = 2048, 
                   MallocFunc custom_malloc = nullptr,
                   FreeFunc custom_free = nullptr,
                   ReallocFunc custom_realloc = nullptr);
  static void shutdown();
  // Updates the volume and position of all playing sounds/streams, and
  // removes finished/stopped ones. Should be called after every game frame.
  // Must not be called concurrently with any other KameMix functions.
  static void update();

  // number of sounds playing including paused
  static int numberPlaying(); 
  static float getMastervolume() { return master_volume; }
  static void setMasterVolume(float volume) { master_volume = volume; }

  static int getFrequency() { return frequency; }
  static int getChannels() { return channels; }
  static AudioFormat getFormat() { return format; }
  // Size in bytes of sample output format
  static int getFormatSize() 
  { 
    if (format == FloatFormat) {
      return sizeof(float);
    }
    assert("Unknown AudioFormat");
    return 0;
  }

  static MallocFunc getMalloc() { return user_malloc; }
  static FreeFunc getFree() { return user_free; }
  static ReallocFunc getRealloc() { return user_realloc; }

  // Returns description string of last error. Do NOT delete string.
  static const char* getError() { return error_string; }
  // Uses same format specifiers as sprintf
  static void setError(const char *error, ...);

private:
  static void addSound(Sound *sound, int loops, int pos, bool paused,
                       float fade);
  static void addStream(Stream *stream, int loops, int pos, bool paused,
                        float fade);
  static void removeSound(int idx); // Sound and Stream
  static bool isSoundFinished(int idx);
  static void pauseSound(int idx, bool paused); // does nothing if finished.
  static bool isSoundPaused(int idx); // returns false if playing or finished.
  static void audioCallback(void *udata, uint8_t *stream, const int len);

  static int channels;
  static int frequency;
  static AudioFormat format;
  static float master_volume;
  static const int error_len = 256;
  static char error_string[error_len];
  static MallocFunc user_malloc;
  static FreeFunc user_free;
  static ReallocFunc user_realloc;

  friend class Sound;
  friend class Stream;
};

} // end namespace KameMix

#endif