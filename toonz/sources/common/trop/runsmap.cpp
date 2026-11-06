

#include "runsmap.h"

#include <cstring>

//---------------------------------------------------------------------------------------------

void RunsMap::setRunLength(TPixelGR8 *pix, TUINT32 length) {
  TPixelGR8 *origPix = pix;
  pix                = origPix;

  TPixelGR8 *pixRev = pix + length - 1;

  --length;
  if (length < 3)
    pix->value = pixRev->value = (length << 6);
  else {
    pix->value = pixRev->value = (3 << 6);
    ++pix, --pixRev;
    if (length < 255)
      pix->value = pixRev->value = length;
    else {
      pix->value = pixRev->value = 255;
      ++pix, pixRev -= 4;
      // The 32-bit length sits at an arbitrary offset in the byte buffer.
      memcpy(pix, &length, sizeof(length));
    }
  }

  assert(runLength(origPix) == (length + 1));
}

//---------------------------------------------------------------------------------------------

TUINT32 RunsMap::runLength(const TPixelGR8 *pix, bool reversed) const {
  int length = pix->value >> 6;
  if (length >= 3) {
    pix += (reversed) ? -1 : 1, length = pix->value;
    if (length >= 255) {
      pix += (reversed) ? -4 : 1;
      TUINT32 stored;
      memcpy(&stored, pix, sizeof(stored));
      length = stored;
    }
  }

  return length + 1;
}
