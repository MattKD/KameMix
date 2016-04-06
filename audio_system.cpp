#include "audio_system.h"
#include "sound.h"
#include "audio_mem.h"
#include <SDL.h>
#include <vector>
#include <new>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <cstdint>

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

static SoundBuf *sounds;
static CopyBuf *copies;
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

int AudioSystem::numberPlaying() { return sounds->size(); }

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

  //format = getSupportedFormat(format);
  int allowed_changes = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;
  SDL_AudioSpec spec_want = { 0 };
  SDL_AudioSpec dev_spec = { 0 };
  spec_want.callback = audioCallback;
  spec_want.channels = 2;
  //spec_want.format = AUDIO_S16SYS;
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

  sounds = km_new<SoundBuf>();
  if (sounds == nullptr) {
    shutdown();
    setError("Out of memory");
    return false;
  }
  sounds->reserve(num_sounds);

  copies = km_new<CopyBuf>();
  if (copies == nullptr) {
    shutdown();
    setError("Out of memory");
    return false;
  }
  copies->reserve(num_sounds);

  SDL_PauseAudioDevice(dev_id, 0);
  return true;
}

void AudioSystem::shutdown()
{
  SDL_CloseAudioDevice(dev_id);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);

  for (PlayingSound &sound : *sounds) {
    sound.sound->mix_idx = -1;
  }

  km_free(audio_tmp_buf);
  audio_tmp_buf = nullptr;
  km_delete(sounds);
  sounds = nullptr;
  km_delete(copies);
  copies = nullptr;
}

void AudioSystem::addSound(Sound *sound, int loops)
{
  assert(sound->buffer.size() > 0 && "Tried playing a Sound without audio data");
  sounds->push_back(PlayingSound(sound, loops));
  sound->mix_idx = sounds->size() - 1;
}

void AudioSystem::removeSound(int idx)
{
  PlayingSound &sound = (*sounds)[idx];
  sound.sound->mix_idx = -1;
  if (idx != sounds->size() - 1) {
    sound = sounds->back();
    sound.sound->mix_idx = idx;
  }
  sounds->pop_back();
}

void AudioSystem::pauseSound(int idx, bool paused)
{
  (*sounds)[idx].paused = paused;
}

bool AudioSystem::isSoundPaused(int idx)
{
  return (*sounds)[idx].paused;
}

void AudioSystem::setSoundPos(int idx, int pos)
{
  (*sounds)[idx].buffer_pos = pos;
  (*sounds)[idx].modified = true;
}

void AudioSystem::replaySound(int idx, int loops)
{
  (*sounds)[idx].buffer_pos = 0;
  (*sounds)[idx].loop_count = loops;
  (*sounds)[idx].modified = true;
}

void AudioSystem::update()
{
  SDL_LockAudioDevice(dev_id);
  int bytes_since_update = 
    num_callbacks_since_update * audio_tmp_buf_len * sizeof(float);

  num_callbacks_since_update = 0;
  copies->clear();
  copies->reserve(sounds->size());

  auto sound_iter = sounds->begin();
  auto end_iter = sounds->end();
  while (sound_iter != end_iter) {
    if (!sound_iter->paused) {
      // user reset position in sound, so set copy to that
      if (sound_iter->modified) {
        sound_iter->modified = false;
      // update sound to actual position in copy
      } else {
        int new_pos = sound_iter->buffer_pos + bytes_since_update;
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
            int idx = sound_iter - sounds->begin();
            removeSound(idx);
            if (idx == sounds->size()) {
              break;
            }
            end_iter = sounds->end();
            continue;
          }
        }
      }

      copies->push_back(PlayingCopy(*sound_iter));
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
  const float max_mod = 0.4f;
  const float base = 1/(1.0f+max_mod);

  if (sound.listener != nullptr) {
    relative_x = (relative_x - sound.listener->getX()) / sound.max_distance;
    relative_y = (relative_y - sound.listener->getY()) / sound.max_distance;
  }

  float distance = sqrt(relative_x * relative_x + relative_y * relative_y);

  if (distance >= 1.0f) {
    left_vol = right_vol = 0.0f;
  } else {
    left_vol *= 1.0f - distance;
    right_vol *= 1.0f - distance;
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
  // remove all copies with buffer_pos set to -1
  auto it = copies->begin();
  auto end_it = copies->end();
  int idx = 0;
  while (it != end_it) {
    if (it->buffer_pos == -1) {
      if (it == (end_it - 1)) { // removing last item
        copies->pop_back();
        break;
      } else {
        *it = copies->back();
        copies->pop_back();
        end_it = copies->end();
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
int copyAndAdvanceSoundCopy(uint8_t *buffer, PlayingCopy &sound, int len)
{
  int buf_left = sound.buf.size() - sound.buffer_pos;
  int cpy_amount = len < buf_left ? len : buf_left;
  int total_copied = cpy_amount;

  memcpy(buffer, sound.buf.data() + sound.buffer_pos, cpy_amount);

  // didn't reach end of sound buffer
  if (buf_left > len) { 
    sound.buffer_pos += len;
  } else {
    // reached end of sound and not looping
    if (sound.loop_count == 0) { 
      sound.buffer_pos = -1; // remove copy
    } else {
      sound.buffer_pos = 0;
      int cpy_left = len - cpy_amount;
      int buf_size = sound.buf.size();

      // buf_left may be equal to len, so cpy_left can be 0, but loop_count
      // should still be decremented if greater than 0
      do {
        if (sound.loop_count > 0) {
          sound.loop_count -= 1;
        } else if (sound.loop_count == 0) {
          sound.buffer_pos = -1;
          break;
        } 

        cpy_amount = cpy_left < buf_size ? cpy_left : buf_size;
        memcpy(buffer + total_copied, sound.buf.data(), cpy_amount);
        cpy_left -= cpy_amount;
        total_copied += cpy_amount;
      } while (cpy_left > 0);

      // last copy wasn't whole sound buffer
      if (cpy_amount < buf_size) {
        sound.buffer_pos = cpy_amount;
      // was whole sound, so decrement loop_count or set to remove if 0
      } else if (sound.loop_count > 0) {
        sound.loop_count -= 1;
      } else if (sound.loop_count == 0) {
        sound.buffer_pos = -1;
      }
    } // end else (loop_count != 0) 
  } // end else (buf_left <= len)

  return total_copied;
}

static 
void audioCallback(void *udata, Uint8 *stream, int len)
{
  ++num_callbacks_since_update;
  memset(stream, 0, len);

  for (auto &sound : *copies) {
    int total_copied = 
      copyAndAdvanceSoundCopy((uint8_t*)audio_tmp_buf, sound, len);
    int num_samples = total_copied / sizeof(float);
    float left_vol = sound.volume;
    float right_vol = sound.volume;
    applyPosition(sound, left_vol, right_vol);
    applyVolumeFloat(audio_tmp_buf, num_samples, left_vol, right_vol);
    mixStreamFloat((float*)stream, audio_tmp_buf, num_samples);
  }

  removeFinishedCopies();

  int num_samples = len / sizeof(float);
  clampFloat((float*)stream, num_samples);
}
