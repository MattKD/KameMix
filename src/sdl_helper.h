#ifndef KAME_MIX_SDL_HELPER_H
#define KAME_MIX_SDL_HELPER_H

#include "audio_system.h"
#include "wav_loader.h"
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
SDL_AudioFormat WAV_formatToSDL(const KameMix_WavFormat format)
{
  switch (format) {
  case KameMix_WAV_U8:
    return AUDIO_U8;
  case KameMix_WAV_S16:
    return AUDIO_S16LSB;
  case KameMix_WAV_Float:
    return AUDIO_F32LSB;
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
