#pragma once
#include <string>
#include <vector>
#include "input_map.h"   // droppix::Rect
namespace droppix {
struct OutputInfo { std::string name; Rect geom; bool enabled = false; };
std::vector<OutputInfo> parse_kscreen_outputs(const std::string& text);
Rect desktop_bounds(const std::vector<OutputInfo>& outs);   // {0,0,maxRight,maxBottom}
// Select the droppix output: an enabled output whose size == mode, preferring a
// name containing evdi/Unknown/droppix. Returns false if none match.
bool select_droppix(const std::vector<OutputInfo>& outs, int mode_w, int mode_h, Rect& out);
bool select_droppix(const std::vector<OutputInfo>& outs, int mode_w, int mode_h, OutputInfo& out);
// Robust identification: the enabled output present in `after` but not in `before`
// (by name) is the just-created droppix monitor, regardless of its size. Returns
// false if there is no single new output (caller falls back to select_droppix).
bool select_new_output(const std::vector<OutputInfo>& before,
                       const std::vector<OutputInfo>& after, Rect& out);
bool select_new_output(const std::vector<OutputInfo>& before,
                       const std::vector<OutputInfo>& after, OutputInfo& out);
}  // namespace droppix
