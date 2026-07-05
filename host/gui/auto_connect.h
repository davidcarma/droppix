#pragma once
#include <QList>
#include <QSet>
#include <QString>

namespace droppix {

// A discovered device the host could auto-connect. `eligible` is precomputed by
// the caller: USB = app-bearing (always true); net = TXT id in the approved store.
struct AutoConnectCandidate {
  QString key;            // "usb:<serial>" or "net:<address>"
  QString id;             // tablet device id (cross-transport identity; may be empty)
  bool eligible = false;
};

// Keys to auto-connect now: enabled AND eligible AND not already active
// (by key, or by non-empty device id already active on another transport).
QList<QString> devicesToConnect(bool enabled,
                                const QList<AutoConnectCandidate>& candidates,
                                const QSet<QString>& activeKeys,
                                const QSet<QString>& activeIds);

}  // namespace droppix
