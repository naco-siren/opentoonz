// TKeyValueStore's factory (step 6b of doc/nexttoonz_core_noqt.md), in both
// builds: an injected factory is the one open() uses, setFactory hands back
// the one it replaced, and nullptr restores the built-in one. Without Qt the
// built-in store holds nothing and writes nothing.

#include <gtest/gtest.h>

#include "tenv.h"
#include "tkeyvaluestore.h"

#include <map>
#include <string>
#include <vector>

namespace {

std::map<std::string, std::wstring> memory;
std::vector<std::wstring> opened;

class MemoryStore final : public TKeyValueStore {
public:
  bool contains(const std::string &key) const override {
    return memory.count(key) != 0;
  }
  std::wstring stringValue(const std::string &key,
                           const std::wstring &defaultValue) const override {
    auto it = memory.find(key);
    return it == memory.end() ? defaultValue : it->second;
  }
  void setStringValue(const std::string &key,
                      const std::wstring &value) override {
    memory[key] = value;
  }
  void sync() override {}
};

std::unique_ptr<TKeyValueStore> openMemory(const TFilePath &path) {
  opened.push_back(path.getWideString());
  return std::unique_ptr<TKeyValueStore>(new MemoryStore());
}

}  // namespace

TEST(TKeyValueStore, InjectedFactoryIsTheOneOpenUses) {
  memory.clear();
  opened.clear();
  TKeyValueStore::Factory builtIn = TKeyValueStore::setFactory(&openMemory);
  ASSERT_NE(builtIn, nullptr);
  {
    auto store = TKeyValueStore::open(TFilePath("some/settings.ini"));
    EXPECT_FALSE(store->contains("group/key"));
    EXPECT_EQ(store->stringValue("group/key", L"default"), L"default");
    store->setStringValue("group/key", L"café, \"quoted\"");
    store->sync();
  }
  EXPECT_EQ(opened, std::vector<std::wstring>{L"some/settings.ini"});
  EXPECT_EQ(memory["group/key"], L"café, \"quoted\"");
  EXPECT_TRUE(TKeyValueStore::open(TFilePath("x.ini"))->contains("group/key"));

  // setFactory hands back what it replaces; nullptr means the built-in one
  EXPECT_EQ(TKeyValueStore::setFactory(nullptr), &openMemory);
  EXPECT_EQ(TKeyValueStore::setFactory(nullptr), builtIn);
  opened.clear();
  TKeyValueStore::open(TFilePath("y.ini"));
  EXPECT_TRUE(opened.empty());
}

#ifndef QT_CORE_LIB
TEST(TKeyValueStore, BuiltInStoreWithoutQtHoldsNothing) {
  auto store = TKeyValueStore::open(TFilePath("anything.ini"));
  store->setStringValue("key", L"value");
  store->sync();
  EXPECT_FALSE(store->contains("key"));
  EXPECT_EQ(store->stringValue("key", L"d"), L"d");
  EXPECT_EQ(store->stringValue("key"), L"");
}
#endif

// tenv reads the system variables through the store, with Qt or without
// (step 6c moved tenv.cpp into the build without Qt).
TEST(TKeyValueStore, TEnvReadsThroughTheInjectedStore) {
  memory.clear();
  opened.clear();
  TKeyValueStore::Factory previous = TKeyValueStore::setFactory(&openMemory);
  const std::string var = TEnv::getSystemVarPrefix() + "CONFIG";
  memory[var]           = L"/injected/config";
  EXPECT_EQ(TEnv::getSystemVarStringValue(var), "/injected/config");
  ASSERT_FALSE(opened.empty());
  EXPECT_NE(opened.back().find(L"SystemVar.ini"), std::wstring::npos);
  TKeyValueStore::setFactory(previous);
}
