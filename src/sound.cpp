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
}

bool Sound::load(const char *filename) 
{ 
  stop();
  return buffer.load(filename); 
}

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
    mix_idx = System::addSound(this, loops, 0, paused, fade_secs);
  }
}

void Sound::fadeinDetached(float fade_secs, int loops) 
{
  if (buffer.isLoaded()) {
    stop();
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
    int byte_pos = bytePos(sec, buffer);
    mix_idx = System::addSound(this, loops, byte_pos, paused, fade_secs);
  }
}

void Sound::fadeinAtDetached(double sec, float fade_secs, int loops) 
{
  if (buffer.isLoaded()) {
    stop();
    int byte_pos = bytePos(sec, buffer);
    System::addSoundDetached(this, loops, byte_pos, fade_secs);
  }
}

// Stops sound without a fade, and detaches.
void Sound::halt() 
{ 
  if (isPlaying()) {
    System::haltSound(this);
    mix_idx = -1;
  }
} 

// Fades out very fast to prevent popping, and detaches.
void Sound::stop() 
{ 
  if (isPlaying()) {
    System::stopSound(this);
    mix_idx = -1;
  }
}

// fade_secs = 0.0f will halt Sound, -1 will have minimum fade. 
// Does not detach.
void Sound::fadeout(float fade_secs)
{
  if (isPlaying()) {
    if (fade_secs == 0) {
      halt();
    } else {
      System::fadeoutSound(mix_idx, fade_secs);
    }
  }
}

// Sound will continue playing in mixer as a copy. The copy will unpause, and
// cannot be updated or stopped.
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
