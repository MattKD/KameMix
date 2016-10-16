#ifndef KAME_MIX_AUDIO_SYSTEM_H
#define KAME_MIX_AUDIO_SYSTEM_H

#include "declspec.h"
#include <cstdint>
#include <cassert>
#include <cstddef>

namespace KameMix {

class Sound;
class Stream;

typedef void* (*MallocFunc)(size_t len);
typedef void (*FreeFunc)(void *ptr);
typedef void* (*ReallocFunc)(void *ptr, size_t len);
typedef void (*SoundFinishedFunc) (Sound *sound, void *udata);
typedef void (*StreamFinishedFunc) (Stream *stream, void *udata);

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

enum OutAudioFormat {
  OutFormat_Float,
  OutFormat_S16
};

class DECLSPEC AudioSystem {
public:
  // Initializes AudioSystem. Must be called before all other KameMix 
  // functions. Only call once in application, even if shutdown is called. 
  static bool init(int freq = 44100, 
                   int sample_buf_size = 2048, 
                   OutAudioFormat format = OutFormat_Float,
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
  static double getMastervolume() { return master_volume; }
  static void setMasterVolume(double volume) { master_volume = volume; }

  static int getFrequency() { return frequency; }
  static int getChannels() { return channels; }
  static OutAudioFormat getFormat() { return format; }

  // Size in bytes of sample output format
  static int getFormatSize() 
  { 
    switch (format) {
    case OutFormat_Float:
      return sizeof(float);
    case OutFormat_S16:
      return sizeof(int16_t);
    }
    assert("Unknown AudioFormat");
    return 0;
  }

  static MallocFunc getMalloc() { return user_malloc; }
  static FreeFunc getFree() { return user_free; }
  static ReallocFunc getRealloc() { return user_realloc; }

  static void setSoundFinished(SoundFinishedFunc func, void *udata)
  {
    sound_finished = func;
    sound_finished_data = udata;
  }

  static void setStreamFinished(StreamFinishedFunc func, void *udata)
  {
    stream_finished = func;
    stream_finished_data = udata;
  }

  static const int MaxFormatSize = sizeof(float);

private:
  static void addSound(Sound *sound, int loops, int pos, bool paused,
                       float fade);
  static void addStream(Stream *stream, int loops, int pos, bool paused,
                        float fade);
  static void removeSound(Sound *sound, int idx, float fade_secs);
  static void removeStream(Stream *stream, int idx, float fade_secs);
  static bool isSoundFinished(int idx);
  static void pauseSound(int idx); 
  static void unpauseSound(int idx); 
  // returns false if playing or finished.
  static bool isSoundPaused(int idx); 
  static void audioCallback(void *udata, uint8_t *stream, const int len);

  static void soundFinished(Sound *sound)
  {
    if (sound_finished) {
      sound_finished(sound, sound_finished_data);
    }
  }

  static void streamFinished(Stream *stream)
  {
    if (stream_finished) {
      stream_finished(stream, stream_finished_data);
    }
  }

  static int channels;
  static int frequency;
  static OutAudioFormat format;
  static double master_volume;
  static MallocFunc user_malloc;
  static FreeFunc user_free;
  static ReallocFunc user_realloc;
  static SoundFinishedFunc sound_finished;
  static StreamFinishedFunc stream_finished;
  static void *sound_finished_data;
  static void *stream_finished_data;

  friend class Sound;
  friend class Stream;
};

} // end namespace KameMix

#endif
