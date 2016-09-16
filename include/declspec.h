#ifndef KAME_MIX_DECLSPEC_H
#define KAME_MIX_DECLSPEC_H

#ifdef _WIN32
  #ifdef KAMEMIX_EXPORTS
    #define DECLSPEC __declspec(dllexport)
  #else
   #define DECLSPEC __declspec(dllimport)
  #endif
#else
  #define DECLSPEC
#endif

#endif
