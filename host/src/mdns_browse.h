#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace droppix {

// A tablet discovered via `avahi-browse -rptk _droppix-client._tcp`.
struct MdnsDevice {
  std::string name;
  std::string address;
  uint16_t port = 0;
};

// Parses the resolved ("=") lines of `avahi-browse -rptk` output, keeping
// only IPv4 records, and returns one MdnsDevice per distinct name (last
// occurrence wins on duplicates).
//
// Note: avahi escapes a literal ';' inside a field as '\;'. This parser does
// a plain split on ';' and does not unescape such sequences; this is
// acceptable for our own service names (Build.MODEL), which never contain
// ';', but would mis-parse a name that legitimately contains one.
std::vector<MdnsDevice> parse_avahi_browse(const std::string& text);

}  // namespace droppix
