#include "audio_system.h"
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
#include <numeric>

using namespace KameMix;
#define PI_F 3.141592653589793f

namespace {

enum PlayingType : uint8_t {
  SoundType,
  StreamType
};

enum PlayState : uint8_t {
  PlayingState,
  StoppedState,
  FinishedState,
  PausedState,
  PausingState,
  UnpausingState
};

struct VolumeData {
  double left_volume;
  double right_volume;
  double fade; // fade_percent
  // increment/decrement to add to fade each time after applying fade to 
  // audio stream 
  double mod; 
  // number of times to add mod to fade; 0 means apply fade once to 
  // audio stream without adding mod after
  int mod_times; 
};

struct PlayingSound {
  union {
    Sound *sound;
    Stream *stream;
  };
  int buffer_pos; // byte pos in sound/stream
  int loop_count; // -1 for infinite loop, 0 to play once, n to loop n times
  float volume; // previous volume of sound * group
  float new_volume; // new volume of sound * group to fade to
  float x, y; // relative position from listener
  float fade_total; // fadein/out total time in sec, negative for fadeout
  float fade_time; // position in fade
  PlayingType tag;
  PlayState state;
};

struct CopyResult {
  int target_amount;
  int src_amount;
};

typedef std::vector<PlayingSound, Alloc<PlayingSound>> SoundBuf;

SDL_AudioDeviceID dev_id;
// for copying sound/stream data before mixing
uint8_t *audio_tmp_buf = nullptr; 
int audio_tmp_buf_len = 0;
int32_t *audio_mix_buf = nullptr; // for mixing int16_t audio
int audio_mix_buf_len = 0;
std::mutex audio_mutex;
double secs_per_callback = 0.0;
SoundBuf *sounds = nullptr;
std::vector<double> *groups;
double master_volume = 1;
float listener_x = 0;
float listener_y = 0;


void initPlayingSound_(PlayingSound &ps, int loops, int buf_pos,
                       bool paused, float fade);
PlayingSound makePlayingSound(Sound *s, int loops, int buf_pos, 
                              bool paused, float fade); 
PlayingSound makePlayingSound(Stream *s, int loops, int buf_pos, 
                              bool paused, float fade); 
bool isPlaying(const PlayingSound &s);
bool isFinished(const PlayingSound &s); 
bool isStopped(const PlayingSound &s); 
bool isPaused(const PlayingSound &s);
bool isPausing(const PlayingSound &s);
bool isUnpausing(const PlayingSound &s);
bool isFadeNeeded(const PlayingSound &s);
bool isFading(const PlayingSound &s);
bool isFadingIn(const PlayingSound &s);
bool isFadingOut(const PlayingSound &s);
bool isPauseChanging(const PlayingSound &s);
bool isVolumeChanging(const PlayingSound &s);
void setFadein(PlayingSound &sound, float fade);
void setFadeout(PlayingSound &sound, float fade);
void unsetFade(PlayingSound &sound); 
void updateSound(PlayingSound &psound, Sound &sound); 
void updateStream(PlayingSound &sound, Stream &stream); 
void decrementLoopCount(PlayingSound &sound);
bool streamSwapNeeded(PlayingSound &sound, StreamBuffer &stream_buf) ;
bool streamSwapBuffers(PlayingSound &sound, StreamBuffer &stream_buf);
VolumeData getVolumeData(PlayingSound &sound);

template <class T>
void applyVolume(T *stream, int len, double left_vol, double right_vol);
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

template <class T, class U>
void mixStream(T *target, U *source, int len);
void applyPosition(double rel_x, double rel_y, VolumeData &vdata);
void clamp(float *buf, int len);
void clamp(int16_t *target, int32_t *src, int len);

} // end anon namespace

namespace KameMix {

int AudioSystem::channels;
int AudioSystem::frequency;
OutAudioFormat AudioSystem::format;
MallocFunc AudioSystem::user_malloc;
FreeFunc AudioSystem::user_free;
ReallocFunc AudioSystem::user_realloc;
SoundFinishedFunc AudioSystem::sound_finished;
StreamFinishedFunc AudioSystem::stream_finished;
void* AudioSystem::sound_finished_data;
void* AudioSystem::stream_finished_data;

void AudioSystem::setListenerPos(float x, float y)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  listener_x = x;
  listener_y = y;
}

void AudioSystem::getListenerPos(float &x, float &y)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  x = listener_x;
  y = listener_y;
}

double AudioSystem::getMasterVolume()
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  return master_volume;
}

void AudioSystem::setMasterVolume(double volume)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  master_volume = volume;
}

int AudioSystem::createGroup()
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  groups->push_back(1);
  return groups->size() - 1;
}

void AudioSystem::setGroupVolume(int group, double volume)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  assert(group >= 0 && group < (int)groups->size());
  (*groups)[group] = volume;
}

double AudioSystem::getGroupVolume(int group)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  assert(group >= 0 && group < (int)groups->size());
  return (*groups)[group];
}

// May include stopped/finished sounds if called far away from update
int AudioSystem::numberPlaying() 
{ 
  std::lock_guard<std::mutex> guard(audio_mutex);
  return (int)sounds->size(); 
}

bool AudioSystem::init(int freq, int sample_buf_size, 
                       OutAudioFormat format,
                       MallocFunc custom_malloc,
                       FreeFunc custom_free,
                       ReallocFunc custom_realloc)
{
  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    return false;
  }

  if (custom_malloc || custom_free || custom_realloc) { // at least one set
    if (!(custom_malloc && custom_free && custom_realloc)) { // not all set
      return false;
    }
  }

  AudioSystem::format = format;
  user_malloc = custom_malloc == nullptr ? malloc : custom_malloc;
  user_free = custom_free == nullptr ? free : custom_free;
  user_realloc = custom_realloc == nullptr ? realloc : custom_realloc;

  int allowed_changes = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;
  SDL_AudioSpec spec_want = { 0 };
  SDL_AudioSpec dev_spec = { 0 };
  spec_want.callback = AudioSystem::audioCallback;
  spec_want.channels = 2;
  spec_want.format = outFormatToSDL(format);
  spec_want.freq = freq;
  spec_want.samples = sample_buf_size;

  dev_id = SDL_OpenAudioDevice(NULL, 0, &spec_want, &dev_spec, 
                               allowed_changes);
  if (dev_id == 0) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return false;
  }

  AudioSystem::frequency = dev_spec.freq;
  AudioSystem::channels = dev_spec.channels;

  // AudioSystem::format must have alread been set above
  audio_tmp_buf_len = 
    dev_spec.samples * dev_spec.channels * getFormatSize();
  audio_tmp_buf = (uint8_t*)km_malloc(audio_tmp_buf_len);
  
  // audio_mix_buf is only used for OutFormat_S16
  if (format == OutFormat_S16) {
    audio_mix_buf_len = dev_spec.samples * dev_spec.channels;
    audio_mix_buf = 
      (int32_t*)km_malloc(audio_mix_buf_len * sizeof(int32_t));
  }

  master_volume = 1.0f;
  listener_x = 0;
  listener_y = 0;
  secs_per_callback = (double)dev_spec.samples / AudioSystem::frequency;

  sounds = km_new<SoundBuf>();
  sounds->reserve(256);
  groups = km_new<std::vector<double>>();

  SDL_PauseAudioDevice(dev_id, 0);
  return true;
}

void AudioSystem::shutdown()
{
  SDL_CloseAudioDevice(dev_id);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);

  for (PlayingSound &sound : *sounds) {
    if (sound.tag == SoundType) {
      sound.sound->mix_idx = -1;
    } else {
      sound.stream->mix_idx = -1;
    }
  }

  km_free(audio_tmp_buf);
  audio_tmp_buf = nullptr;
  audio_tmp_buf_len = 0;
  km_free(audio_mix_buf);
  audio_mix_buf = nullptr;
  audio_mix_buf_len = 0;
  km_delete(sounds);
  km_delete(groups);
  sounds = nullptr;
}

void AudioSystem::addSound(Sound *sound, int loops, int pos, bool paused,
                           float fade)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  sounds->push_back(makePlayingSound(sound, loops, pos, paused, fade));
  sound->mix_idx = (int)sounds->size() - 1;
}

void AudioSystem::addStream(Stream *stream, int loops, int pos, bool paused,
                            float fade)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  sounds->push_back(makePlayingSound(stream, loops, pos, paused, fade));
  stream->mix_idx = (int)sounds->size() - 1;
}

void AudioSystem::removeSound(Sound *sound, int idx, float fade_secs)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  PlayingSound &psound = (*sounds)[idx];
  if (fade_secs == 0.0f) {
    psound.state = StoppedState;
    sound->mix_idx = -1;
  } else {
    setFadeout(psound, fade_secs);
  }
}

void AudioSystem::removeStream(Stream *stream, int idx, float fade_secs)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  PlayingSound &psound = (*sounds)[idx];
  if (fade_secs == 0.0f) {
    psound.state = StoppedState;
    stream->mix_idx = -1;
  } else {
    setFadeout(psound, fade_secs);
  }
}

bool AudioSystem::isSoundFinished(int idx)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  return isFinished((*sounds)[idx]);
}

void AudioSystem::pauseSound(int idx)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  PlayingSound &sound = (*sounds)[idx];
  if (isPlaying(sound)) { 
    sound.state = PausingState;
  } else if (isUnpausing(sound)) {
    sound.state = PausedState;
  }
}

void AudioSystem::unpauseSound(int idx)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  PlayingSound &sound = (*sounds)[idx];
  if (isPaused(sound)) { 
    sound.state = UnpausingState;
  } else if (isPausing(sound)) {
    sound.state = PlayingState;
  }
}

bool AudioSystem::isSoundPaused(int idx)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  PlayingSound &sound = (*sounds)[idx];
  return isPaused(sound) || isPausing(sound); 
}

void AudioSystem::update()
{
  SDL_LockAudioDevice(dev_id);
  std::lock_guard<std::mutex> guard(audio_mutex);

  auto sound_iter = sounds->begin();
  auto sound_end = sounds->end();
  while (sound_iter != sound_end) {
    PlayingSound &sound = *sound_iter;
    // sound finished playing or was stopped
    if (isFinished(sound) || isStopped(sound)) { 
      // only unset mix_idx if it finished playing and wasn't stopped
      if (!(isStopped(sound))) { 
        if (sound.tag == SoundType) {
          sound.sound->mix_idx = -1;
        } else {
          sound.stream->mix_idx = -1;
        }
      }

      // swap sound to remove with last sound in list
      int idx = (int)(sound_iter - sounds->begin());
      if (idx != (int)sounds->size() - 1) { // not removing last sound
        sound = sounds->back();
        if (sound.tag == SoundType) {
          sound.sound->mix_idx = idx;
        } else {
          sound.stream->mix_idx = idx;
        }
        sounds->pop_back();
        sound_end = sounds->end();
        continue;
      } else { // removing last sound
        sounds->pop_back();
        break;
      }
    }

    if (sound.tag == SoundType) {
      updateSound(sound, *sound.sound);
    } else {
      StreamBuffer &stream_buf = sound.stream->buffer;
      // failed to advance in audioCallback, so try here
      if (streamSwapNeeded(sound, stream_buf)) {
        streamSwapBuffers(sound, stream_buf);
        if (isFinished(sound)) { // finished or error occurred
          continue; // will be removed at top of loop
        }
      }
      updateStream(sound, *sound.stream);
    }

    ++sound_iter;
  }

  SDL_UnlockAudioDevice(dev_id);
}

void AudioSystem::audioCallback(void *udata, uint8_t *stream, const int len)
{
  if (AudioSystem::getFormat() == OutFormat_S16) {
    memset(audio_mix_buf, 0, audio_mix_buf_len * sizeof(int32_t));
  } else {
    memset(stream, 0, len);
  }

  std::unique_lock<std::mutex> guard(audio_mutex);

  // Sounds can be added between locks, so use indexing and size(), instead 
  // of iterators or range-based for loop. Nothing is removed until update.
  for (int i = 0; i < (int)sounds->size(); ++i) {
    // audio_mutex must be locked while using sound
    PlayingSound &sound = (*sounds)[i];
    // not paused, stopped, or finished
    if (isPlaying(sound) || isPauseChanging(sound)) {
      int total_copied = 0; 

      if (sound.tag == SoundType) {
        total_copied = copySound(sound, sound.sound->buffer, 
                                 audio_tmp_buf, len);
      } else {
        total_copied = copyStream(sound, sound.stream->buffer, 
                                  audio_tmp_buf, len);
      }

      double rel_x = sound.x;
      double rel_y = sound.y;
      VolumeData vdata = getVolumeData(sound);

      // Unlock audio_mutex when done with sound. Must be unlocked
      // before calling soundFinished/streamFinished

      if (isFinished(sound)) {
        if (sound.tag == SoundType) {
          Sound *s = sound.sound;
          guard.unlock();
          soundFinished(s);
        } else {
          Stream *s = sound.stream;
          guard.unlock();
          streamFinished(s);
        }
      } else {
        guard.unlock();
      }

      // audio_mutex unlocked now, sound must not be used below

      applyPosition(rel_x, rel_y, vdata);

      switch (AudioSystem::getFormat()) {
      case OutFormat_Float: {
        const int num_samples = total_copied / sizeof(float);
        applyVolume((float*)audio_tmp_buf, num_samples, vdata);
        mixStream((float*)stream, (float*)audio_tmp_buf, num_samples);
        break;
      }
      case OutFormat_S16: {
        const int num_samples = total_copied / sizeof(int16_t);
        applyVolume((int16_t*)audio_tmp_buf, num_samples, vdata);
        mixStream(audio_mix_buf, (int16_t*)audio_tmp_buf, num_samples);
        break;
      }
      }

      guard.lock();
    }
  }

  switch (AudioSystem::getFormat()) {
  case OutFormat_Float: 
    clamp((float*)stream, len / sizeof(float));
    break;
  case OutFormat_S16: 
    clamp((int16_t*)stream, audio_mix_buf, len / sizeof(int16_t));
    break;
  }
}

} // end namespace KameMix

namespace {

void initPlayingSound_(PlayingSound &ps, int loops, int buf_pos,
                       bool paused, float fade)
{
  ps.loop_count = loops;
  ps.buffer_pos = buf_pos;
  setFadein(ps, fade);
  if (paused) {
    ps.state = PausedState;
  } else {
    ps.state = PlayingState;
  }
}

inline
PlayingSound makePlayingSound(Sound *s, int loops, int buf_pos, 
                              bool paused, float fade) 
{ 
  PlayingSound ps;
  initPlayingSound_(ps, loops, buf_pos, paused, fade);
  updateSound(ps, *s);
  ps.volume = ps.new_volume;
  ps.sound = s;
  ps.tag = SoundType;
  return ps;
}

inline
PlayingSound makePlayingSound(Stream *s, int loops, int buf_pos, 
                              bool paused, float fade) 
{ 
  PlayingSound ps;
  initPlayingSound_(ps, loops, buf_pos, paused, fade);
  updateStream(ps, *s);
  ps.volume = ps.new_volume;
  ps.stream = s;
  ps.tag = StreamType;
  return ps;
}

inline
bool isPlaying(const PlayingSound &s) 
{ 
  return s.state == PlayingState; 
}

inline
bool isFinished(const PlayingSound &s) 
{ 
  return s.state == FinishedState; 
}

inline
bool isStopped(const PlayingSound &s) 
{ 
  return s.state == StoppedState; 
}

inline
bool isPaused(const PlayingSound &s) 
{ 
  return s.state == PausedState; 
}

inline
bool isPausing(const PlayingSound &s) 
{ 
  return s.state == PausingState; 
}

inline
bool isUnpausing(const PlayingSound &s) 
{ 
  return s.state == UnpausingState; 
}

inline
bool isFadeNeeded(const PlayingSound &s) {
  return isFading(s) || isVolumeChanging(s) || isPauseChanging(s);
}

inline
bool isFading(const PlayingSound &s) 
{ 
  return s.fade_total != 0.0f; 
}
bool isFadingIn(const PlayingSound &s) 
{ 
  return s.fade_total > 0.0f; 
}

inline
bool isFadingOut(const PlayingSound &s) 
{ 
  return s.fade_total < 0.0f; 
}

inline
bool isPauseChanging(const PlayingSound &s) 
{ 
  return isPausing(s) || isUnpausing(s); 
}

inline
bool isVolumeChanging(const PlayingSound &s) 
{ 
  return s.volume != s.new_volume; 
}


void setFadein(PlayingSound &sound, float fade) 
{
  if (fade == 0.0f) {
    sound.fade_total = 0.0f;
  } else if (fade > secs_per_callback) {
    sound.fade_total = fade;
  } else {
    sound.fade_total = (float)secs_per_callback;
  }
  sound.fade_time = 0.0f;
}

// set fade_total to negative for fadeout
void setFadeout(PlayingSound &sound, float fade) {
  if (fade == 0.0f) {
    sound.fade_total = 0.0f;
    sound.fade_time = 0.0f;
    return;
  } 
  if (fade > secs_per_callback) {
    sound.fade_total = -fade;
    sound.fade_time = fade; 
  } else {
    sound.fade_total = (float)-secs_per_callback;
    sound.fade_time = (float)secs_per_callback; 
  }
}

inline
void unsetFade(PlayingSound &sound) 
{ 
  sound.fade_total = sound.fade_time = 0.0f; 
}

// audio_mutex is locked before calling this
inline
void updateSound(PlayingSound &psound, Sound &sound) 
{
  psound.new_volume = (float)(sound.getVolume() * master_volume);
  int group = sound.getGroup();
  if (group >= 0) {
    psound.new_volume *= (float)(*groups)[group];
  }

  if (sound.usingListener()) {
    psound.x = (sound.getX() - listener_x) / sound.getMaxDistance();
    psound.y = (sound.getY() - listener_y) / sound.getMaxDistance();
  } else {
    psound.x = sound.getX();
    psound.y = sound.getY();
  }
}

// audio_mutex is locked before calling this
inline
void updateStream(PlayingSound &sound, Stream &stream) 
{
  sound.new_volume = (float)(stream.getVolume() * master_volume);
  int group = stream.getGroup();
  if (group >= 0) {
    sound.new_volume *= (float)(*groups)[group];
  }

  if (stream.usingListener()) {
    sound.x = (stream.getX() - listener_x) / stream.getMaxDistance();
    sound.y = (stream.getY() - listener_y) / stream.getMaxDistance();
  } else {
    sound.x = stream.getX();
    sound.y = stream.getY();
  }
}

inline
void decrementLoopCount(PlayingSound &sound) 
{
  if (sound.loop_count == 0) {
    sound.state = FinishedState;
  } else if (sound.loop_count > 0) {
    sound.loop_count -= 1;
  }
}

inline
bool streamSwapNeeded(PlayingSound &sound, StreamBuffer &stream_buf) 
{
  return sound.buffer_pos == stream_buf.size();
}

bool streamSwapBuffers(PlayingSound &sound, StreamBuffer &stream_buf)
{
  StreamResult result = stream_buf.swapBuffers();
  if (result == StreamReady) {
    if (stream_buf.endPos() == 0) { // EOF seen immediately on read
      decrementLoopCount(sound);
    }
    sound.buffer_pos = 0;
    if (!stream_buf.fullyBuffered()) {
      std::thread thrd([stream_buf]() mutable { stream_buf.readMore(); }); 
      thrd.detach();
    }
    return true;
  } else if (result == StreamError) {
    sound.state = FinishedState; 
  } else {
    assert(result != StreamNoData); // readMore never called
    sound.buffer_pos = stream_buf.size();
  }
  return false;
}

VolumeData getVolumeData(PlayingSound &sound)
{
  if (isFadeNeeded(sound)) {
    double start_fade = 1.0;
    double end_fade = 1.0;
    bool adjust_fade_time = false;

    if (isFadingIn(sound)) {
      start_fade = (double)sound.fade_time / sound.fade_total;
      end_fade = (sound.fade_time + secs_per_callback) 
                 / sound.fade_total;
      adjust_fade_time = true;
    } else if (isFadingOut(sound)) { // fade_total is negative in fadeout
      start_fade = (double)sound.fade_time / -sound.fade_total;
      end_fade = (sound.fade_time - secs_per_callback) 
                 / -sound.fade_total;
      adjust_fade_time = true;
    }

    double volume = sound.volume;

    if (isVolumeChanging(sound)) {
      if (sound.volume <= 0) {
        volume = 0.01;
      }
      end_fade *= sound.new_volume / volume;
      sound.volume = sound.new_volume;
    }

    if (isPausing(sound)) {
      end_fade = 0.0;
      adjust_fade_time = false;
      sound.state = PausedState;
    } else if (isUnpausing(sound)) {
      start_fade = 0.0;
      adjust_fade_time = false;
      sound.state = PlayingState;
    }

    VolumeData vdata;
    vdata.left_volume = volume;
    vdata.right_volume = volume;
    vdata.fade = start_fade;
    const double fade_delta = end_fade - start_fade;
    // divide fade_delta into 2% steps
    const double delta_step = 0.02;
    vdata.mod_times = (int)std::abs(fade_delta / delta_step);
    if (vdata.mod_times > 50) {
      vdata.mod_times = 50;
    }
    vdata.mod = fade_delta / (vdata.mod_times + 1);

    if (adjust_fade_time) {
      if (isFadingOut(sound)) {
        sound.fade_time -= (float)secs_per_callback;
        if (sound.fade_time <= 0.0f) {
          // Finished not Stopped state, so mix_idx is unset in update
          sound.state = FinishedState; 
          unsetFade(sound);
        }
      } else {
        sound.fade_time += (float)secs_per_callback;
        if (sound.fade_time >= sound.fade_total) {
          unsetFade(sound);
        }
      }
    }

    return vdata;
  } else {
    VolumeData vdata;
    vdata.left_volume = vdata.right_volume = sound.volume;
    vdata.fade = 1.0;
    vdata.mod = 0.0;
    vdata.mod_times = 0;
    return vdata;
  }
}

template <class T>
void applyVolume(T *stream, int len, double left_vol, double right_vol)
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
    double fade = vdata.fade + i * vdata.mod;
    double left_vol = vdata.left_volume * fade;
    double right_vol = vdata.right_volume * fade;
    applyVolume(stream+pos, len, left_vol, right_vol);
    pos += len;
    fade += vdata.mod;
    if (fade > 1.0) {
      fade = 1.0;
    } else if (fade < 0.0) {
      fade = 0.0;
    }
  }

  double fade = vdata.fade + vdata.mod_times * vdata.mod;
  double left_vol = vdata.left_volume * fade;
  double right_vol = vdata.right_volume * fade;
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
    if (isFinished(sound)) { 
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
      decrementLoopCount(sound);
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
    if (isFinished(stream)) { 
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
          decrementLoopCount(stream);
        }
      }

      if (!streamSwapBuffers(stream, stream_buf)) {
        break;
      }
    } else { // end of stream and not end of buffer
      stream.buffer_pos = end_pos;
      decrementLoopCount(stream);
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
    switch (AudioSystem::getFormat()) {
    case OutFormat_Float:
      return copySound(CopyMono<float>(), buf, len, sound, sound_buf);
      break;
    case OutFormat_S16:
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
    switch (AudioSystem::getFormat()) {
    case OutFormat_Float:
      return copyStream(CopyMono<float>(), buf, len, sound, stream_buf);
      break;
    case OutFormat_S16:
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

// Modify left and right channel volume by position. Doesn't change if 
// sound.x and sound.y == 0.
// left_vol and right_vol must be initialized before calling.
void applyPosition(double rel_x, double rel_y, VolumeData &vdata)
{
  if (rel_x == 0 && rel_y == 0) {
    return;
  }

  double distance = sqrt(rel_x * rel_x + rel_y * rel_y);

  if (distance >= 1.0f) {
    vdata.left_volume = vdata.right_volume = 0.0f;
  } else {
    // Sound's volume on left and right speakers vary between 
    // 1.0 to (1.0-max_mod)/(1.0+max_mod), and are at 1.0/(1.0+max_mod) 
    // directly in front or behind listener. With max_mod = 0.3, this is 
    // 1.0 to 0.54 and 0.77 in front and back.
    const double max_mod = 0.3f;
    const double base = 1/(1.0f+max_mod) * (1.0 - distance);
    vdata.left_volume *= base;
    vdata.right_volume *= base;
    double left_mod = 1.0;
    double right_mod = 1.0;

    if (rel_x != 0.0f) { // not 90 or 270 degrees
      double rads = atan(rel_y / rel_x);
      if (rel_y >= 0.0f) { // quadrant 1 and 2
        if (rel_x > 0.0f) { // quadrant 1
          double mod = max_mod - rads / (PI_F/2.0f) * max_mod; 
          left_mod = 1.0f - mod;
          right_mod = 1.0f + mod;
        } else if (rel_x < 0.0f) { // quadrant 2
          double mod = max_mod + rads / (PI_F/2.0f) * max_mod; 
          left_mod = 1.0f + mod;
          right_mod = 1.0f - mod;
        }
      } else { // quadrants 3 and 4
         if (rel_x < 0.0f) { // quadrant 3
          double mod = max_mod - rads / (PI_F/2.0f) * max_mod; 
          left_mod = 1.0f + mod;
          right_mod = 1.0f - mod;
        } else if (rel_x > 0.0f) { // quadrant 4
          double mod = max_mod + rads / (PI_F/2.0f) * max_mod; 
          left_mod = 1.0f - mod;
          right_mod = 1.0f + mod;
        }
      }

      vdata.left_volume *= left_mod;
      vdata.right_volume *= right_mod;
    } // end sound.x != 0
  } // end distance < 1.0
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

