#include "audio_system.h"
#include "vorbis_helper.h"
#include "sdl_helper.h"
#include <SDL_audio.h>

using KameMix::AudioSystem;

namespace {

inline
int64_t convertedSize(int src_freq, int channels, int64_t blocks)
{
  const int dst_freq = AudioSystem::getFrequency(); 
  const SDL_AudioFormat format = KameMix::getOutputFormat();
  const int format_size = AudioSystem::getFormatSize();
  const int bytes_per_block = channels * format_size;
  SDL_AudioCVT cvt;

  if (SDL_BuildAudioCVT(&cvt, format, channels, src_freq,
                        format, channels, dst_freq) < 0) {
   AudioSystem::setError("SDL_BuildAudioCVT failed\n");
   return -1;
  }

  return blocks * bytes_per_block * cvt.len_mult;
}

} // end anon namespace

namespace KameMix {

void getStreamAndOffset(OggVorbis_File &vf, int &bitstream, int64_t &offset)
{
  const int num_bitstreams = ov_streams(&vf);
  bitstream = 0;
  offset = ov_pcm_tell(&vf); // will be offset in bitstream
  int64_t stream_samples = ov_pcm_total(&vf, bitstream);
  while (bitstream < num_bitstreams - 1) {
    if (offset - stream_samples < 0) {
      break;
    }
    offset -= stream_samples;
    ++bitstream;
    stream_samples = ov_pcm_total(&vf, bitstream);
  }
}

bool isMonoOGG(OggVorbis_File &vf)
{
  for (int i = ov_streams(&vf) - 1; i >= 0; --i) {
    if (ov_info(&vf, i)->channels > 1) {
      return false;
    }
  }
  return true;
}

// return bufsize to fill data from OGG file.
// returns -1 if an error occured
int64_t calcBufSizeOGG(OggVorbis_File &vf, int channels)
{
  int64_t buf_len = 0;
  int last_src_freq = ov_info(&vf, 0)->rate;
  int64_t num_blocks = ov_pcm_total(&vf, 0);
  for (int i = 1, end = ov_streams(&vf); i < end; ++i) {
    int src_freq = ov_info(&vf, i)->rate;
    if (src_freq != last_src_freq) {
      buf_len += convertedSize(last_src_freq, channels, num_blocks);
      num_blocks = 0;
      last_src_freq = src_freq;
    }
    num_blocks += ov_pcm_total(&vf, i);
  }

  buf_len += convertedSize(last_src_freq, channels, num_blocks);
  return buf_len;
}

} // namespace KameMix
