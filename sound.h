#ifndef SOUND_H
#define SOUND_H

#include "audio_system.h"
#include "sound_buffer.h"

namespace KameMix {

class Sound {
public:
  Sound::Sound() : 
    group{nullptr}, mix_idx{-1}, volume{1.0f}, 
    x{0}, y{0}, max_distance{1.0f}, listener{nullptr} { }

  explicit
  Sound::Sound(const char *filename, float volume = 1.0f) : 
    group{nullptr}, buffer(filename), mix_idx{-1}, volume{volume}, 
    x{0}, y{0}, max_distance{1.0f}, listener{nullptr} { }

  explicit
  Sound::Sound(const SoundBuffer &buf, float volume = 1.0f) :
    group{nullptr}, buffer(buf), mix_idx{-1}, volume{volume}, 
    x{0}, y{0}, max_distance{1.0f}, listener{nullptr} { }

  Sound::Sound(const Sound &other) : 
    buffer(other.buffer), group{other.group}, mix_idx{-1}, volume{other.volume}, 
    x{other.x}, y{other.y}, max_distance{other.max_distance}, 
    listener{other.listener} { }

  Sound& Sound::operator=(const Sound &other)
  {
    if (this != &other) {
      buffer = other.buffer;
      group = other.group;
      volume = other.volume;
      x = other.x;
      y = other.y;
      max_distance = other.max_distance;
      listener = other.listener;
      stop();
    }
    return *this;
  }

  Sound::~Sound() { stop(); }

  const SoundBuffer& Sound::getSoundBuffer() const { return buffer; }
  SoundBuffer& Sound::getSoundBuffer() { return buffer; }

  bool Sound::load(const char *filename) { return buffer.load(filename); }
  bool Sound::loadOGG(const char *filename) { return buffer.loadOGG(filename); }
  bool Sound::loadWAV(const char *filename) { return buffer.loadWAV(filename); }
  void Sound::release() { buffer.release(); }
  bool Sound::isLoaded() const { return buffer.isLoaded(); }

  float Sound::getVolume() const { return volume; }
  void Sound::setVolume(float v) { volume = v; }

  float Sound::getX() const { return x; }
  float Sound::getY() const { return y; }
  void Sound::setPos(float x_, float y_)
  {
    x = x_;
    y = y_;
  }
  void Sound::moveBy(float dx, float dy)
  {
    x += dx;
    y += dy;
  }
  float Sound::getMaxDistance() const { return max_distance; }
  void Sound::setMaxDistance(float distance) { max_distance = distance; }

  Group* Sound::getGroup() const { return group; }
  void Sound::setGroup(Group &group) { this->group = &group; }
  Listener* Sound::getListener() const { return listener; }
  void Sound::setListener(Listener &listener) { this->listener = &listener; }

  void Sound::play(int loops) 
  {
    if (mix_idx == -1) {
      AudioSystem::addSound(this, loops);
    } else {
      AudioSystem::replaySound(mix_idx, loops);
    }
  }

  void Sound::stop()
  {
    if (mix_idx != -1) {
      AudioSystem::removeSound(mix_idx);
    }
  }

  bool Sound::isPlaying() const
  {
    return mix_idx != -1;
  }

  void Sound::setPaused(bool paused)
  {
    if (mix_idx != -1) {
      AudioSystem::pauseSound(mix_idx, paused);
    }
  }

  bool Sound::isPaused() const
  {
    if (mix_idx != -1) {
      return AudioSystem::isSoundPaused(mix_idx);
    }
    return false;
  }

  /*
  void setSoundPos(int sec, int msec);
  void rewind(int sec, int msec);
  void fastforward(int sec, int msec);
  */

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