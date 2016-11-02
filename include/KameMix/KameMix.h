#ifndef KAMEMIX_H
#define KAMEMIX_H

#include "declspec.h"

#ifdef __cplusplus

#include <cstdint>
#include <cassert>
#include <cstddef>

extern "C" {

#else

#include <stdint.h>
#include <assert.h>
#include <stddef.h>

#endif

struct KameMix_Sound;
struct KameMix_Stream;

enum KameMix_OutputFormat {
  KameMix_OutputFloat,
  KameMix_OutputS16
};

/* KameMix_Channel is used to refer to a playing Sound/Stream. Each time 
   played, a new unique id is created to identify the Sound/Stream in a 
   channel. idx and id should not be manually set. When a Sound/Stream 
   finishes playing or is stopped, the channel it was in will get reused by 
   another Sound/Stream. All KameMix functions that take a KameMix_Channel 
   must be given to valid one returned from another
   function, or be unset first with KameMix_unsetChannel. */
struct KameMix_Channel {
  int idx; 
  unsigned int id; 
};

#define KameMix_isChannelSet(channel) (channel).idx >= 0
#define KameMix_unsetChannel(channel) (channel).idx = -1

typedef void* (*KameMix_MallocFunc)(size_t len);
typedef void (*KameMix_FreeFunc)(void *ptr);
typedef void* (*KameMix_ReallocFunc)(void *ptr, size_t len);

/* Must be called before KameMix_init if using custom allocator. 
   Functions must have same behavior as stdlib.h versions: free accepts NULL,
   malloc returns max_aligned address, or NULL on failure, etc. */
KAMEMIX_DECLSPEC
void KameMix_setAlloc(KameMix_MallocFunc malloc_,
                      KameMix_FreeFunc free_,
                      KameMix_ReallocFunc realloc_);

/* Initializes library. Must be called before all other KameMix 
   functions. Returns 0 on error, 1 on success. freq should same as sample
   frequency of loaded Sounds/Streams to prevent bad resampling. 
   sample_buf_size controls number of samples written to audio device at once.
   Smaller values will cause the audio thread to be run more often and result
   in lower latency: 2048 @44100freq = 2048/44100 = 46ms; 
   1024 @44100freq = 23ms. It must be a power of 2; probably should be 
   1024, 2048, or 4096. format must be KameMix_OutputFloat to use float
   samples, or KameMix_OutputS16 for int16_t samples. */
KAMEMIX_DECLSPEC
int KameMix_init(int freq, int sample_buf_size, KameMix_OutputFormat format);

/* Releases all KameMix resources, except KameMix_Sounds and KameMix_Streams,
   which must be released before calling this. */
KAMEMIX_DECLSPEC void KameMix_shutdown();

/* number of sounds playing including paused */
KAMEMIX_DECLSPEC int KameMix_numberPlaying(); 
KAMEMIX_DECLSPEC float KameMix_getMasterVolume();
KAMEMIX_DECLSPEC void KameMix_setMasterVolume(float volume);

/* Returns unique id of new group. This is used with other group functions.
   groups live until KameMix_shutdown is called. */
KAMEMIX_DECLSPEC int KameMix_createGroup();

/* group must be valid id returned from KameMix_createGroup */
KAMEMIX_DECLSPEC void KameMix_setGroupVolume(int group, float volume);

/* group must be valid id returned from KameMix_createGroup */
KAMEMIX_DECLSPEC float KameMix_getGroupVolume(int group);

KAMEMIX_DECLSPEC int KameMix_getFrequency();
KAMEMIX_DECLSPEC int KameMix_getChannels();
KAMEMIX_DECLSPEC KameMix_OutputFormat KameMix_getFormat();

/* Size in bytes of sample output format */
KAMEMIX_DECLSPEC int KameMix_getFormatSize(); 

KAMEMIX_DECLSPEC KameMix_MallocFunc KameMix_getMalloc();
KAMEMIX_DECLSPEC KameMix_FreeFunc KameMix_getFree();
KAMEMIX_DECLSPEC KameMix_ReallocFunc KameMix_getRealloc();

/* Sets listener's 2d position to x and y. */
KAMEMIX_DECLSPEC void KameMix_setListenerPos(float x, float y);

/* Sets x and y to listener's 2d position. x and y must not be NULL. */
KAMEMIX_DECLSPEC void KameMix_getListenerPos(float *x, float *y);

/*
 * Channel functions
*/

/* Stops Sound/Stream without fade. c must be a valid KameMix_Channel returned
   from a KameMix function or unset with KameMix_unsetChannel. */
KAMEMIX_DECLSPEC void KameMix_halt(KameMix_Channel c);

/* Stops Sound/Stream with fast fade. c must be a valid KameMix_Channel returned
   from a KameMix function or unset with KameMix_unsetChannel. */
KAMEMIX_DECLSPEC void KameMix_stop(KameMix_Channel c);

/* Stops Sound/Stream with fade. c must be a valid KameMix_Channel returned
   from a KameMix function or unset with KameMix_unsetChannel. */
KAMEMIX_DECLSPEC void KameMix_fadeout(KameMix_Channel c, float fade_secs);

/* Pauses Sound/Stream. c must be a valid KameMix_Channel returned
   from a KameMix function or unset with KameMix_unsetChannel. */
KAMEMIX_DECLSPEC void KameMix_pause(KameMix_Channel c); 

/* Unpauses Sound/Stream. c must be a valid KameMix_Channel returned
   from a KameMix function or unset with KameMix_unsetChannel. */
KAMEMIX_DECLSPEC void KameMix_unpause(KameMix_Channel c); 

/* Returns 1 if playing; 0 if finished or paused. */
KAMEMIX_DECLSPEC int KameMix_isPlaying(KameMix_Channel c);

/* Returns 1 if paused; 0 if finished or playing. */
KAMEMIX_DECLSPEC int KameMix_isPaused(KameMix_Channel c);

/* Returns 1 if finished; 0 if playing or paused.  */
KAMEMIX_DECLSPEC int KameMix_isFinished(KameMix_Channel c);

/* Sets number of times to loop: 0 to play once, -1 for infinite repeat, n 
   to repeat n times. c must be a valid KameMix_Channel returned from a 
   KameMix function or unset with KameMix_unsetChannel. Returns passed in 
   channel if channel wasn't finished, otherwise returns unset channel. */
KAMEMIX_DECLSPEC 
KameMix_Channel KameMix_setLoopCount(KameMix_Channel c, int loops);

/* Returns loop_count: -1 for infinite repeat, 0 to play once, n to repeat 
   n times. If c is unset or finished return 0. c must be a valid 
   KameMix_Channel returned from a KameMix function or unset with 
   KameMix_unsetChannel. */
KAMEMIX_DECLSPEC 
int KameMix_getLoopCount(KameMix_Channel c);

/* Sets 2d position of Sound/Stream. c must be a valid KameMix_Channel returned 
   from a KameMix function or unset with KameMix_unsetChannel. Returns passed 
   in channel if channel wasn't finished, otherwise returns unset channel. */
KAMEMIX_DECLSPEC 
KameMix_Channel KameMix_setPos(KameMix_Channel c, float x, float y);

/* Sets 2d position of Sound/Stream to passed in x and y, or to (0.0f, 0.0f)
   if finished. x and y must not be NULL. c must be a valid KameMix_Channel 
   returned from a KameMix function or unset with KameMix_unsetChannel */
KAMEMIX_DECLSPEC 
void KameMix_getPos(KameMix_Channel c, float *x, float *y);

/* Sets SoundStream max distance. Use 0.0f to disable 2d position fading.
   c must be a valid KameMix_Channel returned from a KameMix function or unset 
   with KameMix_unsetChannel. Returns passed in channel if channel wasn't 
   finished, otherwise returns unset channel. */
KAMEMIX_DECLSPEC 
KameMix_Channel KameMix_setMaxDistance(KameMix_Channel c, float distance);

/* Returns Sound/Stream max distance. If it was finished then it returns
   0.0f. c must be a valid KameMix_Channel returned from a KameMix function 
   or unset with KameMix_unsetChannel */
KAMEMIX_DECLSPEC 
float KameMix_getMaxDistance(KameMix_Channel c);

/* Sets group of Sound/Stream. group must be valid id returned from
   KameMix_createGroup or -1 to unset group. c must be a valid 
   KameMix_Channel returned from a KameMix function or unset 
   with KameMix_unsetChannel. Returns passed in channel if channel wasn't 
   finished, otherwise returns unset channel. */
KAMEMIX_DECLSPEC 
KameMix_Channel KameMix_setGroup(KameMix_Channel c, int group);

/* Returns group of Sound/Stream, or -1 if not in group or it was finished.
   c must be a valid KameMix_Channel returned from a KameMix function or unset 
   with KameMix_unsetChannel */
KAMEMIX_DECLSPEC 
int KameMix_getGroup(KameMix_Channel c);

/* Sets volume of Sound/Stream. c must be a valid KameMix_Channel returned 
   from a KameMix function or unset with KameMix_unsetChannel. Returns passed 
   in channel if channel wasn't finished, otherwise returns unset channel. */
KAMEMIX_DECLSPEC 
KameMix_Channel KameMix_setVolume(KameMix_Channel c, float volume);

/* Returns volume of Sound/Stream, or 0.0f if it was finished. c must be a 
   valid KameMix_Channel returned from a KameMix function or unset 
   with KameMix_unsetChannel */
KAMEMIX_DECLSPEC 
float KameMix_getVolume(KameMix_Channel c);

/*
 * Sound functions
*/

/* Loads OGG Vorbis and WAV files. Returns NULL on error. */
KAMEMIX_DECLSPEC KameMix_Sound* KameMix_loadSound(const char *file);

/* Decrements refcount, and free when count reaches 0. sound can be NULL */
KAMEMIX_DECLSPEC void KameMix_freeSound(KameMix_Sound *sound);

/* Increment refcount to safely share sound. Uses an atomic incrment for
   thread safety. */
KAMEMIX_DECLSPEC void KameMix_incSoundRef(KameMix_Sound *sound);

/* Play sound with options. sound can be played multiple times. c must be a 
   valid KameMix_Channel returned from a KameMix function or unset with 
   KameMix_unsetChannel. If it's valid then the previous sound is stopped. 
   startpos_sec is the starting position in the sound in seconds. loops is 
   the number of times to repeat: -1 for infinite repeat, 0 to play once. 
   vol is the base volume of sound. fade_secs is the number of seconds to 
   fadein sound. x and y are the 2d position. max_distance is the cutoff 
   distance where volume is 0%; set to 0.0f to disable 2d position fading. 
   group is the group id from KameMix_createGroup or -1 to not
   use a group. paused is 0 to start unpaused, or 1 to pause. */
KAMEMIX_DECLSPEC 
KameMix_Channel 
KameMix_playSound(KameMix_Sound *sound, KameMix_Channel c, 
                  double startpos_sec, int loops, 
                  float vol, float fade_secs, float x, float y, 
                  float max_distance, int group, int paused);

/*
 * Stream functions
*/

/* Loads OGG Vorbis and WAV files. Returns NULL on error. */
KAMEMIX_DECLSPEC KameMix_Stream* KameMix_loadStream(const char *file);

/* Decrements private refcount used by KameMix, and frees when count reaches 0. 
   stream can be NULL */
KAMEMIX_DECLSPEC void KameMix_freeStream(KameMix_Stream *stream);

/* Play stream with options. stream must only be played once at time. c must 
   be a valid KameMix_Channel returned from a previous KameMix_playStream 
   call or unset with KameMix_unsetChannel if playing stream for the first 
   time. The stream is halted if still playing. startpos_sec is the
   starting position in the stream in seconds. loops is the number of times 
   to repeat: -1 for infinite repeat, 0 to play once. vol is the base volume of 
   stream. fade_secs is the number of seconds to fadein stream. x and y are the 
   2d position. max_distance is the cutoff distance where volume is 0%; set to 
   0.0f to disable 2d position fading. group is the group id from 
   KameMix_createGroup or -1 to not use a group. paused is 0 to start unpaused, 
   or 1 to pause. This blocks if a disk read is needed to set stream to 
   startpos_secs. */
KAMEMIX_DECLSPEC 
KameMix_Channel 
KameMix_playStream(KameMix_Stream *stream, KameMix_Channel c, 
                   double startpos_secs, int loops, 
                   float vol, float fade_secs, float x, float y, 
                   float max_distance, int group, int paused);

#ifdef __cplusplus
} // end extern "C"
#endif
#endif
