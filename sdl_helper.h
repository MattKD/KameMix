#ifndef KAME_MIX_SDL_HELPER_H
#define KAME_MIX_SDL_HELPER_H

#include "audio_system.h"
#include <SDL_audio.h>

namespace KameMix {

inline
SDL_AudioFormat formatToSDL(AudioFormat format)
{
  if (format == FloatFormat) {
    return AUDIO_F32SYS;
  }

  assert("Unknown AudioFormat");
  return 0;
}

inline
SDL_AudioFormat getOutputFormat()
{
  return formatToSDL(AudioSystem::getFormat());
}

}

#endif
