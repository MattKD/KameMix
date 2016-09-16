# KameMix
Simple audio mixer for SDL2

Windows Visual Studio 2015 Build:

1) Clone repo: git clone https://github.com/MattKD/KameMix.git

2) Download 3rd party libs:

2a) SDL2 development lib (Visual C++): https://www.libsdl.org/download-2.0.php

2b) libvorbis and libogg: https://xiph.org/downloads/

3) Extract libs somewhere, for example C:\Libs\SDL2-2.0.4, C:\Libs\libogg-1.3.2, C:\Libs\libvorbis-1.3.5

4) Open Window/Windows.sln and create a property sheet for KameMix project to locate 3rd party libs:

4a) Click Property Manager tab on left side of window

4b) Right click KameMix and click Add New Property Sheet; save as whatever, for example KameMix_Libs.props

4c) Back in Property Manager, click KameMix/Debug/KameMix_Libs

4d) In Common Properties/VC++ Directories, add Include and Library Directories to 3rd Party Libs you extracted in step 3

4e) In Common Properties/Linker/Input, add Additional Dependencies: "libvorbisfile.lib" "SDL2.lib"

4f) Click OK to save KameMix_Libs.props. These steps will apply to both Debug and Release x86/x64

5) Solution should now compile for Debug and Release: Build/Build Solution (F7)

Linux/Mac Build:

TODO

Documentation:

TODO
