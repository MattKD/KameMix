#include "audio_system.h"
#include "sound.h"
#include "audio_mem.h"
#include <SDL.h>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <vector>
#include <new>

#define PI_F 3.141592653589793f

struct PlayingSound {
  PlayingSound(KameMix::Sound *s, int loops) :
    sound{s}, buffer_pos{0}, loop_count{loops}, modified{false}, paused{false}
  { }

  KameMix::Sound *sound;
  int buffer_pos;
  int loop_count;
  bool modified;
  bool paused;
};

struct PlayingCopy {
  PlayingCopy(PlayingSound &s) :
    buf(s.sound->getSoundBuffer()), buffer_pos{s.buffer_pos}, 
    loop_count{s.loop_count}, x{s.sound->getX()},
    y{s.sound->getY()}, max_distance{s.sound->getMaxDistance()},
    listener{s.sound->getListener()}
  { 
    volume = KameMix::AudioSystem::getMastervolume() * s.sound->getVolume();
    KameMix::Group *group = s.sound->getGroup();
    if (group) {
      volume *= group->getVolume();
    }
  }

  KameMix::SoundBuffer buf;
  int buffer_pos;
  int loop_count;
  float volume;
  float x, y;
  float max_distance;
  KameMix::Listener *listener;
};

typedef std::vector<PlayingSound, KameMix::Alloc<PlayingSound>> SoundBuf;
typedef std::vector<PlayingCopy, KameMix::Alloc<PlayingCopy>> CopyBuf;

struct PlayBuffers {
  SoundBuf sounds;
  CopyBuf copies;
};

static PlayBuffers *play_buffers;
static SDL_AudioDeviceID dev_id;
static int num_callbacks_since_update;
static float *audio_tmp_buf;
static int audio_tmp_buf_len;

static 
void audioCallback(void *udata, Uint8 *stream, int len);

namespace KameMix {

int AudioSystem::channels;
int AudioSystem::frequency;
float AudioSystem::master_volume = 1.0f;
const char *AudioSystem::error_string = "";
MallocFunc AudioSystem::km_malloc;
FreeFunc AudioSystem::km_free;
ReallocFunc AudioSystem::km_realloc;

int AudioSystem::numberPlaying() { return play_buffers->sounds.size(); }

bool AudioSystem::init(int num_sounds, int freq, int sample_buf_size,
                       MallocFunc custom_malloc,
                       FreeFunc custom_free,
                       ReallocFunc custom_realloc)
{
  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    setError("SDL_Init(SDL_INIT_AUDIO) failed\n");
    return false;
  }

  km_malloc = custom_malloc == nullptr ? malloc : custom_malloc;
  km_free = custom_free == nullptr ? free : custom_free;
  km_realloc = custom_realloc == nullptr ? realloc : custom_realloc;

  int allowed_changes = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;
  SDL_AudioSpec spec_want = { 0 };
  SDL_AudioSpec dev_spec = { 0 };
  spec_want.callback = audioCallback;
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
  AudioSystem::channels = dev_spec.channels;
  AudioSystem::master_volume = 1.0f;

  audio_tmp_buf_len = dev_spec.samples * dev_spec.channels;
  audio_tmp_buf = (float*) km_malloc(audio_tmp_buf_len * sizeof(float));

  play_buffers = km_new<PlayBuffers>();
  if (play_buffers == nullptr) {
    shutdown();
    setError("Out of memory");
    return false;
  }
  play_buffers->sounds.reserve(num_sounds);
  play_buffers->copies.reserve(num_sounds);

  SDL_PauseAudioDevice(dev_id, 0);
  return true;
}

void AudioSystem::shutdown()
{
  SDL_CloseAudioDevice(dev_id);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);

  for (PlayingSound &sound : play_buffers->sounds) {
    sound.sound->mix_idx = -1;
  }

  km_free(audio_tmp_buf);
  audio_tmp_buf = nullptr;
  km_delete(play_buffers);
  play_buffers = nullptr;
}

void AudioSystem::addSound(Sound *sound, int loops)
{
  assert(sound->buffer.size() > 0 && "Tried playing a Sound without audio data");
  SoundBuf &sounds = play_buffers->sounds;
  sounds.push_back(PlayingSound(sound, loops));
  sound->mix_idx = sounds.size() - 1;
}

void AudioSystem::removeSound(int idx)
{
  SoundBuf &sounds = play_buffers->sounds;
  PlayingSound &sound = sounds[idx];
  sound.sound->mix_idx = -1;
  if (idx != sounds.size() - 1) {
    sound = sounds.back();
    sound.sound->mix_idx = idx;
  }
  sounds.pop_back();
}

void AudioSystem::pauseSound(int idx, bool paused)
{
  play_buffers->sounds[idx].paused = paused;
}

bool AudioSystem::isSoundPaused(int idx)
{
  return play_buffers->sounds[idx].paused;
}

void AudioSystem::setSoundPos(int idx, int pos)
{
  SoundBuf &sounds = play_buffers->sounds;
  sounds[idx].buffer_pos = pos;
  sounds[idx].modified = true;
}

void AudioSystem::replaySound(int idx, int loops)
{
  SoundBuf &sounds = play_buffers->sounds;
  sounds[idx].buffer_pos = 0;
  sounds[idx].loop_count = loops;
  sounds[idx].modified = true;
}

void AudioSystem::update()
{
  SoundBuf &sounds = play_buffers->sounds;
  CopyBuf &copies = play_buffers->copies;
  SDL_LockAudioDevice(dev_id);
  int bytes_since_update = 
    num_callbacks_since_update * audio_tmp_buf_len * sizeof(float);

  num_callbacks_since_update = 0;
  copies.clear();
  copies.reserve(sounds.size());

  auto sound_iter = sounds.begin();
  auto end_iter = sounds.end();
  while (sound_iter != end_iter) {
    if (!sound_iter->paused) {
      // user reset position in sound, so set copy to that
      if (sound_iter->modified) {
        sound_iter->modified = false;
      // update sound to actual position in copy
      } else {
        int new_pos;
        // if mono sound advance buffer by half since data converted to stereo
        if (sound_iter->sound->getSoundBuffer().numChannels() == 1) {
          new_pos = sound_iter->buffer_pos + bytes_since_update / 2;
        } else {
          new_pos = sound_iter->buffer_pos + bytes_since_update;
        }
        int sound_size = sound_iter->sound->getSoundBuffer().size();
        // times sound finished playing since last update
        // should be 0 or 1 if update is called consistantly.
        // sound_size is never 0
        int loops = new_pos / sound_size; 
        sound_iter->buffer_pos = new_pos % sound_size;
        // doesn't loop infinitely
        if (sound_iter->loop_count != -1) {
          sound_iter->loop_count -= loops;
          if (sound_iter->loop_count < 0) {
            int idx = sound_iter - sounds.begin();
            removeSound(idx);
            // check if removed last sound and break
            if (idx == sounds.size()) {
              break;
            }
            // end iter was invalidated in removeSound
            end_iter = sounds.end();
            continue;
          }
        }
      }

      copies.push_back(PlayingCopy(*sound_iter));
    } // end if (!paused)

    ++sound_iter;
  }

  SDL_UnlockAudioDevice(dev_id);
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

// Modify left and right channel volume by position. 
// left_vol and right_vol must be initialized.
static
void applyPosition(PlayingCopy &sound, float &left_vol, float &right_vol)
{
  float relative_x = sound.x;
  float relative_y = sound.y;
  if (sound.listener != nullptr) {
    relative_x = (relative_x - sound.listener->getX()) / sound.max_distance;
    relative_y = (relative_y - sound.listener->getY()) / sound.max_distance;
  }

  float distance = sqrt(relative_x * relative_x + relative_y * relative_y);

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

    if (relative_x != 0.0f) { // not 90 or 270 degrees
      float rads = atan(sound.y / sound.x);
      if (relative_y >= 0.0f) { // quadrant 1 and 2
        if (relative_x > 0.0f) { // quadrant 1
          float mod = max_mod - rads / (PI_F/2.0f) * max_mod; 
          left_mod = 1.0f - mod;
          right_mod = 1.0f + mod;
        } else if (relative_x < 0.0f) { // quadrant 2
          float mod = max_mod + rads / (PI_F/2.0f) * max_mod; 
          left_mod = 1.0f + mod;
          right_mod = 1.0f - mod;
        }
      } else { // quadrants 3 and 4
         if (relative_x < 0.0f) { // quadrant 3
          float mod = max_mod - rads / (PI_F/2.0f) * max_mod; 
          left_mod = 1.0f + mod;
          right_mod = 1.0f - mod;
        } else if (relative_x > 0.0f) { // quadrant 4
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
void removeFinishedCopies()
{
  CopyBuf &copies = play_buffers->copies;
  // remove all copies with buffer_pos set to -1
  auto it = copies.begin();
  auto end_it = copies.end();
  int idx = 0;
  while (it != end_it) {
    if (it->buffer_pos == -1) {
      if (it == (end_it - 1)) { // removing last item
        copies.pop_back();
        break;
      } else {
        *it = copies.back();
        copies.pop_back();
        end_it = copies.end();
        continue;
      }
    }
    ++it;
    ++idx;
  }
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
int copyAndAdvanceMonoCopy(uint8_t *buf, const int buf_len, 
                           PlayingCopy &sound)
{
  const int half_buf_len = buf_len / 2;
  int src_left = sound.buf.size() - sound.buffer_pos;
  int half_cpy_amount = half_buf_len < src_left ? half_buf_len : src_left;
  int total_half_copies = half_cpy_amount;

  float *dst = (float*)buf; 
  {
    float *src = (float*)(sound.buf.data() + sound.buffer_pos); 
    float *src_end = src + half_cpy_amount / sizeof(float); 
    while (src != src_end) {
      *dst++ = *src;
      *dst++ = *src++;
    }
  }
    
  // didn't reach end of sound buffer
  if (half_cpy_amount < src_left) { 
    sound.buffer_pos += half_cpy_amount;;
  } else {
    // reached end of sound and not looping
    if (sound.loop_count == 0) { 
      sound.buffer_pos = -1; // remove copy
    } else { // reached end of sound but are looping
      sound.buffer_pos = 0;
      int half_cpy_left = half_buf_len - half_cpy_amount;
      const int src_size = sound.buf.size();

      // decrement loop_count at least once
      do {
        if (sound.loop_count > 0) {
          sound.loop_count -= 1;
        } else if (sound.loop_count == 0) {
          sound.buffer_pos = -1;
          break;
        } 

        half_cpy_amount = half_cpy_left < src_size ? half_cpy_left : src_size;
        float *src = (float*)sound.buf.data(); 
        float *src_end = src + half_cpy_amount / sizeof(float); 
        while (src != src_end) {
          *dst++ = *src;
          *dst++ = *src++;
        }
        half_cpy_left -= half_cpy_amount;
        total_half_copies += half_cpy_amount;
      } while (half_cpy_left > 0);

      // last copy wasn't whole sound buffer
      if (half_cpy_amount < src_size) {
        sound.buffer_pos = half_cpy_amount;
      // was whole sound, so decrement loop_count or set to remove if 0
      } else if (sound.loop_count > 0) {
        sound.loop_count -= 1;
      } else if (sound.loop_count == 0) {
        sound.buffer_pos = -1;
      } // do nothing if sound.loop_count == -1
    } // end else (loop_count != 0) 
  } // end else (src_left <= half_buf_len)

  return total_half_copies * 2;
}

int copyAndAdvanceStereoCopy(uint8_t *buffer, const int buf_len, 
                             PlayingCopy &sound)
{
  int src_left = sound.buf.size() - sound.buffer_pos;
  int cpy_amount = buf_len < src_left ? buf_len : src_left;
  int total_copied = cpy_amount;

  memcpy(buffer, sound.buf.data() + sound.buffer_pos, cpy_amount);

  // didn't reach end of sound buffer
  if (src_left > buf_len) { 
    sound.buffer_pos += buf_len;
  } else {
    // reached end of sound and not looping
    if (sound.loop_count == 0) { 
      sound.buffer_pos = -1; // remove copy
    } else { // reached end of sound but are looping
      sound.buffer_pos = 0;
      int cpy_left = buf_len - cpy_amount;
      const int src_size = sound.buf.size();

      // decrement loop_count at least once
      do {
        if (sound.loop_count > 0) {
          sound.loop_count -= 1;
        } else if (sound.loop_count == 0) {
          sound.buffer_pos = -1;
          break;
        } 

        cpy_amount = cpy_left < src_size ? cpy_left : src_size;
        memcpy(buffer + total_copied, sound.buf.data(), cpy_amount);
        cpy_left -= cpy_amount;
        total_copied += cpy_amount;
      } while (cpy_left > 0);

      // last copy wasn't whole sound buffer
      if (cpy_amount < src_size) {
        sound.buffer_pos = cpy_amount;
      // was whole sound, so decrement loop_count or set to remove if 0
      } else if (sound.loop_count > 0) {
        sound.loop_count -= 1;
      } else if (sound.loop_count == 0) {
        sound.buffer_pos = -1;
      }
    } // end else (loop_count != 0) 
  } // end else (src_left <= buf_len)

  return total_copied;
}

static 
void audioCallback(void *udata, Uint8 *stream, const int len)
{
  CopyBuf &copies = play_buffers->copies;
  memset(stream, 0, len);
  ++num_callbacks_since_update;

  for (auto &sound : copies) {
    int total_copied; 
    if (sound.buf.numChannels() == 1) {
      total_copied = 
        copyAndAdvanceMonoCopy((uint8_t*)audio_tmp_buf, len, sound);
    } else {
      total_copied = 
        copyAndAdvanceStereoCopy((uint8_t*)audio_tmp_buf, len, sound);
    }
    const int num_samples = total_copied / sizeof(float);
    float left_vol = sound.volume;
    float right_vol = sound.volume;
    applyPosition(sound, left_vol, right_vol);
    applyVolumeFloat(audio_tmp_buf, num_samples, left_vol, right_vol);
    mixStreamFloat((float*)stream, audio_tmp_buf, num_samples);
  }

  removeFinishedCopies();

  const int num_samples = len / sizeof(float);
  clampFloat((float*)stream, num_samples);
}
