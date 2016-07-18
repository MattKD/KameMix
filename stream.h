#ifndef KAME_MIX_STREAM_H
#define KAME_MIX_STREAM_H

#include "audio_system.h"
#include "stream_buffer.h"

namespace KameMix {

class Stream {
public:
  Stream::Stream() : 
    group{nullptr}, mix_idx{-1}, volume{1.0f}, 
    x{0}, y{0}, max_distance{1.0f}, listener{nullptr} { }

  explicit
  Stream::Stream(const char *filename, double sec = 0.0) : 
    group{nullptr}, mix_idx{-1}, volume{1.0f}, 
    x{0}, y{0}, max_distance{1.0f}, listener{nullptr} 
  { 
    load(filename, sec); 
  }

  Stream::Stream(const Stream &other) = delete;
  Stream& Stream::operator=(const Stream &other) = delete;

  Stream::~Stream() { stop(); }

  bool Stream::load(const char *filename, double sec = 0.0) 
  { 
    stop();
    if (buffer.load(filename, sec)) {
      StreamBuffer &buf = buffer;
      std::thread thrd([buf]() mutable { buf.readMore(); }); 
      thrd.detach();
      return true;
    }
    return false;
  }

  bool Stream::loadOGG(const char *filename, double sec = 0.0) 
  { 
    stop();
    if (buffer.loadOGG(filename, sec)) {
      StreamBuffer &buf = buffer;
      std::thread thrd([buf]() mutable { buf.readMore(); }); 
      thrd.detach();
      return true;
    }
    return false;
  }

  void Stream::release() 
  { 
    stop(); 
    buffer.release(); 
  }

  bool Stream::isLoaded() { return buffer.isLoaded(); }

  float Stream::getVolume() const { return volume; }
  void Stream::setVolume(float v) { volume = v; }
  float getVolumeInGroup() const
  {
    if (group) {
      return group->getVolume() * volume;
    }
    return volume;
  }

  float Stream::getX() const { return x; }
  float Stream::getY() const { return y; }
  void Stream::setPos(float x_, float y_)
  {
    x = x_;
    y = y_;
  }
  void Stream::moveBy(float dx, float dy)
  {
    x += dx;
    y += dy;
  }
  float Stream::getMaxDistance() const { return max_distance; }
  void Stream::setMaxDistance(float distance) { max_distance = distance; }

  void getRelativeDistance(float &x, float &y) const
  {
    x = this->x;
    y = this->y;
    if (listener) {
      x = (x - listener->getX()) / getMaxDistance();
      y = (y - listener->getY()) / getMaxDistance();
    }
  }

  Group* Stream::getGroup() const { return group; }
  void Stream::setGroup(Group &group) { this->group = &group; }
  Listener* Stream::getListener() const { return listener; }
  void Stream::setListener(Listener &listener) { this->listener = &listener; }

  void Stream::play(int loops, bool paused = false) 
  {
    if (isLoaded()) {
      stop();
      // After stop, startPos can be called without lock
      int start_pos = buffer.startPos();
      if (start_pos == -1) { // start not in buffer
        // read start of stream into main buffer, so no swap needed
        if (!buffer.setPos(0.0, true)) { 
          return;
        }
        StreamBuffer &buf = buffer;
        std::thread thrd([buf]() mutable { buf.readMore(); }); 
        thrd.detach();
        start_pos = 0;
      }

      AudioSystem::addStream(this, loops, start_pos, paused);
    }
  }

  void Stream::playAt(int loops, double sec, bool paused = false) 
  {
    if (isLoaded()) {
      stop();
      if (sec < 0.0 || sec >= buffer.totalTime()) {
        sec = 0.0;
      }
      // After stop, getPos can be called without lock
      int byte_pos = buffer.getPos(sec);
      if (byte_pos == -1) { // pos not in buffer
        // read stream at new pos into main buffer, so no swap needed
        if (!buffer.setPos(sec, true)) { 
          return;
        }

        StreamBuffer &buf = buffer;
        std::thread thrd([buf]() mutable { buf.readMore(); }); 
        thrd.detach();
        byte_pos = 0;
      }

      AudioSystem::addStream(this, loops, byte_pos, paused);
    }
  }

  void Stream::stop()
  {
    if (mix_idx != -1) {
      AudioSystem::removeSound(mix_idx);
      mix_idx = -1;
    }
  }

  bool Stream::isPlaying() const
  {
    return mix_idx != -1 && AudioSystem::isSoundFinished(mix_idx) == false;
  }

  void Stream::setPaused(bool paused)
  {
    if (mix_idx != -1) {
      AudioSystem::pauseSound(mix_idx, paused);
    }
  }

  bool Stream::isPaused() const
  {
    return mix_idx != -1 && AudioSystem::isSoundPaused(mix_idx);
  }

private:
  StreamBuffer buffer;
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
