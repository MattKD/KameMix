#ifndef KAMEMIX_STREAM_H
#define KAMEMIX_STREAM_H

#include "KameMix.h"

namespace KameMix {

class Stream {
public:
  Stream();
  explicit Stream(const char *filename); 
  ~Stream();
  Stream(const Stream &other) = delete;
  Stream& operator=(const Stream &other) = delete;

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
  KameMix_Stream *stream;
  KameMix_Channel channel;
  int group;
  float volume;
  float x, y;
  float max_distance;
  friend class System;
};

inline
Stream::Stream() : 
  stream{nullptr}, group{-1}, volume{1.0f}, x{0}, y{0}, max_distance{0} 
{ 
  KameMix_unsetChannel(channel);
}

inline
Stream::Stream(const char *filename) : 
  group{-1}, volume{1.0f}, x{0}, y{0}, max_distance{0} 
{ 
  KameMix_unsetChannel(channel);
  stream = KameMix_loadStream(filename);
}

inline
Stream::~Stream() 
{ 
  release();
}

inline
bool Stream::load(const char *filename) 
{ 
  release();
  stream = KameMix_loadStream(filename);
  return stream != nullptr;
}

inline
void Stream::release() 
{ 
  stop();
  KameMix_unsetChannel(channel);
  KameMix_freeStream(stream);
  stream = nullptr;
}

inline
bool Stream::isLoaded() const { return stream != nullptr; }

inline
float Stream::getVolume() const { return volume; }

inline
void Stream::setVolume(float v) 
{ 
  volume = v; 
  KameMix_setVolume(channel, v);
}

inline
float Stream::getX() const { return x; }

inline
float Stream::getY() const { return y; }

inline
void Stream::setPos(float x_, float y_)
{
  x = x_;
  y = y_;
  KameMix_setPos(channel, x, y);
}

inline
void Stream::moveBy(float dx, float dy)
{
  x += dx;
  y += dy;
  KameMix_setPos(channel, x, y);
}

inline
float Stream::getMaxDistance() const { return max_distance; }

inline
void Stream::setMaxDistance(float distance) 
{ 
  KameMix_setMaxDistance(channel, distance);
  max_distance = distance; 
}

inline
int Stream::getGroup() const { return group; }

// must be group id from KameMix_createGroup, or -1 to unset
inline
void Stream::setGroup(int group_) 
{ 
  group = group_; 
  KameMix_setGroup(channel, group); 
}

inline
void Stream::unsetGroup() { setGroup(-1); }

inline
void Stream::play(int loops, bool paused) 
{
  fadein(-1, loops, paused);
}

inline
void Stream::fadein(float fade_secs, int loops, bool paused) 
{
  if (isLoaded()) {
    channel = KameMix_playStream(stream, channel, 0, loops, volume, fade_secs,
                                x, y, max_distance, group, paused);
  }
}

inline
void Stream::playAt(double sec, int loops, bool paused)
{
  fadeinAt(sec, -1, loops, paused);
}

inline
void Stream::fadeinAt(double sec, float fade_secs, int loops, bool paused) 
{
  if (isLoaded()) {
    channel = KameMix_playStream(stream, channel, sec, loops, volume, 
                                fade_secs, x, y, max_distance, group, paused); 
  }
}

// Stops stream without a fade, and detaches.
inline
void Stream::halt() 
{ 
  KameMix_halt(channel);
  KameMix_unsetChannel(channel);
} 

// Fades out very fast to prevent popping, does not detach.
inline
void Stream::stop() 
{ 
  KameMix_stop(channel);
  // don't unsetChannel incase halt is needed in play functions
}

// fade_secs = 0.0f will halt Stream, -1 will have minimum fade. 
// Does not detach.
inline
void Stream::fadeout(float fade_secs)
{
  KameMix_fadeout(channel, fade_secs);
  // don't unsetChannel incase halt is needed in play functions
}

// Stream will continue playing in mixer, but you no longer have control of
// it. unpause is called before detaching. isLoaded returns false after.
inline
void Stream::detach()
{
  unpause();
  KameMix_freeStream(stream);
  stream = nullptr;
  KameMix_unsetChannel(channel);
}

inline
bool Stream::isPlaying() const 
{ 
  return KameMix_isPlaying(channel) == 1; 
}

inline
void Stream::pause()
{
  KameMix_pause(channel);
}

inline
void Stream::unpause()
{
  KameMix_unpause(channel);
}

inline
bool Stream::isPaused() const
{
  return KameMix_isPaused(channel) == 1;
}

inline
void Stream::setLoopCount(int loops)
{
  KameMix_setLoopCount(channel, loops);
}

} // end namespace KameMix

#endif
