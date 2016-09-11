#include "audio_system.h"
#include "sound.h"
#include "stream.h"
#include "audio_mem.h"
#include <SDL.h>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <vector>

#define PI_F 3.141592653589793f

namespace KameMix {

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

} // end namespace KameMix

using namespace KameMix;

typedef std::vector<PlayingSound, Alloc<PlayingSound>> SoundBuf;

static 
void applyVolumeFloat(float *stream, int len, float left_vol, float right_vol);
static
void mixStreamFloat(float *target, float *source, int len);
static
void applyPosition(float rel_x, float rel_y, float &left_vol, float &right_vol);
static
void clampFloat(float *buf, int len);
static
int copyMonoSound(uint8_t *buf, const int buf_len, 
                  PlayingSound &sound, SoundBuffer &sound_buf);
static
int copyMonoStream(uint8_t *buf, const int buf_len, 
                   PlayingSound &stream, StreamBuffer &stream_buf);
static
int copyStereoSound(uint8_t *buffer, const int buf_len, 
                    PlayingSound &sound, SoundBuffer &sound_buf);
static
int copyStereoStream(uint8_t *buffer, const int buf_len, 
                     PlayingSound &sound, StreamBuffer &stream_buf);

static SoundBuf *sounds;
static SDL_AudioDeviceID dev_id;
static float *audio_tmp_buf;
static int audio_tmp_buf_len;
static std::mutex audio_mutex;
static float secs_per_callback;

namespace KameMix {

int AudioSystem::channels;
int AudioSystem::frequency;
AudioFormat AudioSystem::format;
float AudioSystem::master_volume = 1.0f;
char AudioSystem::error_string[error_len] = "\0";
MallocFunc AudioSystem::user_malloc;
FreeFunc AudioSystem::user_free;
ReallocFunc AudioSystem::user_realloc;
SoundFinishedFunc AudioSystem::sound_finished;
StreamFinishedFunc AudioSystem::stream_finished;

int AudioSystem::numberPlaying() 
{ 
  std::lock_guard<std::mutex> guard(audio_mutex);
  return sounds->size(); 
}

void AudioSystem::setError(const char *err, ...)
{
  va_list args;
  va_start(args, err);
  vsnprintf(error_string, error_len, err, args); 
  va_end(args);
}

bool AudioSystem::init(int freq, int sample_buf_size,
                       MallocFunc custom_malloc,
                       FreeFunc custom_free,
                       ReallocFunc custom_realloc)
{
  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    setError("SDL_Init(SDL_INIT_AUDIO) failed\n");
    return false;
  }

  if (custom_malloc || custom_free || custom_realloc) { // at least one set
    if (!(custom_malloc && custom_free && custom_realloc)) { // not all set
      setError("Custom malloc supplied without custom free\n");
      return false;
    }
  }

  user_malloc = custom_malloc == nullptr ? malloc : custom_malloc;
  user_free = custom_free == nullptr ? free : custom_free;
  user_realloc = custom_realloc == nullptr ? realloc : custom_realloc;

  int allowed_changes = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;
  SDL_AudioSpec spec_want = { 0 };
  SDL_AudioSpec dev_spec = { 0 };
  spec_want.callback = AudioSystem::audioCallback;
  spec_want.channels = 2;
  spec_want.format = AUDIO_F32SYS;
  spec_want.freq = freq;
  spec_want.samples = sample_buf_size;

  dev_id = SDL_OpenAudioDevice(NULL, 0, &spec_want, &dev_spec, 
                               allowed_changes);
  if (dev_id == 0) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    setError("SDL_OpenAudioDevice failed\n");
    return false;
  }

  AudioSystem::frequency = dev_spec.freq;
  AudioSystem::format = FloatFormat;
  AudioSystem::channels = dev_spec.channels;
  AudioSystem::master_volume = 1.0f;

  audio_tmp_buf_len = dev_spec.samples * dev_spec.channels;
  audio_tmp_buf = (float*)km_malloc(sizeof(float) * audio_tmp_buf_len);
  
  secs_per_callback = (float)dev_spec.samples / (float)AudioSystem::frequency;

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
  km_delete(sounds);
  sounds = nullptr;
}

void AudioSystem::addSound(Sound *sound, int loops, int pos, bool paused,
                           float fade)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  sounds->push_back(PlayingSound(sound, loops, pos, paused, fade));
  sound->mix_idx = sounds->size() - 1;
}

void AudioSystem::addStream(Stream *stream, int loops, int pos, bool paused,
                            float fade)
{
  std::lock_guard<std::mutex> guard(audio_mutex);
  sounds->push_back(PlayingSound(stream, loops, pos, paused, fade));
  stream->mix_idx = sounds->size() - 1;
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
      int idx = sound - sounds->begin();
      if (idx != sounds->size() - 1) { // not removing last sound
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
  memset(stream, 0, len);
  std::unique_lock<std::mutex> guard(audio_mutex);

  // Sounds can be added between locks, so use indexing and size(), instead 
  // of iterators or range-based for loop. Nothing is removed until update.
  for (int i = 0; i < (int)sounds->size(); ++i) {
    // audio_mutex must be locked while using sound
    PlayingSound &sound = (*sounds)[i];
    if (sound.state == ReadyState || sound.state == FadeOutState) {
      int total_copied = 0; 

      if (sound.tag == SoundType) {
        SoundBuffer &sound_buf = sound.sound->buffer;
        if (sound_buf.numChannels() == 1) {
          total_copied = copyMonoSound((uint8_t*)audio_tmp_buf, 
                                       len, sound, sound_buf);
        } else {
          total_copied = copyStereoSound((uint8_t*)audio_tmp_buf, 
                                         len, sound, sound_buf);
        }
      } else {
        StreamBuffer &stream_buf = sound.stream->buffer;
        if (stream_buf.numChannels() == 1) {
          total_copied = copyMonoStream((uint8_t*)audio_tmp_buf, 
                                        len, sound, stream_buf);
        } else {
          total_copied = copyStereoStream((uint8_t*)audio_tmp_buf, 
                                          len, sound, stream_buf);
        }
      }

      float rel_x = sound.x;
      float rel_y = sound.y;
      float left_vol = sound.volume;
      float right_vol = sound.volume;
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

      guard.unlock();

      if (sound.state == FinishedState) {
        if (sound.tag == SoundType) {
          soundFinished(sound.sound);
        } else {
          streamFinished(sound.stream);
        }
      }

      const int num_samples = total_copied / sizeof(float);
      applyPosition(rel_x, rel_y, left_vol, right_vol);
      applyVolumeFloat(audio_tmp_buf, num_samples, left_vol, right_vol);
      mixStreamFloat((float*)stream, audio_tmp_buf, num_samples);
      guard.lock();
    }
  }

  const int num_samples = len / sizeof(float);
  clampFloat((float*)stream, num_samples);
}

} // end namespace KameMix

// 
// file static functions
//

static 
void applyVolumeFloat(float *stream, int len, float left_vol, float right_vol)
{
  float *iter = stream; // left channel start
  float *iter_end = iter + len;

  while (iter < iter_end) {
    *iter *= left_vol;
    iter += 2;
  }

  iter = stream + 1; // right channel start
  while (iter < iter_end) {
    *iter *= right_vol;
    iter += 2;
  }
}

static inline
void mixStreamFloat(float *target, float *source, int len) 
{
  for (int i = 0; i < len; ++i) {
    // clamp here?
    target[i] += source[i];
  }
}

// Modify left and right channel volume by position. Doesn't change if 
// sound.x and sound.y == 0.
// left_vol and right_vol must be initialized before calling.
static
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

static
void clampFloat(float *buf, int len)
{
  float *buf_end = buf + len;
  while (buf != buf_end) {
    if (*buf > 1.0f) {
      *buf = 1.0f;
    } else if (*buf < -1.0f) {
      *buf = -1.0f;
    }
    ++buf;
  }
}

static
int copyMonoSound(uint8_t *buf, const int buf_len, 
                  PlayingSound &sound, SoundBuffer &sound_buf)
{
  int half_buf_left = buf_len / 2;
  int total_half_copied = 0;
  float *dst = (float*)buf; 

  while (true) {
    // FinishedState from decrementLoopCount
    if (sound.state == FinishedState) { 
      break;
    }

    int src_left = sound_buf.size() - sound.buffer_pos;
    int half_cpy_amount = half_buf_left < src_left ? half_buf_left : src_left;

    {
      float *src = (float*)(sound_buf.data() + sound.buffer_pos); 
      float *src_end = (float*)((uint8_t*)src + half_cpy_amount); 
      while (src != src_end) {
        *dst++ = *src;
        *dst++ = *src++;
      }
    }
    
    total_half_copied += half_cpy_amount;

    // didn't reach end of sound buffer
    if (half_cpy_amount < src_left) { 
      sound.buffer_pos += half_cpy_amount;;
      break;
    } else {
      // reached end of sound
      sound.decrementLoopCount();
      sound.buffer_pos = 0;
      half_buf_left -= half_cpy_amount;
    } 
  }

  return total_half_copied * 2;
}

static
int copyStereoSound(uint8_t *buffer, const int buf_len, 
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
    int cpy_amount = buf_left < src_left ? buf_left : src_left;

    memcpy(buffer + total_copied, sound_buf.data() + sound.buffer_pos, 
           cpy_amount);
    total_copied += cpy_amount;

    // didn't reach end of sound buffer
    if (cpy_amount < src_left) { 
      sound.buffer_pos += cpy_amount;;
      break;
    } else {
      // reached end of sound
      sound.decrementLoopCount();
      sound.buffer_pos = 0;
      buf_left -= cpy_amount;
    } 
  }

  return total_copied;
}

static
int copyMonoStream(uint8_t *buf, const int buf_len, 
                   PlayingSound &stream, StreamBuffer &stream_buf)
{
  int half_buf_left = buf_len / 2;
  // total copied from stream_buf before mono to stereo conversion
  int total_half_copied = 0; 
  float *dst = (float*)buf; 

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
    int half_cpy_amount = half_buf_left < src_left ? half_buf_left : src_left;

    {
      float *src = (float*)(stream_buf.data() + stream.buffer_pos); 
      float *src_end = (float*)((uint8_t*)src + half_cpy_amount); 
      while (src != src_end) {
        *dst++ = *src;
        *dst++ = *src++;
      }
    }

    total_half_copied += half_cpy_amount;
    half_buf_left -= half_cpy_amount;
      
    // didn't reach end of stream or its main buffer
    if (half_cpy_amount < src_left) { 
      stream.buffer_pos += half_cpy_amount;;
      break;
    // reached end of buffer
    } else if (stream.buffer_pos + half_cpy_amount == stream_buf.size()) { 
      if (end_pos == stream_buf.size()) { // end of stream at end of buffer
        // buffer_pos may be at end from last time and failed to swap buffers
        if (half_cpy_amount > 0) { 
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

  return total_half_copied * 2;
}

static
int copyStereoStream(uint8_t *buffer, const int buf_len, 
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
    int cpy_amount = buf_left < src_left ? buf_left : src_left;

    memcpy(buffer + total_copied, stream_buf.data() + stream.buffer_pos, 
           cpy_amount);
    total_copied += cpy_amount;
    buf_left -= cpy_amount;
      
    // didn't reach end of stream or its main buffer
    if (cpy_amount < src_left) { 
      stream.buffer_pos += cpy_amount;
      break;
    // reached end of buffer
    } else if (stream.buffer_pos + cpy_amount == stream_buf.size()) { 
      if (end_pos == stream_buf.size()) { // end of stream at end of buffer
        // buffer_pos may be at end from last time and failed to swap buffers
        if (cpy_amount > 0) { 
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

