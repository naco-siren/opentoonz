// The QString members of TProperty and TEnumProperty (tproperty.h), in the
// build with Qt only (doc/nexttoonz_core_noqt.md, Q2 and 8d-0). The names are
// kept as std::wstring; these convert on the way in and out.

#include "tproperty.h"

#include <QString>

QString TProperty::getQStringName() const {
  return QString::fromStdWString(getUINameW());
}

void TProperty::setQStringName(const QString &str) {
  setUINameW(str.toStdWString());
}

//-----------------------------------------------------------------------------

TEnumProperty::Item::Item(const QString &name)
    : m_uiName(name.toStdWString()) {}

TEnumProperty::Item::Item(const QString &name, const QString &icon)
    : m_uiName(name.toStdWString()), m_iconName(icon.toStdWString()) {}

// An empty name comes back as a null QString, as the QString() an Item was
// made with by default.
QString TEnumProperty::Item::getUIName() const {
  return m_uiName.empty() ? QString() : QString::fromStdWString(m_uiName);
}

QString TEnumProperty::Item::getIconName() const {
  return m_iconName.empty() ? QString() : QString::fromStdWString(m_iconName);
}

//-----------------------------------------------------------------------------

void TEnumProperty::addValueWithUIName(std::wstring value,
                                       const QString &name) {
  addValueWithUINameW(value, name.toStdWString());
}

void TEnumProperty::addValueWithUIName(std::wstring value, const QString &name,
                                       const QString &iconName) {
  addValueWithUINameW(value, name.toStdWString(), iconName.toStdWString());
}

void TEnumProperty::addValue(std::wstring value, const QString &iconName) {
  addValueWithUINameW(value, value, iconName.toStdWString());
}

void TEnumProperty::setItemUIName(std::wstring value, const QString &name) {
  setItemUINameW(value, name.toStdWString());
}
