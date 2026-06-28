#include "mdns_browse.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace droppix {

namespace {

std::vector<std::string> split(const std::string& line, char delim) {
  std::vector<std::string> fields;
  std::string field;
  std::istringstream iss(line);
  while (std::getline(iss, field, delim)) {
    fields.push_back(field);
  }
  return fields;
}

}  // namespace

std::vector<MdnsDevice> parse_avahi_browse(const std::string& text) {
  std::vector<MdnsDevice> devices;

  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.empty() || line[0] != '=') continue;

    std::vector<std::string> fields = split(line, ';');
    // =;iface;proto;name;type;domain;hostname;address;port;txt
    if (fields.size() < 9) continue;
    if (fields[2] != "IPv4") continue;

    MdnsDevice dev;
    dev.name = fields[3];
    dev.address = fields[7];
    dev.port = static_cast<uint16_t>(std::atoi(fields[8].c_str()));

    // Dedup by name: last occurrence wins.
    auto it = std::find_if(devices.begin(), devices.end(),
                            [&](const MdnsDevice& d) { return d.name == dev.name; });
    if (it != devices.end()) {
      *it = dev;
    } else {
      devices.push_back(dev);
    }
  }

  return devices;
}

}  // namespace droppix
