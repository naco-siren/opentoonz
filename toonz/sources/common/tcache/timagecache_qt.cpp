// The QString members of TImageCache (timagecache.h), in the build with Qt
// only (doc/nexttoonz_core_noqt.md, Q2 and 8d-0).

#include "timagecache.h"

#include <QString>

// The std::string versions do what these did.
void TImageCache::add(const QString &id, const TImageP &img, bool overwrite) {
  add(id.toStdString(), img, overwrite);
}

//------------------------------------------------------------------------------

void TImageCache::remove(const QString &id) { remove(id.toStdString()); }

//------------------------------------------------------------------------------

TImageP TImageCache::get(const QString &id, bool toBeModified) const {
  return get(id.toStdString(), toBeModified);
}
