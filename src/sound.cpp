#include "sound.h"
#include "system.h"

namespace KameMix {

Sound::Sound() : 
  group{-1}, mix_idx{-1}, volume{1.0f}, 
  x{0}, y{0}, max_distance{0} { }

Sound::Sound(const char *filename) : 
  buffer(filename), group{-1}, mix_idx{-1},  
  volume{1.0f}, x{0}, y{0}, max_distance{0} { }

Sound::Sound(const Sound &other) : 
  buffer(other.buffer), group{other.group}, mix_idx{-1},
  volume{other.volume}, x{other.x}, y{other.y},
  max_distance{other.max_distance} { }

Sound& Sound::operator=(const Sound &other)
{
  if (this != &other) {
    stop();
    detach(); // mix_idx = -1 after
    buffer = other.buffer;
    group = other.group;
    volume = other.volume;
    x = other.x;
    y = other.y;
    max_distance = other.max_distance;
  }
  return *this;
}

Sound::~Sound() 
{ 
  stop();
  detach();
}

bool Sound::load(const char *filename) 
{ 
  stop();
  detach();
  return buffer.load(filename); 
}

bool Sound::loadOGG(const char *filename) 
{ 
  stop();
  detach();
  return buffer.loadOGG(filename); 
}

bool Sound::loadWAV(const char *filename) 
{ 
  stop();
  detach();
  return buffer.loadWAV(filename); 
}

void Sound::release() 
{ 
  stop();
  detach();
  buffer.release(); 
}

bool Sound::isLoaded() const { return buffer.isLoaded(); }
int Sound::useCount() const { return buffer.useCount(); }

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

int Sound::getGroup() const { return group; }
void Sound::setGroup(int group_) { group = group_; }
void Sound::unsetGroup() { group = -1; }

void Sound::play(int loops, bool paused) 
{
  fadein(-1, loops, paused);
}

void Sound::playDetached(int loops) 
{
  fadeinDetached(-1, loops);
}

void Sound::fadein(float fade_secs, int loops, bool paused) 
{
  if (buffer.isLoaded()) {
    stop();
    detach();
    mix_idx = System::addSound(this, loops, 0, paused, fade_secs);
  }
}

void Sound::fadeinDetached(float fade_secs, int loops) 
{
  if (buffer.isLoaded()) {
    stop();
    detach();
    System::addSoundDetached(this, loops, 0, fade_secs);
  }
}

void Sound::playAt(double sec, int loops, bool paused)
{
  fadeinAt(sec, -1, loops, paused);
}

void Sound::playAtDetached(double sec, int loops)
{
  fadeinAtDetached(sec, -1, loops);
}

static inline
int bytePos(double sec, const SoundBuffer &sbuf)
{
  int sample_pos = (int) (sec * System::getFrequency());
  int byte_pos = sample_pos * sbuf.sampleBlockSize();
  if (byte_pos < 0 || byte_pos >= sbuf.size()) {
    byte_pos = 0;
  }
  return byte_pos;
}

void Sound::fadeinAt(double sec, float fade_secs, int loops, bool paused) 
{
  if (buffer.isLoaded()) {
    stop();
    detach();
    int byte_pos = bytePos(sec, buffer);
    mix_idx = System::addSound(this, loops, byte_pos, paused, fade_secs);
  }
}

void Sound::fadeinAtDetached(double sec, float fade_secs, int loops) 
{
  if (buffer.isLoaded()) {
    stop();
    detach();
    int byte_pos = bytePos(sec, buffer);
    System::addSoundDetached(this, loops, byte_pos, fade_secs);
  }
}

void Sound::halt() { fadeout(0); } // instant remove
void Sound::stop() { fadeout(-1); } // removes with min fade

void Sound::fadeout(float fade_secs)
{
  if (isPlaying()) {
    if (fade_secs == 0) {
      System::haltSound(this);
      mix_idx = -1;
    } else {
      System::stopSound(mix_idx, fade_secs);
    }
  }
}

void Sound::detach()
{
  if (isPlaying()) {
    System::detachSound(this);
    mix_idx = -1;
  }
}

bool Sound::isPlaying() const { return mix_idx != -1; }

void Sound::pause()
{
  if (isPlaying()) {
    System::pauseSound(mix_idx);
  }
}

void Sound::unpause()
{
  if (isPlaying()) {
    System::unpauseSound(mix_idx);
  }
}

bool Sound::isPaused() const
{
  return isPlaying() && System::isSoundPaused(mix_idx);
}

void Sound::setLoopCount(int loops)
{
  if (isPlaying()) {
    System::setSoundLoopCount(mix_idx, loops);
  }
}

} // end namespace KameMix
