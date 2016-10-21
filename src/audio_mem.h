#ifndef KAME_MIX_AUDIO_MEM_H
#define KAME_MIX_AUDIO_MEM_H

#include <new>
#include "audio_system.h"

namespace KameMix {

//
// Helper functions using user defined malloc,free,realloc
//

inline
void km_free(void *ptr)
{
  AudioSystem::getFree()(ptr);
}

inline
void* km_malloc_(size_t len)
{
  return KameMix::AudioSystem::getMalloc()(len);
}

inline
void* km_malloc(size_t len)
{
  void *tmp = km_malloc_(len);
  if (tmp) {
    return tmp;
  } else {
    throw std::bad_alloc();
  }
}

inline
void* km_realloc_(void *ptr, size_t len)
{
  return KameMix::AudioSystem::getRealloc()(ptr, len);
}

inline
void* km_realloc(void *ptr, size_t len)
{
  void *tmp = km_realloc_(ptr, len);
  if (tmp) {
    return tmp;
  } else {
    throw std::bad_alloc();
  }
}

template <class T>
T* km_new()
{
  T *buf = (T*) KameMix::AudioSystem::getMalloc()(sizeof(T));
  if (buf) {
    buf = new (buf) T();
    return buf;
  } else {
    throw std::bad_alloc();
  }
}

template <class T>
T* km_new_n(size_t num)
{
  T *buf = (T*) KameMix::AudioSystem::getMalloc()(num * sizeof(T));
  if (buf) {
    T *tmp = buf;
    T *buf_end = buf + num;
    while (tmp != buf_end) {
      tmp = new (tmp) T();
      ++tmp;
    }
    return buf;
  } else {
    throw std::bad_alloc();
  }
}

template <class T>
void km_delete(T *ptr)
{
  ptr->~T();
  KameMix::AudioSystem::getFree()(ptr);
}

template <class T>
void km_delete_n(T *buf, int num)
{
  T *tmp = buf;
  T *buf_end = buf + num;
  while (tmp != buf_end) {
    tmp->~T();
    ++tmp;
  }
  KameMix::AudioSystem::getFree()(buf);
}

// Allocator using user defined km_malloc & km_free for use with
// sounds & sound_copies std::vectors. Must not be used until 
// AudioSystem::init is called.
template <class T>
struct Alloc{
  typedef T value_type;

  template <class U>
  struct rebind { typedef Alloc<U> other; };

  Alloc() { }

  template <class U> Alloc(const Alloc<U> &) { }

  T* allocate(std::size_t n) 
  { 
    return (T*) KameMix::AudioSystem::getMalloc()(n * sizeof(T)); 
  }

  void deallocate(T *ptr, std::size_t n) 
  { 
    KameMix::AudioSystem::getFree()(ptr);
  }
};

template <class T, class U>
bool operator==(const Alloc<T>&, const Alloc<U>&)
{
  return true;
}

template <class T, class U>
bool operator!=(const Alloc<T>&, const Alloc<U>&)
{
  return false;
}

} // end namespace KameMix
#endif
