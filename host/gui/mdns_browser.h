#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QList>
#include <QMetaType>
#include "mdns_browse.h"

Q_DECLARE_METATYPE(droppix::MdnsDevice)

namespace droppix {

// Periodically runs `avahi-browse -rptk _droppix-client._tcp` to discover
// tablets advertising the droppix client service, and emits the parsed list.
class MdnsBrowser : public QObject {
  Q_OBJECT
 public:
  explicit MdnsBrowser(QObject* p = nullptr);
  bool available() const;
  void start();
  void stop();
 signals:
  void devicesChanged(QList<droppix::MdnsDevice> devices);
 private:
  void runBrowse();
  QProcess proc_;
  QTimer timer_;
};

}  // namespace droppix
