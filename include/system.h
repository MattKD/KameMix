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
  // functions, except setAlloc. If called again after a shutdown, all
  // Sounds and Streams must be released and reloaded.
  static bool init(int freq = 44100, 
                   int sample_buf_size = 2048, 
                   OutAudioFormat format = OutFormat_Float);
  static void shutdown();
  // Updates the volume and position of all playing sounds/streams, and
  // removes finished/stopped ones. Should be called after every game frame.
  // Must not be called concurrently with any other KameMix functions.
  static void update();

  // All three must be set before init if using custom allocators.
  static void setAlloc(MallocFunc custom_malloc, FreeFunc custom_free,
                       ReallocFunc custom_realloc);
  static MallocFunc getMalloc();
  static FreeFunc getFree();
  static ReallocFunc getRealloc();

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
  static int addSound(Sound *sound, int loops, int pos, bool paused, 
                      float fade);
  static int addStream(Stream *stream, int loops, int pos, bool paused, 
                       float fade);
  static void removeSound(int idx);  
  static void removeSound(int idx, float fade_secs);  
  static void setSoundLoopCount(int idx, int loops);
  static bool isSoundFinished(int idx);
  static void pauseSound(int idx);
  static void unpauseSound(int idx);
  static bool isSoundPaused(int idx);
  static void audioCallback(void *udata, uint8_t *stream, const int len);

  friend class Sound;
  friend class Stream;
};

} // end namespace KameMix

#endif
