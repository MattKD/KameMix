#ifndef KAMEMIX_SDL_HELPER_H
#define KAMEMIX_SDL_HELPER_H

#include "KameMix.h"
#include "wav_loader.h"
#include <SDL_audio.h>

namespace KameMix {

inline
SDL_AudioFormat outFormatToSDL(KameMix_OutputFormat format)
{
  switch (format) {
  case KameMix_OutputFloat:
    return AUDIO_F32SYS;
  case KameMix_OutputS16:
    return AUDIO_S16SYS;
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
  return outFormatToSDL(KameMix_getFormat());
}

}

#endif
