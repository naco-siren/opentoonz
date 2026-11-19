// The factory of TKeyValueStore (tkeyvaluestore.h) and the store the build
// without Qt falls back on. The QSettings store, the default of the build
// with Qt, is in tkeyvaluestore_qt.cpp.

#include "tkeyvaluestore.h"

#include <atomic>

#ifdef QT_CORE_LIB
// tkeyvaluestore_qt.cpp
std::unique_ptr<TKeyValueStore> openQSettingsStore(const TFilePath &path);
#endif

namespace {

// Holds nothing, writes nothing.
class EmptyStore final : public TKeyValueStore {
public:
  bool contains(const std::string &) const override { return false; }
  std::wstring stringValue(const std::string &,
                           const std::wstring &defaultValue) const override {
    return defaultValue;
  }
  void setStringValue(const std::string &, const std::wstring &) override {}
  void sync() override {}
};

std::unique_ptr<TKeyValueStore> openEmptyStore(const TFilePath &) {
  return std::unique_ptr<TKeyValueStore>(new EmptyStore());
}

TKeyValueStore::Factory builtInFactory() {
#ifdef QT_CORE_LIB
  return &openQSettingsStore;
#else
  return &openEmptyStore;
#endif
}

std::atomic<TKeyValueStore::Factory> currentFactory{nullptr};

}  // namespace

TKeyValueStore::~TKeyValueStore() {}

std::unique_ptr<TKeyValueStore> TKeyValueStore::open(const TFilePath &path) {
  Factory factory = currentFactory.load();
  return (factory ? factory : builtInFactory())(path);
}

TKeyValueStore::Factory TKeyValueStore::setFactory(Factory factory) {
  Factory previous = currentFactory.exchange(factory);
  return previous ? previous : builtInFactory();
}
