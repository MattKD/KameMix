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
    group{-1}, volume{1.0f}, 
    x{0}, y{0}, max_distance{1.0f}, use_listener{false} { }

  explicit
  Stream(const char *filename, double sec = 0.0) : 
    group{-1}, volume{1.0f}, 
    x{0}, y{0}, max_distance{1.0f}, use_listener{false} 
  { 
    load(filename, sec); 
  }

  Stream(const Stream &other) = delete;
  Stream& operator=(const Stream &other) = delete;

  ~Stream() { halt(); }

  bool load(const char *filename, double sec = 0.0) 
  { 
    halt();
    if (buffer.load(filename, sec)) {
      readMore();
      return true;
    }
    return false;
  }

  bool loadOGG(const char *filename, double sec = 0.0) 
  { 
    halt();
    if (buffer.loadOGG(filename, sec)) {
      readMore();
      return true;
    }
    return false;
  }

  bool loadWAV(const char *filename, double sec = 0.0) 
  { 
    halt();
    if (buffer.loadWAV(filename, sec)) {
      readMore();
      return true;
    }
    return false;
  }

  void release() 
  { 
    halt(); 
    buffer.release(); 
  }

  bool isLoaded() { return buffer.isLoaded(); }

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
    if (isLoaded()) {
      halt();
      // After stop, startPos can be called without lock
      int start_pos = buffer.startPos();
      if (start_pos == -1) { // start not in buffer
        // read start of stream into main buffer, so no swap needed
        if (!buffer.setPos(0.0, true)) { 
          return;
        }
        readMore(); 
        start_pos = 0;
      }

      AudioSystem::addStream(this, loops, start_pos, paused, fade_secs);
    }
  }

  void playAt(int loops, double sec, bool paused = false)
  {
    fadeinAt(loops, sec, -1, paused);
  }

  void fadeinAt(int loops, double sec, float fade_secs, bool paused = false) 
  {
    if (isLoaded()) {
      halt();
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

        readMore();
        byte_pos = 0;
      }

      AudioSystem::addStream(this, loops, byte_pos, paused, fade_secs);
    }
  }

  void halt() { fadeout(0); } // instant remove
  void stop() { fadeout(-1); } // removes with min fade

  void fadeout(float fade_secs)
  {
    if (isPlaying()) {
      AudioSystem::removeStream(this, fade_secs);
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
  void readMore()
  {
    StreamBuffer &buf = buffer;
    std::thread thrd([buf]() mutable { buf.readMore(); }); 
    thrd.detach();
  }

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
