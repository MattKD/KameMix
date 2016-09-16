#ifndef KAME_MIX_VORBIS_HELPER_H
#define KAME_MIX_VORBIS_HELPER_H

#include <vorbis/vorbisfile.h>

namespace KameMix {

void getStreamAndOffset(OggVorbis_File &vf, int &bitstream, int64_t &offset);
bool isMonoOGG(OggVorbis_File &vf);
// return bufsize to fill data from OGG file.
// returns -1 if an error occured
int64_t calcBufSizeOGG(OggVorbis_File &vf, int channels);

} // namespace KameMix

#endif