#include "sound_buffer.h"
#include "audio_system.h"
#include <SDL.h>
#include <memory>
#include <cassert>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <vorbisfile.h>

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
  char prefix[4] = {tolower(iter[0]), tolower(iter[1]), tolower(iter[2]), '\0'};

  if (strcmp(prefix, "ogg") == 0) {
    return loadOGG(filename);
  } else if (strcmp(prefix, "wav") == 0) {
    return loadWAV(filename);
  }

  return true;
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
                      format, AudioSystem::getChannels(), 
                      AudioSystem::getFrequency());
  if (build_cvt_result < 0) {
    AudioSystem::setError("SDL_BuildAudioCVT failed\n");
    return false;
  }

  unique_ptr<Uint8, void (*)(void*)> dst_buf(nullptr, km_free);
  int dst_buf_len;

  if (build_cvt_result == 0) {
    Uint8 *tmp = (uint8_t*) km_malloc(wav_buf_len + sizeof(int));
    if (!tmp) {
      return false;
    }
    dst_buf.reset(tmp);
    dst_buf_len = wav_buf_len;
    memcpy(dst_buf.get() + sizeof(int), wav_buf, wav_buf_len);
  } else {
    cvt.len = wav_buf_len;
    Uint8 *tmp = (uint8_t*) km_malloc(cvt.len * cvt.len_mult + sizeof(int));
    if (!tmp) {
      return false;
    }
    dst_buf.reset(tmp);
    cvt.buf = dst_buf.get() + sizeof(int);
    memcpy(cvt.buf, wav_buf, cvt.len);

    if (SDL_ConvertAudio(&cvt) != 0) {
      return false;;
    }
    
    dst_buf_len = cvt.len_cvt;
    if (dst_buf_len < cvt.len * cvt.len_mult) {
      Uint8 *tmp = (Uint8*) km_realloc(dst_buf.get(), 
                                       dst_buf_len + sizeof(int));
      if (!tmp) {
        return false;
      }
      dst_buf.release(); // ptr was maybe moved or still same as tmp
      dst_buf.reset(tmp);
    }
  }

  release();
  refcount = (int*) dst_buf.get(); 
  *refcount = 1;
  buffer = dst_buf.release() + sizeof(int);
  buffer_size = dst_buf_len;

  return true;
}

// return bufsize to fill data from OGG file.
// returns -1 if an error occured
static int calcBufSizeOGG(OggVorbis_File *vf)
{
  const int dst_freq = AudioSystem::getFrequency(); 
  const int channels = AudioSystem::getChannels();
  const int bytes_per_sample = sizeof(float);
  const int bytes_per_block = channels * bytes_per_sample;

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

  int num_blocks = (int)ov_pcm_total(vf, -1); 
  if (num_blocks == OV_EINVAL) {
    num_blocks = 22050; // 0.5sec
  }

  return num_blocks * bytes_per_block * cvt.len_mult + sizeof(int);
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

  const int channels = AudioSystem::getChannels();
  assert(channels == 2 && "Only stereo output is implemented");
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
  int buf_len = calcBufSizeOGG(&vf);
  if (buf_len == -1) {
    return false;
  }
  unique_ptr<uint8_t, FreeFunc> buf((uint8_t*)km_malloc(buf_len), km_free);
  cvt.len = 0; // don't know len of data to be converted yet
  cvt.buf = buf.get() + sizeof(int); // point past refcount
  float *dst = (float*)cvt.buf;
  // pointer to array of channels, each channel is array of floats
  float **channel_buf; 
  int section = 0;
  
  while (true) {
    const int samples_want = 4096;
    int samples_read = ov_read_float(&vf, &channel_buf, samples_want, &section);

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
        if (build_cvt_result > 0) {
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
      }

      // check if buf is big enough for next samples read
      const int buf_left = buf_len - ((uint8_t*)dst - buf.get());
      const int more_needed = samples_read * bytes_per_block * cvt.len_mult;
      if (buf_left < more_needed) {
        int dst_idx = (uint8_t*)dst - buf.get();
        int cvt_buf_idx = cvt.buf - buf.get(); 
        // *=2 is big enough, since min buf_left can hold 22050 samples * 
        // max cvt.len_mult and only read max 4096 samples
        buf_len *= 2; 
        uint8_t *tmp = (uint8_t*)km_realloc(buf.get(), buf_len * 2);
        if (tmp == nullptr) {
          AudioSystem::setError("Out of memory\n");
          return false;
        }
        buf.release(); // release in case realloc moved memory
        buf.reset(tmp);
        dst = (float*)(buf.get() + dst_idx);
        cvt.buf = buf.get() + cvt_buf_idx;
      }

      // maybe keep as mono and convert to stereo as needed to save memory?
      if (ov_info(&vf, section)->channels == 1) {
        float *chan = *channel_buf;
        float *chan_end = (*channel_buf) + samples_read;
        while (chan != chan_end) {
          *dst++ = *chan;
          *dst++ = *chan++; // copy mono channel twice to stereo
        }
      } else { // more than 1 channel but only care about 2
        float *chan = *channel_buf;
        float *chan2 = channel_buf[1];
        float *chan_end = (*channel_buf) + samples_read;
        while (chan != chan_end) {
          *dst++ = *chan++;
          *dst++ = *chan2++;
        }
      }
    }
  }

  // do final conversion if needed, and shrink buffer if bigger than needed
  if (build_cvt_result > 0) {
    cvt.len = (uint8_t*)dst - cvt.buf; // len of data to be converted
    if (SDL_ConvertAudio(&cvt) < 0) {
      AudioSystem::setError("SDL_ConvertAudio failed\n");
      return false;
    }
    cvt.buf += cvt.len_cvt;
    int buf_len_used = cvt.buf - buf.get(); // including refcount
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
  }

  release();
  refcount = (int*)buf.get(); 
  *refcount = 1;
  buffer = buf.release() + sizeof(int);
  buffer_size = buf_len - sizeof(int);


  return true;
}

void SoundBuffer::release()
{
  if (refcount == nullptr) {
    return;
  }

  if (*refcount == 1) {
    AudioSystem::getFree()(refcount);
    refcount = nullptr;
    buffer = nullptr;
    buffer_size = 0;
  } else {
    *refcount -= 1;
  }
}

} // end namespace KameMix
