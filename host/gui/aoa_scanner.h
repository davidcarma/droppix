#pragma once
#include <QObject>
#include <QTimer>
#include <QList>
#include <QString>
#include <QMetaType>

namespace droppix {

// A plugged Android tablet discovered over USB (sysfs), streamable via AOA.
struct AoaClient {
  QString serial;
  QString product;
  bool accessoryMode = false;
};

// Polls /sys/bus/usb/devices every 2s via parse_usb_sysfs and emits the current
// AOA-capable device set. No root, no external tool. Mirrors TetherScanner.
class AoaScanner : public QObject {
  Q_OBJECT
 public:
  explicit AoaScanner(QObject* p = nullptr);
  bool available() const { return true; }
  void start();   // emit immediately, then every 2s
  void stop();
 signals:
  void clientsChanged(QList<droppix::AoaClient> clients);
 private:
  void tick();
  QTimer timer_;
};

}  // namespace droppix

Q_DECLARE_METATYPE(droppix::AoaClient)
