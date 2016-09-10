#ifndef KAME_MIX_SOUND_H
#define KAME_MIX_SOUND_H

#include "audio_system.h"
#include "sound_buffer.h"

namespace KameMix {

class Sound {
public:
  Sound::Sound() : 
    group{nullptr}, mix_idx{-1}, volume{1.0f}, 
    x{0}, y{0}, max_distance{1.0f}, listener{nullptr} { }

  explicit
  Sound::Sound(const char *filename) : 
    group{nullptr}, buffer(filename), mix_idx{-1}, volume{1.0f}, 
    x{0}, y{0}, max_distance{1.0f}, listener{nullptr} { }

  Sound::Sound(const Sound &other) : 
    buffer(other.buffer), group{other.group}, mix_idx{-1}, volume{other.volume}, 
    x{other.x}, y{other.y}, max_distance{other.max_distance}, 
    listener{other.listener} { }

  Sound& Sound::operator=(const Sound &other)
  {
    if (this != &other) {
      stop();
      buffer = other.buffer;
      group = other.group;
      volume = other.volume;
      x = other.x;
      y = other.y;
      max_distance = other.max_distance;
      listener = other.listener;
    }
    return *this;
  }

  Sound::~Sound() { stop(); }

  bool Sound::load(const char *filename) { return buffer.load(filename); }
  bool Sound::loadOGG(const char *filename) 
  { 
    stop();
    return buffer.loadOGG(filename); 
  }
  bool Sound::loadWAV(const char *filename) 
  { 
    stop();
    return buffer.loadWAV(filename); 
  }
  void Sound::release() 
  { 
    stop(); 
    buffer.release(); 
  }
  bool Sound::isLoaded() const { return buffer.isLoaded(); }

  float getVolume() const { return volume; }
  void setVolume(float v) { volume = v; }
  float getVolumeInGroup() const
  {
    if (group) {
      return group->getVolume() * volume;
    }
    return volume;
  }

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

  void getRelativeDistance(float &x, float &y) const
  {
    x = this->x;
    y = this->y;
    if (listener) {
      x = (x - listener->getX()) / getMaxDistance();
      y = (y - listener->getY()) / getMaxDistance();
    }
  }

  Group* Sound::getGroup() const { return group; }
  void Sound::setGroup(Group &group) { this->group = &group; }
  Listener* Sound::getListener() const { return listener; }
  void Sound::setListener(Listener &listener) { this->listener = &listener; }

  void Sound::play(int loops, bool paused = false, float fade_secs = 0.0f) 
  {
    if (buffer.isLoaded()) {
      stop();
      AudioSystem::addSound(this, loops, 0, paused, fade_secs);
    }
  }

  void Sound::playAt(int loops, double sec, bool paused = false, 
                     float fade_secs = 0.0f) 
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

  void Sound::stop(float fade_secs = 0.0f)
  {
    if (mix_idx != -1) {
      AudioSystem::removeSound(mix_idx, fade_secs);
      if (fade_secs <= 0.0f) {
        mix_idx = -1;
      }
    }
  }

  bool Sound::isPlaying() const
  {
    return mix_idx != -1 && AudioSystem::isSoundFinished(mix_idx) == false;
  }

  void Sound::setPaused(bool paused)
  {
    if (mix_idx != -1) {
      AudioSystem::pauseSound(mix_idx, paused);
    }
  }

  bool Sound::isPaused() const
  {
    return mix_idx != -1 && AudioSystem::isSoundPaused(mix_idx);
  }

private:
  SoundBuffer buffer;
  Group *group;
  int mix_idx;
  float volume;
  float x, y;
  float max_distance;
  Listener *listener;
  friend class AudioSystem;
};

} // end namespace KameMix

#endif