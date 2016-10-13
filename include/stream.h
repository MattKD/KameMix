#ifndef KAME_MIX_STREAM_H
#define KAME_MIX_STREAM_H

#include "audio_system.h"
#include "stream_buffer.h"
#include "declspec.h"
#include <thread>

namespace KameMix {

class DECLSPEC Stream {
public:
  Stream() : 
    group{nullptr}, mix_idx{-1}, volume{1.0f}, 
    x{0}, y{0}, max_distance{1.0f}, listener{nullptr} { }

  explicit
  Stream(const char *filename, double sec = 0.0) : 
    group{nullptr}, mix_idx{-1}, volume{1.0f}, 
    x{0}, y{0}, max_distance{1.0f}, listener{nullptr} 
  { 
    load(filename, sec); 
  }

  Stream(const Stream &other) = delete;
  Stream& operator=(const Stream &other) = delete;

  ~Stream() { stop(); }

  bool load(const char *filename, double sec = 0.0) 
  { 
    stop();
    if (buffer.load(filename, sec)) {
      readMore();
      return true;
    }
    return false;
  }

  bool loadOGG(const char *filename, double sec = 0.0) 
  { 
    stop();
    if (buffer.loadOGG(filename, sec)) {
      readMore();
      return true;
    }
    return false;
  }

  bool loadWAV(const char *filename, double sec = 0.0) 
  { 
    stop();
    if (buffer.loadWAV(filename, sec)) {
      readMore();
      return true;
    }
    return false;
  }

  void release() 
  { 
    stop(); 
    buffer.release(); 
  }

  bool isLoaded() { return buffer.isLoaded(); }

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

  Group* getGroup() const { return group; }
  void setGroup(Group &group) { this->group = &group; }
  Listener* getListener() const { return listener; }
  void setListener(Listener &listener) { this->listener = &listener; }

  void play(int loops, bool paused = false)
  {
    fadein(loops, 0.0f, paused);
  }

  void fadein(int loops, float fade_secs, bool paused = false) 
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

      AudioSystem::addStream(this, loops, start_pos, paused, fade_secs);
    }
  }

  void playAt(int loops, double sec, bool paused = false)
  {
    fadeinAt(loops, sec, 0.0f, paused);
  }

  void fadeinAt(int loops, double sec, float fade_secs, bool paused = false) 
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

      AudioSystem::addStream(this, loops, byte_pos, paused, fade_secs);
    }
  }

  void stop()
  {
    fadeout(0.0f); // removes and sets mix_idx to -1
  }

  void fadeout(float fade_secs)
  {
    if (mix_idx != -1) {
      AudioSystem::removeStream(this, mix_idx, fade_secs);
    }
  }

  bool isPlaying() const
  {
    return mix_idx != -1 && AudioSystem::isSoundFinished(mix_idx) == false;
  }

  void setPaused(bool paused)
  {
    if (mix_idx != -1) {
      AudioSystem::pauseSound(mix_idx, paused);
    }
  }

  bool isPaused() const
  {
    return mix_idx != -1 && AudioSystem::isSoundPaused(mix_idx);
  }

private:
  void readMore()
  {
    StreamBuffer &buf = buffer;
    std::thread thrd([buf]() mutable { buf.readMore(); }); 
    thrd.detach();
  }

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
