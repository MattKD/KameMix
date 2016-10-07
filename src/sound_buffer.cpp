#include "sound_buffer.h"
#include "audio_mem.h"
#include "vorbis_helper.h"
#include "sdl_helper.h"
#include "wav_loader.h"
#include "scope_exit.h"
#include <cassert>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <limits>

static const int MAX_BUFF_SIZE = std::numeric_limits<int>::max();

namespace KameMix {

bool SoundBuffer::load(const char *filename)
{
  int dot_idx = -1;
  int size = 0;
  const char *iter = filename;

  while (*iter != '\0') {
    if (*iter == '.') {
      dot_idx = (int)(iter - filename);
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
  release();

  WavFile wf;
  WavResult wav_result = wf.open(filename);
  if (wav_result != WAV_OK) {
    AudioSystem::setError(wavErrResultToStr(wav_result));
    return false;
  }

  const SDL_AudioFormat src_format = WAV_formatToSDL(wf.format);
  const SDL_AudioFormat dst_format = getOutputFormat();
  const int dst_freq = AudioSystem::getFrequency();
  const int channels = wf.num_channels >= 2 ? 2 : 1;
  SDL_AudioCVT cvt;
  if (SDL_BuildAudioCVT(&cvt, src_format, wf.num_channels, 
                        wf.rate, dst_format, channels, dst_freq) < 0) {
    AudioSystem::setError("SDL_BuildAudioCVT failed\n");
    return false;
  }

  uint8_t *dst_buf = nullptr;
  auto dst_buf_cleanup = makeScopeExit([&dst_buf]() { km_free(dst_buf); });

  int audio_buf_len = wf.stream_size;
  if (!cvt.needed) {
    if (audio_buf_len > MAX_BUFF_SIZE) {
      AudioSystem::setError("Audio file is too large\n");
      return false;
    }

    dst_buf = (uint8_t*)km_malloc_(audio_buf_len + HEADER_SIZE);
    if (!dst_buf) {
      AudioSystem::setError("Out of memory");
      return false;
    }
    audio_buf_len = (int)wf.read(dst_buf + HEADER_SIZE, audio_buf_len);
    if (audio_buf_len < 0) {
      AudioSystem::setError("WavFile::read error");
      return false;
    }
  } else {
    if ((int64_t)cvt.len_mult * (int64_t)audio_buf_len > MAX_BUFF_SIZE) {
      AudioSystem::setError("Audio file is too large\n");
      return false;
    }
    cvt.len = audio_buf_len;
    dst_buf = (uint8_t*)km_malloc_(cvt.len * cvt.len_mult + HEADER_SIZE);
    if (!dst_buf) {
      AudioSystem::setError("Out of memory");
      return false;
    }
    cvt.buf = dst_buf + HEADER_SIZE;
    audio_buf_len = (int)wf.read(dst_buf + HEADER_SIZE, audio_buf_len);
    if (audio_buf_len < 0) {
      AudioSystem::setError("WavFile::read error");
      return false;
    }

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
  dst_buf_cleanup.cancel(); // don't free dst_buf
  return true;
}

bool SoundBuffer::loadOGG(const char *filename)
{
  release();
  OggVorbis_File vf;

  if (ov_fopen(filename, &vf) != 0) {
    AudioSystem::setError("ov_fopen failed\n");
    return false;
  }

  auto vf_cleanup = makeScopeExit([&vf]() { ov_clear(&vf); });

  ov_pcm_seek(&vf, 0);
  const int channels = isMonoOGG(vf) ? 1 : 2;
  int last_src_freq = ov_info(&vf, 0)->rate;
  int audio_buf_len;
  uint8_t *dst_buf;
  // calc size of audio_buf including needed conversion
  {
    int64_t tmp_len = calcBufSizeOGG(vf, channels); 
    if (tmp_len > MAX_BUFF_SIZE) {
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

  auto dst_buf_cleanup = makeScopeExit([&dst_buf]() { km_free(dst_buf); });

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
          const SDL_AudioFormat dst_format = getOutputFormat();
          SDL_AudioCVT cvt;
          if (SDL_BuildAudioCVT(&cvt, AUDIO_F32SYS, channels, last_src_freq, 
                                dst_format, channels, dst_freq) < 0) {
            AudioSystem::setError("SDL_BuildAudioCVT failed\n");
            return false;
          }
          cvt.buf = cvt_buf;
          // len of data to be converted
          cvt.len = (int)((uint8_t*)dst - cvt.buf); 
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
  const int audio_buf_used = (int)((uint8_t*)dst - audio_buf);
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
  dst_buf_cleanup.cancel(); // don't free dst_buf

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
