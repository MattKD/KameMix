#ifndef KAMEMIX_SOUND_H
#define KAMEMIX_SOUND_H

#include "Kamemix/KameMix.h"

namespace KameMix {

class Sound {
public:
  Sound();
  explicit Sound(const char *filename); 
  Sound(const Sound &other);
  Sound& operator=(const Sound &other);
  ~Sound();

  bool load(const char *filename);
  void release(); 
  bool isLoaded() const;

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
  void detach();
  bool isPlaying() const;
  void pause();
  void unpause();
  bool isPaused() const;
  void setLoopCount(int loops);

private:
  KameMix_Sound *sound;
  KameMix_Channel channel;
  int group;
  float volume;
  float x, y;
  float max_distance;
  friend class System;
};

inline
Sound::Sound() : 
  sound{nullptr}, group{-1}, volume{1.0f}, x{0}, y{0}, max_distance{0} 
{ 
  KameMix_unsetChannel(channel);
}

inline
Sound::Sound(const char *filename) : 
  group{-1}, volume{1.0f}, x{0}, y{0}, max_distance{0} 
{ 
  KameMix_unsetChannel(channel);
  sound = KameMix_loadSound(filename);
}

inline
Sound::Sound(const Sound &other) : 
  group{other.group}, volume{other.volume}, x{other.x}, y{other.y},
  max_distance{other.max_distance} 
{ 
  KameMix_unsetChannel(channel);
  sound = other.sound;
  if (sound) {
    KameMix_incSoundRef(sound);
  }
}

inline
Sound& Sound::operator=(const Sound &other)
{
  if (this != &other) {
    release();
    sound = other.sound;
    if (sound) {
      KameMix_incSoundRef(sound);
    }
    group = other.group;
    volume = other.volume;
    x = other.x;
    y = other.y;
    max_distance = other.max_distance;
  }
  return *this;
}

inline
Sound::~Sound() 
{ 
  release();
}

inline
bool Sound::load(const char *filename) 
{ 
  release();
  sound = KameMix_loadSound(filename);
  return sound != nullptr;
}

inline
void Sound::release() 
{ 
  stop();
  KameMix_freeSound(sound);
  sound = nullptr;
  KameMix_unsetChannel(channel);
}

inline
bool Sound::isLoaded() const { return sound != nullptr; }

inline
float Sound::getVolume() const { return volume; }

inline
void Sound::setVolume(float v) 
{ 
  volume = v; 
  KameMix_setVolume(channel, v);
}

inline
float Sound::getX() const { return x; }

inline
float Sound::getY() const { return y; }

inline
void Sound::setPos(float x_, float y_)
{
  x = x_;
  y = y_;
  KameMix_setPos(channel, x, y);
}

inline
void Sound::moveBy(float dx, float dy)
{
  x += dx;
  y += dy;
  KameMix_setPos(channel, x, y);
}

inline
float Sound::getMaxDistance() const { return max_distance; }

inline
void Sound::setMaxDistance(float distance) 
{ 
  KameMix_setMaxDistance(channel, distance);
  max_distance = distance; 
}

inline
int Sound::getGroup() const { return group; }

inline
void Sound::setGroup(int group_) 
{ 
  group = group_; 
  KameMix_setGroup(channel, group); 
}

inline
void Sound::unsetGroup() { setGroup(-1); }

inline
void Sound::play(int loops, bool paused) 
{
  fadein(-1, loops, paused);
}

inline
void Sound::fadein(float fade_secs, int loops, bool paused) 
{
  if (isLoaded()) {
    channel = KameMix_playSound(sound, channel, 0, loops, volume, fade_secs,
                                x, y, max_distance, group, paused);
  }
}

inline
void Sound::playAt(double sec, int loops, bool paused)
{
  fadeinAt(sec, -1, loops, paused);
}

inline
void Sound::fadeinAt(double sec, float fade_secs, int loops, bool paused) 
{
  if (isLoaded()) {
    channel = KameMix_playSound(sound, channel, sec, loops, volume, 
                                fade_secs, x, y, max_distance, group, paused); 
  }
}

// Stops sound without a fade, and detaches.
inline
void Sound::halt() 
{ 
  KameMix_halt(channel);
  KameMix_unsetChannel(channel);
} 

// Fades out very fast to prevent popping, and detaches.
inline
void Sound::stop() 
{ 
  KameMix_stop(channel);
  KameMix_unsetChannel(channel);
}

// fade_secs = 0.0f will halt Sound, -1 will have minimum fade. 
// Does not detach.
inline
void Sound::fadeout(float fade_secs)
{
  KameMix_fadeout(channel, fade_secs);
  KameMix_unsetChannel(channel);
}

// Sound will continue playing in mixer, but you no longer have control of
// it. unpause is called before detaching. 
inline
void Sound::detach()
{
  unpause();
  KameMix_unsetChannel(channel);
}

inline
bool Sound::isPlaying() const 
{ 
  return KameMix_isPlaying(channel) == 1; 
}

inline
void Sound::pause()
{
  KameMix_pause(channel);
}

inline
void Sound::unpause()
{
  KameMix_unpause(channel);
}

inline
bool Sound::isPaused() const
{
  return KameMix_isPaused(channel) == 1;
}

inline
void Sound::setLoopCount(int loops)
{
  KameMix_setLoopCount(channel, loops);
}

} // end namespace KameMix
#endif
