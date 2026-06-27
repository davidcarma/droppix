#pragma once
#include <QString>
#include <QStringList>
#include "settings.h"

namespace droppix {
// Named Settings profiles persisted to <dir>/profiles.json.
class ProfileStore {
 public:
  explicit ProfileStore(QString dir);
  QStringList names() const;
  bool save(const QString& name, const Settings& s);
  bool load(const QString& name, Settings& out) const;
  bool remove(const QString& name);
  // Remember which profile was last in use so it can be restored on next launch.
  void setLastUsed(const QString& name);
  QString lastUsed() const;
 private:
  QString path() const;          // <dir>/profiles.json
  QString lastUsedPath() const;  // <dir>/last_profile
  QString dir_;
};
}  // namespace droppix
