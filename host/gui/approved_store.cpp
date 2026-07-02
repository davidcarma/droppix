#include "approved_store.h"
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace droppix {

ApprovedStore::ApprovedStore(QString dir) : dir_(std::move(dir)) {
  QFile f(path());
  if (f.open(QIODevice::ReadOnly)) {
    QTextStream in(&f);
    while (!in.atEnd()) {
      const QString line = in.readLine().trimmed();
      if (!line.isEmpty()) ids_.insert(line);
    }
  }
}

QString ApprovedStore::path() const { return dir_ + "/approved"; }

bool ApprovedStore::isApproved(const QString& id) const { return ids_.contains(id); }

void ApprovedStore::approve(const QString& id) {
  if (ids_.contains(id)) return;
  ids_.insert(id);
  QDir().mkpath(dir_);
  QFile f(path());
  if (f.open(QIODevice::Append)) {
    f.write((id + "\n").toUtf8());
  }
}

QStringList ApprovedStore::ids() const {
  QStringList out(ids_.begin(), ids_.end());
  out.sort();
  return out;
}

void ApprovedStore::remove(const QString& id) {
  if (ids_.remove(id)) rewrite();
}

void ApprovedStore::clear() {
  ids_.clear();
  QFile f(path());
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    f.close();
  }
}

void ApprovedStore::rewrite() const {
  QDir().mkpath(dir_);
  QFile f(path());
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QTextStream out(&f);
    for (const auto& id : ids_) out << id << "\n";
  }
}
}  // namespace droppix
