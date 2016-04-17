#include "sound_buffer.h"
#include <SDL.h>
#include <vorbisfile.h>
#include <cassert>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <memory>

namespace KameMix {

bool SoundBuffer::load(const char *filename)
{
  int dot_idx = -1;
  int size = 0;
  const char *iter = filename;

  while (*iter != '\0') {
    if (*iter == '.') {
      dot_idx = iter - filename;
    }
    ++iter;
    ++size;
  }

  // (size - dot_idx - 1) is num chars past last dot
  if (dot_idx == -1 || (size - dot_idx - 1) != 3 ){
    return false;
  }

  iter = filename + dot_idx + 1; // 1 past last dot
  char prefix[4]; 
  prefix[0] = (char)tolower(iter[0]);
  prefix[1] = (char)tolower(iter[1]);
  prefix[2] = (char)tolower(iter[2]);
  prefix[3] = '\0';

  if (strcmp(prefix, "ogg") == 0) {
    return loadOGG(filename);
  } else if (strcmp(prefix, "wav") == 0) {
    return loadWAV(filename);
  }

  return false;
}


bool SoundBuffer::loadWAV(const char *filename)
{
  using std::unique_ptr;
  MallocFunc km_malloc = AudioSystem::getMalloc();
  FreeFunc km_free = AudioSystem::getFree();
  ReallocFunc km_realloc = AudioSystem::getRealloc();

  Uint8 *wav_buf;
  Uint32 wav_buf_len = 0;
  SDL_AudioSpec wav_spec = { 0 };

  if (!SDL_LoadWAV(filename, &wav_spec, &wav_buf, &wav_buf_len)) {
    AudioSystem::setError("SDL_LoadWAV failed\n");
    return false;
  }

  unique_ptr<Uint8, void(*)(Uint8*)> wav_buf_cleanup(wav_buf, SDL_FreeWAV);

  SDL_AudioCVT cvt;
  const SDL_AudioFormat format = AUDIO_F32SYS;
  int build_cvt_result = 
    SDL_BuildAudioCVT(&cvt, wav_spec.format, wav_spec.channels, wav_spec.freq, 
                      format, wav_spec.channels, 
                      AudioSystem::getFrequency());
  if (build_cvt_result < 0) {
    AudioSystem::setError("SDL_BuildAudioCVT failed\n");
    return false;
  }

  unique_ptr<Uint8, void (*)(void*)> dst_buf(nullptr, km_free);
  int dst_buf_len;

  if (build_cvt_result == 0) {
    Uint8 *tmp = (uint8_t*) km_malloc(wav_buf_len + sizeof(MiscData));
    if (!tmp) {
      AudioSystem::setError("Out of memory\n");
      return false;
    }
    dst_buf.reset(tmp);
    dst_buf_len = wav_buf_len;
    memcpy(dst_buf.get() + sizeof(int), wav_buf, wav_buf_len);
  } else {
    cvt.len = wav_buf_len;
    Uint8 *tmp = (uint8_t*) km_malloc(cvt.len * cvt.len_mult + 
      sizeof(MiscData));
    if (!tmp) {
      AudioSystem::setError("Out of memory\n");
      return false;
    }
    dst_buf.reset(tmp);
    cvt.buf = dst_buf.get() + sizeof(MiscData);
    memcpy(cvt.buf, wav_buf, cvt.len);

    if (SDL_ConvertAudio(&cvt) != 0) {
      AudioSystem::setError("SDL_ConvertAudio failed\n");
      return false;;
    }
    
    dst_buf_len = cvt.len_cvt;
    if (dst_buf_len < cvt.len * cvt.len_mult) {
      Uint8 *tmp = (Uint8*) km_realloc(dst_buf.get(), 
                                       dst_buf_len + sizeof(MiscData));
      if (!tmp) {
        AudioSystem::setError("Out of memory\n");
        return false;
      }
      dst_buf.release(); // ptr was maybe moved or still same as tmp
      dst_buf.reset(tmp);
    }
  }

  release();
  mdata = (MiscData*) dst_buf.get(); 
  mdata->refcount = 1;
  mdata->channels = wav_spec.channels;
  buffer = dst_buf.release() + sizeof(MiscData);
  buffer_size = dst_buf_len;

  return true;
}

// return bufsize to fill data from OGG file.
// returns -1 if an error occured
static int calcBufSizeOGG(OggVorbis_File *vf, int channels, int *num_blocks)
{
  const int dst_freq = AudioSystem::getFrequency(); 
  const int bytes_per_sample = sizeof(float);
  const int bytes_per_block = channels * bytes_per_sample;

  *num_blocks = (int)ov_pcm_total(vf, -1); 
  if (*num_blocks == OV_EINVAL) {
    AudioSystem::setError("File is unseekable\n");
    return -1;
  }

  int max_src_freq = ov_info(vf, 0)->rate;
  for (int i = ov_streams(vf) - 1; i > 0; --i) {
    int src_freq = ov_info(vf, i)->rate;
    if (src_freq > max_src_freq) {
      max_src_freq = src_freq;
    }
  }

  SDL_AudioCVT cvt;
  int build_cvt_result = 
    SDL_BuildAudioCVT(&cvt, AUDIO_F32SYS, channels, max_src_freq,
                      AUDIO_F32SYS, channels, dst_freq); 
  if (build_cvt_result < 0) {
    AudioSystem::setError("SDL_BuildAudioCVT failed\n");
    return -1;
  }
  return *num_blocks * bytes_per_block * cvt.len_mult;
}

static inline
bool isMonoOGG(OggVorbis_File &vf)
{
  for (int i = ov_streams(&vf) - 1; i >= 0; --i) {
    if (ov_info(&vf, i)->channels > 1) {
      return false;
    }
  }
  return true;
}

bool SoundBuffer::loadOGG(const char *filename)
{
  using std::unique_ptr;
  MallocFunc km_malloc = AudioSystem::getMalloc();
  FreeFunc km_free = AudioSystem::getFree();
  ReallocFunc km_realloc = AudioSystem::getRealloc();
  OggVorbis_File vf;

  if (ov_fopen(filename, &vf) != 0) {
    AudioSystem::setError("ov_fopen failed\n");
    return false;
  }

  unique_ptr<OggVorbis_File, int (*)(OggVorbis_File*)>  
    vf_cleanup(&vf, ov_clear);

  ov_time_seek(&vf, 0);
  bool is_mono_src = isMonoOGG(vf); // all bitsreams are mono

  const int channels = is_mono_src ? 1 : 2;
  const int dst_freq = AudioSystem::getFrequency();
  const SDL_AudioFormat format = AUDIO_F32SYS;
  int last_src_freq = ov_info(&vf, 0)->rate;
  SDL_AudioCVT cvt;
  int build_cvt_result = 
    SDL_BuildAudioCVT(&cvt, format, channels, last_src_freq, 
                      format, channels, dst_freq); 
  if (build_cvt_result < 0) {
    AudioSystem::setError("SDL_BuildAudioCVT failed\n");
    return false;
  }

  const int bytes_per_sample = sizeof(float);
  const int bytes_per_block = channels * bytes_per_sample;
  int blocks_left; // filled with number of total number of sample blocks
  int buf_len = calcBufSizeOGG(&vf, channels, &blocks_left); 
  if (buf_len == -1) {
    // error set in caclBufSizeOGG
    return false;
  }
  buf_len += sizeof(MiscData); // calcbufSizeOGG doesn't include MiscData

  unique_ptr<uint8_t, FreeFunc> buf((uint8_t*)km_malloc(buf_len), km_free);
  cvt.len = 0; // don't know len of data to be converted yet
  cvt.buf = buf.get() + sizeof(MiscData); // point past MiscData
  float *dst = (float*)cvt.buf;
  // pointer to array of channels, each channel is array of floats
  float **channel_buf; 
  int section = 0;
  
  while (true) {
    int samples_read = ov_read_float(&vf, &channel_buf, blocks_left, &section);
    blocks_left -= samples_read;

    if (samples_read < 0) {
      AudioSystem::setError("ov_read_float error\n");
      return false;
    } else if (samples_read == 0) {
      break;
    } else {
      int src_freq = ov_info(&vf, section)->rate;

      // freq is different in this bitstream than last, so convert all data
      // read and not converted if necessary 
      if (src_freq != last_src_freq) {
        cvt.len = (uint8_t*)dst - cvt.buf; // len of data to be converted
        if (SDL_ConvertAudio(&cvt) < 0) {
          AudioSystem::setError("SDL_ConvertAudio failed\n");
          return false;
        }
        uint8_t *tmp = cvt.buf + cvt.len_cvt;
        dst = (float*)tmp;
        last_src_freq = src_freq;
        build_cvt_result = 
          SDL_BuildAudioCVT(&cvt, format, channels, src_freq, 
                            format, channels, dst_freq); 
        if (build_cvt_result < 0) {
          AudioSystem::setError("SDL_BuildAudioCVT failed\n");
          return false;
        }
        cvt.buf = tmp; // SDL_BuildAudioCVT sets buf to null
      }

      if (is_mono_src) { // src has only mono streams, so store as mono
        memcpy(dst, *channel_buf, samples_read * sizeof(float));
        dst += samples_read;
      } else { 
        // more than 1 channel but only care about 2
        if (ov_info(&vf, section)->channels > 1) {
          float *chan = *channel_buf;
          float *chan2 = channel_buf[1];
          float *chan_end = chan + samples_read;
          while (chan != chan_end) {
            *dst++ = *chan++;
            *dst++ = *chan2++;
          }
        } else {
          // this stream is mono, but store as stereo
          float *src = *channel_buf;
          float *src_end = src + samples_read;
          while (src != src_end) {
            // copy src twice for left & right channel
            *dst++ = *src;
            *dst++ = *src++;
          }
        }
      }
    }
  }

  // do final conversion if needed
  if (build_cvt_result > 0) {
    cvt.len = (uint8_t*)dst - cvt.buf; // len of data to be converted
    if (SDL_ConvertAudio(&cvt) < 0) {
      AudioSystem::setError("SDL_ConvertAudio failed\n");
      return false;
    }
    cvt.buf += cvt.len_cvt;
    dst = (float*)cvt.buf;
  }
  // shrink buffer if bigger than needed
  int buf_len_used = (uint8_t*)dst - buf.get(); // including MiscData
  if (buf_len_used < buf_len) {
    buf_len = buf_len_used;
    uint8_t *tmp_buf = (uint8_t*)km_realloc(buf.get(), buf_len);
    if (tmp_buf == nullptr) {
      AudioSystem::setError("Out of memory\n");
      return false;
    }
    buf.release();
    buf.reset(tmp_buf);
  }

  release();
  mdata = (MiscData*) buf.get();
  mdata->refcount = 1;
  mdata->channels = channels;
  buffer = buf.release() + sizeof(MiscData);
  buffer_size = buf_len - sizeof(MiscData);

  return true;
}

} // end namespace KameMix
