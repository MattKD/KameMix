#include "system.h"
#include "sound.h"
#include "stream.h"
#include "audio_mem.h"
#include "sdl_helper.h"
#include <SDL.h>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <vector>
#include <thread>
#include <mutex>
#include <numeric>
#include <algorithm>

using namespace KameMix;
#define PI_F 3.141592653589793f

namespace {

enum PlayingType : uint8_t {
  SoundType,
  StreamType,
  InvalidType
};

enum PlayState : uint8_t {
  PlayingState,
  FinishedState,
  PausedState,
  PausingState,
  UnpausingState
};

struct VolumeData {
  float left_volume;
  float right_volume;
  float lfade; // fade_percent
  float rfade; // fade_percent
  // increment/decrement to add to fade each time after applying fade to 
  // audio stream 
  float lmod; 
  float rmod; 
  // number of times to add mod to fade; 0 means apply fade once to 
  // audio stream without adding mod after
  int mod_times; 
};

struct VolumeFade {
  float left_fade;
  float right_fade;
};

struct CopyResult {
  int target_amount;
  int src_amount;
};

// For using SoundBuffer and StreamBuffer in union without unrestricted 
// union support. Both are Pimpl classes, so void* should be fine for 
// alignment.
struct SoundBuffer_ {
  union {
    char data[sizeof(SoundBuffer)];
    void *align_;
  };
};
struct StreamBuffer_ {
  union {
    char data[sizeof(StreamBuffer)];
    void *align_;
  };
};

// audio_mutex must be locked when reading/writing PlayingSound to
// synchronize user calls with audioCallback. reading from sound and
// stream must only be done in System::update which must not be 
// called concurrently with other KameMix audio functions. SoundBuffer
// and StreamBuffer though can be read/modified in audioCallback
// becuase they aren't modified in Sound/Stream while playing (halt()
// is called before modification).
struct PlayingSound {
  PlayingSound(Sound *s, SoundBuffer &sbuf, int loops, int buf_pos, 
               float fade, bool paused);
  PlayingSound(Stream *s, StreamBuffer &sbuf, int loops, int buf_pos, 
               float fade, bool paused);
  ~PlayingSound() { release(); }
  PlayingSound(const PlayingSound &other);
  PlayingSound& operator=(const PlayingSound &other);

  void release();

  bool isPlaying() const { return state == PlayingState; }
  bool isFinished() const { return state == FinishedState; }
  bool isPaused() const { return state == PausedState; }
  bool isPausing() const { return state == PausingState; }
  bool isUnpausing() const { return state == UnpausingState; }
  bool isPauseChanging() const { return isPausing() || isUnpausing(); }
  bool isFading() const { return fade_total != 0.0f; }
  bool isFadingIn() const { return fade_total > 0.0f; }
  bool isFadingOut() const { return fade_total < 0.0f; }
  bool isVolumeChanging(float new_lvol, float new_rvol) const { 
    return lvolume != new_lvol || rvolume != new_rvol; }
  bool isSoundDetached() {
    assert(tag == SoundType);
    return sound == nullptr;
  }
  bool isStreamDetached() {
    assert(tag == StreamType);
    return stream == nullptr;
  }

  void setFadein(float fade);
  void setFadeout(float fade);
  void unsetFade() { fade_total = fade_time = 0; }
  float fadeinTotal() const { return fade_total; }
  float fadeoutTotal() const { return -fade_total; }

  void updateFromSound();
  void updateFromStream();
  void decrementLoopCount();
  bool streamSwapNeeded();
  bool streamSwapBuffers();
  VolumeData getVolumeData();

  const SoundBuffer& soundBuffer() const { 
    assert(tag == SoundType); 
    return *(SoundBuffer*)&sound_buffer_;
  }
  const StreamBuffer& streamBuffer() const  { 
    assert(tag == StreamType); 
    return *(StreamBuffer*)&stream_buffer_;
  }
  SoundBuffer& soundBuffer() { 
    assert(tag == SoundType); 
    return *(SoundBuffer*)&sound_buffer_;
  }
  StreamBuffer& streamBuffer() { 
    assert(tag == StreamType); 
    return *(StreamBuffer*)&stream_buffer_;
  }
  union {
    Sound *sound;
    Stream *stream;
  };
  union {
    SoundBuffer_ sound_buffer_;
    StreamBuffer_ stream_buffer_;
  };
  int loop_count; // -1 for infinite loop, 0 to play once, n to loop n times
  int buffer_pos; // byte pos in sound/stream
  float fade_total; // fadein/out total time in sec, negative for fadeout
  float fade_time; // elapsed time for fadein, time left for fadeout
  float lvolume; // previous volume after position fade
  float rvolume;
  float new_volume; // updated volume of sound * group * master
  float x, y; // relative position from listener
  PlayingType tag;
  PlayState state;
};

typedef std::vector<PlayingSound, Alloc<PlayingSound>> SoundBuf;

struct SystemData {
  SDL_AudioDeviceID dev_id;
  // for copying sound/stream data before mixing
  uint8_t *audio_tmp_buf; 
  int audio_tmp_buf_len;
  int32_t *audio_mix_buf; // for mixing int16_t audio
  int audio_mix_buf_len;
  std::mutex audio_mutex;
  float secs_per_callback;
  SoundBuf *sounds;
  std::vector<float> *groups;
  float master_volume;
  float listener_x;
  float listener_y;
  int channels;
  int frequency;
  OutputFormat format;
  MallocFunc user_malloc;
  FreeFunc user_free;
  ReallocFunc user_realloc;
} system_;

VolumeFade applyPosition(float rel_x, float rel_y);
void clamp(float *buf, int len);
void clamp(int16_t *target, int32_t *src, int len);
template <class T, class U>
void mixStream(T *target, U *source, int len);
template <class T>
void applyVolume(T *stream, int len, float left_vol, float right_vol);
template <class T>
void applyVolume(T *stream, int len_, const VolumeData &vdata);
template <class CopyFunc>
int copySound(CopyFunc copy, uint8_t *buffer, const int buf_len, 
              PlayingSound &sound, SoundBuffer &sound_buf);
template <class CopyFunc>
int copyStream(CopyFunc copy, uint8_t *buffer, const int buf_len, 
               PlayingSound &stream, StreamBuffer &stream_buf);
int copySound(PlayingSound &sound, SoundBuffer &sound_buf, 
              uint8_t *buf, int len);
int copyStream(PlayingSound &sound, StreamBuffer &stream_buf,
              uint8_t *buf, int len);
template <class T>
struct CopyMono {
  CopyResult operator()(uint8_t *dst_, int target_len, uint8_t *src_,
                        int src_len);
};
struct CopyStereo {
  CopyResult operator()(uint8_t *dst, int dst_len, uint8_t *src,
                        int src_len);
};

} // end anon namespace

namespace KameMix {

//
// System functions
//
int System::getFrequency() { return system_.frequency; }
int System::getChannels() { return system_.channels; }
OutputFormat System::getFormat() { return system_.format; }

// Size in bytes of sample output format
int System::getFormatSize() 
{ 
  switch (system_.format) {
  case OutputFloat:
    return sizeof(float);
  case OutputS16:
    return sizeof(int16_t);
  }
  assert("Unknown AudioFormat");
  return 0;
}

MallocFunc System::getMalloc() { return system_.user_malloc; }
FreeFunc System::getFree() { return system_.user_free; }
ReallocFunc System::getRealloc() { return system_.user_realloc; }

void System::setListenerPos(float x, float y)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  system_.listener_x = x;
  system_.listener_y = y;
}

void System::getListenerPos(float &x, float &y)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  x = system_.listener_x;
  y = system_.listener_y;
}

float System::getMasterVolume()
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  return system_.master_volume;
}

void System::setMasterVolume(float volume)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  system_.master_volume = volume;
}

int System::createGroup()
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  system_.groups->push_back(1);
  return system_.groups->size() - 1;
}

void System::setGroupVolume(int group, float volume)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  assert(group >= 0 && group < (int)system_.groups->size());
  (*system_.groups)[group] = volume;
}

float System::getGroupVolume(int group)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  assert(group >= 0 && group < (int)system_.groups->size());
  return (*system_.groups)[group];
}

// May include stopped/finished sounds if called far away from update
int System::numberPlaying() 
{ 
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  return (int)system_.sounds->size(); 
}

void System::setAlloc(MallocFunc custom_malloc, FreeFunc custom_free,
                      ReallocFunc custom_realloc)
{
  if (custom_malloc && custom_free && custom_realloc) { // all user defined
    system_.user_malloc = custom_malloc;
    system_.user_free = custom_free;
    system_.user_realloc = custom_realloc;
  }
}

bool System::init(int freq, int sample_buf_size, OutputFormat format_)
{
  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    return false;
  }

  if (!system_.user_malloc || !system_.user_free ||
      !system_.user_realloc) { // not all set
    system_.user_malloc = malloc;
    system_.user_free = free;
    system_.user_realloc = realloc;
  }

  system_.format = format_;
  int allowed_changes = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;
  SDL_AudioSpec spec_want = { 0 };
  SDL_AudioSpec dev_spec = { 0 };
  spec_want.callback = System::audioCallback;
  spec_want.channels = 2;
  spec_want.format = outFormatToSDL(system_.format);
  spec_want.freq = freq;
  spec_want.samples = sample_buf_size;

  system_.dev_id = SDL_OpenAudioDevice(NULL, 0, &spec_want, &dev_spec, 
                               allowed_changes);
  if (system_.dev_id == 0) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return false;
  }

  system_.frequency = dev_spec.freq;
  system_.channels = dev_spec.channels;

  // System::format must have alread been set above
  system_.audio_tmp_buf_len = 
    dev_spec.samples * dev_spec.channels * getFormatSize();
  system_.audio_tmp_buf = (uint8_t*)km_malloc(system_.audio_tmp_buf_len);
  
  // audio_mix_buf is only used for OutputS16
  if (system_.format == OutputS16) {
    system_.audio_mix_buf_len = dev_spec.samples * dev_spec.channels;
    system_.audio_mix_buf = 
      (int32_t*)km_malloc(system_.audio_mix_buf_len * sizeof(int32_t));
  }

  system_.master_volume = 1.0f;
  system_.listener_x = 0;
  system_.listener_y = 0;
  system_.secs_per_callback = 
    (float)dev_spec.samples / system_.frequency;

  system_.sounds = km_new<SoundBuf>();
  system_.sounds->reserve(256);
  system_.groups = km_new<std::vector<float>>();

  SDL_PauseAudioDevice(system_.dev_id, 0);
  return true;
}

void System::shutdown()
{
  SDL_CloseAudioDevice(system_.dev_id);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);

  for (PlayingSound &sound : *system_.sounds) {
    if (sound.tag == SoundType) {
      if (!sound.isSoundDetached()) {
        sound.sound->mix_idx = -1;
      }
    } else {
      if (!sound.isStreamDetached()) {
        sound.stream->mix_idx = -1;
      }
    }
  }

  km_free(system_.audio_tmp_buf);
  system_.audio_tmp_buf = nullptr;
  system_.audio_tmp_buf_len = 0;

  km_free(system_.audio_mix_buf);
  system_.audio_mix_buf = nullptr;
  system_.audio_mix_buf_len = 0;

  km_delete(system_.sounds);
  system_.sounds = nullptr;

  km_delete(system_.groups);
  system_.groups = nullptr;
}

// No KameMix functions can be called during update!
void System::update()
{
  SDL_LockAudioDevice(system_.dev_id);

  int sounds_sz = (int)system_.sounds->size();
  int idx = 0;
  while (idx < sounds_sz) { 
    PlayingSound &sound = (*system_.sounds)[idx];
    if (sound.isFinished()) { 
      // only unset mix_idx if it wasn't detached or halted
      if (sound.tag == SoundType) {
        if (!sound.isSoundDetached()) {
          sound.sound->mix_idx = -1;
        }
      } else {
        if (!sound.isStreamDetached()) {
          sound.stream->mix_idx = -1;
        }
      }

      // swap sound to remove with last sound in list
      if (idx != (int)system_.sounds->size() - 1) { // not removing last sound
        sound = system_.sounds->back();
        if (sound.tag == SoundType) {
          if (!sound.isSoundDetached()) {
            sound.sound->mix_idx = idx;
          }
        } else {
          if (!sound.isStreamDetached()) {
            sound.stream->mix_idx = idx;
          }
        }
        system_.sounds->pop_back();
        sounds_sz -= 1;
        continue; // continue at same idx
      } else { // removing last sound
        system_.sounds->pop_back();
        break;
      }
    }

    if (sound.tag == SoundType) {
      if (!sound.isSoundDetached()) {
        sound.updateFromSound();
      }
    } else {
      // failed to advance in audioCallback, so try here
      if (sound.streamSwapNeeded()) {
        sound.streamSwapBuffers();
        if (sound.isFinished()) { // finished or error occurred
          continue; // will be removed at top of loop
        }
      }
      if (!sound.isStreamDetached()) {
        sound.updateFromStream();
      }
    }
    idx += 1;
  }

  SDL_UnlockAudioDevice(system_.dev_id);
}

void System::audioCallback(void *udata, uint8_t *stream, const int len)
{
  if (System::getFormat() == OutputS16) {
    memset(system_.audio_mix_buf, 0, 
           system_.audio_mix_buf_len * sizeof(int32_t));
  } else {
    memset(stream, 0, len);
  }

  std::unique_lock<std::mutex> guard(system_.audio_mutex);

  // Sounds can be added between locks, so use indexing and size(), instead 
  // of iterators or range-based for loop. Nothing is removed until update.
  for (int i = 0; i < (int)system_.sounds->size(); ++i) {
    // system_.audio_mutex must be locked while using sound
    PlayingSound &sound = (*system_.sounds)[i];
    // not paused or finished
    if (sound.isPlaying() || sound.isPauseChanging()) {
      int total_copied = 0; 

      if (sound.tag == SoundType) {
        total_copied = copySound(sound, sound.soundBuffer(), 
                                 system_.audio_tmp_buf, len);
      } else {
        total_copied = copyStream(sound, sound.streamBuffer(), 
                                  system_.audio_tmp_buf, len);
      }

      VolumeData vdata = sound.getVolumeData();

      // Unlock system_.audio_mutex when done with sound.
      guard.unlock();

      switch (System::getFormat()) {
      case OutputFloat: {
        const int num_samples = total_copied / sizeof(float);
        applyVolume((float*)system_.audio_tmp_buf, num_samples, vdata);
        mixStream((float*)stream, (float*)system_.audio_tmp_buf, num_samples);
        break;
      }
      case OutputS16: {
        const int num_samples = total_copied / sizeof(int16_t);
        applyVolume((int16_t*)system_.audio_tmp_buf, num_samples, vdata);
        mixStream(system_.audio_mix_buf, (int16_t*)system_.audio_tmp_buf, num_samples);
        break;
      }
      }

      guard.lock();
    }
  }

  switch (System::getFormat()) {
  case OutputFloat: 
    clamp((float*)stream, len / sizeof(float));
    break;
  case OutputS16: 
    clamp((int16_t*)stream, system_.audio_mix_buf, len / sizeof(int16_t));
    break;
  }
}

void System::setSoundLoopCount(int idx, int loops)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  PlayingSound &sound = (*system_.sounds)[idx];
  sound.loop_count = loops;
}

void System::pauseSound(int idx)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  PlayingSound &sound = (*system_.sounds)[idx];
  if (sound.isPlaying()) { 
    sound.state = PausingState;
  } else if (sound.isUnpausing()) {
    sound.state = PausedState;
  }
}

void System::unpauseSound(int idx)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  PlayingSound &sound = (*system_.sounds)[idx];
  if (sound.isPaused()) { 
    sound.state = UnpausingState;
  } else if (sound.isPausing()) {
    sound.state = PlayingState;
  }
}

bool System::isSoundPaused(int idx)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  PlayingSound &sound = (*system_.sounds)[idx];
  return sound.isPaused() || sound.isPausing(); 
}

int System::addSound(Sound *sound, int loops, int pos, bool paused, float fade)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  system_.sounds->push_back(PlayingSound(sound, sound->buffer, loops, pos, 
                                         fade, paused));
  return (int)system_.sounds->size() - 1;
}

void System::addSoundDetached(Sound *sound, int loops, int pos, float fade)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  system_.sounds->push_back(PlayingSound(sound, sound->buffer, loops, pos, 
                                         fade, false));
  system_.sounds->back().sound = nullptr;
  assert(system_.sounds->back().isSoundDetached());
}

int System::addStream(Stream *stream, int loops, int pos, bool paused, 
                      float fade)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  system_.sounds->push_back(PlayingSound(stream, stream->buffer, loops, pos, 
                            fade, paused));
  return (int)system_.sounds->size() - 1;
}

void System::haltSound(Sound *s)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  PlayingSound &sound = (*system_.sounds)[s->mix_idx];
  sound.state = FinishedState;
  sound.sound = nullptr;
  sound.soundBuffer().release();
}

void System::haltStream(Stream *s)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  PlayingSound &sound = (*system_.sounds)[s->mix_idx];
  sound.state = FinishedState;
  sound.stream = nullptr;
  sound.streamBuffer().release();
}

void System::fadeoutSound(int idx, float fade)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  PlayingSound &sound = (*system_.sounds)[idx];
  sound.setFadeout(fade);
}

void System::stopSound(Sound *s)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  PlayingSound &sound = (*system_.sounds)[s->mix_idx];
  sound.setFadeout(-1);
  sound.sound = nullptr;
  assert(sound.isSoundDetached());
}

void System::stopStream(Stream *s)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  PlayingSound &sound = (*system_.sounds)[s->mix_idx];
  sound.setFadeout(-1);
  sound.stream = nullptr;
  assert(sound.isStreamDetached());
}

void System::detachSound(Sound *s)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  PlayingSound &sound = (*system_.sounds)[s->mix_idx];
  sound.sound = nullptr;
  if (sound.isPaused() || sound.isPausing()) {
    sound.state = PlayingState;
  }
  assert(sound.isSoundDetached());
}

void System::detachStream(Stream *s)
{
  std::lock_guard<std::mutex> guard(system_.audio_mutex);
  PlayingSound &sound = (*system_.sounds)[s->mix_idx];
  sound.stream = nullptr;
  if (sound.isPaused() || sound.isPausing()) {
    sound.state = PlayingState;
  }
  assert(sound.isStreamDetached());
}


} // end namespace KameMix

namespace {

//
// PlayingSound, PlayingSound functions
//

PlayingSound::PlayingSound(Sound *s, SoundBuffer &sbuf, int loops, 
                           int buf_pos, float fade, bool paused)
  : sound{s}, loop_count{loops}, buffer_pos{buf_pos}, tag{SoundType}
{
  new (&sound_buffer_) SoundBuffer(sbuf);
  setFadein(fade);
  updateFromSound();
  lvolume = new_volume;
  rvolume = new_volume;
  if (paused) {
    state = PausedState;
  } else {
    state = PlayingState;
  }
}

PlayingSound::PlayingSound(Stream *s, StreamBuffer &sbuf, int loops,
                           int buf_pos, float fade, bool paused)
  : stream{s}, loop_count{loops}, buffer_pos{buf_pos}, tag{StreamType}
{
  new (&stream_buffer_) StreamBuffer(sbuf);
  setFadein(fade);
  updateFromStream();
  lvolume = new_volume;
  rvolume = new_volume;
  if (paused) {
    state = PausedState;
  } else {
    state = PlayingState;
  }
}

void PlayingSound::release()
{
  switch (tag) {
  case SoundType:
    soundBuffer().~SoundBuffer();
    break;
  case StreamType:
    streamBuffer().~StreamBuffer();
    break;
  default:
    break;
  }
}

PlayingSound::PlayingSound(const PlayingSound &other)
  : loop_count{other.loop_count}, buffer_pos{other.buffer_pos}, 
  fade_total{other.fade_total}, fade_time{other.fade_time}, 
  lvolume{other.lvolume}, rvolume{other.rvolume}, 
  new_volume{other.new_volume}, x{other.x}, y{other.y}, tag{other.tag},
  state{other.state}
{
  switch (tag) {
  case SoundType:
    sound = other.sound;
    new (&sound_buffer_) SoundBuffer(other.soundBuffer());
    break;
  case StreamType:
    stream = other.stream;
    new (&stream_buffer_) StreamBuffer(other.streamBuffer());
    break;
  default:
    break;
  }
}

PlayingSound& PlayingSound::operator=(const PlayingSound &other)
{
  if (this != &other) {
    release();
    switch (other.tag) {
    case SoundType:
      sound = other.sound;
      new (&sound_buffer_) SoundBuffer(other.soundBuffer());
      break;
    case StreamType:
      stream = other.stream;
      new (&stream_buffer_) StreamBuffer(other.streamBuffer());
      break;
    default:
      break;
    }

    loop_count = other.loop_count;
    buffer_pos = other.buffer_pos; 
    fade_total = other.fade_total;
    fade_time = other.fade_time;
    lvolume = other.lvolume;
    rvolume = other.rvolume; 
    new_volume = other.new_volume;
    x = other.x; 
    y = other.y;
    tag = other.tag;
    state = other.state;
  }
  return *this;
}

inline
void PlayingSound::updateFromSound()
{
  new_volume = sound->getVolume() * system_.master_volume;
  const int group = sound->getGroup();
  if (group >= 0) {
    new_volume *= (*system_.groups)[group];
  }

  // get relative position if max_distance set to valid value
  const float max_distance = sound->getMaxDistance();
  if (max_distance > 0) {
    x = (sound->getX() - system_.listener_x) / max_distance;
    y = (sound->getY() - system_.listener_y) / max_distance;
  } else { // max_distance not set, so not using position data
    x = 0;
    y = 0;
  }
}

inline
void PlayingSound::updateFromStream()
{
  new_volume = stream->getVolume() * system_.master_volume;
  const int group = stream->getGroup();
  if (group >= 0) {
    new_volume *= (*system_.groups)[group];
  }

  // get relative position if max_distance set to valid value
  const float max_distance = stream->getMaxDistance();
  if (max_distance > 0) {
    x = (stream->getX() - system_.listener_x) / max_distance;
    y = (stream->getY() - system_.listener_y) / max_distance;
  } else { // max_distance not set, so not using position data
    x = 0;
    y = 0;
  }
}

void PlayingSound::setFadein(float fade) 
{
  if (fade == 0.0f) {
    fade_total = 0.0f;
  } else if (fade > system_.secs_per_callback) {
    fade_total = fade;
  } else {
    fade_total = system_.secs_per_callback;
  }
  fade_time = 0.0f;
}

// set fade_total to negative for fadeout
void PlayingSound::setFadeout(float fade) {
  if (fade > system_.secs_per_callback) {
    fade_total = -fade;
    fade_time = fade; 
  } else {
    fade_total = -system_.secs_per_callback;
    fade_time = system_.secs_per_callback; 
  }
}

inline
void PlayingSound::decrementLoopCount() 
{
  if (loop_count == 0) {
    state = FinishedState;
  } else if (loop_count > 0) {
    loop_count -= 1;
  }
}

inline
bool PlayingSound::streamSwapNeeded() 
{
  return buffer_pos == streamBuffer().size();
}

bool PlayingSound::streamSwapBuffers()
{
  StreamBuffer &sbuf = streamBuffer();
  StreamResult result = sbuf.swapBuffers();
  if (result == StreamReady) {
    if (sbuf.endPos() == 0) { // EOF seen immediately on read
      decrementLoopCount();
    }
    buffer_pos = 0;
    if (!sbuf.fullyBuffered()) {
      std::thread thrd([sbuf]() mutable { sbuf.readMore(); }); 
      thrd.detach();
    }
    return true;
  } else if (result == StreamError) {
    state = FinishedState; 
  } else {
    assert(result != StreamNoData); // readMore never called
    buffer_pos = sbuf.size();
  }
  return false;
}

VolumeData PlayingSound::getVolumeData()
{
  VolumeFade vfade = applyPosition(x, y);
  float new_lvol = new_volume * vfade.left_fade;
  float new_rvol = new_volume * vfade.right_fade;

  if (isFading() || isPauseChanging() || isVolumeChanging(new_lvol, new_rvol)) {
    float start_lfade = 1.0;
    float start_rfade = 1.0;
    float end_lfade = 1.0;
    float end_rfade = 1.0;
    bool adjust_fade_time = false;

    if (isFadingIn()) {
      start_lfade = fade_time / fade_total;
      start_rfade = start_lfade;
      end_lfade = (fade_time + system_.secs_per_callback) / fade_total;
      end_rfade = end_lfade;
      adjust_fade_time = true;
    } else if (isFadingOut()) { // fade_total is negative in fadeout
      start_lfade = fade_time / -fade_total;
      start_rfade = start_lfade;
      end_lfade = (fade_time - system_.secs_per_callback) / -fade_total;
      end_rfade = end_lfade;
      adjust_fade_time = true;
    }

    VolumeData vdata;
    // set to previous played volume, which we'll fade from to new volume
    vdata.left_volume = lvolume;
    vdata.right_volume = rvolume;
    if (isVolumeChanging(new_lvol, new_rvol)) {
      // if previous volume was 0, set to small value prevent div by 0
      if (vdata.left_volume == 0) {
        vdata.left_volume = 0.01f;
      }
      if (vdata.right_volume == 0) {
        vdata.right_volume = 0.01f;
      }
      end_lfade *= new_lvol / vdata.left_volume;
      end_rfade *= new_rvol / vdata.right_volume;
      // reset previous volumes to new volume after postion fading
      lvolume = new_lvol;
      rvolume = new_rvol;
    }

    if (isPausing()) {
      end_lfade = 0.0;
      end_rfade = 0.0;
      adjust_fade_time = false;
      state = PausedState;
    } else if (isUnpausing()) {
      start_lfade = 0.0;
      start_rfade = 0.0;
      adjust_fade_time = false;
      state = PlayingState;
    }

    vdata.lfade = start_lfade;
    vdata.rfade = start_rfade;
    const float lfade_delta = end_lfade - start_lfade;
    const float rfade_delta = end_rfade - start_rfade;
    const float max_delta = std::max(lfade_delta, rfade_delta);
    // divide fade_delta into 2% steps
    const float delta_step = 0.02f;
    vdata.mod_times = (int)std::abs(max_delta / delta_step);
    if (vdata.mod_times > 50) {
      vdata.mod_times = 50;
    }
    vdata.lmod = lfade_delta / (vdata.mod_times + 1);
    vdata.rmod = rfade_delta / (vdata.mod_times + 1);

    if (adjust_fade_time) {
      if (isFadingOut()) {
        fade_time -= system_.secs_per_callback;
        if (fade_time <= 0.0f) {
          // Finished not Stopped state, so mix_idx is unset in update
          state = FinishedState; 
          unsetFade();
        }
      } else {
        fade_time += system_.secs_per_callback;
        if (fade_time >= fade_total) {
          unsetFade();
        }
      }
    }

    return vdata;
  } else {
    VolumeData vdata;
    vdata.left_volume = lvolume;
    vdata.right_volume = rvolume;
    vdata.lfade = 1.0;
    vdata.rfade = 1.0;
    vdata.lmod = 0.0;
    vdata.rmod = 0.0;
    vdata.mod_times = 0;
    return vdata;
  }
}

//
// Audio mixing and volume functions
//

// Modify left and right channel volume by position. Doesn't change if 
// relative position from listener is (0,0).
VolumeFade applyPosition(float rel_x, float rel_y)
{
  if (rel_x == 0 && rel_y == 0) {
    VolumeFade vf = {1.0f, 1.0f};
    return vf;
  }

  float distance = sqrt(rel_x * rel_x + rel_y * rel_y);

  if (distance >= 1.0f) {
    VolumeFade vf = {0.0f, 0.0f};
    return vf;
  }

  // Sound's volume on left and right speakers vary between 
  // 1.0 to (1.0-max_mod)/(1.0+max_mod), and are at 1.0/(1.0+max_mod) 
  // directly in front or behind listener. With max_mod = 0.3, this is 
  // 1.0 to 0.54 and 0.77 in front and back.
  const float max_mod = 0.3f;
  const float base = 1/(1.0f+max_mod) * (1.0f - distance);
  VolumeFade vfade = {base, base};
  float left_mod = 1.0;
  float right_mod = 1.0;

  if (rel_x != 0.0f) { // not 90 or 270 degrees
    float rads = atan(rel_y / rel_x);
    if (rel_y >= 0.0f) { // quadrant 1 and 2
      if (rel_x > 0.0f) { // quadrant 1
        float mod = max_mod - rads / (PI_F/2.0f) * max_mod; 
        left_mod = 1.0f - mod;
        right_mod = 1.0f + mod;
      } else if (rel_x < 0.0f) { // quadrant 2
        float mod = max_mod + rads / (PI_F/2.0f) * max_mod; 
        left_mod = 1.0f + mod;
        right_mod = 1.0f - mod;
      }
    } else { // quadrants 3 and 4
      if (rel_x < 0.0f) { // quadrant 3
        float mod = max_mod - rads / (PI_F/2.0f) * max_mod; 
        left_mod = 1.0f + mod;
        right_mod = 1.0f - mod;
      } else if (rel_x > 0.0f) { // quadrant 4
        float mod = max_mod + rads / (PI_F/2.0f) * max_mod; 
        left_mod = 1.0f - mod;
        right_mod = 1.0f + mod;
      }
    }

    vfade.left_fade *= left_mod;
    vfade.right_fade *= right_mod;
  } 
  return vfade;
}

template <class T>
void applyVolume(T *stream, int len, float left_vol, float right_vol)
{
  for (int i = 0; i < len; i += 2) {
    stream[i] = (T)(stream[i] * left_vol);
    stream[i+1] = (T)(stream[i+1] * right_vol);
  }
}

template <class T>
void applyVolume(T *stream, int len_, const VolumeData &vdata)
{
  int pos = 0;
  // make sure len is even after dividing by mod_times+1
  int len = (len_ / 2) / (vdata.mod_times + 1) * 2;

  for (int i = 0; i < vdata.mod_times; ++i) {
    float lfade = vdata.lfade + i * vdata.lmod;
    float rfade = vdata.rfade + i * vdata.rmod;
    float left_vol = vdata.left_volume * lfade;
    float right_vol = vdata.right_volume * rfade;
    applyVolume(stream+pos, len, left_vol, right_vol);
    pos += len;
  }

  float lfade = vdata.lfade + vdata.mod_times * vdata.lmod;
  float rfade = vdata.rfade + vdata.mod_times * vdata.rmod;
  float left_vol = vdata.left_volume * lfade;
  float right_vol = vdata.right_volume * rfade;
  // len*(fdata.mod_times+1) can be less than len_, so make sure to
  // include all last samples
  len = len_ - pos;
  applyVolume(stream+pos, len, left_vol, right_vol);
}

template <class CopyFunc>
int copySound(CopyFunc copy, uint8_t *buffer, const int buf_len, 
              PlayingSound &sound, SoundBuffer &sound_buf)
{
  int buf_left = buf_len;
  int total_copied = 0;

  while (true) {
    // FinishedState from decrementLoopCount
    if (sound.isFinished()) { 
      break;
    }

    int src_left = sound_buf.size() - sound.buffer_pos;
    CopyResult cpy_amount = 
      copy(buffer + total_copied, buf_left, 
           sound_buf.data() + sound.buffer_pos, src_left);
                                  
    total_copied += cpy_amount.target_amount;

    // didn't reach end of sound buffer
    if (cpy_amount.src_amount < src_left) { 
      sound.buffer_pos += cpy_amount.src_amount;
      break;
    } else {
      // reached end of sound
      sound.decrementLoopCount();
      sound.buffer_pos = 0;
      buf_left -= cpy_amount.target_amount;
    } 
  }

  return total_copied;
}

template <class CopyFunc>
int copyStream(CopyFunc copy, uint8_t *buffer, const int buf_len, 
               PlayingSound &stream, StreamBuffer &stream_buf)
{
  int buf_left = buf_len;
  int total_copied = 0; 

  while (true) {
    // FinishedState from decrementLoopCount or on error advancing
    if (stream.isFinished()) { 
      break;
    }

    int end_pos = stream_buf.endPos(); // -1 if not in buffer
    int src_left;
    if (end_pos <= stream.buffer_pos) {
      src_left = stream_buf.size() - stream.buffer_pos;
    } else {
      src_left = end_pos - stream.buffer_pos;
    }
    //int cpy_amount = buf_left < src_left ? buf_left : src_left;

    CopyResult cpy_amount = 
      copy(buffer + total_copied, buf_left, 
           stream_buf.data() + stream.buffer_pos, src_left);

    total_copied += cpy_amount.target_amount;
    buf_left -= cpy_amount.target_amount;
      
    // didn't reach end of stream or its main buffer
    if (cpy_amount.src_amount < src_left) { 
      stream.buffer_pos += cpy_amount.src_amount;
      break;
    // reached end of buffer
    } else if (stream.buffer_pos + cpy_amount.src_amount 
               == stream_buf.size()) { 
      if (end_pos == stream_buf.size()) { // end of stream at end of buffer
        // buffer_pos may be at end from last time and failed to swap buffers
        if (cpy_amount.target_amount > 0) { 
          stream.decrementLoopCount();
        }
      }

      if (!stream.streamSwapBuffers()) {
        break;
      }
    } else { // end of stream and not end of buffer
      stream.buffer_pos = end_pos;
      stream.decrementLoopCount();
    }
  }

  return total_copied;
}

template <class T>
CopyResult 
CopyMono<T>::operator()(uint8_t *dst_, int target_len, 
                     uint8_t *src_, int src_len)
{
  int half_target_left = target_len / 2;
  int half_cpy_amount = half_target_left < src_len ? 
                        half_target_left : src_len;

  const int len = half_cpy_amount / sizeof(T);
  T *src = (T*)src_; 
  T *src_end = src + len; 
  T *dst = (T*)dst_;
  while (src != src_end) {
    const T val = *src++;
    dst[0] = val;
    dst[1] = val;
    dst += 2;
  }

  CopyResult result = { half_cpy_amount * 2, half_cpy_amount };
  return result;
}

CopyResult 
CopyStereo::operator()(uint8_t *dst, int dst_len, uint8_t *src, int src_len)
{
  int cpy_amount = dst_len < src_len ? dst_len : src_len;
  memcpy(dst, src, cpy_amount);

  CopyResult result = { cpy_amount, cpy_amount };
  return result;
}

int copySound(PlayingSound &sound, SoundBuffer &sound_buf, 
              uint8_t *buf, int len)
{
  if (sound_buf.numChannels() == 1) {
    switch (System::getFormat()) {
    case OutputFloat:
      return copySound(CopyMono<float>(), buf, len, sound, sound_buf);
      break;
    case OutputS16:
      return copySound(CopyMono<int16_t>(), buf, len, sound, sound_buf);
      break;
    }
    assert("Invalid Output Format");
    return -1;
  } else {
    return copySound(CopyStereo(), buf, len, sound, sound_buf);
  }
}

int copyStream(PlayingSound &sound, StreamBuffer &stream_buf,
              uint8_t *buf, int len)
{
  if (stream_buf.numChannels() == 1) {
    switch (System::getFormat()) {
    case OutputFloat:
      return copyStream(CopyMono<float>(), buf, len, sound, stream_buf);
      break;
    case OutputS16:
      return copyStream(CopyMono<int16_t>(), buf, len, sound, stream_buf);
      break;
    }
    assert("Invalid Output Format");
    return -1;
  } else {
    return  copyStream(CopyStereo(), buf, len, sound, stream_buf);
  }
}

template <class T, class U>
void mixStream(T *target, U *source, int len) 
{
  for (int i = 0; i < len; ++i) {
    target[i] += source[i];
  }
}

void clamp(float *buf, int len)
{
  for (int i = 0; i < len; ++i) {
    float val = buf[i];
    if (val > 1.0f) {
      buf[i] = 1.0f;
    } else if (val < -1.0f) {
      buf[i] = -1.0f;
    }
  }
}

void clamp(int16_t *target, int32_t *src, int len)
{
  const int32_t max_val = std::numeric_limits<int16_t>::max();
  const int32_t min_val = std::numeric_limits<int16_t>::min();
  for (int i = 0; i < len; ++i) {
    int32_t val = src[i];
    if (val > max_val) {
      target[i] = max_val;
    } else if (val < min_val) {
      target[i] = min_val;
    } else {
      target[i] = val;
    }
  }
}

} // end anon namespace
