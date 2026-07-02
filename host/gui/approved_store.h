#pragma once
#include <QSet>
#include <QString>
#include <QStringList>

namespace droppix {
// Remembers device ids the user has approved, persisted to <dir>/approved (one id per line).
class ApprovedStore {
 public:
  explicit ApprovedStore(QString dir);
  bool isApproved(const QString& id) const;
  void approve(const QString& id);
  QStringList ids() const;            // all remembered ids (for the "forget devices" UI)
  void remove(const QString& id);     // forget one id (rewrites the file)
  void clear();
 private:
  QString path() const;  // <dir>/approved
  void rewrite() const;  // persist ids_ to disk (truncate + write)
  QString dir_;
  QSet<QString> ids_;
};
}  // namespace droppix
