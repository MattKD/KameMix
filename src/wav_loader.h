#ifndef KAME_MIX_WAV_LOADER_H
#define KAME_MIX_WAV_LOADER_H

#include <cstdint>
#include <fstream>

namespace KameMix {

enum WavFormat {
  WAV_FormatU8,
  WAV_FormatS16, // Little endian
  WAV_FormatFloat // Little endian
};

enum WavResult {
  WAV_OK = 0,
  WAV_FileOpenError = -1,
  WAV_BadHeader = -2,
  WAV_UnsupportedFormat = -3
};

inline
const char* wavErrResultToStr(WavResult result)
{
  switch (result) {
  case WAV_FileOpenError:
    return "Couldn't open WAV file";
  case WAV_BadHeader:
    return "WAV header was bad";
  case WAV_UnsupportedFormat:
    return "WAV file was in an unsupported format";
  }
  return "";
}

struct WavFile {
  WavFile() : stream_pos{0} { }
  //explicit WavFile(const char *file) : stream_pos{0} { open(file); }

  WavResult open(const char *file);
  void close() { file.close(); }
  bool isOpen() const { return file.is_open(); }
 
  int64_t read(uint8_t *buf, uint32_t buf_len);
  bool blockSeek(uint32_t block);
  bool timeSeek(double sec)
  {
    uint32_t block = (uint32_t)(sec * rate);
    blockSeek(block);
    return true;
  }

  bool isEOF() const
  {
    return stream_pos == stream_size; 
  }

  int bytesPerSample() const 
  {
    switch (format) {
    case WAV_FormatU8:
      return 1;
    case WAV_FormatS16:
      return 2;
    case WAV_FormatFloat:
      return 4;
    }
    return -1;
  }

  int blockSize() const { return bytesPerSample() * num_channels; }

  uint32_t blocksLeft() const
  {
    const uint32_t bytes_left = stream_size - stream_pos;
    return bytes_left / blockSize();
  }

  uint32_t totalBlocks() const
  {
    return stream_size / blockSize();
  }

  double totalTime() const
  {
    return (double)totalBlocks() / rate;
  }

  std::ifstream file;  
  uint32_t stream_start;
  uint32_t stream_size;
  uint32_t stream_pos;
  uint32_t rate;
  WavFormat format;
  uint8_t num_channels;
};

inline
std::ostream& operator<<(std::ostream& os, const WavFile &wf)
{
  os << "stream_start = " << wf.stream_start << "\n"
    << "stream_size = " << wf.stream_size << "\n"
    << "stream_pos = " << wf.stream_pos << "\n"
    << "rate = " << wf.rate << "\n"
    << "format = " << wf.format << "\n"
    << "num_channels = " << (int)wf.num_channels << "\n";
  return os;
}

} // end namespace KameMix

#endif
