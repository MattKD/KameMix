#include "sound_buffer.h"
#include "audio_mem.h"
#include "vorbis_helper.h"
#include "sdl_helper.h"
#include <SDL_audio.h>
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

  AudioSystem::setError("Unsupported file type");
  return false;
}

bool SoundBuffer::loadWAV(const char *filename)
{
  using std::unique_ptr;
  release();
  uint8_t *wav_buf;
  uint32_t wav_buf_len = 0;
  SDL_AudioSpec wav_spec = { 0 };

  if (!SDL_LoadWAV(filename, &wav_spec, &wav_buf, &wav_buf_len)) {
    AudioSystem::setError("SDL_LoadWAV failed\n");
    return false;
  }

  unique_ptr<uint8_t, void(*)(uint8_t*)> wav_buf_cleanup(wav_buf, SDL_FreeWAV);

  const SDL_AudioFormat dst_format = getOutputFormat();
  const int dst_freq = AudioSystem::getFrequency();
  const int channels = wav_spec.channels > 2 ? 2 : wav_spec.channels;
  SDL_AudioCVT cvt;
  if (SDL_BuildAudioCVT(&cvt, wav_spec.format, wav_spec.channels, 
                        wav_spec.freq, dst_format, channels, dst_freq) < 0) {
    AudioSystem::setError("SDL_BuildAudioCVT failed\n");
    return false;
  }

  uint8_t *dst_buf = nullptr;
  unique_ptr<uint8_t*, void (*)(uint8_t**)> 
    dst_buf_cleanup(&dst_buf, [](uint8_t **buf) { km_free(*buf); });

  int audio_buf_len;
  if (!cvt.needed) {
    // max size of buffer is INT_MAX
    if (wav_buf_len > INT_MAX) {
      AudioSystem::setError("Audio file is too large\n");
      return false;
    }

    dst_buf = (uint8_t*)km_malloc_(wav_buf_len + HEADER_SIZE);
    if (!dst_buf) {
      AudioSystem::setError("Out of memory");
      return false;
    }
    audio_buf_len = wav_buf_len;
    memcpy(dst_buf + HEADER_SIZE, wav_buf, wav_buf_len);
  } else {
    if ((int64_t)cvt.len_mult * (int64_t)wav_buf_len > INT_MAX) {
      AudioSystem::setError("Audio file is too large\n");
      return false;
    }
    cvt.len = wav_buf_len;
    dst_buf = (uint8_t*)km_malloc_(cvt.len * cvt.len_mult + HEADER_SIZE);
    if (!dst_buf) {
      AudioSystem::setError("Out of memory");
      return false;
    }
    cvt.buf = dst_buf + HEADER_SIZE;
    memcpy(cvt.buf, wav_buf, cvt.len);

    if (SDL_ConvertAudio(&cvt) != 0) {
      AudioSystem::setError("SDL_ConvertAudio failed\n");
      return false;
    }
    
    const int block_size = channels * AudioSystem::getFormatSize();
    audio_buf_len = (cvt.len_cvt / block_size) * block_size;
    // try to shrink if at least 1KB unused
    if ((cvt.len * cvt.len_mult - audio_buf_len) > 1024) {
      uint8_t *tmp = 
        (uint8_t*)km_realloc_(dst_buf, audio_buf_len + HEADER_SIZE);
      // if fails do nothing, dst_buf is still good
      if (tmp) {
        dst_buf = tmp;
      }
    }
  }

  uint8_t *audio_buf = dst_buf + HEADER_SIZE;
  sdata = new (dst_buf) SharedData(audio_buf, audio_buf_len, channels);
  dst_buf_cleanup.release(); // don't free dst_buf
  return true;
}

bool SoundBuffer::loadOGG(const char *filename)
{
  using std::unique_ptr;
  release();
  OggVorbis_File vf;

  if (ov_fopen(filename, &vf) != 0) {
    AudioSystem::setError("ov_fopen failed\n");
    return false;
  }

  unique_ptr<OggVorbis_File, int (*)(OggVorbis_File*)>  
    vf_cleanup(&vf, ov_clear);

  ov_pcm_seek(&vf, 0);
  const int channels = isMonoOGG(vf) ? 1 : 2;
  int last_src_freq = ov_info(&vf, 0)->rate;
  int audio_buf_len;
  uint8_t *dst_buf;
  // calc size of audio_buf including needed conversion
  {
    int64_t tmp_len = calcBufSizeOGG(vf, channels); 
    if (tmp_len > INT_MAX) {
      AudioSystem::setError("Audio file is too large\n");
      return false;
    }
    if (tmp_len == -1) {
      return false; // error set in caclBufSizeOGG
    }
    audio_buf_len = (int)tmp_len; 
    dst_buf = (uint8_t*)km_malloc_(audio_buf_len + HEADER_SIZE);
    if (!dst_buf) {
      AudioSystem::setError("Out of memory");
      return false;
    }
  }

  unique_ptr<uint8_t*, void(*)(uint8_t**)> 
    dst_buf_cleanup(&dst_buf, [](uint8_t **buf) { km_free(*buf); });
  uint8_t *cvt_buf = dst_buf + HEADER_SIZE;
  float *dst = (float*)cvt_buf;
  // pointer to array of channels, each channel is array of floats
  float **channel_buf; 
  int64_t sample_offset = 0;
  int stream_idx = 0;
  int64_t stream_samples = ov_pcm_total(&vf, 0);
  const int num_streams = ov_streams(&vf);
  
  while (true) {
    int samples_read;
    {
      int tmp_idx;
      samples_read = ov_read_float(&vf, &channel_buf, 0xFFFF, &tmp_idx);
      assert(stream_idx == tmp_idx &&
             "stream idx is detected before ov_read_float");
    }
    sample_offset += samples_read;

    if (samples_read <= 0) {
      assert(samples_read != 0 &&
             "EOF should be detected before calling ov_read_float");
      AudioSystem::setError("ov_read_float error\n");
      return false;
    } else {
      if (channels == 1) { // src has only mono streams, so store as mono
        memcpy(dst, *channel_buf, samples_read * sizeof(float));
        dst += samples_read;
      } else { 
        // more than 1 channel but only care about 2
        if (ov_info(&vf, stream_idx)->channels > 1) {
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

      if (sample_offset == stream_samples) { // end of logical stream
        ++stream_idx;
        if (stream_idx == num_streams) {
          stream_idx = 0;
        }
        int src_freq = ov_info(&vf, stream_idx)->rate;

        // freq is different in this stream than last, or reached end of
        // physical straem so convert all data not converted yet if necessary 
        if (src_freq != last_src_freq || stream_idx == 0) {
          const int dst_freq = AudioSystem::getFrequency();
          const SDL_AudioFormat format = getOutputFormat();
          SDL_AudioCVT cvt;
          if (SDL_BuildAudioCVT(&cvt, format, channels, last_src_freq, 
                                format, channels, dst_freq) < 0) {
            AudioSystem::setError("SDL_BuildAudioCVT failed\n");
            return false;
          }
          cvt.buf = cvt_buf;
          cvt.len = (uint8_t*)dst - cvt.buf; // len of data to be converted
          if (cvt.needed) {
            if (SDL_ConvertAudio(&cvt) < 0) {
              AudioSystem::setError("SDL_ConvertAudio failed\n");
              return false;
            }
            const int block_size = channels * AudioSystem::getFormatSize();
            cvt_buf += (cvt.len_cvt / block_size) * block_size;
            dst = (float*)cvt_buf;
          }

          if (stream_idx == 0) { // reached end of physical stream
            break;
          }
          last_src_freq = src_freq;
        }
        sample_offset = 0;
        stream_samples = ov_pcm_total(&vf, stream_idx);
      }
    }
  }

  uint8_t *audio_buf = dst_buf + HEADER_SIZE;
  // shrink buffer at least 1KB unused memory
  const int audio_buf_used = (uint8_t*)dst - audio_buf;
  if (audio_buf_len - audio_buf_used > 1024) { 
    uint8_t *tmp = 
      (uint8_t*)km_realloc_(dst_buf, audio_buf_used + HEADER_SIZE);
    // if fails do nothing, dst_buf is still good
    if (tmp) {
      dst_buf = tmp;
      audio_buf = dst_buf + HEADER_SIZE;
    }
  }

  sdata = new(dst_buf) SharedData(audio_buf, audio_buf_len, channels);
  dst_buf_cleanup.release(); // don't free dst_buf

  return true;
}

void SoundBuffer::release()
{
  if (sdata != nullptr) {
    if (sdata->refcount.fetch_sub(1, std::memory_order_release) == 1) {
      std::atomic_thread_fence(std::memory_order_acquire);
      sdata->~SharedData();
      km_free(sdata);
      sdata = nullptr;
    }
  }
}


} // end namespace KameMix
