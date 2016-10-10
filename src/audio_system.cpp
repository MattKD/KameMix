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

enum PlayingState : uint8_t {
  ReadyState,
  StoppedState,
  FadeOutState,
  FinishedState,
  PausedState
};

struct PlayingSound {
  PlayingSound(Sound *s, int loops, int buf_pos, bool paused, float fade) :
    sound{s}, buffer_pos{buf_pos}, loop_count{loops}, fade_total{fade},
    fade_time{0.0f}, tag{SoundType}, state{ReadyState}
  { 
    updateSound();
  }

  PlayingSound(Stream *s, int loops, int buf_pos, bool paused, float fade) :
    stream{s}, buffer_pos{buf_pos}, loop_count{loops}, fade_total{fade},
    fade_time{0.0f}, tag{StreamType}, state{ReadyState}
  { 
    updateStream();
  }

  void updateSound()
  {
    volume = sound->getVolumeInGroup();
    sound->getRelativeDistance(x, y);
  }

  void updateStream()
  {
    volume = stream->getVolumeInGroup();
    stream->getRelativeDistance(x, y);
  }

  void decrementLoopCount()
  {
    if (loop_count == 0) {
      state = FinishedState;
    } else if (loop_count > 0) {
      --loop_count;
    }
  }

  bool streamSwapNeeded(StreamBuffer &stream_buf)
  {
    return buffer_pos == stream_buf.size();
  }

  bool streamSwapBuffers(StreamBuffer &stream_buf)
  {
    StreamResult result = stream_buf.swapBuffers();
    if (result == StreamReady) {
      if (stream_buf.endPos() == 0) { // EOF seen immediately on read
        decrementLoopCount();
      }
      buffer_pos = 0;
      if (!stream_buf.fullyBuffered()) {
        std::thread thrd([stream_buf]() mutable { stream_buf.readMore(); }); 
        thrd.detach();
      }
      return true;
    } else if (result == StreamError) {
      state = FinishedState; 
    } else {
      assert(result != StreamNoData); // readMore never called
      buffer_pos = stream_buf.size();
    }
    return false;
  }

  union {
    Sound *sound;
    Stream *stream;
  };
  int buffer_pos; // byte pos in sound/stream
  int loop_count; // -1 for infinite loop, 0 to play once, n to loop n times
  float volume; // volume of sound * group
  float x, y; // relative position from listener
  float fade_total; // fadein/fadeout total time in sec
  float fade_time; // position in fade
  PlayingType tag;
  PlayingState state;
};

typedef std::vector<PlayingSound, Alloc<PlayingSound>> SoundBuf;

SoundBuf *sounds = nullptr;
SDL_AudioDeviceID dev_id;
// for copying sound/stream data before mixing
uint8_t *audio_tmp_buf = nullptr; 
int audio_tmp_buf_len = 0;
int32_t *audio_mix_buf = nullptr; // for mixing int16_t audio
int audio_mix_buf_len = 0;
std::mutex audio_mutex;
float secs_per_callback = 0.0f;

template <class T>
void applyVolume(T *stream, int len, float left_vol, float right_vol)
{
  for (int i = 0; i < len; i += 2) {
    stream[i] = (T)(stream[i] * left_vol);
    stream[i+1] = (T)(stream[i+1] * right_vol);
  }
}

struct CopyResult {
  int target_amount;
  int src_amount;
};

template <class CopyFunc>
int copySound(CopyFunc copy, uint8_t *buffer, const int buf_len, 
              PlayingSound &sound, SoundBuffer &sound_buf)
{
  int buf_left = buf_len;
  int total_copied = 0;

  while (true) {
    // FinishedState from decrementLoopCount
    if (sound.state == FinishedState) { 
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
    if (stream.state == FinishedState) { 
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

      if (!stream.streamSwapBuffers(stream_buf)) {
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
struct CopyMono {
  CopyResult operator()(uint8_t *dst_, int target_len, uint8_t *src_,
                        int src_len)
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
};

struct CopyStereo {
  CopyResult operator()(uint8_t *dst, int dst_len, uint8_t *src,
                        int src_len)
  {
    int cpy_amount = dst_len < src_len ? dst_len : src_len;
    memcpy(dst, src, cpy_amount);

    CopyResult result = { cpy_amount, cpy_amount };
    return result;
  }
};

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

void applyFade(PlayingSound &sound, float &left_vol, float &right_vol)
{
  if (sound.fade_total > 0.0f) {
    float fade_percent = sound.fade_time / sound.fade_total;
    if (sound.state == FadeOutState) {
      sound.fade_time -= secs_per_callback;
      if (sound.fade_time <= 0.0f) {
        // Finished not Stopped state, so mix_idx is unset in update
        sound.state = FinishedState; 
        sound.fade_total = 0.0f;
        sound.fade_time = 0.0f;
      }
    } else {
      sound.fade_time += secs_per_callback;
      if (sound.fade_time >= sound.fade_total) {
        sound.fade_total = 0.0f;
        sound.fade_time = 0.0f;
      }
    }
    left_vol *= fade_percent;
    right_vol *= fade_percent;
  }
}

// Modify left and right channel volume by position. Doesn't change if 
// sound.x and sound.y == 0.
// left_vol and right_vol must be initialized before calling.
void applyPosition(float rel_x, float rel_y, float &left_vol, float &right_vol)
{
  if (rel_x == 0 && rel_y == 0) {
    return;
  }

  float distance = sqrt(rel_x * rel_x + rel_y * rel_y);

  if (distance >= 1.0f) {
    left_vol = right_vol = 0.0f;
  } else {
    // Sound's volume on left and right speakers vary between 
    // 1.0 to (1.0-max_mod)/(1.0+max_mod), and are at 1.0/(1.0+max_mod) 
    // directly in front or behind listener. With max_mod = 0.3, this is 
    // 1.0 to 0.54 and 0.77 in front and back.
    const float max_mod = 0.3f;
    const float base = 1/(1.0f+max_mod) * (1.0f - distance);
    left_vol *= base;
    right_vol *= base;
    float left_mod = 1.0f;
    float right_mod = 1.0f;

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

      left_vol *= left_mod;
      right_vol *= right_mod;
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

} // end anon KameMix

namespace KameMix {

int AudioSystem::channels;
int AudioSystem::frequency;
OutAudioFormat AudioSystem::format;
float AudioSystem::master_volume = 1.0f;
MallocFunc AudioSystem::user_malloc;
FreeFunc AudioSystem::user_free;
ReallocFunc AudioSystem::user_realloc;
SoundFinishedFunc AudioSystem::sound_finished;
StreamFinishedFunc AudioSystem::stream_finished;
void* AudioSystem::sound_finished_data;
void* AudioSystem::stream_finished_data;

int AudioSystem::numberPlaying() 
{ 
  std::lock_guard<std::mutex> guard(audio_mutex);
  return (int)sounds->size(); 
}

bool AudioSystem::init(int freq, int sample_buf_size, OutAudioFormat format,
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
  AudioSystem::master_volume = 1.0f;

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

  secs_per_callback = 
    (float)dev_spec.samples / (float)AudioSystem::frequency;

  sounds = km_new<SoundBuf>();
  sounds->reserve(256);

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
  sounds = nullptr;
}

void AudioSystem::addSound(Sound *sound, int loops, int pos, bool paused,
                           float fade)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  sounds->push_back(PlayingSound(sound, loops, pos, paused, fade));
  sound->mix_idx = (int)sounds->size() - 1;
}

void AudioSystem::addStream(Stream *stream, int loops, int pos, bool paused,
                            float fade)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  sounds->push_back(PlayingSound(stream, loops, pos, paused, fade));
  stream->mix_idx = (int)sounds->size() - 1;
}

void AudioSystem::removeSound(int idx, float fade_secs)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  PlayingSound &sound = (*sounds)[idx];
  if (fade_secs > 0.0f) {
    sound.state = FadeOutState;
    sound.fade_time = fade_secs;
    sound.fade_total = fade_secs;
  } else {
    sound.state = StoppedState;
    // Sound and Stream mix_idx set to -1 in stop
  }
}

bool AudioSystem::isSoundFinished(int idx)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  return (*sounds)[idx].state == FinishedState;
}

void AudioSystem::pauseSound(int idx, bool paused)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  PlayingSound &sound = (*sounds)[idx];
  if (sound.state != FinishedState) {
    sound.state = paused ? PausedState : ReadyState;
  }
}

bool AudioSystem::isSoundPaused(int idx)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  PlayingSound &sound = (*sounds)[idx];
  return sound.state == PausedState;
}

void AudioSystem::update()
{
  SDL_LockAudioDevice(dev_id);
  std::lock_guard<std::mutex> guard(audio_mutex);

  auto sound = sounds->begin();
  auto sound_end = sounds->end();
  while (sound != sound_end) {
    // sound finished playing or was stopped
    if (sound->state == FinishedState || sound->state == StoppedState) { 
      // only unset mix_idx if it finished playing and wasn't stopped
      if (sound->state == FinishedState) { 
        if (sound->tag == SoundType) {
          sound->sound->mix_idx = -1;
        } else {
          sound->stream->mix_idx = -1;
        }
      }

      // swap sound to remove with last sound in list
      int idx = (int)(sound - sounds->begin());
      if (idx != (int)sounds->size() - 1) { // not removing last sound
        *sound = sounds->back();
        if (sound->tag == SoundType) {
          sound->sound->mix_idx = idx;
        } else {
          sound->stream->mix_idx = idx;
        }
        sounds->pop_back();
        sound_end = sounds->end();
        continue;
      } else { // removing last sound
        sounds->pop_back();
        break;
      }
    }

    if (sound->tag == SoundType) {
      sound->updateSound();
    } else {
      StreamBuffer &stream_buf = sound->stream->buffer;
      // failed to advance in audioCallback, so try here
      if (sound->streamSwapNeeded(stream_buf)) {
        sound->streamSwapBuffers(stream_buf);
        if (sound->state == FinishedState) { // finished or error occurred
          continue; // will be removed at top of loop
        }
      }
      sound->updateStream();
    }

    ++sound;
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
    if (sound.state == ReadyState || sound.state == FadeOutState) {
      int total_copied = 0; 

      if (sound.tag == SoundType) {
        total_copied = copySound(sound, sound.sound->buffer, 
                                 audio_tmp_buf, len);
      } else {
        total_copied = copyStream(sound, sound.stream->buffer, 
                                  audio_tmp_buf, len);
      }

      float rel_x = sound.x;
      float rel_y = sound.y;
      float left_vol = sound.volume;
      float right_vol = sound.volume;
      applyFade(sound, left_vol, right_vol);

      // Unlock audio_mutex when done with sound. Must be unlocked
      // before calling soundFinished/streamFinished
      if (sound.state == FinishedState) {
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

      applyPosition(rel_x, rel_y, left_vol, right_vol);

      switch (AudioSystem::getFormat()) {
      case OutFormat_Float: {
        const int num_samples = total_copied / sizeof(float);
        applyVolume((float*)audio_tmp_buf, num_samples, 
                    left_vol, right_vol);
        mixStream((float*)stream, (float*)audio_tmp_buf, num_samples);
        break;
      }
      case OutFormat_S16: {
        const int num_samples = total_copied / sizeof(int16_t);
        applyVolume((int16_t*)audio_tmp_buf, num_samples, 
                    left_vol, right_vol);
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
