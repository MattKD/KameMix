#ifndef KAME_MIX_SOUND_H
#define KAME_MIX_SOUND_H

#include "audio_system.h"
#include "sound_buffer.h"
#include "declspec.h"

namespace KameMix {

class KAMEMIX_DECLSPEC Sound {
public:
  Sound() : 
    group{-1}, volume{1.0f}, 
    x{0}, y{0}, max_distance{1.0f}, use_listener{false} { }

  explicit
  Sound(const char *filename) : 
    buffer(filename), group{-1}, 
    volume{1.0f}, x{0}, y{0}, max_distance{1.0f}, use_listener{false} { }

  Sound(const Sound &other) : 
    buffer(other.buffer), group{other.group}, 
    volume{other.volume}, x{other.x}, y{other.y},
    max_distance{other.max_distance}, use_listener{other.use_listener} { }

  Sound& operator=(const Sound &other)
  {
    if (this != &other) {
      halt();
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

  bool load(const char *filename) 
  { 
    halt();
    return buffer.load(filename); 
  }
  bool loadOGG(const char *filename) 
  { 
    halt();
    return buffer.loadOGG(filename); 
  }
  bool loadWAV(const char *filename) 
  { 
    halt();
    return buffer.loadWAV(filename); 
  }
  void release() 
  { 
    halt(); 
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

  void play(int loops = 0, bool paused = false) 
  {
    fadein(-1, loops, paused);
  }

  void fadein(float fade_secs, int loops = 0, bool paused = false) 
  {
    if (buffer.isLoaded()) {
      halt();
      AudioSystem::addSound(this, loops, 0, paused, fade_secs);
    }
  }

  void playAt(double sec, int loops = 0, bool paused = false)
  {
    fadeinAt(sec, -1, loops, paused);
  }

  void fadeinAt(double sec, float fade_secs, int loops = 0, bool paused = false) 
  {
    if (buffer.isLoaded()) {
      halt();
      int sample_pos = (int) (sec * AudioSystem::getFrequency());
      int byte_pos = sample_pos * buffer.sampleBlockSize();
      if (byte_pos < 0 || byte_pos >= buffer.size()) {
        byte_pos = 0;
      }
      AudioSystem::addSound(this, loops, byte_pos, paused, fade_secs);
    }
  }

  void halt() { fadeout(0); } // instant remove
  void stop() { fadeout(-1); } // removes with min fade

  void fadeout(float fade_secs)
  {
    if (isPlaying()) {
      AudioSystem::removeSound(this, fade_secs);
    }
  }

  bool isPlaying() const { return mix_idx.isSet(); }

  bool isPlayingReal() const
  {
    return isPlaying() && AudioSystem::isSoundFinished(mix_idx) == false;
  }

  void pause()
  {
    if (isPlaying()) {
      AudioSystem::pauseSound(mix_idx);
    }
  }

  void unpause()
  {
    if (isPlaying()) {
      AudioSystem::unpauseSound(mix_idx);
    }
  }

  bool isPaused() const
  {
    return isPlaying() && AudioSystem::isSoundPaused(mix_idx);
  }

  void setLoopCount(int loops)
  {
    if (isPlaying()) {
      AudioSystem::setLoopCount(mix_idx, loops);
    }
  }

private:
  SoundBuffer buffer;
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
