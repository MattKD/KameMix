#define _FILE_OFFSET_BITS 64 // for fseeko

#include "wav_loader.h"
#include "scope_exit.h"
#include <cstring>
#include <cstdio>

using namespace KameMix;

namespace {

inline
int64_t fseekWrapper(FILE *file, int64_t offset, int origin)
{
#ifdef _WIN32
  return _fseeki64(file, offset, origin);
#else
  return fseeko(file, offset, origin); 
#endif
}

inline
int64_t ftellWrapper(FILE *file)
{
#ifdef _WIN32
  return _ftelli64(file);
#else
  return ftello(file); 
#endif
}

bool readID(KameMix_WavFile &wf, const char *id)
{
  char chunk_id[5];
  FILE *file = (FILE*)wf.file;
  size_t num_read = fread(chunk_id, 1, 4, file);
  if (num_read != 4) {
    return false;
  }
  chunk_id[4] = '\0';
  if (strcmp(chunk_id, id) != 0) {
    return false;
  }
  return true;
}

template <class T>
bool readNum(KameMix_WavFile &wf, T &out)
{
  FILE *file = (FILE*)wf.file;
  size_t num_read = fread(&out, 1, sizeof(T), file);
  if (num_read != sizeof(T)) {
    return false;
  }

  return true;
}

KameMix_WavResult readHeaders(KameMix_WavFile &wf)
{
  FILE *file = (FILE*)wf.file;
  auto file_cleanup = makeScopeExit([file]() {
    fclose(file);
  });

  if (!readID(wf, "RIFF")) {
    return KameMix_WAV_BadHeader;
  }

  uint32_t chunk_size;
  if (!readNum(wf, chunk_size)) {
    return KameMix_WAV_BadHeader;
  }

  if (!readID(wf, "WAVE")) {
    return KameMix_WAV_BadHeader;
  }

  // skip all chunks until 'fmt '
  while (!readID(wf, "fmt ")) {
    if (!readNum(wf, chunk_size)) {
      return KameMix_WAV_BadHeader;
    }
    fseekWrapper(file, chunk_size, SEEK_CUR);
  }

  // after 'fmt ' now
  if (!readNum(wf, chunk_size)) {
    return KameMix_WAV_BadHeader;
  }

  if (chunk_size != 16 && chunk_size != 18 && chunk_size != 40) {
    return KameMix_WAV_BadHeader;
  }
  
  uint16_t fmt_code;
  if (!readNum(wf, fmt_code)) {
    return KameMix_WAV_BadHeader;
  }
  // not PCM or float
  if (fmt_code != 1 && fmt_code != 3) {
    return KameMix_WAV_UnsupportedFormat;
  }
 
  uint16_t num_channels;
  if (!readNum(wf, num_channels)) {
    return KameMix_WAV_BadHeader;
  }
  wf.num_channels = (uint8_t)num_channels;

  uint32_t sample_rate;
  if (!readNum(wf, sample_rate)) {
    return KameMix_WAV_BadHeader;
  }
  wf.sample_rate = sample_rate;

  uint32_t byte_rate;
  if (!readNum(wf, byte_rate)) {
    return KameMix_WAV_BadHeader;
  }

  uint16_t block_align;
  if (!readNum(wf, block_align)) {
    return KameMix_WAV_BadHeader;
  }

  uint16_t bits_per_sample;
  if (!readNum(wf, bits_per_sample)) {
    return KameMix_WAV_BadHeader;
  }
  if (fmt_code == 1) {
    if (bits_per_sample == 8) {
      wf.format = KameMix_WAV_U8;
    } else if (bits_per_sample == 16) {
      wf.format = KameMix_WAV_S16;
    } else {
      return KameMix_WAV_UnsupportedFormat;
    }
  } else {
    if (bits_per_sample == 32) {
      wf.format = KameMix_WAV_Float;
    } else {
      return KameMix_WAV_UnsupportedFormat;
    }
  }

  // if there was extension data in format chunk, just skip it
  if (chunk_size > 16) {
    fseekWrapper(file, chunk_size-16, SEEK_CUR);
  }

  // skip all chunks until 'data'
  while (!readID(wf, "data")) {
    if (!readNum(wf, chunk_size)) {
      return KameMix_WAV_BadHeader;
    }
    fseekWrapper(file, chunk_size, SEEK_CUR);
  }

  if (!readNum(wf, chunk_size)) {
    return KameMix_WAV_BadHeader;
  }
  wf.stream_size = chunk_size;
  int64_t stream_start = ftellWrapper(file);
  if (stream_start < 0) {
    return KameMix_WAV_BadHeader;
  }
  wf.stream_start = (uint32_t)stream_start;

  file_cleanup.cancel(); // don't close file
  return KameMix_WAV_OK;
}

} // end anon namespace

extern "C" {

KameMix_WavResult KameMix_wavOpen(KameMix_WavFile *wf, const char *filename)
{
  FILE *file = fopen(filename, "rb");
  if (!file) {
    return KameMix_WAV_FileOpenError;
  }
  
  wf->file = file;
  return readHeaders(*wf); // calls flose on erro
}

void KameMix_wavClose(KameMix_WavFile *wf)
{
  fclose((FILE*)wf->file);
}

int KameMix_wavBlockSeek(KameMix_WavFile *wf, uint32_t block)
{
  uint32_t byte_offset = KameMix_wavBlockSize(wf) * block;
  if (byte_offset >= wf->stream_size) {
    byte_offset = 0;
  }

  FILE *file = (FILE*)wf->file;
  if (fseekWrapper(file, wf->stream_start + byte_offset, SEEK_SET) != 0) {
    clearerr(file);
    return 0;
  }

  wf->stream_pos = byte_offset;
  return 1;
}

int64_t KameMix_wavRead(KameMix_WavFile *wf, uint8_t *buf, 
                        uint32_t buf_len)
{
  const int block_size = KameMix_wavBlockSize(wf);
  buf_len = (buf_len / block_size) * block_size;
  FILE *file = (FILE*)wf->file;
  uint32_t num_read = (uint32_t)fread(buf, 1, buf_len, file);
  
  if (num_read < buf_len) {
    if (feof(file) == 0) { // not eof, so there was an error
      clearerr(file);
      return -1;
    }
    clearerr(file); // clear eof
  }

  wf->stream_pos += num_read;
  return num_read;
}

} // end extern "C"


