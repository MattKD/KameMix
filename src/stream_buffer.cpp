#include "stream_buffer.h"
#include "audio_mem.h"
#include "vorbis_helper.h"
#include "wav_loader.h"
#include "scope_exit.h"
#include "sdl_helper.h"
#include <cassert>
#include <cstring>
#include <cctype>
#include <cstdlib>

#include <iostream>

using KameMix::WavFile;

namespace KameMix {

enum StreamType {
  VorbisType,
  WavType,
  InvalidType
};

struct alignas(std::max_align_t) SharedData2 : StreamBuffer::SharedData {
  SharedData2() : type{InvalidType} { }
  ~SharedData2()
  {
    switch (type) {
    case VorbisType:
      ov_clear(&vf);
      type = InvalidType;
      break;
    case WavType:
      wf.~WavFile();
      type = InvalidType;
      break;
    case InvalidType:
      // do nothing
      break;
    }
  }

  StreamType type;
  union {
    OggVorbis_File vf;
    WavFile wf;
  };
};

} // end namespace KameMix

namespace {

// 0.5 sec of stereo float samples at 44100 sample rate
const int STREAM_SIZE = 22050 * sizeof(float) * 2;
const int MIN_READ_SAMPLES = 64;
const size_t HEADER_SIZE = sizeof(KameMix::SharedData2);
int readMoreOGG(OggVorbis_File &vf, uint8_t *buffer, int buf_len, 
                int &end_pos, int channels);

int readMoreWAV(WavFile &wf, uint8_t *buffer, int buf_len, 
                int &end_pos, int channels);

} // end anon namespace 

namespace KameMix {

bool StreamBuffer::allocData()
{
  sdata = (SharedData*)km_malloc_(STREAM_SIZE * 2 + HEADER_SIZE);
  if (!sdata) {
    AudioSystem::setError("Out of memory");
    return false;
  }

  SharedData2 *sdata2 = new (this->sdata) SharedData2(); 
  sdata2->buffer = (uint8_t*)(sdata2 + 1);
  sdata2->buffer2 = sdata2->buffer + STREAM_SIZE;
  return true;
}

void StreamBuffer::release()
{ 
  if (sdata) {
    if (sdata->refcount.fetch_sub(1, std::memory_order_release) == 1) {
      std::atomic_thread_fence(std::memory_order_acquire);
      SharedData2 *sdata2 = static_cast<SharedData2*>(this->sdata);
      sdata2->~SharedData2();
      km_free(sdata); 
    }
    sdata = nullptr;
  }
}

bool StreamBuffer::load(const char *filename, double sec)
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
    return loadOGG(filename, sec);
  } else if (strcmp(prefix, "wav") == 0) {
    return loadWAV(filename);
  }

  return false;
}

bool StreamBuffer::loadWAV(const char *filename, double sec)
{
  // Release and alloc new sdata. This will be sole owner
  // of sdata so no lock is needed.
  release();
  if (!allocData()) { // failed to allocate
    return false;
  }

  SharedData2 *sdata = static_cast<SharedData2*>(this->sdata);
  // call release on error
  auto err_cleanup = makeScopeExit([this]() { release(); });

  WavFile &wf = sdata->wf;
  new(&wf) WavFile;
  WavResult wav_result = wf.open(filename);
  if (wav_result != WAV_OK) {
    AudioSystem::setError(wavErrResultToStr(wav_result));
    return false;
  }

  sdata->type = WavType;
  sdata->channels = wf.num_channels >= 2 ? 2 : 1;
  sdata->total_time = wf.totalTime();
  const int freq = AudioSystem::getFrequency();
  int64_t total_size = wf.totalBlocks() * sampleBlockSize();
  int buf_len = STREAM_SIZE;
  // If the size of decoded file is less than one buffer then 
  // read full file into first buffer without looping to start. 
  if (total_size <= STREAM_SIZE) {
    sdata->fully_buffered = true;
    sec = 0.0;
    buf_len = STREAM_SIZE * 2;
  }

  sdata->time = sec;
  if (!wf.timeSeek(sec)) {
    AudioSystem::setError("wavTimeSeek failed");
    return false;
  }

  sdata->buffer_size = readMoreWAV(wf, sdata->buffer, buf_len, 
    sdata->end_pos, sdata->channels);
  if (sdata->buffer_size > 0) {
    if (sdata->fully_buffered && sdata->end_pos == -1) {
      assert("End of stream must be reached when fully buffered");
      sdata->end_pos = sdata->buffer_size;
    }
    err_cleanup.cancel();
    return true;
  }

  return false;
}


bool StreamBuffer::loadOGG(const char *filename, double sec)
{
  // Release and alloc new sdata. This will be sole owner
  // of sdata so no lock is needed.
  release();
  if (!allocData()) { // failed to allocate
    return false;
  }
  SharedData2 *sdata = static_cast<SharedData2*>(this->sdata);
  // call release on error
  auto err_cleanup = makeScopeExit([this]() { release(); });

  if (ov_fopen(filename, &sdata->vf) != 0) {
    AudioSystem::setError("ov_fopen failed\n");
    return false;
  }

  sdata->type = VorbisType;

  if (ov_seekable(&sdata->vf) == 0) {
    AudioSystem::setError("OGG file is not seekable\n");
    return false;
  }

  bool is_mono_src = isMonoOGG(sdata->vf); // all bitsreams are mono
  sdata->channels = is_mono_src ? 1 : 2;
  sdata->total_time = ov_time_total(&sdata->vf, -1);
  const int freq = AudioSystem::getFrequency();
  int64_t total_samples = (int64_t)(sdata->total_time * freq);
  int64_t total_size = total_samples * sampleBlockSize();
  int buf_len = STREAM_SIZE;
  // If the size of decoded file is less than one buffer then 
  // read full file into first buffer without looping to start. 
  if (total_size <= STREAM_SIZE) {
    sdata->fully_buffered = true;
    sec = 0.0;
    buf_len = STREAM_SIZE * 2;
  }

  sdata->time = sec;
  if (ov_time_seek(&sdata->vf, sec) != 0) {
    AudioSystem::setError("ov_time_seek failed");
    return false;
  }
  sdata->buffer_size = readMoreOGG(sdata->vf, sdata->buffer, buf_len, 
    sdata->end_pos, sdata->channels);
  if (sdata->buffer_size > 0) {
    if (sdata->fully_buffered && sdata->end_pos == -1) {
      assert("End of stream must be reached when fully buffered");
      sdata->end_pos = sdata->buffer_size;
    }
    err_cleanup.cancel();
    return true;
  }

  return false;
}

void StreamBuffer::calcTime()
{
  const double freq = AudioSystem::getFrequency();
  const int block_size = AudioSystem::getFormatSize() * sdata->channels;

  // stream ended in buffer2
  if (sdata->end_pos2 != -1) {
    // end of stream reached immediately, so buffer2 is start
    if (sdata->end_pos2 == 0) {
      sdata->time2 = 0.0;
    // end of stream found, so set time to (total time - secs from end)
    } else {
      int end_samples = sdata->end_pos2 / block_size;
      double time_to_end = (double)end_samples/freq;
      sdata->time2 = sdata->total_time - time_to_end;
    }
  } else {
    // stream ended in main buffer
    if (sdata->end_pos != -1) {
      // EOF was at end of buffer, so buffer2 is start of stream
      if (sdata->end_pos == sdata->buffer_size) {
        sdata->time2 = 0.0;
      // start is in buffer, so set time to secs past start
      } else {
        int bytes_past_start = sdata->buffer_size - sdata->end_pos;
        int samples_past_start = bytes_past_start / block_size;
        sdata->time2 = (double)samples_past_start/freq;
      }
    // set time2 to time + buffer duration
    } else {
      int buf_samples = sdata->buffer_size / block_size;
      double time_inc = (double)buf_samples/freq;
      sdata->time2 = sdata->time + time_inc;
    }
  }
}

bool StreamBuffer::readMore()
{
  // Full stream in buffer, so don't need to read. No lock needed.
  if (sdata->fully_buffered) {
    return true;
  }

  std::lock_guard<std::mutex> guard(sdata->mutex2);
  // buffer2 already has data
  if (sdata->buffer_size2 > 0) {
    return true;
  }

  // failed last read, so will fail again
  if (sdata->error) {
    return false;
  }

  SharedData2 *sdata = static_cast<SharedData2*>(this->sdata);
  switch (sdata->type) {
  case VorbisType:
    sdata->buffer_size2 = readMoreOGG(sdata->vf, sdata->buffer2, STREAM_SIZE, 
      sdata->end_pos2, sdata->channels);
    break;
  case WavType:
    sdata->buffer_size2 = readMoreWAV(sdata->wf, sdata->buffer2, 
      STREAM_SIZE, sdata->end_pos2, sdata->channels);
    break;
  case InvalidType:
    assert("StreamBuffer tag was invalid");
    break;
  }

  if (sdata->buffer_size2 > 0) {
    calcTime();
    return true;
  }

  sdata->error = true;
  return false;
}

bool StreamBuffer::setPos(double sec, bool swap_buffers)
{
  // Full stream in buffer, so don't need to read. No lock needed.
  if (sdata->fully_buffered) {
    return true;
  }

  std::lock_guard<std::mutex> guard(sdata->mutex2);

  // unset previous buffer2 data
  sdata->buffer_size2 = 0;
  sdata->end_pos2 = -1;
  sdata->time2 = 0.0;
  sdata->pos_set = false; // may have been set in previous setPos
  sdata->error = true;

  if (sec < 0.0 || sec >= sdata->total_time) {
    sec = 0.0;
  }

  SharedData2 *sdata2 = static_cast<SharedData2*>(this->sdata);
  switch (sdata2->type) {
  case VorbisType:
    if (ov_time_seek(&sdata2->vf, sec) != 0) {
      AudioSystem::setError("ov_time_seek failed");
      return false;
    }

    sdata->buffer_size2 = readMoreOGG(sdata2->vf, sdata->buffer2, 
      STREAM_SIZE, sdata->end_pos2, sdata->channels);
    break;
  case WavType:
    if (!sdata2->wf.timeSeek(sec)) {
      AudioSystem::setError("wavTimeSeek failed");
      return false;
    }

    sdata->buffer_size2 = readMoreWAV(sdata2->wf, sdata->buffer2, 
      STREAM_SIZE, sdata->end_pos2, sdata->channels);
    break;
  case InvalidType:
    assert("setPos called with an invalid file type");
    break;
  }

  if (sdata->buffer_size2 > 0) {
    sdata->time2 = sec;
    sdata->pos_set = true;
    if (swap_buffers) {
      swapBuffersImpl();
    }
    sdata->error = false;
    return true;
  }

  return false;
}

StreamResult StreamBuffer::advance() 
{
  // Full stream in buffer, so don't need to read. No lock needed.
  if (sdata->fully_buffered) {
    return StreamReady;
  }

  std::unique_lock<std::mutex> guard(sdata->mutex2, std::try_to_lock_t());
  if (!guard.owns_lock()) {
    return StreamNotReady;
  }

  if (sdata->buffer_size2 > 0 && !sdata->pos_set) {
    swapBuffersImpl();
    return StreamReady;
  }

  // readMore not called, an error occurred reading, or setPos was called
  if (sdata->error) {
    return StreamError;
  } else if (sdata->pos_set) {
    return StreamPositionSet;
  } else {
    return StreamNoData;
  }
}

StreamResult StreamBuffer::updatePos() 
{
  // Full stream in buffer, so don't need to read. No lock needed.
  if (sdata->fully_buffered) {
    return StreamReady;
  }

  std::unique_lock<std::mutex> guard(sdata->mutex2, 
                                     std::try_to_lock_t());
  if (!guard.owns_lock()) {
    return StreamNotReady;
  }

  if (sdata->pos_set) { // if pos_set than buffer_size2 > 0
    swapBuffersImpl();
    return StreamReady;
  }

  if (sdata->error) {
    return StreamError;
  } else if (!sdata->pos_set) {
    return StreamPositionNotSet;
  } else {
    return StreamNoData;
  }
}

StreamResult StreamBuffer::swapBuffers() 
{
  // Full stream in buffer, so don't need to read. No lock needed.
  if (sdata->fully_buffered) {
    return StreamReady;
  }

  std::unique_lock<std::mutex> guard(sdata->mutex2, std::try_to_lock_t());
  if (!guard.owns_lock()) {
    return StreamNotReady;
  }

  if (sdata->buffer_size2 > 0) { // no error if buffer_size > 0
    swapBuffersImpl();
    return StreamReady;
  }

  // readMore/setPos not called, or an error occurred reading
  if (sdata->error) {
    return StreamError;
  } else {
    return StreamNoData;
  }
}

void StreamBuffer::swapBuffersImpl() 
{
  std::lock_guard<std::mutex> guard(sdata->mutex);
  sdata->time = sdata->time2;
  sdata->time2 = 0.0;
  sdata->end_pos = sdata->end_pos2;
  sdata->end_pos2 = -1;
  uint8_t *tmp = sdata->buffer;
  sdata->buffer = sdata->buffer2;
  sdata->buffer2 = tmp;
  sdata->buffer_size = sdata->buffer_size2;
  sdata->buffer_size2 = 0; 
  sdata->pos_set = false;
}

} // end namespace KameMix

namespace {

int readMoreOGG(OggVorbis_File &vf, uint8_t *buffer, int buf_len, 
                int &end_pos, int channels)
{
  using namespace KameMix;
  end_pos = -1;
  const int dst_freq = AudioSystem::getFrequency();
  // Only float output supported for now
  const SDL_AudioFormat src_format = AUDIO_F32SYS;
  const SDL_AudioFormat dst_format = AUDIO_F32SYS;
  const int bytes_per_block = sizeof(float) * channels;

  int stream_idx; 
  int64_t offset;
  getStreamAndOffset(vf, stream_idx, offset);
  const int num_streams = ov_streams(&vf);
  int64_t stream_samples = ov_pcm_total(&vf, stream_idx);
  // check if at end of stream
  if (offset == stream_samples) { 
    offset = 0;
    stream_idx = 0;
    end_pos = 0;
    ov_pcm_seek(&vf, 0);
  }

  int last_src_freq = ov_info(&vf, stream_idx)->rate;
  SDL_AudioCVT cvt;
  if (SDL_BuildAudioCVT(&cvt, src_format, channels, last_src_freq, 
                        dst_format, channels, dst_freq) < 0) {
    AudioSystem::setError("SDL_BuildAudioCVT failed\n");
    return 0;
  }
  cvt.buf = buffer;
  float *dst = (float*)buffer;
  int buf_samples_left = buf_len / bytes_per_block;
  // pointer to array of sample channels, each channel is array of floats
  float **channel_buf; 
  bool done = false;
  
  while (!done) {
    int samples_want = buf_samples_left / cvt.len_mult;
    int samples_read;
    {
      int tmp_idx;
      samples_read = ov_read_float(&vf, &channel_buf, samples_want, &tmp_idx);
      assert (stream_idx == tmp_idx &&
              "stream idx is detected before ov_read_float");
    }

    if (samples_read <= 0) {
      // EOF is checked manually with bitstream/offset
      assert (samples_read < 0);
      AudioSystem::setError("ov_read_float error\n");
      return 0;
    } 

    if (channels == 1) { // src has only mono streams, so store as mono
      memcpy(dst, *channel_buf, samples_read * sizeof(float));
      dst += samples_read;
    } else { 
      // more than 1 channel but only care about 2
      if (ov_info(&vf, -1)->channels > 1) {
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

    bool end_pos_found = false;
    bool convert_needed = false;
    bool freq_changed = false;
    buf_samples_left -= samples_read * cvt.len_mult;
    offset += samples_read;
    
    if (offset == stream_samples) { // reached end of logical stream
      offset = 0;
      ++stream_idx;
      if (stream_idx == num_streams) { // reached end of physical stream
        stream_idx = 0;
        if (end_pos == -1) {
          ov_pcm_seek(&vf, 0);
          end_pos_found = true; // set end_pos after possible convert
        } else {
          // can only have 1 end_pos, so leave EOF for next read
          done = true;
          convert_needed = true;
        }
      }
      stream_samples = ov_pcm_total(&vf, stream_idx);
      int src_freq = ov_info(&vf, stream_idx)->rate;
      if (last_src_freq != src_freq) {
        last_src_freq = src_freq;
        convert_needed = true;
        freq_changed = true;
      }         
    }

    // try to convert to save space, if stream frequency changed, 
    // or EOF reached 2 times
    if (buf_samples_left / cvt.len_mult < MIN_READ_SAMPLES ||
        convert_needed) {
      if (cvt.needed) {
        // len of data to be converted
        cvt.len = (int)((uint8_t*)dst - cvt.buf); 
        if (SDL_ConvertAudio(&cvt) < 0) {
          AudioSystem::setError("SDL_ConvertAudio failed\n");
          return 0;
        }
        cvt.buf += (cvt.len_cvt / sizeof(float)) * sizeof(float);
        dst = (float*)cvt.buf;
      } else {
        cvt.buf = (uint8_t*)dst;
      }

      if (freq_changed) {
        if (SDL_BuildAudioCVT(&cvt, src_format, channels, last_src_freq, 
                              dst_format, channels, dst_freq) < 0) {
          AudioSystem::setError("SDL_BuildAudioCVT failed\n");
          return 0;
        }
        cvt.buf = (uint8_t*)dst;
      }

      buf_samples_left = (buf_len - (int)(cvt.buf - buffer)) 
        / bytes_per_block;
    }

    // end_pos is at dst, and must be set after possible convert
    if (end_pos_found) {
      end_pos = (int)((uint8_t*)dst - buffer);
    }
    // stop when buffer almost filled after trying to convert
    if (buf_samples_left / cvt.len_mult < MIN_READ_SAMPLES) {
      done = true;
    }
  }

  // size of decoded data may be smaller than buf_len
  return (int)((uint8_t*)dst - buffer);
}

int readMoreWAV(WavFile &wf, uint8_t *buffer, int buf_len, 
                int &end_pos, int channels)
{
  using namespace KameMix;
  end_pos = -1;
  const int dst_freq = AudioSystem::getFrequency();
  const SDL_AudioFormat src_format = WAV_formatToSDL(wf.format);
  const SDL_AudioFormat dst_format = getOutputFormat();
  const int bytes_per_block = AudioSystem::getFormatSize() * channels;

  int src_freq = wf.rate;
  SDL_AudioCVT cvt;
  if (SDL_BuildAudioCVT(&cvt, src_format, wf.num_channels, src_freq, 
                        dst_format, channels, dst_freq) < 0) {
    AudioSystem::setError("SDL_BuildAudioCVT failed\n");
    return 0;
  }

  cvt.buf = buffer;
  uint8_t *dst = buffer;
  int buf_left = buf_len;
  bool done = false;
  const int MIN_READ_BYTES = MIN_READ_SAMPLES * bytes_per_block;
  
  // Is possible to be at eof from last read, so end_pos to start of buffer
  if (wf.isEOF()) {
    wf.blockSeek(0);
    end_pos = 0;
  }

  while (!done) {
    int bytes_want = buf_left / cvt.len_mult;
    int bytes_read = (int)wf.read(cvt.buf, bytes_want);
    if (bytes_read < 0) {
      AudioSystem::setError("wavRead error\n");
      return 0;
    } 

    dst += bytes_read;
    if (cvt.needed) {
      // len of data to be converted
      cvt.len = (int)(dst - cvt.buf); 
      if (SDL_ConvertAudio(&cvt) < 0) {
        AudioSystem::setError("SDL_ConvertAudio failed\n");
        return 0;
      }
      cvt.buf += (cvt.len_cvt / bytes_per_block) * bytes_per_block;
      dst = cvt.buf;
    } else {
      cvt.buf = dst;
    }

    buf_left = (buf_len - (int)(cvt.buf - buffer));

    // end_pos is at dst, and must be set after possible convert
    if (wf.isEOF()) {
      if (end_pos == -1) {
        wf.blockSeek(0);
        end_pos = (int)(dst - buffer);
      } else {
        // leave at eof for next read
        done = true; 
      }
    }

    // stop when buffer almost filled after trying to convert
    if (buf_left / cvt.len_mult < MIN_READ_BYTES) {
      done = true;
    }
  }

  // size of decoded data may be smaller than buf_len
  return (int)(dst - buffer);
}

} // end anon namespace
