#include "aoa_scanner.h"
#include "aoa_scan.h"

namespace droppix {

AoaScanner::AoaScanner(QObject* p) : QObject(p) {
  timer_.setInterval(2000);
  connect(&timer_, &QTimer::timeout, this, &AoaScanner::tick);
}

void AoaScanner::start() { tick(); timer_.start(); }
void AoaScanner::stop()  { timer_.stop(); }

void AoaScanner::tick() {
  QList<AoaClient> clients;
  for (const auto& d : parse_usb_sysfs()) {
    AoaClient c;
    c.serial = QString::fromStdString(d.serial);
    c.product = QString::fromStdString(d.product);
    c.accessoryMode = d.accessory_mode;
    clients.push_back(c);
  }
  emit clientsChanged(clients);
}

}  // namespace droppix
