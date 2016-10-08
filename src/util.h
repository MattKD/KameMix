#ifndef KAME_MIX_UTIL_H
#define KAME_MIX_UTIL_H

#include "audio_system.h"

/* Get the header size needed to make sure the largest audio format type
   is correctly aligned when placed after a header of type T in a
   buffer from malloc.
*/
#define ALIGNED_HEADER_SIZE(T) \
  sizeof(T) % KameMix::AudioSystem::MaxFormatSize == 0 ? sizeof(T) : \
  (sizeof(T) / KameMix::AudioSystem::MaxFormatSize + 1) * \
    KameMix::AudioSystem::MaxFormatSize

#endif
