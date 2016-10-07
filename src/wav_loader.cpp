#include "wav_loader.h"
#include "scope_exit.h"
#include <cstring>
#include <fstream>

using namespace KameMix;

namespace {

bool readID(WavFile &wf, const char *id)
{
  char chunk_id[5];
  wf.file.read(chunk_id, 4);
  if (!wf.file.good()) {
    return false;
  }
  chunk_id[4] = '\0';
  if (strcmp(chunk_id, id) != 0) {
    return false;
  }
  return true;
}

template <class T>
bool readNum(WavFile &wf, T &out)
{
  wf.file.read((char*)&out, sizeof(T));
  if (wf.file.gcount() != sizeof(T)) {
    return false;
  }

  return true;
}

WavResult readHeaders(WavFile &wf)
{
  auto scope_exit = makeScopeExit([&wf]() {
    wf.file.close();
  });

  if (!readID(wf, "RIFF")) {
    return WAV_BadHeader;
  }

  uint32_t chunk_size;
  if (!readNum(wf, chunk_size)) {
    return WAV_BadHeader;
  }

  if (!readID(wf, "WAVE")) {
    return WAV_BadHeader;
  }

  // skip all chunks until 'fmt '
  while (!readID(wf, "fmt ")) {
    if (!readNum(wf, chunk_size)) {
      return WAV_BadHeader;
    }
    wf.file.seekg(chunk_size, std::ios_base::cur);
  }

  // after 'fmt ' now
  if (!readNum(wf, chunk_size)) {
    return WAV_BadHeader;
  }

  if (chunk_size != 16 && chunk_size != 18 && chunk_size != 40) {
    return WAV_BadHeader;
  }
  
  uint16_t fmt_code;
  if (!readNum(wf, fmt_code)) {
    return WAV_BadHeader;
  }
  // not PCM or float
  if (fmt_code != 1 && fmt_code != 3) {
    return WAV_UnsupportedFormat;
  }
 
  uint16_t num_channels;
  if (!readNum(wf, num_channels)) {
    return WAV_BadHeader;
  }
  wf.num_channels = (uint8_t)num_channels;

  uint32_t sample_rate;
  if (!readNum(wf, sample_rate)) {
    return WAV_BadHeader;
  }
  wf.rate = sample_rate;

  uint32_t byte_rate;
  if (!readNum(wf, byte_rate)) {
    return WAV_BadHeader;
  }

  uint16_t block_align;
  if (!readNum(wf, block_align)) {
    return WAV_BadHeader;
  }

  uint16_t bits_per_sample;
  if (!readNum(wf, bits_per_sample)) {
    return WAV_BadHeader;
  }
  if (fmt_code == 1) {
    if (bits_per_sample == 8) {
      wf.format = WAV_FormatU8;
    } else if (bits_per_sample == 16) {
      wf.format = WAV_FormatS16;
    } else {
      return WAV_UnsupportedFormat;
    }
  } else {
    if (bits_per_sample == 32) {
      wf.format = WAV_FormatFloat;
    } else {
      return WAV_UnsupportedFormat;
    }
  }

  // if there was extension data in format chunk, just skip it
  if (chunk_size > 16) {
    wf.file.seekg(chunk_size-16, std::ios_base::cur);
  }

  // skip all chunks until 'data'
  while (!readID(wf, "data")) {
    if (!readNum(wf, chunk_size)) {
      return WAV_BadHeader;
    }
    wf.file.seekg(chunk_size, std::ios_base::cur);
  }

  if (!readNum(wf, chunk_size)) {
    return WAV_BadHeader;
  }
  wf.stream_size = chunk_size;
  int64_t stream_start = (int64_t)wf.file.tellg();
  if (stream_start < 0) {
    return WAV_BadHeader;
  }
  wf.stream_start = (uint32_t)stream_start;

  scope_exit.cancel(); // don't close file or set error
  return WAV_OK;
}

} // end anon namespace

namespace KameMix {

WavResult WavFile::open(const char *filename)
{
  file.open(filename, std::ios_base::in | std::ios_base::binary);
  if (!file.is_open()) {
    return WAV_FileOpenError;
  }
  
  return readHeaders(*this);
}

bool WavFile::blockSeek(uint32_t block)
{
  uint32_t byte_offset = blockSize() * block;
  if (byte_offset >= stream_size) {
    byte_offset = 0;
  }

  file.seekg(stream_start + byte_offset, std::ios_base::beg);
  if (!file.good()) {
    file.clear(); // clear error bits
    return false;
  }
  stream_pos = byte_offset;
  return true;
}

int64_t WavFile::read(uint8_t *buf, uint32_t buf_len)
{
  buf_len = buf_len / blockSize() * blockSize();
  file.read((char*)buf, buf_len);
  
  if (!file.good()) {
    if (!file.eof()) {
      file.clear(); // clear error bits
      return -1;
    }
    file.clear(); // clear eof
  }

  uint32_t num_read = (uint32_t)file.gcount();
  stream_pos += num_read;
  return num_read;
}

}


