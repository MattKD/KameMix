#include "stream.h"
#include "system.h"
#include <thread>

namespace KameMix {

Stream::Stream() : 
  group{-1}, mix_idx{-1}, volume{1.0f}, x{0}, y{0}, max_distance{1.0f} { }

Stream::Stream(const char *filename, double sec) : 
  group{-1}, mix_idx{-1}, volume{1.0f}, x{0}, y{0}, max_distance{0}
{ 
  load(filename, sec); 
}

Stream::~Stream() { halt(); }

bool Stream::load(const char *filename, double sec) 
{ 
  halt();
  if (buffer.load(filename, sec)) {
    readMore();
    return true;
  }
  return false;
}

bool Stream::loadOGG(const char *filename, double sec) 
{ 
  halt();
  if (buffer.loadOGG(filename, sec)) {
    readMore();
    return true;
  }
  return false;
}

bool Stream::loadWAV(const char *filename, double sec) 
{ 
  halt();
  if (buffer.loadWAV(filename, sec)) {
    readMore();
    return true;
  }
  return false;
}

void Stream::release() 
{ 
  halt(); 
  buffer.release(); 
}

bool Stream::isLoaded() { return buffer.isLoaded(); }

float Stream::getVolume() const { return volume; }
void Stream::setVolume(float v) { volume = v; }

int Stream::getGroup() const { return group; }
void Stream::setGroup(int group_) { group = group_; }
void Stream::unsetGroup() { group = -1; }

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

void Stream::play(int loops, bool paused)
{
  fadein(-1, loops, paused);
}

void Stream::fadein(float fade_secs, int loops, bool paused) 
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

    System::addStream(this, loops, start_pos, paused, fade_secs);
  }
}

void Stream::playAt(double sec, int loops, bool paused)
{
  fadeinAt(sec, -1, loops, paused);
}

void Stream::fadeinAt(double sec, float fade_secs, int loops, bool paused) 
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

    System::addStream(this, loops, byte_pos, paused, fade_secs);
  }
}

void Stream::halt() { fadeout(0); }
void Stream::stop() { fadeout(-1); }

void Stream::fadeout(float fade_secs)
{
  if (isPlaying()) {
    if (fade_secs == 0) {
      System::removeSound(mix_idx);
      mix_idx = -1;
    } else {
      System::removeSound(mix_idx, fade_secs);
    }
  }
}

bool Stream::isPlaying() const { return mix_idx != -1; }

void Stream::pause()
{
  if (isPlaying()) {
    System::pauseSound(mix_idx);
  }
}

void Stream::unpause()
{
  if (isPlaying()) {
    System::unpauseSound(mix_idx);
  }
}

bool Stream::isPaused() const
{
  return isPlaying() && System::isSoundPaused(mix_idx);
}

void Stream::setLoopCount(int loops)
{
  if (isPlaying()) {
    System::setSoundLoopCount(mix_idx, loops);
  }
}

void Stream::readMore()
{
  StreamBuffer &buf = buffer;
  std::thread thrd([buf]() mutable { buf.readMore(); }); 
  thrd.detach();
}

} // end namespace KameMix