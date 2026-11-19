// The desktop's TKeyValueStore: the settings file is a QSettings INI file,
// read and written exactly as the code did before it went through the store
// (doc/nexttoonz_core_noqt.md, step 6b). The build without Qt leaves this
// file out.

#include "tkeyvaluestore.h"

#include <QSettings>
#include <QString>
#include <QVariant>

namespace {

class QSettingsStore final : public TKeyValueStore {
  QSettings m_settings;

  static QString qKey(const std::string &key) {
    return QString::fromStdString(key);
  }

public:
  explicit QSettingsStore(const TFilePath &path)
      : m_settings(QString::fromStdWString(path.getWideString()),
                   QSettings::IniFormat) {}

  bool contains(const std::string &key) const override {
    return m_settings.contains(qKey(key));
  }

  std::wstring stringValue(const std::string &key,
                           const std::wstring &defaultValue) const override {
    return m_settings.value(qKey(key), QString::fromStdWString(defaultValue))
        .toString()
        .toStdWString();
  }

  void setStringValue(const std::string &key,
                      const std::wstring &value) override {
    m_settings.setValue(qKey(key), QString::fromStdWString(value));
  }

  void sync() override { m_settings.sync(); }
};

}  // namespace

std::unique_ptr<TKeyValueStore> openQSettingsStore(const TFilePath &path) {
  return std::unique_ptr<TKeyValueStore>(new QSettingsStore(path));
}
