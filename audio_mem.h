#ifndef KAME_MIX_AUDIO_MEM_H
#define KAME_MIX_AUDIO_MEM_H

namespace KameMix {

//
// Helper functions using user defined malloc,free,realloc
//
template <class T>
T* km_alloc_type(int num)
{
  T *tmp = (T*) KameMix::AudioSystem::getMalloc()(num * sizeof(T));
  if (tmp) {
    return tmp;
  } else {
    throw std::bad_alloc();
  }
}

template <class T>
T* km_realloc_type(T *ptr, int num)
{
  T *tmp = (T*) KameMix::AudioSystem::getRealloc()(ptr, num * sizeof(T));
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
T* km_new_n(int num)
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
