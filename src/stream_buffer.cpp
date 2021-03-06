#include "stream_buffer.h"
#include "audio_mem.h"
#include "scope_exit.h"
#include "vorbis_helper.h"
#include "sdl_helper.h"
#include <cassert>
#include <cstring>
#include <cctype>
#include <cstdlib>

namespace {

// 0.5 sec of stereo float samples at 44100 sample rate
const int STREAM_SIZE = 22050 * sizeof(float) * 2;
const int MIN_READ_SAMPLES = 64;

int readMoreOGG(OggVorbis_File &vf, uint8_t *buffer, int buf_len, 
                int &end_pos, int channels, bool stop_at_eof);

int readMoreWAV(KameMix_WavFile &wf, uint8_t *buffer, int buf_len, 
                int &end_pos, int channels, bool stop_at_eof);

} // end anon namespace 

namespace KameMix {

bool StreamBuffer::allocData()
{
  buffer = (uint8_t*)km_malloc_(STREAM_SIZE * 2);
  if (!buffer) {
    return false;
  }

  buffer2 = buffer + STREAM_SIZE;
  return true;
}

void StreamBuffer::release()
{ 
  if (buffer) {
    switch (type) {
    case VorbisType:
      ov_clear(&vf);
      type = InvalidType;
      break;
    case WavType:
      KameMix_wavClose(&wf);
      type = InvalidType;
      break;
    case InvalidType:
      // do nothing
      break;
    }

    uint8_t *buf = buffer < buffer2 ? buffer : buffer2;
    km_free(buf); 
    buffer = nullptr;
    buffer2 = nullptr;
    buffer_size = 0;
    buffer_size2 = 0;
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

  // call release on error
  auto err_cleanup = makeScopeExit([this]() { release(); });

  KameMix_WavResult wav_result = KameMix_wavOpen(&wf, filename);
  if (wav_result != KameMix_WAV_OK) {
    return false;
  }

  type = WavType;
  channels = wf.num_channels >= 2 ? 2 : 1;
  total_time = KameMix_wavTotalTime(&wf);
  const int64_t total_size = 
    KameMix_wavTotalBlocks(&wf) * sampleBlockSize();
  int buf_len = STREAM_SIZE;
  // If the size of decoded file is less than one buffer then 
  // read full file into first buffer without looping to start. 
  if (total_size <= STREAM_SIZE) {
    fully_buffered = true;
    sec = 0.0;
    buf_len = STREAM_SIZE * 2;
  }

  time = sec;
  if (!KameMix_wavTimeSeek(&wf, sec)) {
    return false;
  }

  buffer_size = readMoreWAV(wf, buffer, buf_len, 
    end_pos, channels, fully_buffered);
  if (buffer_size > 0) {
    if (fully_buffered && end_pos == -1) {
      assert("End of stream must be reached when fully buffered");
      end_pos = buffer_size;
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
  // call release on error
  auto err_cleanup = makeScopeExit([this]() { release(); });

  if (ov_fopen(filename, &vf) != 0) {
    return false;
  }

  type = VorbisType;

  if (ov_seekable(&vf) == 0) {
    return false;
  }

  bool is_mono_src = isMonoOGG(vf); // all bitsreams are mono
  channels = is_mono_src ? 1 : 2;
  total_time = ov_time_total(&vf, -1);
  const int freq = KameMix_getFrequency();
  int64_t total_samples = (int64_t)(total_time * freq);
  int64_t total_size = total_samples * sampleBlockSize();

  int buf_len = STREAM_SIZE;
  // If the size of decoded file is less than one buffer then 
  // read full file into first buffer without looping to start. 
  if (total_size <= STREAM_SIZE) {
    fully_buffered = true;
    sec = 0.0;
    buf_len = STREAM_SIZE * 2;
  }

  time = sec;
  if (ov_time_seek(&vf, sec) != 0) {
    return false;
  }
  buffer_size = readMoreOGG(vf, buffer, buf_len, 
    end_pos, channels, fully_buffered);
  if (buffer_size > 0) {
    if (fully_buffered && end_pos == -1) {
      assert("End of stream must be reached when fully buffered");
      end_pos = buffer_size;
    }
    err_cleanup.cancel();
    return true;
  }

  return false;
}

void StreamBuffer::calcTime()
{
  const double freq = KameMix_getFrequency();
  const int block_size = KameMix_getFormatSize() * channels;

  // stream ended in buffer2
  if (end_pos2 != -1) {
    // end of stream reached immediately, so buffer2 is start
    if (end_pos2 == 0) {
      time2 = 0.0;
    // end of stream found, so set time to (total time - secs from end)
    } else {
      int end_samples = end_pos2 / block_size;
      double time_to_end = (double)end_samples/freq;
      time2 = total_time - time_to_end;
    }
  } else {
    // stream ended in main buffer
    if (end_pos != -1) {
      // EOF was at end of buffer, so buffer2 is start of stream
      if (end_pos == buffer_size) {
        time2 = 0.0;
      // start is in buffer, so set time to secs past start
      } else {
        int bytes_past_start = buffer_size - end_pos;
        int samples_past_start = bytes_past_start / block_size;
        time2 = (double)samples_past_start/freq;
      }
    // set time2 to time + buffer duration
    } else {
      int buf_samples = buffer_size / block_size;
      double time_inc = (double)buf_samples/freq;
      time2 = time + time_inc;
    }
  }
}

bool StreamBuffer::readMore()
{
  // Full stream in buffer, so don't need to read. No lock needed.
  if (fully_buffered) {
    return true;
  }

  std::lock_guard<std::mutex> guard(mutex2);
  // buffer2 already has data
  if (buffer_size2 > 0) {
    return true;
  }

  // failed last read, so will fail again
  if (error) {
    return false;
  }

  switch (type) {
  case VorbisType:
    buffer_size2 = readMoreOGG(vf, buffer2, STREAM_SIZE, 
      end_pos2, channels, false);
    break;
  case WavType:
    buffer_size2 = readMoreWAV(wf, buffer2, 
      STREAM_SIZE, end_pos2, channels, false);
    break;
  case InvalidType:
    assert("StreamBuffer tag was invalid");
    break;
  }

  if (buffer_size2 > 0) {
    calcTime();
    return true;
  }

  error = true;
  return false;
}

bool StreamBuffer::setPos(double sec, bool swap_buffers)
{
  // Full stream in buffer, so don't need to read. No lock needed.
  if (fully_buffered) {
    return true;
  }

  std::lock_guard<std::mutex> guard(mutex2);

  // unset previous buffer2 data
  buffer_size2 = 0;
  end_pos2 = -1;
  time2 = 0.0;
  pos_set = false; // may have been set in previous setPos
  error = true;

  if (sec < 0.0 || sec >= total_time) {
    sec = 0.0;
  }

  switch (type) {
  case VorbisType:
    if (ov_time_seek(&vf, sec) != 0) {
      return false;
    }

    buffer_size2 = readMoreOGG(vf, buffer2, 
      STREAM_SIZE, end_pos2, channels, false);
    break;
  case WavType: {
    if (!KameMix_wavTimeSeek(&wf, sec)) {
      return false;
    }

    buffer_size2 = readMoreWAV(wf, buffer2, 
      STREAM_SIZE, end_pos2, channels, false);
    break;
  }
  case InvalidType:
    assert("setPos called with an invalid file type");
    break;
  }

  if (buffer_size2 > 0) {
    time2 = sec;
    pos_set = true;
    if (swap_buffers) {
      swapBuffersImpl();
    }
    error = false;
    return true;
  }

  return false;
}

StreamResult StreamBuffer::advance() 
{
  // Full stream in buffer, so don't need to read. No lock needed.
  if (fully_buffered) {
    return StreamReady;
  }

  std::unique_lock<std::mutex> guard(mutex2, std::try_to_lock_t());
  if (!guard.owns_lock()) {
    return StreamNotReady;
  }

  if (buffer_size2 > 0 && !pos_set) {
    swapBuffersImpl();
    return StreamReady;
  }

  // readMore not called, an error occurred reading, or setPos was called
  if (error) {
    return StreamError;
  } else if (pos_set) {
    return StreamPositionSet;
  } else {
    return StreamNoData;
  }
}

StreamResult StreamBuffer::updatePos() 
{
  // Full stream in buffer, so don't need to read. No lock needed.
  if (fully_buffered) {
    return StreamReady;
  }

  std::unique_lock<std::mutex> guard(mutex2, 
                                     std::try_to_lock_t());
  if (!guard.owns_lock()) {
    return StreamNotReady;
  }

  if (pos_set) { // if pos_set than buffer_size2 > 0
    swapBuffersImpl();
    return StreamReady;
  }

  if (error) {
    return StreamError;
  } else if (!pos_set) {
    return StreamPositionNotSet;
  } else {
    return StreamNoData;
  }
}

StreamResult StreamBuffer::swapBuffers() 
{
  // Full stream in buffer, so don't need to read. No lock needed.
  if (fully_buffered) {
    return StreamReady;
  }

  std::unique_lock<std::mutex> guard(mutex2, std::try_to_lock_t());
  if (!guard.owns_lock()) {
    return StreamNotReady;
  }

  if (buffer_size2 > 0) { // no error if buffer_size > 0
    swapBuffersImpl();
    return StreamReady;
  }

  // readMore/setPos not called, or an error occurred reading
  if (error) {
    return StreamError;
  } else {
    return StreamNoData;
  }
}

void StreamBuffer::swapBuffersImpl() 
{
  std::lock_guard<std::mutex> guard(mutex);
  time = time2;
  time2 = 0.0;
  end_pos = end_pos2;
  end_pos2 = -1;
  uint8_t *tmp = buffer;
  buffer = buffer2;
  buffer2 = tmp;
  buffer_size = buffer_size2;
  buffer_size2 = 0; 
  pos_set = false;
}

} // end namespace KameMix

namespace {

int readMoreOGG(OggVorbis_File &vf, uint8_t *buffer, int buf_len, 
                int &end_pos, int channels, bool stop_at_eof)
{
  using namespace KameMix;
  end_pos = -1;
  const int dst_freq = KameMix_getFrequency();
  // Only float output supported for now
  const SDL_AudioFormat src_format = AUDIO_F32SYS;
  const SDL_AudioFormat dst_format = getOutputFormat();
  const int bytes_per_src_block = sizeof(float) * channels;
  const int bytes_per_dst_block = KameMix_getFormatSize() * channels;

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
    return 0;
  }
  cvt.buf = buffer;
  float *dst = (float*)buffer;
  int buf_samples_left = buf_len / bytes_per_src_block;
  // pointer to array of sample channels, each channel is array of floats
  float **channel_buf; 
  bool done = false;
  
  while (!done) {
    int samples_want = buf_samples_left / cvt.len_mult;
    int samples_read;
    {
      int tmp_idx;
      samples_read = ov_read_float(&vf, &channel_buf, samples_want, 
        &tmp_idx);
      assert(stream_idx == tmp_idx &&
             "stream idx is detected before ov_read_float");
    }

    if (samples_read <= 0) {
      // EOF is checked manually with bitstream/offset
      assert (samples_read < 0);
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
          convert_needed = true;
          if (stop_at_eof) {
            done = true;
          }
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
    // or EOF reached (2 times or with stop_at_eof)
    if (buf_samples_left / cvt.len_mult < MIN_READ_SAMPLES ||
        convert_needed) {
      if (cvt.needed) {
        // len of data to be converted
        cvt.len = (int)((uint8_t*)dst - cvt.buf); 
        if (SDL_ConvertAudio(&cvt) < 0) {
          return 0;
        }
        cvt.needed = 1; // make sure not unset in SDL_ConvertAudio
        cvt.buf += (cvt.len_cvt / bytes_per_dst_block) 
                * bytes_per_dst_block;
        dst = (float*)cvt.buf;
      } else {
        cvt.buf = (uint8_t*)dst;
      }

      if (freq_changed) {
        if (SDL_BuildAudioCVT(&cvt, src_format, channels, last_src_freq, 
                              dst_format, channels, dst_freq) < 0) {
          return 0;
        }
        cvt.buf = (uint8_t*)dst;
      }

      buf_samples_left = (buf_len - (int)(cvt.buf - buffer)) 
        / bytes_per_src_block;
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

int readMoreWAV(KameMix_WavFile &wf, uint8_t *buffer, int buf_len, 
                int &end_pos, int channels, bool stop_at_eof)
{
  using namespace KameMix;
  end_pos = -1;
  const int dst_freq = KameMix_getFrequency();
  const SDL_AudioFormat src_format = WAV_formatToSDL(wf.format);
  const SDL_AudioFormat dst_format = getOutputFormat();
  const int bytes_per_block = KameMix_getFormatSize() * channels;

  SDL_AudioCVT cvt;
  if (SDL_BuildAudioCVT(&cvt, src_format, wf.num_channels, wf.sample_rate, 
                        dst_format, channels, dst_freq) < 0) {
    return 0;
  }

  cvt.buf = buffer;
  uint8_t *dst = buffer;
  int buf_left = buf_len;
  bool done = false;
  const int MIN_READ_BYTES = MIN_READ_SAMPLES * bytes_per_block;
  
  // Is possible to be at eof from last read, so end_pos to start of buffer
  if (KameMix_wavIsEOF(&wf)) {
    KameMix_wavBlockSeek(&wf, 0);
    end_pos = 0;
  }

  while (!done) {
    int bytes_want = buf_left / cvt.len_mult;
    int bytes_read = (int)KameMix_wavRead(&wf, cvt.buf, bytes_want);
    if (bytes_read < 0) {
      return 0;
    } 

    dst += bytes_read;
    if (cvt.needed) {
      // len of data to be converted
      cvt.len = (int)(dst - cvt.buf); 
      if (SDL_ConvertAudio(&cvt) < 0) {
        return 0;
      }
      cvt.buf += (cvt.len_cvt / bytes_per_block) * bytes_per_block;
      dst = cvt.buf;
    } else {
      cvt.buf = dst;
    }

    buf_left = (buf_len - (int)(cvt.buf - buffer));

    // end_pos is at dst, and must be set after possible convert
    if (KameMix_wavIsEOF(&wf)) {
      if (end_pos == -1) {
        KameMix_wavBlockSeek(&wf, 0);
        end_pos = (int)(dst - buffer);
        if (stop_at_eof) {
          done = true;
        }
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
