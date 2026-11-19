// The Qt members of TLevelReader and TLevelWriter (tlevel_io.h) and the
// renumbering of frame files as it was, with QDir and QFile; built with Qt
// only (doc/nexttoonz_core_noqt.md, 8d-0).

#include "tlevel_io.h"
#include "tsystem.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

namespace {
void append(QStringList &out, const std::vector<std::string> &names) {
  for (const std::string &n : names) out.push_back(QString::fromStdString(n));
}
}  // namespace

QString TLevelReader::getCreator() {
  return QString::fromStdWString(getCreatorW());
}

void TLevelReader::getSupportedFormats(QStringList &names) {
  std::vector<std::string> v;
  getSupportedFormats(v);
  append(names, v);
}

void TLevelReader::define(QString extension, int reader,
                          TLevelReaderCreateProc *proc) {
  define(extension.toStdString(), reader, proc);
}

void TLevelReader::define(QString extension, TLevelReaderCreateProc *proc) {
  define(extension.toStdString(), 0, proc);
}

//-----------------------------------------------------------

QString TLevelWriter::creator() const {
  return QString::fromStdWString(m_creatorW);
}

void TLevelWriter::setCreator(const QString &creator) {
  m_creatorW = creator.toStdWString();
}

void TLevelWriter::getSupportedFormats(QStringList &names,
                                       bool onlyRenderFormats) {
  std::vector<std::string> v;
  getSupportedFormats(v, onlyRenderFormats);
  append(names, v);
}

void TLevelWriter::define(QString extension, TLevelWriterCreateProc *proc,
                          bool isRenderFormat) {
  define(extension.toStdString(), proc, isRenderFormat);
}

//-----------------------------------------------------------

// TLevelWriter::renumberFids() as it was (tlevel_io.cpp chooses it in the
// build with Qt).
void renumberFidsWithQt(const TFilePath &m_path,
                        const std::map<TFrameId, TFrameId> &table) {
  typedef std::map<TFrameId, TFrameId> Table;

  struct locals {
    static inline QString qstring(const TFilePath &fp) {
      return QString::fromStdWString(fp.getWideString());
    }
    static inline QString temp(const QString &str) {
      return str + QString("_");
    }
  };

  if (m_path.getDots() == "..") {
    try {
      // Extract all image file paths of the level
      QDir parentDir(
          QString::fromStdWString(m_path.getParentDir().getWideString()));
      parentDir.setFilter(QDir::Files);

      QStringList nameFilters;
      // check for both period and underscore
      nameFilters << QString::fromStdWString(m_path.getWideName()) + ".*." +
                         QString::fromStdString(m_path.getType())
                  << QString::fromStdWString(m_path.getWideName()) + "_*." +
                         QString::fromStdString(m_path.getType());
      parentDir.setNameFilters(nameFilters);

      TFilePathSet fpset;
      TSystem::readDirectory(fpset, parentDir, false);  // Could throw

      // Traverse each file, trying to match it with a table entry
      std::vector<QString> storedDstPaths;

      TFilePathSet::iterator st, sEnd(fpset.end());
      for (st = fpset.begin(); st != sEnd; ++st) {
        const QString &src  = locals::qstring(*st);
        const TFrameId &fid = st->getFrame();  // Could throw ! (and I'm quite
                                               // appalled of that  o.o')

        Table::const_iterator dt(table.find(fid));
        if (dt == table.end()) {
          // The frame must be removed
          QFile::remove(src);
        } else {
          if (fid == dt->second) continue;

          // The frame must be renumbered
          const QString &dst = locals::qstring(st->withFrame(dt->second));

          if (!QFile::rename(src, dst)) {
            // Use a temporary file rename to ensure that other frames to be
            // renumbered
            // are not overwritten.
            if (QFile::rename(locals::qstring(*st), locals::temp(dst)))
              storedDstPaths.push_back(dst);

            // If the second rename did not happen, the problem was not on dst,
            // but on src.
            // Alas, it means that rename on source is not possible - skip.
          }
        }
      }

      // At this point, temporaries should be restored to originals. In case the
      // rename of one of those files cannot be finalized, leave the temporary -
      // as
      // it may be impossible to roll back (another frame could have been
      // renumbered
      // to the would-roll-back frame) !

      std::vector<QString>::iterator dt, dEnd(storedDstPaths.end());
      for (dt = storedDstPaths.begin(); dt != dEnd; ++dt)
        QFile::rename(locals::temp(*dt), *dt);
    } catch (...) {
      // Could not read the directory - skip silently
    }
  }
}
