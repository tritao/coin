#ifndef COIN_TEST_RENDERWORKLOADS_H
#define COIN_TEST_RENDERWORKLOADS_H

#include <vector>

class SoCoordinate3;
class SoMaterial;
class SoOrthographicCamera;
class SoSeparator;
class SoTranslation;

namespace coin_test {

enum class WorkloadKind {
  ManyDraws,
  MaterialChurn,
  Transparency,
  DensePicking,
  FeatureRich,
  SharedAssemblyExpanded,
  SharedAssemblySources,
  SharedAssemblyRecipe
};

struct SceneMutationHandles {
  std::vector<SoTranslation *> transforms;
  std::vector<SoMaterial *> materials;
  std::vector<SoCoordinate3 *> coordinates;
  std::vector<SoCoordinate3 *> definitionCoordinates;
};

const char * workloadName(WorkloadKind kind);
bool parseWorkloadKind(const char * name, WorkloadKind & kind);
bool isAssemblyWorkload(WorkloadKind kind);
int assemblyDefinitionCount(int occurrenceCount);
SoSeparator * makeScene(WorkloadKind kind, int drawCount,
                        SoOrthographicCamera *& camera,
                        SceneMutationHandles * mutations = nullptr);

} // namespace coin_test

#endif // COIN_TEST_RENDERWORKLOADS_H
