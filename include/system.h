#ifndef KAME_MIX_SYSTEM_H
#define KAME_MIX_SYSTEM_H

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

enum OutAudioFormat {
  OutFormat_Float,
  OutFormat_S16
};

class KAMEMIX_DECLSPEC System {
public:
  // Initializes System. Must be called before all other KameMix 
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
  static double getMasterVolume();
  static void setMasterVolume(double volume);

  static int createGroup();
  static void setGroupVolume(int group, double volume);
  static double getGroupVolume(int group);

  static int getFrequency();
  static int getChannels();
  static OutAudioFormat getFormat();
  // Size in bytes of sample output format
  static int getFormatSize();

  static MallocFunc getMalloc();
  static FreeFunc getFree();
  static ReallocFunc getRealloc();

  // User callbacks to call when a Sound or Stream finishes playing.
  // All System functions are thread safe to use in callbacks besides 
  // resetting the callbacks. Sound/Stream are NOT thread safe, so the
  // passed in Sound*/Stream* should not be used to call their methods.
  static void setSoundFinished(SoundFinishedFunc func, void *udata);
  static void setStreamFinished(StreamFinishedFunc func, void *udata);

  static void setListenerPos(float x, float y);
  static void getListenerPos(float &x, float &y);

  static const int MaxFormatSize = sizeof(float);

private:
  static void audioCallback(void *udata, uint8_t *stream, const int len);
};

} // end namespace KameMix

#endif
