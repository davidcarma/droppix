#pragma once
namespace droppix {
// Accumulates samples for a reporting window: count, mean, peak. Not thread-safe;
// used from the single capture/encode loop.
class StatAccumulator {
 public:
  void add(double v) { sum_ += v; if (count_ == 0 || v > peak_) peak_ = v; ++count_; }
  int count() const { return count_; }
  double avg() const { return count_ ? sum_ / count_ : 0.0; }
  double peak() const { return count_ ? peak_ : 0.0; }
  void reset() { sum_ = 0.0; peak_ = 0.0; count_ = 0; }
 private:
  double sum_ = 0.0;
  double peak_ = 0.0;
  int count_ = 0;
};
}  // namespace droppix
