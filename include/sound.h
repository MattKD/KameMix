#ifndef KAME_MIX_SOUND_H
#define KAME_MIX_SOUND_H

#include "sound_buffer.h"

namespace KameMix {

class KAMEMIX_DECLSPEC Sound {
public:
  Sound();
  explicit Sound(const char *filename); 
  Sound(const Sound &other);
  Sound& operator=(const Sound &other);
  ~Sound();

  bool load(const char *filename);
  bool loadOGG(const char *filename); 
  bool loadWAV(const char *filename); 
  void release(); 
  bool isLoaded() const;
  int useCount() const;

  float getVolume() const;
  void setVolume(float v);

  float getX() const;
  float getY() const;
  void setPos(float x_, float y_);
  void moveBy(float dx, float dy);
  float getMaxDistance() const;
  // Must be set to greater than 0 to use position.
  void setMaxDistance(float distance);

  int getGroup() const;
  void setGroup(int group_);
  void unsetGroup();

  void play(int loops = 0, bool paused = false); 
  void fadein(float fade_secs, int loops = 0, bool paused = false);
  void playAt(double sec, int loops = 0, bool paused = false);
  void fadeinAt(double sec, float fade_secs, int loops = 0, 
                bool paused = false); 
  void halt(); // instant remove
  void stop(); // removes with min fade
  void fadeout(float fade_secs);
  bool isPlaying() const;
  bool isPlayingReal() const;
  void pause();
  void unpause();
  bool isPaused() const;
  void setLoopCount(int loops);

private:
  SoundBuffer buffer;
  int group;
  int mix_idx;
  float volume;
  float x, y;
  float max_distance;
  friend class System;
};

} // end namespace KameMix

#endif
