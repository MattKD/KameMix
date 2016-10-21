#ifndef KAME_MIX_STREAM_H
#define KAME_MIX_STREAM_H

#include "stream_buffer.h"
#include "declspec.h"

namespace KameMix {

class KAMEMIX_DECLSPEC Stream {
public:
  Stream();
  explicit Stream(const char *filename, double sec = 0.0);
  Stream(const Stream &other) = delete;
  Stream& operator=(const Stream &other) = delete;
  ~Stream();

  bool load(const char *filename, double sec = 0.0);
  bool loadOGG(const char *filename, double sec = 0.0);
  bool loadWAV(const char *filename, double sec = 0.0);
  void release();
  bool isLoaded();

  float getVolume() const;
  void setVolume(float v);

  int getGroup() const;
  void setGroup(int group_);
  void unsetGroup();

  float getX() const;
  float getY() const;
  void setPos(float x_, float y_);
  void moveBy(float dx, float dy);
  float getMaxDistance() const;
  void setMaxDistance(float distance);

  void useListener(bool use_listener_);
  bool usingListener() const;

  void play(int loops = 0, bool paused = false);
  void fadein(float fade_secs, int loops = 0, bool paused = false); 
  void playAt(double sec, int loops = 0, bool paused = false);
  void fadeinAt(double sec, float fade_secs, int loops = 0, 
                bool paused = false); 
  void halt(); // instant remove
  void stop(); // removes with min fade

  // fade_secs = 0 for instant remove, same as halt(); -1 for fastest fade,
  // same as stop()
  void fadeout(float fade_secs);
  bool isPlaying() const;
  bool isPlayingReal() const;
  void pause();
  void unpause();
  bool isPaused() const;
  void setLoopCount(int loops);

private:
  void readMore();

  StreamBuffer buffer;
  int group;
  AudioSystemMixIdx mix_idx;
  float volume;
  float x, y;
  float max_distance;
  bool use_listener;
  friend class AudioSystem;
};

} // end namespace KameMix

#endif
