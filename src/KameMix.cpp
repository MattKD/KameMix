#include "KameMix.h"
#include "sound_buffer.h"
#include "stream_buffer.h"
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

struct KameMix_Sound {
  KameMix_Sound(const char *file) : buffer{file}, refcount{1} { }
  SoundBuffer buffer;
  std::atomic<int> refcount;
};

struct KameMix_Stream {
  KameMix_Stream(const char *file) : buffer{file}, refcount{1}  { }
  StreamBuffer buffer;
  std::atomic<int> refcount;
};

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

struct CopyResult {
  int target_amount;
  int src_amount;
};

struct VolumeFade {
  float left_fade;
  float right_fade;
};

struct Position2d {
  float x, y;
};

struct PlayingSound {
  PlayingSound() : tag{InvalidType} { }
  PlayingSound(KameMix_Sound *s, int loops, int buf_pos, int paused, 
               float fade, float vol, float x, float y, float max_distance, 
               int group, unsigned id);
  PlayingSound(KameMix_Stream *s, int loops, int buf_pos, int paused, 
               float fade, float vol, float x, float y, float max_distance, 
               int group, unsigned id);
  // No destructor, so must call release manually. Otherwise must write 
  // copy ctor and assign op

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

  void release();
  void setFadein(float fade);
  void setFadeout(float fade);
  void unsetFade() { fade_total = fade_time = 0; }
  float fadeinTotal() const { return fade_total; }
  float fadeoutTotal() const { return -fade_total; }
  void decrementLoopCount();
  Position2d getRelativePos() const;
  float volumeInGroup() const;
  VolumeData getVolumeData();

  bool streamSwapNeeded();
  bool streamSwapBuffers();

  KameMix_Sound& sound() { 
    assert(tag == SoundType);
    return *sound_; 
  }

  KameMix_Stream& stream() { 
    assert(tag == StreamType);
    return *stream_; 
  }

  union {
    KameMix_Sound *sound_;
    KameMix_Stream *stream_;
  };
  int buffer_pos; // byte pos in sound/stream
  int loop_count; // -1 for infinite loop, 0 to play once, n to loop n times
  int group;
  unsigned id;
  float fade_total; // fadein/out total time in sec, negative for fadeout
  float fade_time; // elapsed time for fadein, time left for fadeout
  float new_volume; // updated volume
  float lvolume; // previous played volume (* group * master * fades)
  float rvolume;
  float x, y; // absolute position
  float max_distance;
  PlayingType tag;
  PlayState state;
};

typedef std::vector<PlayingSound, Alloc<PlayingSound>> SoundBuf;
typedef std::vector<int, Alloc<int>> FreeList;

struct KameMixData {
  SDL_AudioDeviceID dev_id;
  // for copying sound/stream data before mixing
  uint8_t *audio_tmp_buf; 
  int audio_tmp_buf_len;
  int32_t *audio_mix_buf; // for mixing int16_t audio
  int audio_mix_buf_len;
  std::mutex audio_mutex;
  float secs_per_callback;
  SoundBuf *sounds;
  FreeList *free_list;
  int number_playing;
  std::vector<float> *groups;
  float master_volume;
  float listener_x;
  float listener_y;
  unsigned int next_id;
  int channels;
  int frequency;
  KameMix_OutputFormat format;
  KameMix_MallocFunc user_malloc;
  KameMix_FreeFunc user_free;
  KameMix_ReallocFunc user_realloc;
} kame_mix;

inline unsigned getNextID_locked() { return kame_mix.next_id++; }

void audioCallback(void *udata, uint8_t *stream, const int len);
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

extern "C" {

void KameMix_setListenerPos(float x, float y)
{
  std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
  kame_mix.listener_x = x;
  kame_mix.listener_y = y;
}

void KameMix_getListenerPos(float *x, float *y)
{
  std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
  *x = kame_mix.listener_x;
  *y = kame_mix.listener_y;
}

float KameMix_getMasterVolume()
{
  std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
  return kame_mix.master_volume;
}

void KameMix_setMasterVolume(float volume)
{
  std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
  kame_mix.master_volume = volume;
}

int KameMix_createGroup()
{
  std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
  kame_mix.groups->push_back(1.0f); // group start at 100% volume
  return kame_mix.groups->size() - 1; // idx to group
}

void KameMix_setGroupVolume(int group, float volume)
{
  std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
  assert(group >= 0 && group < (int)kame_mix.groups->size());
  (*kame_mix.groups)[group] = volume;
}

float KameMix_getGroupVolume(int group)
{
  std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
  assert(group >= 0 && group < (int)kame_mix.groups->size());
  return (*kame_mix.groups)[group];
}

// May include stopped/finished kame_mix.sounds if called far away from update
int KameMix_numberPlaying() 
{ 
  std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
  return kame_mix.number_playing; 
}

int KameMix_getFrequency() { return kame_mix.frequency; }
int KameMix_getChannels() { return kame_mix.channels; }
KameMix_OutputFormat KameMix_getFormat() { return kame_mix.format; }

int KameMix_getFormatSize()
{ 
  switch (kame_mix.format) {
  case KameMix_OutputFloat:
    return sizeof(float);
  case KameMix_OutputS16:
    return sizeof(int16_t);
  }
  assert("Unknown AudioFormat");
  return 0;
}

void KameMix_setAlloc(KameMix_MallocFunc malloc_,
                      KameMix_FreeFunc free_,
                      KameMix_ReallocFunc realloc_)
{
  if (malloc_ && free_ && realloc_) { // all set
    kame_mix.user_malloc = malloc_; 
    kame_mix.user_free = free_;
    kame_mix.user_realloc = realloc_;
  }
}

KameMix_MallocFunc KameMix_getMalloc() { return kame_mix.user_malloc; }
KameMix_FreeFunc KameMix_getFree() { return kame_mix.user_free; }
KameMix_ReallocFunc KameMix_getRealloc() { return kame_mix.user_realloc; }

int KameMix_init(int freq, int sample_buf_size, KameMix_OutputFormat format_)
{
  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    return false;
  }

  // set to stdlib version if not user defined
  if (!kame_mix.user_malloc || !kame_mix.user_free ||!kame_mix.user_realloc) {
    kame_mix.user_malloc = malloc;
    kame_mix.user_free = free;
    kame_mix.user_realloc = realloc;
  }

  kame_mix.format = format_;

  int allowed_changes = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;
  SDL_AudioSpec spec_want = { 0 };
  SDL_AudioSpec dev_spec = { 0 };
  spec_want.callback = audioCallback;
  spec_want.channels = 2;
  spec_want.format = outFormatToSDL(kame_mix.format);
  spec_want.freq = freq;
  spec_want.samples = sample_buf_size;

  kame_mix.dev_id = SDL_OpenAudioDevice(NULL, 0, &spec_want, &dev_spec, 
                               allowed_changes);
  if (kame_mix.dev_id == 0) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return false;
  }

  kame_mix.frequency = dev_spec.freq;
  kame_mix.channels = dev_spec.channels;

  // kame_mix.format must have already been set above
  kame_mix.audio_tmp_buf_len = 
    dev_spec.samples * dev_spec.channels * KameMix_getFormatSize();
  kame_mix.audio_tmp_buf = (uint8_t*)km_malloc(kame_mix.audio_tmp_buf_len);
  
  // kame_mix.audio_mix_buf is only used for OutputS16
  if (kame_mix.format == KameMix_OutputS16) {
    kame_mix.audio_mix_buf_len = dev_spec.samples * dev_spec.channels;
    kame_mix.audio_mix_buf = 
      (int32_t*)km_malloc(kame_mix.audio_mix_buf_len * sizeof(int32_t));
  }

  kame_mix.master_volume = 1.0f;
  kame_mix.listener_x = 0;
  kame_mix.listener_y = 0;
  kame_mix.secs_per_callback = (float)dev_spec.samples / kame_mix.frequency;
  kame_mix.next_id = 1;

  kame_mix.sounds = km_new<SoundBuf>();
  kame_mix.sounds->reserve(128);
  kame_mix.free_list = km_new<FreeList>();
  kame_mix.free_list->reserve(128);
  kame_mix.groups = km_new<std::vector<float>>();

  SDL_PauseAudioDevice(kame_mix.dev_id, 0);
  return 1;
}

void KameMix_shutdown()
{
  SDL_CloseAudioDevice(kame_mix.dev_id);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);

  for (PlayingSound &sound : *kame_mix.sounds) {
    sound.release();
  }

  km_free(kame_mix.audio_tmp_buf);
  kame_mix.audio_tmp_buf = nullptr;
  kame_mix.audio_tmp_buf_len = 0;

  km_free(kame_mix.audio_mix_buf);
  kame_mix.audio_mix_buf = nullptr;
  kame_mix.audio_mix_buf_len = 0;

  km_delete(kame_mix.sounds);
  kame_mix.sounds = nullptr;

  km_delete(kame_mix.free_list);
  kame_mix.free_list = nullptr;

  km_delete(kame_mix.groups);
  kame_mix.groups = nullptr;
}

//
// Static functions used by Sound/Stream/Channel functions
//

static inline 
int findFreeChannel_locked()
{
  kame_mix.number_playing += 1;
  if (!kame_mix.free_list->empty()) {
    int idx = kame_mix.free_list->back();
    kame_mix.free_list->pop_back();
    return idx;
  }

  kame_mix.sounds->push_back(PlayingSound()); // add uninitialized
  return kame_mix.sounds->size() - 1;
}

static inline
void freeChannel_locked(int idx, PlayingSound &sound)
{
  kame_mix.free_list->push_back(idx);
  kame_mix.number_playing -= 1;
  sound.state = FinishedState;
  sound.release(); // decrement Sound/Stream refcount; set to InvalidType
}

static inline
void fadeoutChannel_locked(KameMix_Channel c, float fade_secs)
{
  PlayingSound &sound = (*kame_mix.sounds)[c.idx];
  if (sound.id == c.id && sound.tag != InvalidType) {
    sound.setFadeout(fade_secs);
  }
}

static inline
void haltChannel_locked(KameMix_Channel c)
{
  PlayingSound &sound = (*kame_mix.sounds)[c.idx];
  if (sound.id == c.id && sound.tag != InvalidType) {
    freeChannel_locked(c.idx, sound);
  }
}

//
// Channel functions
//

static inline
KameMix_Channel nullChannel()
{
  KameMix_Channel c;
  KameMix_unsetChannel(c);
  return c;
}

void KameMix_halt(KameMix_Channel c)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    haltChannel_locked(c);
  }
}

void KameMix_stop(KameMix_Channel c)
{
  KameMix_fadeout(c, -1.0f);
}

void KameMix_fadeout(KameMix_Channel c, float fade_secs)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    fadeoutChannel_locked(c, fade_secs);
  }
}

int KameMix_isPlaying(KameMix_Channel c)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id && (sound.isPlaying() || sound.isUnpausing())) {
      return 1;
    }
  }
  return 0;
}

int KameMix_isPaused(KameMix_Channel c)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id && (sound.isPaused() || sound.isPausing())) {
      return 1;
    }
  }
  return 0;
}

int KameMix_isFinished(KameMix_Channel c)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id) {
      if (sound.isFinished()) {
        return 1;
      }
      return 0;
    }
  }
  return 1;
}

void KameMix_pause(KameMix_Channel c)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (c.id == sound.id) {
      if (sound.isPlaying()) { 
        sound.state = PausingState;
      } else if (sound.isUnpausing()) {
        sound.state = PausedState;
      }
    }
  }
}

void KameMix_unpause(KameMix_Channel c)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (c.id == sound.id) {
      if (sound.isPaused()) { 
        sound.state = UnpausingState;
      } else if (sound.isPausing()) {
        sound.state = PlayingState;
      }
    }
  }
}

KameMix_Channel KameMix_setLoopCount(KameMix_Channel c, int loops)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id) {
      sound.loop_count = loops;
      return c;
    }
  }
  return nullChannel();
}

int KameMix_getLoopCount(KameMix_Channel c)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id) {
      return sound.loop_count;
    }
  }
  return 0;
}

KameMix_Channel KameMix_setPos(KameMix_Channel c, float x, float y)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id) {
      sound.x = x;
      sound.y = y;
      return c;
    }
  }
  return nullChannel();
}

void KameMix_getPos(KameMix_Channel c, float *x, float *y)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id) {
      *x = sound.x;
      *y = sound.y;
      return;
    }
  }
  *x = 0;
  *y = 0;
}

KameMix_Channel KameMix_setMaxDistance(KameMix_Channel c, float distance)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id) {
      sound.max_distance = distance;
      return c;
    }
  }
  return nullChannel();
}

float KameMix_getMaxDistance(KameMix_Channel c)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id) {
      return sound.max_distance;
    }
  }
  return 0.0f;
}

KameMix_Channel KameMix_setGroup(KameMix_Channel c, int group)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id) {
      sound.group = group;
      return c;
    }
  }
  return nullChannel();
}

int KameMix_getGroup(KameMix_Channel c)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id) {
      return sound.group;
    }
  }
  return -1;
}

KameMix_Channel KameMix_setVolume(KameMix_Channel c, float volume)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id) {
      sound.new_volume = volume;
      return c;
    }
  }
  return nullChannel();
}

float KameMix_getVolume(KameMix_Channel c)
{
  if (KameMix_isChannelSet(c)) {
    std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
    PlayingSound &sound = (*kame_mix.sounds)[c.idx];
    if (sound.id == c.id) {
      return sound.new_volume;
    }
  }
  return 1.0f;
}

//
// Sound functions
//

KameMix_Sound* KameMix_loadSound(const char *file)
{
  using KameMix::km_malloc_;
  KameMix_Sound *sound = (KameMix_Sound*)km_malloc_(sizeof(KameMix_Sound));
  if (sound) {
    new (sound) KameMix_Sound(file);
    if (sound->buffer.isLoaded()) {
      return sound;
    }
    KameMix_freeSound(sound);
  }
  return NULL;
}

void KameMix_freeSound(KameMix_Sound *sound)
{
  if (sound) {
    if (sound->refcount.fetch_sub(1, std::memory_order_release) == 1) {
      std::atomic_thread_fence(std::memory_order_acquire);
      sound->~KameMix_Sound();
      KameMix::km_free(sound);
    }
  }
}

void KameMix_incSoundRef(KameMix_Sound *sound)
{
  assert(sound != NULL);
  sound->refcount.fetch_add(1, std::memory_order_relaxed);
}

static inline
int soundTimeToBytePos(KameMix_Sound *sound, double secs)
{
  if (secs == 0) {
    return 0;
  }
  SoundBuffer &buffer = sound->buffer;
  int sample_pos = (int) (secs * KameMix_getFrequency());
  int byte_pos = sample_pos * buffer.sampleBlockSize();
  if (byte_pos < 0 || byte_pos >= buffer.size()) {
    byte_pos = 0;
  }
  return byte_pos;
}

KameMix_Channel 
KameMix_playSound(KameMix_Sound *sound, KameMix_Channel c, double start_sec, 
                  int loops, float vol, float fade_secs, float x, float y, 
                  float max_distance, int group, int paused)
{
  std::lock_guard<std::mutex> guard(kame_mix.audio_mutex);
  if (KameMix_isChannelSet(c)) {
    fadeoutChannel_locked(c, -1.0f);
  }

  c.idx = findFreeChannel_locked();
  c.id = getNextID_locked();

  int byte_pos = soundTimeToBytePos(sound, start_sec);
  // PlayingSound ctor increments refcount
  (*kame_mix.sounds)[c.idx] = 
    PlayingSound(sound, loops, byte_pos, paused, fade_secs, vol,
                 x, y, max_distance, group, c.id);
  return c;
}

//
// Stream functions
//

static inline
void KameMix_incStreamRef(KameMix_Stream *stream)
{
  assert(stream != NULL);
  stream->refcount.fetch_add(1, std::memory_order_relaxed);
}

static inline
void streamReadMore(KameMix_Stream *stream)
{
  KameMix_incStreamRef(stream);
  std::thread thrd([stream]() mutable { 
    stream->buffer.readMore(); 
    KameMix_freeStream(stream);
  }); 
  thrd.detach();
}

KameMix_Stream* KameMix_loadStream(const char *file)
{
  using KameMix::km_malloc_;
  KameMix_Stream *stream = (KameMix_Stream*)km_malloc_(sizeof(KameMix_Stream));
  if (stream) {
    new (stream) KameMix_Stream(file);

    if (stream->buffer.isLoaded()) {
      streamReadMore(stream); // read into 2nd buffer in different thread
      return stream;
    }
    KameMix_freeStream(stream);
  }
  return NULL;
}

void KameMix_freeStream(KameMix_Stream *stream)
{
  if (stream) {
    if (stream->refcount.fetch_sub(1, std::memory_order_release) == 1) {
      std::atomic_thread_fence(std::memory_order_acquire);
      stream->~KameMix_Stream();
      KameMix::km_free(stream);
    }
  }
}

KameMix_Channel 
KameMix_playStream(KameMix_Stream *stream, KameMix_Channel c, 
                   double start, int loops, float vol, 
                   float fade_secs, float x, float y, float max_distance, 
                   int group, int paused)
{
  std::unique_lock<std::mutex> guard(kame_mix.audio_mutex);
  if (KameMix_isChannelSet(c)) {
    haltChannel_locked(c);
  }

  StreamBuffer &buffer = stream->buffer;
  int byte_pos;
  if (start == 0) {
    byte_pos = buffer.startPos();
  } else {
    if (start < 0 || start >= buffer.totalTime()) {
      start = 0;
    }

    byte_pos = buffer.getPos(start);
  }

  if (byte_pos == -1) { // pos not in buffer
    guard.unlock();
    // read stream at new pos into main buffer, so no swap needed
    if (!buffer.setPos(start, true)) { 
      KameMix_unsetChannel(c);
      return c;
    }

    streamReadMore(stream);
    byte_pos = 0;
    guard.lock();
  }

  // audio_mutex must be locked here
  c.idx = findFreeChannel_locked();
  c.id = getNextID_locked();

  // PlayingSound ctor increments refcount
  (*kame_mix.sounds)[c.idx] = 
    PlayingSound(stream, loops, byte_pos, paused, fade_secs, vol,
                 x, y, max_distance, group, c.id);
  return c;
}

} // end extern "C"

namespace {

//
// PlayingSound functions
//

PlayingSound::PlayingSound(KameMix_Sound *sound, int loops, int buf_pos, 
                           int paused, float fade, float vol, float x_, float y_, 
                           float max_distance_, int group_, unsigned id_)
{
  tag = SoundType;
  sound_ = sound;
  KameMix_incSoundRef(sound_);

  buffer_pos = buf_pos; 
  loop_count = loops; 
  group = group_;
  id = id_;
  new_volume = vol; 
  lvolume = vol;
  rvolume = vol;
  x = x_;
  y = y_; 
  max_distance = max_distance_;

  if (fade == 0) {
    unsetFade();
  } else {
    setFadein(fade);
  }

  if (paused) {
    state = PausedState;
  } else {
    state = PlayingState;
  }
}

PlayingSound::PlayingSound(KameMix_Stream *stream, int loops, int buf_pos, 
                           int paused, float fade, float vol, float x_, float y_, 
                           float max_distance_, int group_, unsigned id_)
{
  tag = StreamType;
  stream_ = stream;
  KameMix_incStreamRef(stream);

  buffer_pos = buf_pos; 
  loop_count = loops; 
  group = group_;
  id = id_;
  new_volume = vol; 
  lvolume = vol;
  rvolume = vol;
  x = x_;
  y = y_; 
  max_distance = max_distance_;

  if (fade == 0) {
    unsetFade();
  } else {
    setFadein(fade);
  }

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
    KameMix_freeSound(sound_);
    break;
  case StreamType:
    KameMix_freeStream(stream_);
    break;
  case InvalidType:
    break;
  }
  tag = InvalidType;
  id = 0;
}

inline
float PlayingSound::volumeInGroup() const
{
  float v = new_volume * kame_mix.master_volume;
  if (group >= 0) {
    v *= (*kame_mix.groups)[group];
  }
  return v;
}

inline
Position2d PlayingSound::getRelativePos() const
{
  Position2d pos;
  if (max_distance > 0) {
    pos.x = (x - kame_mix.listener_x) / max_distance;
    pos.y = (y - kame_mix.listener_y) / max_distance;
  } else {
    pos.x = 0;
    pos.y = 0;
  }
  return pos;
}

void PlayingSound::setFadein(float fade) 
{
  if (fade > kame_mix.secs_per_callback) {
    fade_total = fade;
  } else {
    fade_total = kame_mix.secs_per_callback;
  }
  fade_time = 0.0f;
}

// set fade_total to negative for fadeout
void PlayingSound::setFadeout(float fade) {
  if (fade > kame_mix.secs_per_callback) {
    fade_total = -fade;
    fade_time = fade; 
  } else {
    fade_total = -kame_mix.secs_per_callback;
    fade_time = kame_mix.secs_per_callback; 
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
  return buffer_pos == stream().buffer.size();
}

bool PlayingSound::streamSwapBuffers()
{
  StreamBuffer &sbuf = stream().buffer;
  StreamResult result = sbuf.swapBuffers();
  if (result == StreamReady) {
    if (sbuf.endPos() == 0) { // EOF seen immediately on read
      decrementLoopCount();
    }
    buffer_pos = 0;
    if (!sbuf.fullyBuffered()) {
      streamReadMore(stream_);
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

// kame_mix.audio_mutex must be locked
VolumeData PlayingSound::getVolumeData()
{
  float new_lvol = volumeInGroup(); // new_volume * group * master
  float new_rvol = new_lvol;
  Position2d rel_pos = getRelativePos();
  VolumeFade vfade = applyPosition(rel_pos.x, rel_pos.y);
  new_lvol *= vfade.left_fade;
  new_rvol *= vfade.right_fade;

  if (isFading() || isPauseChanging() || isVolumeChanging(new_lvol, new_rvol)) {
    float start_lfade = 1.0;
    float start_rfade = 1.0;
    float end_lfade = 1.0;
    float end_rfade = 1.0;
    bool adjust_fade_time = false;

    if (isFadingIn()) {
      start_lfade = fade_time / fade_total;
      start_rfade = start_lfade;
      end_lfade = (fade_time + kame_mix.secs_per_callback) / fade_total;
      end_rfade = end_lfade;
      adjust_fade_time = true;
    } else if (isFadingOut()) { // fade_total is negative in fadeout
      start_lfade = fade_time / -fade_total;
      start_rfade = start_lfade;
      end_lfade = (fade_time - kame_mix.secs_per_callback) / -fade_total;
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
        fade_time -= kame_mix.secs_per_callback;
        if (fade_time <= 0.0f) {
          state = FinishedState; 
          unsetFade();
        }
      } else {
        fade_time += kame_mix.secs_per_callback;
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

// Get volume modification fades from relative position. Returns no fade 
// (1.0f, 1.0f) if releative position from listener is (0.0f, 0.0f).
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
    switch (KameMix_getFormat()) {
    case KameMix_OutputFloat:
      return copySound(CopyMono<float>(), buf, len, sound, sound_buf);
      break;
    case KameMix_OutputS16:
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
    switch (KameMix_getFormat()) {
    case KameMix_OutputFloat:
      return copyStream(CopyMono<float>(), buf, len, sound, stream_buf);
      break;
    case KameMix_OutputS16:
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

void audioCallback(void *udata, uint8_t *stream, const int len)
{
  if (KameMix_getFormat() == KameMix_OutputS16) {
    memset(kame_mix.audio_mix_buf, 0, 
           kame_mix.audio_mix_buf_len * sizeof(int32_t));
  } else {
    memset(stream, 0, len);
  }

  std::unique_lock<std::mutex> guard(kame_mix.audio_mutex);

  // Sounds can be added between locks, so use indexing and size(), instead 
  // of iterators or range-based for loop.
  for (int i = 0; i < (int)kame_mix.sounds->size(); ++i) {
    // kame_mix.audio_mutex must be locked while using sound
    PlayingSound &sound = (*kame_mix.sounds)[i];
    // not paused or finished
    if (sound.isPlaying() || sound.isPauseChanging()) {
      int total_copied = 0; 

      if (sound.tag == SoundType) {
        total_copied = copySound(sound, sound.sound().buffer, 
                                 kame_mix.audio_tmp_buf, len);
      } else {
        total_copied = copyStream(sound, sound.stream().buffer, 
                                  kame_mix.audio_tmp_buf, len);
      }

      VolumeData vdata = sound.getVolumeData();

      // finished in copy or getVolumeData
      if (sound.isFinished()) {
        freeChannel_locked(i, sound); // free Sound/Stream; set to InvalidType
      }

      // Unlock kame_mix.audio_mutex when done with sound.
      guard.unlock();

      switch (KameMix_getFormat()) {
      case KameMix_OutputFloat: {
        const int num_samples = total_copied / sizeof(float);
        applyVolume((float*)kame_mix.audio_tmp_buf, num_samples, vdata);
        mixStream((float*)stream, (float*)kame_mix.audio_tmp_buf, num_samples);
        break;
      }
      case KameMix_OutputS16: {
        const int num_samples = total_copied / sizeof(int16_t);
        applyVolume((int16_t*)kame_mix.audio_tmp_buf, num_samples, vdata);
        mixStream(kame_mix.audio_mix_buf, (int16_t*)kame_mix.audio_tmp_buf, num_samples);
        break;
      }
      }

      guard.lock();
    }
  }

  switch (KameMix_getFormat()) {
  case KameMix_OutputFloat: 
    clamp((float*)stream, len / sizeof(float));
    break;
  case KameMix_OutputS16: 
    clamp((int16_t*)stream, kame_mix.audio_mix_buf, len / sizeof(int16_t));
    break;
  }
}

} // end anon namespace


