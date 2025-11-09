#ifndef COIN_RENDER_TOLERANCE_H
#define COIN_RENDER_TOLERANCE_H

#include <string>

namespace CoinVisualTests {

enum class ComparisonPolicy {
  Default,
  Strict,
  Exact,
  Relaxed,
};

struct ToleranceSpec {
  int per_channel = 2;
  double max_diff_pct = 0.5;
  double rmse = 1.0;
};

inline bool parse_comparison_policy(const std::string& value, ComparisonPolicy& policy) {
  if (value == "default") {
    policy = ComparisonPolicy::Default;
  } else if (value == "strict") {
    policy = ComparisonPolicy::Strict;
  } else if (value == "exact") {
    policy = ComparisonPolicy::Exact;
  } else if (value == "relaxed") {
    policy = ComparisonPolicy::Relaxed;
  } else {
    return false;
  }
  return true;
}

inline ToleranceSpec tolerance_for_policy(ComparisonPolicy policy) {
  switch (policy) {
  case ComparisonPolicy::Strict:
    return {1, 0.1, 0.25};
  case ComparisonPolicy::Exact:
    return {0, 0.0, 0.0};
  case ComparisonPolicy::Relaxed:
    return {4, 2.0, 4.0};
  case ComparisonPolicy::Default:
  default:
    return {2, 0.5, 1.0};
  }
}

} // namespace CoinVisualTests

#endif
