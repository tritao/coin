#include "rendering/SoRetainedUpdater.h"

#include <Inventor/SoPath.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/fields/SoField.h>
#include <Inventor/lists/SoAuditorList.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTranslation.h>

namespace {

const size_t MAX_INCREMENTAL_MATERIAL_NOTIFICATIONS = 256;

enum class UpdateKind {
  Unsupported,
  Switch,
  Translation,
  LegacyDiffuseColor,
  Geometry
};

UpdateKind
classify(const SoRetainedNotification & notification,
         size_t notificationCount)
{
  if (!notification.node || !notification.field || !notification.path) {
    return UpdateKind::Unsupported;
  }
  if (notification.node->isOfType(SoSwitch::getClassTypeId())) {
    SoSwitch * node = static_cast<SoSwitch *>(notification.node);
    const int child = node->whichChild.getValue();
    if (notification.field == &node->whichChild &&
        node->getNumChildren() == 1 &&
        (child == SO_SWITCH_ALL || child == SO_SWITCH_NONE || child == 0)) {
      return UpdateKind::Switch;
    }
  }
  else if (notification.node->isOfType(SoTranslation::getClassTypeId()) &&
           notification.field ==
             &static_cast<SoTranslation *>(notification.node)->translation) {
    return UpdateKind::Translation;
  }
  else if (notificationCount <= MAX_INCREMENTAL_MATERIAL_NOTIFICATIONS &&
           notification.node->isOfType(SoMaterial::getClassTypeId()) &&
           notification.field ==
             &static_cast<SoMaterial *>(notification.node)->diffuseColor) {
    return UpdateKind::LegacyDiffuseColor;
  }
  else if (notification.node->isOfType(SoCoordinate3::getClassTypeId()) &&
           notification.field ==
             &static_cast<SoCoordinate3 *>(notification.node)->point) {
    return UpdateKind::Geometry;
  }
  return UpdateKind::Unsupported;
}

SoRenderInvalidation
invalidationFor(UpdateKind kind, bool transformAffectsPlan)
{
  switch (kind) {
  case UpdateKind::Translation:
    return transformAffectsPlan
      ? SoRenderInvalidation::Content | SoRenderInvalidation::Plan
      : SoRenderInvalidation::Content;
  case UpdateKind::LegacyDiffuseColor:
    return SoRenderInvalidation::Content;
  case UpdateKind::Switch:
    return SoRenderInvalidation::Content | SoRenderInvalidation::Plan;
  case UpdateKind::Geometry:
    return SoRenderInvalidation::Content | SoRenderInvalidation::Plan |
      SoRenderInvalidation::Resources | SoRenderInvalidation::PickTopology;
  case UpdateKind::Unsupported:
  default:
    return SoRenderInvalidation::Rebuild;
  }
}

}

bool
SoRetainedUpdater::hasOneParentOccurrence(SoNode * node)
{
  if (!node) return false;
  const auto cached = this->uniqueParentCache.find(node);
  if (cached != this->uniqueParentCache.end()) return cached->second != FALSE;

  const SoAuditorList & auditors = node->getAuditors();
  unsigned int occurrences = 0;
  for (int auditorIndex = 0; auditorIndex < auditors.getLength();
       ++auditorIndex) {
    if (auditors.getType(auditorIndex) != SoNotRec::PARENT) continue;
    SoNode * parentNode = static_cast<SoNode *>(
      auditors.getObject(auditorIndex));
    if (!parentNode->isOfType(SoGroup::getClassTypeId())) {
      this->uniqueParentCache[node] = FALSE;
      return false;
    }
    SoGroup * parent = static_cast<SoGroup *>(parentNode);
    for (int childIndex = 0; childIndex < parent->getNumChildren();
         ++childIndex) {
      if (parent->getChild(childIndex) == node && ++occurrences > 1) {
        this->uniqueParentCache[node] = FALSE;
        return false;
      }
    }
  }
  const SbBool unique = occurrences == 1;
  this->uniqueParentCache[node] = unique;
  return unique != FALSE;
}

SoRetainedUpdater::Result
SoRetainedUpdater::update(
  SoIRRenderAction & action,
  const std::vector<SoRetainedNotification> & notifications)
{
  Result result;
  if (notifications.empty()) return result;

  UpdateKind kind = UpdateKind::Unsupported;
  std::vector<const SoPath *> paths;
  paths.reserve(notifications.size());
  for (const SoRetainedNotification & notification : notifications) {
    const UpdateKind next = classify(notification, notifications.size());
    if (next == UpdateKind::Unsupported ||
        (kind != UpdateKind::Unsupported && kind != next) ||
        !this->hasOneParentOccurrence(notification.node)) {
      return result;
    }
    kind = next;
    paths.push_back(notification.path);
  }

  if (kind == UpdateKind::Translation) {
    const bool affectsPlan =
      action.transformUpdateAffectsPlanning(paths) != FALSE;
    result.updatedCommands = action.updateCommandMatricesForStatePaths(paths);
    if (result.updatedCommands > 0) {
      result.invalidation = invalidationFor(kind, affectsPlan);
    }
  }
  else if (kind == UpdateKind::LegacyDiffuseColor) {
    result.updatedCommands =
      action.updateCommandDiffuseColorsForStatePaths(paths);
  }
  else if (kind == UpdateKind::Geometry) {
    result.updatedCommands = action.updateCommandGeometryForStatePaths(paths);
  }
  else if (kind == UpdateKind::Switch) {
    for (const SoRetainedNotification & notification : notifications) {
      SoSwitch * node = static_cast<SoSwitch *>(notification.node);
      const int updated = action.updateCommandVisibilityForSwitchPath(
        notification.path, node->whichChild.getValue() != SO_SWITCH_NONE);
      if (updated == 0) {
        result.updatedCommands = 0;
        return result;
      }
      result.updatedCommands += updated;
    }
  }

  if (result.updatedCommands > 0 && kind != UpdateKind::Translation) {
    result.invalidation = invalidationFor(kind, false);
  }
  if (result.updatedCommands > 0) {
    action.getMutableDrawList().applyRetainedInvalidation(
      coin_has_invalidation(result.invalidation, SoRenderInvalidation::Plan),
      coin_has_invalidation(result.invalidation,
                            SoRenderInvalidation::Resources),
      coin_has_invalidation(result.invalidation,
                            SoRenderInvalidation::PickTopology));
  }
  return result;
}

void
SoRetainedUpdater::reset()
{
  this->uniqueParentCache.clear();
}
