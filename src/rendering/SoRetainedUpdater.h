#ifndef COIN_SORETAINEDUPDATER_H
#define COIN_SORETAINEDUPDATER_H

#include <Inventor/SbBasic.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

class SoField;
class SoIRRenderAction;
class SoNode;
class SoPath;

enum class SoRenderInvalidation : uint32_t {
  None = 0,
  Content = 1u << 0,
  Plan = 1u << 1,
  Resources = 1u << 2,
  PickTopology = 1u << 3,
  Rebuild = 1u << 4
};

inline SoRenderInvalidation
operator|(SoRenderInvalidation lhs, SoRenderInvalidation rhs)
{
  return static_cast<SoRenderInvalidation>(
    static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline bool
coin_has_invalidation(SoRenderInvalidation value, SoRenderInvalidation domain)
{
  return (static_cast<uint32_t>(value) & static_cast<uint32_t>(domain)) != 0;
}

struct SoRetainedNotification {
  SoNode * node = nullptr;
  SoField * field = nullptr;
  const SoPath * path = nullptr;
};

class SoRetainedUpdater {
public:
  struct Result {
    int updatedCommands = 0;
    SoRenderInvalidation invalidation = SoRenderInvalidation::Rebuild;

    bool succeeded() const { return this->updatedCommands > 0; }
  };

  Result update(SoIRRenderAction & action,
                const std::vector<SoRetainedNotification> & notifications);
  void reset();

private:
  bool hasOneParentOccurrence(SoNode * node);
  std::unordered_map<SoNode *, SbBool> uniqueParentCache;
};

#endif // COIN_SORETAINEDUPDATER_H
