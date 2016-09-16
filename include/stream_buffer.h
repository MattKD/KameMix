#ifndef KAME_MIX_STREAM_BUFFER_H
#define KAME_MIX_STREAM_BUFFER_H

#include "audio_system.h"
#include "declspec.h"
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <atomic>

namespace KameMix {

enum StreamResult {
  StreamReady,
  StreamNotReady,
  StreamError,
  StreamNoData,
  StreamPositionSet,
  StreamPositionNotSet
};

/*
No lock required: total_time, channels, full_buffered, refcount

mutex: 
  read/write: time, buffer, buffer_size, end_pos

mutex2:
  write: time2, buffer2, buffer_size2, end_pos2, set_pos, error, SharedData2
  read: everything except buffer

Modifying functions:
advance/updatePos/swapBuffers: 
  time, time2, buffer, buffer2, buffer_size, buffer_size2, end_pos,
  end_pos2, pos_set, error
readMore/setPos: time2, buffer2, buffer_size2, end_pos2, pos_set, error
*/

class DECLSPEC StreamBuffer {
public:
  StreamBuffer() : sdata{nullptr} {  }

  explicit StreamBuffer(const char *filename, double sec = 0.0) 
    : sdata{nullptr} 
  { 
    load(filename, sec); 
  }

  StreamBuffer(const StreamBuffer &other) : sdata{other.sdata}
  {
    incRefCount();
  }

  StreamBuffer& operator=(const StreamBuffer &other)
  {
    if (this != &other) {
      release();
      sdata = other.sdata;
      incRefCount();
    }
    return *this;
  }

  StreamBuffer(StreamBuffer &&other) : sdata{other.sdata}
  {
    other.sdata = nullptr;
  }

  StreamBuffer& operator=(StreamBuffer &&other)
  {
    if (this != &other) {
      release();
      sdata = other.sdata;
      other.sdata = nullptr;
    }
    return *this;
  }

  ~StreamBuffer() { release(); }

  // Load audio file starting as position 'sec' in seconds. 
  // Returns true on success.
  bool load(const char *filename, double sec = 0.0);
  // Load OGG file starting as position 'sec' in seconds. 
  // Returns true on success.
  bool loadOGG(const char *filename, double sec = 0.0);
  bool isLoaded() const { return sdata != nullptr; }
  // Release loaded file and free allocated data. isLoaded() is false after.
  void release();

  // true if entire file is buffered. readMore()/setPos() do nothing if true.
  bool fullyBuffered() const 
  { 
    assert(sdata && 
           "StreamBuffer must be loaded before calling fullyBuffered()");
    return sdata->fully_buffered; 
  }

  // Read more data into buffer2. Returns true if there is data in buffer2,
  // else return false if an error occurred.
  bool readMore();
  // Read data into buffer2 after setting file position to 'sec' position
  // in seconds. If 'swap_buffers' is true then swap buffers before return.
  bool setPos(double sec, bool swap_buffers = false);

  /* 
    Swaps buffers, if buffer2 has data ready from a call to readMore(),
    and not setPos(). Do not read/write from last buffer 
    (last call to data()) after this call. readMore() should be called
    after this. lock() must not be active in same thread. 
    Returns 'StreamReady' on success.
  */
  StreamResult advance();
  /* 
    Swaps buffers, if buffer2 has data ready from a call to setPos(),
    and not readMore(). Do not read/write from last buffer 
    (last call to data()) after this call. readMore() should be called 
    after this. lock() must not be active in same thread. 
    Returns 'StreamReady' on success.
  */
  StreamResult updatePos();
  /* 
    Swaps buffers, if buffer2 has data ready from a call to readMore() or
    setPos(). Do not read/write from last buffer (last call to data()) 
    after this call. readMore() should be called after this. lock() must 
    not be active in same thread. 
    Returns 'StreamReady' on success.
  */
  StreamResult swapBuffers();
    
  // Return total duration of audio file in seconds.
  double totalTime() const 
  { 
    assert(sdata && 
           "StreamBuffer must be loaded before calling totalTime()");
    return sdata->total_time; 
  }

  // Returns 1 for mono, 2 for stereo.
  int numChannels() const 
  { 
    assert(sdata && 
           "StreamBuffer must be loaded before calling numChannels()");
    return sdata->channels; 
  }

  // Returns size of audio format * number of channels in bytes.
  int sampleBlockSize() const 
  { 
    assert(sdata && 
           "StreamBuffer must be loaded before calling sampleBlockSize()");
    return sdata->channels * AudioSystem::getFormatSize(); 
  }

  // Lock to safely call data(), size(), time(), endPos(), startPos(),
  // or getPos(), while an advance(), updatePos(), or swapBuffers() may be
  // called.
  void lock() 
  { 
    assert(sdata && 
           "StreamBuffer must be loaded before calling lock()");
    sdata->mutex.lock(); 
  }

  // Must be called when finished with lock().
  void unlock() 
  { 
    assert(sdata && 
           "StreamBuffer must be loaded before calling unlock()");
    sdata->mutex.unlock(); 
  }

  // Pointer to currently loaded audio data in stream, is invalid after
  // advance(), updatePos(), or swapBuffers() is successfuly called. 
  uint8_t* data() const 
  { 
    assert(sdata && 
           "StreamBuffer must be loaded before calling data()");
    return sdata->buffer; 
  }

  // Size in bytes of audio data in data() buffer.
  int size() const 
  { 
    assert(sdata && 
           "StreamBuffer must be loaded before calling size()");
    return sdata->buffer_size; 
  }

  // Current time in whole stream in seconds.
  double time() const 
  { 
    assert(sdata && 
           "StreamBuffer must be loaded before calling time()");
    return sdata->time; 
  }

  // Returns byte position of 1 sample past last sample of stream in data(),
  // 0 if EOF was encountered immediatly in readMore(), or
  // -1 if end is not in buffer. 
  int endPos() const 
  { 
    assert(sdata && 
           "StreamBuffer must be loaded before calling endPos()");
    return sdata->end_pos; 
  }

  // Returns byte position of start sample of stream in data(), or
  // -1 if start is not in buffer. 
  int startPos() const 
  {
    assert(sdata && 
           "StreamBuffer must be loaded before calling startPos()");
    if (sdata->time == 0.0) {
      return 0;
    } else if (sdata->end_pos != -1 && sdata->end_pos != sdata->buffer_size) {
      return sdata->end_pos;
    }
    return -1;
  }

  // Returns byte position of sample at 'sec' position of stream in data(), 
  // or -1 if sample is not in buffer. 
  int getPos(double sec) const 
  {
    assert(sdata && 
           "StreamBuffer must be loaded before calling getPos()");
    int sample_pos = (int)((sec - sdata->time) * AudioSystem::getFrequency());
    int byte_pos = sample_pos * sampleBlockSize();
    if (byte_pos < 0 || byte_pos > sdata->buffer_size) {
      return -1;
    }
    return byte_pos;
  }

private:
  struct SharedData {
    SharedData() : total_time{0.0}, time{0.0}, time2{0.0}, buffer{nullptr},
      buffer2{nullptr}, buffer_size{0}, buffer_size2{0}, end_pos{-1}, 
      end_pos2{-1}, channels{0}, refcount(1), fully_buffered{false}, 
      pos_set{false}, error{false} { }

    double total_time; // total stream time in seconds
    double time; // current time in seconds
    double time2; // current time of buffer2 in seconds
    uint8_t *buffer;
    uint8_t *buffer2;
    int buffer_size;
    int buffer_size2; 
    int end_pos; // 1 past end of stream in bytes, or -1 if end not in buffer
    int end_pos2; // 1 past end of stream in bytes, or -1 if end not in buffer2
    int channels;
    std::atomic<int> refcount;
    std::mutex mutex;
    std::mutex mutex2;
    bool fully_buffered; // whole stream fit into buffer
    bool pos_set; // setPos called
    bool error; // error reading
  };

  void incRefCount() 
  { 
    if (sdata) {
      sdata->refcount.fetch_add(1, std::memory_order_relaxed);
    }
  }

  bool allocData();
  void releaseFile();
  void swapBuffersImpl();
  void calcTime(); // sets time2, needs file_read_mutex locked before

  SharedData *sdata;
  friend struct SharedData2;
};


} // end namespace KameMix
#endif
