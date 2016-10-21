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

struct StreamSharedData;

class KAMEMIX_DECLSPEC StreamBuffer {
public:
  StreamBuffer();
  explicit StreamBuffer(const char *filename, double sec = 0.0); 
  StreamBuffer(const StreamBuffer &other);
  StreamBuffer& operator=(const StreamBuffer &other);
  StreamBuffer(StreamBuffer &&other);
  StreamBuffer& operator=(StreamBuffer &&other);
  ~StreamBuffer();

  // Load audio file starting as position 'sec' in seconds. 
  // Returns true on success.
  bool load(const char *filename, double sec = 0.0);
  // Load OGG file starting as position 'sec' in seconds. 
  // Returns true on success.
  bool loadOGG(const char *filename, double sec = 0.0);
  // Load WAV file starting as position 'sec' in seconds. 
  // Returns true on success.
  bool loadWAV(const char *filename, double sec = 0.0);
  bool isLoaded() const;
  // Release loaded file and free allocated data. isLoaded() is false after.
  void release();

  // true if entire file is buffered. readMore()/setPos() do nothing if true.
  bool fullyBuffered() const; 

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
    
  // Return total duration of audio file in seconds, or 0 if not loaded.
  double totalTime() const; 
  // Returns 1 for mono, 2 for stereo, or 0 if not loaded.
  int numChannels() const; 
  // Returns size of audio format * number of channels in bytes, 
  // or 0 if not laoded.
  int sampleBlockSize() const;

  // Lock to safely call data(), size(), time(), endPos(), startPos(),
  // or getPos(), while an advance(), updatePos(), or swapBuffers() may be
  // called.
  void lock(); 
  // Must be called when finished with lock().
  void unlock(); 

  // Returns pointer to currently loaded audio data in stream, or nullptr
  // if not loaded. It is invalid after advance(), updatePos(), or 
  // swapBuffers() is successfuly called. 
  uint8_t* data() const;
  // Returns size in bytes of audio data in data() buffer, or 0 if not loaded.
  int size() const; 
  // Returns current time in whole stream in seconds, or -1 if not loaded.
  double time() const; 
  // Returns byte position of 1 sample past last sample of stream in data(),
  // 0 if EOF was encountered immediatly in readMore(), or
  // -1 if end is not in buffer or not loaded. 
  int endPos() const; 
  // Returns byte position of start sample of stream in data(), or
  // -1 if start is not in buffer or not loaded. 
  int startPos() const; 
  // Returns byte position of sample at 'sec' position of stream in data(), 
  // or -1 if sample is not in buffer or not loaded. 
  int getPos(double sec) const;

private:
  void incRefCount();
  bool allocData();
  void swapBuffersImpl();
  void calcTime(); // sets time2, needs file_read_mutex locked before

  StreamSharedData *sdata;
};


} // end namespace KameMix
#endif
