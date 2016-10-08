#ifndef KAME_MIX_WAV_LOADER_H
#define KAME_MIX_WAV_LOADER_H

#ifdef __cplusplus

#include <cstdint>

extern "C" {

#else

#include <stdint.h>

#endif


enum KameMix_WavFormat {
  KameMix_WAV_U8,
  KameMix_WAV_S16, // Little endian
  KameMix_WAV_Float // Little endian
};

enum KameMix_WavResult {
  KameMix_WAV_OK = 0,
  KameMix_WAV_FileOpenError = -1,
  KameMix_WAV_BadHeader = -2,
  KameMix_WAV_UnsupportedFormat = -3
};

inline
const char* KameMix_wavErrStr(KameMix_WavResult result)
{
  switch (result) {
  case KameMix_WAV_FileOpenError:
    return "Couldn't open WAV file";
  case KameMix_WAV_BadHeader:
    return "WAV header was bad";
  case KameMix_WAV_UnsupportedFormat:
    return "WAV file was in an unsupported format";
  case KameMix_WAV_OK:
    break;
  }
  return "";
}

struct KameMix_WavFile {
  void *file;  
  uint32_t stream_start;
  uint32_t stream_size;
  uint32_t stream_pos;
  uint32_t sample_rate;
  KameMix_WavFormat format;
  uint8_t num_channels;
};

KameMix_WavResult KameMix_wavOpen(KameMix_WavFile *wf, const char *file);
void KameMix_wavClose(KameMix_WavFile *wf); 

int64_t KameMix_wavRead(KameMix_WavFile *wf, uint8_t *buf, uint32_t buf_len);
int KameMix_wavBlockSeek(KameMix_WavFile *wf, uint32_t block);

inline
int KameMix_wavTimeSeek(KameMix_WavFile *wf, double sec)
{
  uint32_t block = (uint32_t)(sec * wf->sample_rate);
  return KameMix_wavBlockSeek(wf, block);
}

inline
int KameMix_wavIsEOF(const KameMix_WavFile *wf)
{
  return wf->stream_pos == wf->stream_size;
}

inline
int KameMix_wavBytesPerSample(const KameMix_WavFile *wf)
{
  switch (wf->format) {
  case KameMix_WAV_U8:
    return 1;
  case KameMix_WAV_S16:
    return 2;
  case KameMix_WAV_Float:
    return 4;
  }
  return -1;
}

inline
int KameMix_wavBlockSize(const KameMix_WavFile *wf) 
{ 
  return KameMix_wavBytesPerSample(wf) * wf->num_channels; 
}

inline
uint32_t KameMix_wavBlocksLeft(const KameMix_WavFile *wf)
{
  const uint32_t bytes_left = wf->stream_size - wf->stream_pos;
  return bytes_left / KameMix_wavBlockSize(wf);
}

inline
uint32_t KameMix_wavTotalBlocks(const KameMix_WavFile *wf)
{
  return wf->stream_size / KameMix_wavBlockSize(wf);
}

inline
double KameMix_wavTotalTime(const KameMix_WavFile *wf)
{
  return (double)KameMix_wavTotalBlocks(wf) / wf->sample_rate;
}

#ifdef __cplusplus
} // end extern "C"
#endif

#endif
