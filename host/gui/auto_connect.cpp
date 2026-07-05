#include "auto_connect.h"

namespace droppix {

QList<QString> devicesToConnect(bool enabled,
                                const QList<AutoConnectCandidate>& candidates,
                                const QSet<QString>& activeKeys,
                                const QSet<QString>& activeIds) {
  QList<QString> out;
  if (!enabled) return out;
  for (const auto& c : candidates) {
    if (!c.eligible) continue;
    if (activeKeys.contains(c.key)) continue;
    if (!c.id.isEmpty() && activeIds.contains(c.id)) continue;
    out.push_back(c.key);
  }
  return out;
}

}  // namespace droppix
