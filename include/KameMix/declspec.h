#ifndef KAME_MIX_DECLSPEC_H
#define KAME_MIX_DECLSPEC_H

#ifdef _WIN32
  #ifdef KAMEMIX_EXPORTS
    #define KAMEMIX_DECLSPEC __declspec(dllexport)
  #else
   #define KAMEMIX_DECLSPEC __declspec(dllimport)
  #endif
#else
  #define KAMEMIX_DECLSPEC
#endif

#endif
