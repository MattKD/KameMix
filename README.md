# KameMix
Simple audio mixer for SDL2

This is still in an alpha state. It hasn't been thoroughly tested, and the interface hasn't stabilized yet, but feel free to try it and report and bugs. It supports OGG and WAV files loaded fully into memory as KameMix_Sound objects, or in small chunks read in a separate thread as KameMix_Stream objects. Sounds can be played multiple time and shared with KameMix_incSoundRef. Streams must not be played multiple times and cannot be shared; always use the returned KameMix_Channel from KameMix_playStream when replaying stream. Use KameMix_unsetChannel on channel when playing a sound/stream for the first time. Sounds and streams can have their volume modified through KameMix_setVolume, KameMix_setGroupVolume, and KameMix_setMasterVolume. Volume is also affected by setting sound, stream, and listener 2d position: KameMix_setPosition and KameMix_setListenerPos. Fading in and out and pausing is also supported. Replaying a stream (not sound) while in the middle of playing can cause an audio pop. To prevent call KameMix_stop and wait until not playing (checking with KameMix_isFinished). KameMix_init must be called before using any other function, and KameMix_shutdown when finished and after releasing all sounds and streams. Loading sounds/streams and playing a stream at new position will block until reading from disk and decoding finishes. See KameMix.h for a full list of functions.

---

Windows Visual Studio 2015 Build:

1) Clone repo: git clone https://github.com/MattKD/KameMix.git

2) Download 3rd party libs:

2a) SDL2 development lib (Visual C++): https://www.libsdl.org/download-2.0.php

2b) libvorbis and libogg: https://xiph.org/downloads/

3) Extract libs somewhere, for example C:\Libs\SDL2-2.0.4, C:\Libs\libogg-1.3.2, C:\Libs\libvorbis-1.3.5. Libogg and libvorbis need to be built.

3a) libvorbis/libvorbisfile VS project need libogg's include/lib path set (in "VC++ Directories", and libogg.lib as an Additional Dependency (in "Linker/Input"); libvorbisfile also needs libvorbis.lib as an Additional Dependency.

4) Open KameMix/Windows/Windows.sln and edit KameMix's property sheets: KameMix_Libs.props and KameMix_Libs_x64.props to locate 3rd party libs:

4a) Click Property Manager tab on left side of window, and edit both "KameMix/Debug|Win32/KameMix_Libs.props" and "KameMix/Debug|x64/KameMix_Libs_x64.props"

4b) In "VC++ Directories", change Include and Library Directories to 3rd Party Libs you used in step 3

5) Solution should now compile for Debug/Release x86/x64: click Build/Build Solution (F7)

6) To run KameMixTest make sure all dll's (KameMix.dll, SDL2.dll, libogg.dll, libvorbis.dll, libvorbisfile.dll) and the sound directory (from "KameMix/test/") are in the same directory as KameMixTest.exe. Only x86 release dll's are included in "KameMix/Windows/KameMixTest/", so x64 KameMixTest builds won't run without you supplying the x64 dll's.

---

Linux Build:

1) Make sure libsdl2-dev and libvorbis-dev are installed. On Ubuntu:
```
sudo apt install libsdl2-dev libvorbis-dev
```

2) Run make to build KameMix/Linux/KameMix/libKameMix.so and KameMix/Linux/KameMixTest/KameMixTest:
```
cd KameMix/Linux
make
```
3) To run KameMixTest:
```
cd KameMixTest
./KameMixTest
```
This assumes sound/ and libKameMix.so are in same directory as KameMixTest, which is true if run from Linux/KameMixTest/ as symlinks to both are created from make.

4) To delete all build files including libKameMix.so and KameMixTest (copy/move them first to save):
```
cd KameMix/Linux
make clean
```

