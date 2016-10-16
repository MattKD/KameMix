#ifndef KAME_MIX_SOUND_H
#define KAME_MIX_SOUND_H

#include "audio_system.h"
#include "sound_buffer.h"
#include "declspec.h"

namespace KameMix {

class DECLSPEC Sound {
public:
  Sound() : 
    group{-1}, mix_idx{-1}, volume{1.0f}, 
    x{0}, y{0}, max_distance{1.0f}, use_listener{false} { }

  explicit
  Sound(const char *filename) : 
    buffer(filename), group{-1}, mix_idx{-1}, 
    volume{1.0f}, x{0}, y{0}, max_distance{1.0f}, use_listener{false} { }

  Sound(const Sound &other) : 
    buffer(other.buffer), group{other.group}, mix_idx{-1}, 
    volume{other.volume}, x{other.x}, y{other.y},
    max_distance{other.max_distance}, use_listener{other.use_listener} { }

  Sound& operator=(const Sound &other)
  {
    if (this != &other) {
      stop();
      buffer = other.buffer;
      group = other.group;
      volume = other.volume;
      x = other.x;
      y = other.y;
      max_distance = other.max_distance;
      use_listener = other.use_listener;
    }
    return *this;
  }

  ~Sound() { fadeout(0); }

  bool load(const char *filename) { return buffer.load(filename); }
  bool loadOGG(const char *filename) 
  { 
    stop();
    return buffer.loadOGG(filename); 
  }
  bool loadWAV(const char *filename) 
  { 
    stop();
    return buffer.loadWAV(filename); 
  }
  void release() 
  { 
    stop(); 
    buffer.release(); 
  }
  bool isLoaded() const { return buffer.isLoaded(); }

  float getVolume() const { return volume; }
  void setVolume(float v) { volume = v; }

  float getX() const { return x; }
  float getY() const { return y; }
  void setPos(float x_, float y_)
  {
    x = x_;
    y = y_;
  }
  void moveBy(float dx, float dy)
  {
    x += dx;
    y += dy;
  }
  float getMaxDistance() const { return max_distance; }
  void setMaxDistance(float distance) { max_distance = distance; }

  void useListener(bool use_listener_) { use_listener = use_listener_; }
  bool usingListener() const { return use_listener; }

  int getGroup() const { return group; }
  void setGroup(int group_) { group = group_; }
  void unsetGroup() { group = -1; }

  void play(int loops, bool paused = false) 
  {
    fadein(loops, -1, paused);
  }

  void fadein(int loops, float fade_secs, bool paused = false) 
  {
    if (buffer.isLoaded()) {
      stop();
      AudioSystem::addSound(this, loops, 0, paused, fade_secs);
    }
  }

  void playAt(int loops, double sec, bool paused = false)
  {
    fadeinAt(loops, sec, -1, paused);
  }

  void fadeinAt(int loops, double sec, float fade_secs, bool paused = false) 
  {
    if (buffer.isLoaded()) {
      stop();
      int sample_pos = (int) (sec * AudioSystem::getFrequency());
      int byte_pos = sample_pos * buffer.sampleBlockSize();
      if (byte_pos < 0 || byte_pos >= buffer.size()) {
        byte_pos = 0;
      }
      AudioSystem::addSound(this, loops, byte_pos, paused, fade_secs);
    }
  }

  void stop()
  {
    fadeout(-1); // removes and sets mix_idx to -1
  }

  void fadeout(float fade_secs)
  {
    if (mix_idx != -1) {
      AudioSystem::removeSound(this, mix_idx, fade_secs);
    }
  }

  bool isPlaying() const
  {
    return mix_idx != -1 && AudioSystem::isSoundFinished(mix_idx) == false;
  }

  void pause()
  {
    if (mix_idx != -1) {
      AudioSystem::pauseSound(mix_idx);
    }
  }

  void unpause()
  {
    if (mix_idx != -1) {
      AudioSystem::unpauseSound(mix_idx);
    }
  }

  bool isPaused() const
  {
    return mix_idx != -1 && AudioSystem::isSoundPaused(mix_idx);
  }

private:
  SoundBuffer buffer;
  int group;
  int mix_idx;
  float volume;
  float x, y;
  float max_distance;
  bool use_listener;
  friend class AudioSystem;
};

} // end namespace KameMix

#endif
