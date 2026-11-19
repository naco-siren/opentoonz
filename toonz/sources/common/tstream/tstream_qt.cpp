// The QString operators of TOStream and TIStream (tstream.h), for the code
// on the Qt side; built with Qt only (doc/nexttoonz_core_noqt.md, step 5).

#include "tstream.h"

#include <QString>

TOStream &TOStream::operator<<(QString v) {
  // It wrote what operator<<(std::string) writes, line for line.
  return operator<<(v.toStdString());
}

TIStream &TIStream::operator>>(QString &v) {
  // It read what operator>>(std::string &) reads, appending each byte to the
  // QString with QString::append(char). That went through QChar's integer
  // constructor, so a byte from 0x80 up became the 16-bit value of the
  // signed char: 0xC3 became U+FFC3, not U+00C3. Kept as it was.
  std::string s;
  operator>>(s);
  v.clear();
  v.reserve(int(s.size()));
  for (char c : s) v.append(QChar(ushort(short((signed char)c))));
  return *this;
}
