#ifndef KAME_MIX_UTIL_H
#define KAME_MIX_UTIL_H

#include "audio_system.h"

#define ALIGNED_HEADER_SIZE(T) \
  sizeof(T) % KameMix::AudioSystem::MaxFormatSize == 0 ? sizeof(T) : \
  (sizeof(T) / KameMix::AudioSystem::MaxFormatSize + 1) * \
    KameMix::AudioSystem::MaxFormatSize

#endif
