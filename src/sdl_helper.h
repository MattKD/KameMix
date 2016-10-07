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
SDL_AudioFormat WAV_formatToSDL(const WavFormat format)
{
  switch (format) {
  case WAV_FormatU8:
    return AUDIO_U8;
  case WAV_FormatS16:
    return AUDIO_S16LSB;
  case WAV_FormatFloat:
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
