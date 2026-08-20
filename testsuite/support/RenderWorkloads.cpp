#include "RenderWorkloads.h"

#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTranslation.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace coin_test {
namespace {

struct AssemblyPart {
  SoCoordinate3 * coordinates = nullptr;
  SoNormal * normals = nullptr;
  SoIndexedFaceSet * faces = nullptr;
  SoIndexedLineSet * edges = nullptr;
};

SoCoordinate3 * addAssemblyGeometry(SoSeparator * parent, WorkloadKind kind,
                                    const AssemblyPart & part,
                                    SoMaterial * faceMaterial,
                                    SoMaterial * edgeMaterial)
{
  SoIndexedFaceSet * faces = part.faces;
  SoIndexedLineSet * edges = part.edges;
  SoCoordinate3 * coordinates = part.coordinates;
  if (kind == WorkloadKind::SharedAssemblyExpanded) {
    coordinates = new SoCoordinate3;
    coordinates->point = part.coordinates->point;
    SoNormal * normals = new SoNormal;
    normals->vector = part.normals->vector;
    faces = new SoIndexedFaceSet;
    faces->coordIndex = part.faces->coordIndex;
    edges = new SoIndexedLineSet;
    edges->coordIndex = part.edges->coordIndex;
    parent->addChild(coordinates);
    parent->addChild(normals);
  }
  else if (kind == WorkloadKind::SharedAssemblySources) {
    faces = new SoIndexedFaceSet;
    faces->coordIndex = part.faces->coordIndex;
    edges = new SoIndexedLineSet;
    edges->coordIndex = part.edges->coordIndex;
  }

  SoSeparator * faceBranch = new SoSeparator;
  faceBranch->renderCaching = SoSeparator::OFF;
  faceBranch->addChild(faceMaterial);
  faceBranch->addChild(faces);
  parent->addChild(faceBranch);
  SoSeparator * edgeBranch = new SoSeparator;
  edgeBranch->renderCaching = SoSeparator::OFF;
  edgeBranch->addChild(edgeMaterial);
  edgeBranch->addChild(edges);
  parent->addChild(edgeBranch);
  return coordinates;
}

} // namespace

const char * workloadName(WorkloadKind kind)
{
  switch (kind) {
  case WorkloadKind::ManyDraws: return "many_small_draws";
  case WorkloadKind::MaterialChurn: return "many_material_changes";
  case WorkloadKind::Transparency: return "transparent_sorting";
  case WorkloadKind::DensePicking: return "single_pick_dense_scene";
  case WorkloadKind::FeatureRich: return "feature_rich_scene_end_to_end";
  case WorkloadKind::SharedAssemblyExpanded:
    return "shared_assembly_expanded";
  case WorkloadKind::SharedAssemblySources:
    return "shared_assembly_sources";
  case WorkloadKind::SharedAssemblyRecipe:
    return "shared_assembly_recipe";
  }
  return "unknown";
}

bool parseWorkloadKind(const char * name, WorkloadKind & kind)
{
  if (!name) return false;
  const WorkloadKind workloads[] = {
    WorkloadKind::ManyDraws,
    WorkloadKind::MaterialChurn,
    WorkloadKind::Transparency,
    WorkloadKind::DensePicking,
    WorkloadKind::FeatureRich,
    WorkloadKind::SharedAssemblyExpanded,
    WorkloadKind::SharedAssemblySources,
    WorkloadKind::SharedAssemblyRecipe
  };
  for (WorkloadKind candidate : workloads) {
    if (std::strcmp(name, workloadName(candidate)) == 0) {
      kind = candidate;
      return true;
    }
  }
  return false;
}

bool isAssemblyWorkload(WorkloadKind kind)
{
  return kind == WorkloadKind::SharedAssemblyExpanded ||
    kind == WorkloadKind::SharedAssemblySources ||
    kind == WorkloadKind::SharedAssemblyRecipe;
}

int assemblyDefinitionCount(int occurrenceCount)
{
  return std::min(20, std::max(1,
    static_cast<int>(std::sqrt(static_cast<double>(occurrenceCount)))));
}

void populateAssemblyScene(SoSeparator * root, WorkloadKind kind,
                           int occurrenceCount,
                           SceneMutationHandles * mutations = nullptr)
{
  // A small deterministic set of reusable definitions is enough to expose
  // the ownership difference. Geometry size varies by definition so the
  // workload also contains a realistic mixture of small and medium parts.
  const int definitionCount = assemblyDefinitionCount(occurrenceCount);
  std::vector<AssemblyPart> parts(static_cast<size_t>(definitionCount));
  for (int definition = 0; definition < definitionCount; ++definition) {
    AssemblyPart & part = parts[static_cast<size_t>(definition)];
    part.coordinates = new SoCoordinate3;
    part.normals = new SoNormal;
    part.faces = new SoIndexedFaceSet;
    part.edges = new SoIndexedLineSet;
    const int triangleCount = 8 + (definition % 5) * 8;
    std::vector<SbVec3f> positions;
    std::vector<SbVec3f> normals;
    std::vector<int32_t> faceIndices;
    std::vector<int32_t> edgeIndices;
    positions.reserve(static_cast<size_t>(triangleCount + 1));
    normals.reserve(static_cast<size_t>(triangleCount + 1));
    faceIndices.reserve(static_cast<size_t>(triangleCount * 4));
    edgeIndices.reserve(static_cast<size_t>(triangleCount * 3));
    positions.push_back(SbVec3f(0.0f, 0.0f, 0.04f));
    normals.push_back(SbVec3f(0.0f, 0.0f, 1.0f));
    for (int vertex = 0; vertex < triangleCount; ++vertex) {
      const float angle = static_cast<float>(vertex) *
        6.28318530718f / static_cast<float>(triangleCount);
      const float radius = 0.25f + 0.015f * static_cast<float>(definition);
      positions.push_back(SbVec3f(std::cos(angle) * radius,
                                  std::sin(angle) * radius,
                                  0.01f * static_cast<float>(vertex % 3)));
      normals.push_back(SbVec3f(0.0f, 0.0f, 1.0f));
      const int current = vertex + 1;
      const int next = ((vertex + 1) % triangleCount) + 1;
      faceIndices.push_back(0);
      faceIndices.push_back(current);
      faceIndices.push_back(next);
      faceIndices.push_back(-1);
      edgeIndices.push_back(current);
      edgeIndices.push_back(next);
      edgeIndices.push_back(-1);
    }
    part.coordinates->point.setValues(0, static_cast<int>(positions.size()),
                                      positions.data());
    part.normals->vector.setValues(0, static_cast<int>(normals.size()),
                                   normals.data());
    part.faces->coordIndex.setValues(0, static_cast<int>(faceIndices.size()),
                                     faceIndices.data());
    part.edges->coordIndex.setValues(0, static_cast<int>(edgeIndices.size()),
                                     edgeIndices.data());
    if (mutations) mutations->definitionCoordinates.push_back(part.coordinates);
  }

  SoNormalBinding * normalBinding = new SoNormalBinding;
  normalBinding->value = SoNormalBinding::PER_VERTEX_INDEXED;
  root->addChild(normalBinding);
  std::vector<SoSeparator *> definitionBranches(
    static_cast<size_t>(definitionCount));
  for (int definition = 0; definition < definitionCount; ++definition) {
    SoSeparator * branch = new SoSeparator;
    branch->renderCaching = SoSeparator::OFF;
    if (kind != WorkloadKind::SharedAssemblyExpanded) {
      const AssemblyPart & part = parts[static_cast<size_t>(definition)];
      branch->addChild(part.coordinates);
      branch->addChild(part.normals);
    }
    definitionBranches[static_cast<size_t>(definition)] = branch;
    root->addChild(branch);
  }
  const int occurrencesPerDefinition =
    (occurrenceCount + definitionCount - 1) / definitionCount;
  const int layoutSlots = occurrencesPerDefinition * definitionCount;
  const int columns = static_cast<int>(std::ceil(std::sqrt(
    static_cast<double>(layoutSlots))));
  const int rows = (layoutSlots + columns - 1) / columns;
  const float spacing = 1.15f;
  const SbColor palette[] = {
    SbColor(0.32f, 0.62f, 0.78f),
    SbColor(0.42f, 0.72f, 0.48f),
    SbColor(0.76f, 0.55f, 0.30f),
    SbColor(0.65f, 0.45f, 0.72f),
    SbColor(0.30f, 0.70f, 0.68f),
    SbColor(0.78f, 0.43f, 0.45f),
    SbColor(0.58f, 0.62f, 0.34f),
    SbColor(0.48f, 0.52f, 0.68f)
  };
  for (int occurrence = 0; occurrence < occurrenceCount; ++occurrence) {
    SoSeparator * instance = new SoSeparator;
    instance->renderCaching = SoSeparator::OFF;
    const int definition = std::min(definitionCount - 1,
      occurrence / occurrencesPerDefinition);
    const int definitionOccurrence = occurrence % occurrencesPerDefinition;
    // Keep traversal grouped by definition for retained batching, but place
    // definitions next to each other so ownership is visible in the grid.
    const int layoutIndex = definitionOccurrence * definitionCount + definition;
    const int layoutRow = layoutIndex / columns;
    const int rowWidth = std::min(columns, layoutSlots - layoutRow * columns);
    SoTranslation * placement = new SoTranslation;
    placement->translation.setValue(
      (static_cast<float>(layoutIndex % columns) -
       static_cast<float>(rowWidth - 1) * 0.5f) * spacing,
      (static_cast<float>(layoutRow) -
       static_cast<float>(rows - 1) * 0.5f) * spacing,
      -0.002f * static_cast<float>(occurrence % 7));
    instance->addChild(placement);
    if (mutations) mutations->transforms.push_back(placement);
    SoMaterial * material = new SoMaterial;
    material->diffuseColor = palette[definition % 8];
    if (mutations) mutations->materials.push_back(material);
    SoMaterial * edgeMaterial = new SoMaterial;
    edgeMaterial->diffuseColor.setValue(0.10f, 0.11f, 0.14f);
    SoCoordinate3 * occurrenceCoordinates = addAssemblyGeometry(instance, kind,
      parts[static_cast<size_t>(definition)], material, edgeMaterial);
    if (mutations) mutations->coordinates.push_back(occurrenceCoordinates);
    definitionBranches[static_cast<size_t>(definition)]->addChild(instance);
  }
}

SoSeparator * makeScene(WorkloadKind kind, int drawCount,
                        SoOrthographicCamera *& camera,
                        SceneMutationHandles * mutations)
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  root->renderCaching = SoSeparator::OFF;
  camera = new SoOrthographicCamera;
  camera->ref();
  camera->position.setValue(0.0f, 0.0f, 10.0f);
  camera->height = 24.0f;
  camera->nearDistance = 0.1f;
  camera->farDistance = 100.0f;
  camera->focalDistance = 10.0f;
  SoLightModel * lightModel = new SoLightModel;
  lightModel->model = kind == WorkloadKind::FeatureRich
    ? SoLightModel::PHONG : SoLightModel::BASE_COLOR;
  root->addChild(lightModel);
  if (kind == WorkloadKind::FeatureRich) {
    SoDirectionalLight * light = new SoDirectionalLight;
    light->direction.setValue(0.0f, 0.0f, -1.0f);
    root->addChild(light);
  }
  SoMaterial * defaultMaterial = new SoMaterial;
  defaultMaterial->diffuseColor.setValue(0.3f, 0.7f, 1.0f);
  root->addChild(defaultMaterial);

  if (isAssemblyWorkload(kind)) {
    const int definitions = assemblyDefinitionCount(drawCount);
    const int occurrencesPerDefinition =
      (drawCount + definitions - 1) / definitions;
    const int layoutSlots = occurrencesPerDefinition * definitions;
    const int columns = static_cast<int>(std::ceil(std::sqrt(
      static_cast<double>(layoutSlots))));
    const int rows = (layoutSlots + columns - 1) / columns;
    camera->height = std::max(8.0f, rows * 1.15f + 1.0f);
    populateAssemblyScene(root, kind, drawCount, mutations);
    return root;
  }

  const SbVec3f triangle[] = {
    SbVec3f(-0.42f, -0.42f, 0.0f),
    SbVec3f(0.42f, -0.42f, 0.0f),
    SbVec3f(0.0f, 0.42f, 0.0f)
  };
  const int columns = static_cast<int>(std::ceil(std::sqrt(
    static_cast<double>(drawCount))));
  const int rows = (drawCount + columns - 1) / columns;
  SoCoordinate3 * sharedCoordinates = nullptr;
  SoNormal * sharedNormals = nullptr;
  SoNormalBinding * sharedNormalBinding = nullptr;
  SoTextureCoordinate2 * sharedTexcoords = nullptr;
  SoTexture2 * sharedTexture = nullptr;
  SoMaterial * litMaterial = nullptr;
  SoMaterial * vertexMaterial = nullptr;
  SoMaterialBinding * vertexBinding = nullptr;
  SoMaterial * transparentMaterial = nullptr;
  SoFaceSet * sharedFace = nullptr;
  if (kind == WorkloadKind::FeatureRich) {
    sharedCoordinates = new SoCoordinate3;
    sharedCoordinates->point.setValues(0, 3, triangle);
    const SbVec3f normals[] = {
      SbVec3f(0.0f, 0.0f, 1.0f), SbVec3f(0.0f, 0.0f, 1.0f),
      SbVec3f(0.0f, 0.0f, 1.0f)
    };
    sharedNormals = new SoNormal;
    sharedNormals->vector.setValues(0, 3, normals);
    sharedNormalBinding = new SoNormalBinding;
    sharedNormalBinding->value = SoNormalBinding::PER_VERTEX;
    const SbVec2f textureCoordinates[] = {
      SbVec2f(0.0f, 0.0f), SbVec2f(1.0f, 0.0f), SbVec2f(0.5f, 1.0f)
    };
    sharedTexcoords = new SoTextureCoordinate2;
    sharedTexcoords->point.setValues(0, 3, textureCoordinates);
    const unsigned char texels[] = {
      220, 80, 40, 255, 40, 180, 220, 255,
      40, 180, 220, 255, 220, 80, 40, 255
    };
    sharedTexture = new SoTexture2;
    sharedTexture->image.setValue(SbVec2s(2, 2), 4, texels);
    litMaterial = new SoMaterial;
    litMaterial->diffuseColor.setValue(0.65f, 0.72f, 0.85f);
    const SbColor vertexColors[] = {
      SbColor(1.0f, 0.2f, 0.2f), SbColor(0.2f, 1.0f, 0.2f),
      SbColor(0.2f, 0.2f, 1.0f)
    };
    vertexMaterial = new SoMaterial;
    vertexMaterial->diffuseColor.setValues(0, 3, vertexColors);
    vertexBinding = new SoMaterialBinding;
    vertexBinding->value = SoMaterialBinding::PER_VERTEX;
    transparentMaterial = new SoMaterial;
    transparentMaterial->diffuseColor.setValue(0.65f, 0.72f, 0.85f);
    transparentMaterial->transparency = 0.45f;
    sharedFace = new SoFaceSet;
    sharedFace->numVertices.set1Value(0, 3);
  }
  for (int i = 0; i < drawCount; ++i) {
    SoSeparator * draw = new SoSeparator;
    draw->renderCaching = SoSeparator::OFF;
    SoTranslation * translation = new SoTranslation;
    const float x = kind == WorkloadKind::DensePicking ? 0.0f :
      (static_cast<float>(i % columns) -
       static_cast<float>(columns - 1) * 0.5f) * 1.05f;
    const float y = kind == WorkloadKind::DensePicking ? 0.0f :
      (static_cast<float>(i / columns) -
       static_cast<float>(rows - 1) * 0.5f) * 1.05f;
    const float z = kind == WorkloadKind::Transparency
      ? -static_cast<float>(i % 32) * 0.01f
      : (kind == WorkloadKind::DensePicking
         ? -static_cast<float>(i) * 0.001f : 0.0f);
    translation->translation.setValue(x, y, z);
    draw->addChild(translation);
    if (mutations) mutations->transforms.push_back(translation);

    if (kind == WorkloadKind::FeatureRich) {
      const int group = i < drawCount * 2 / 5 ? 0
        : (i < drawCount * 3 / 5 ? 1
          : (i < drawCount * 4 / 5 ? 2 : 3));
      if (group == 2) {
        draw->addChild(vertexMaterial);
        draw->addChild(vertexBinding);
      }
      else {
        draw->addChild(group == 3 ? transparentMaterial : litMaterial);
      }
      if (group == 1) {
        draw->addChild(sharedTexture);
        draw->addChild(sharedTexcoords);
      }
      draw->addChild(sharedCoordinates);
      draw->addChild(sharedNormals);
      draw->addChild(sharedNormalBinding);
      draw->addChild(sharedFace);
    }
    else if (kind != WorkloadKind::ManyDraws) {
      SoMaterial * material = new SoMaterial;
      const float value = static_cast<float>((i * 17) % 101) / 100.0f;
      material->diffuseColor.setValue(0.2f + value * 0.8f,
                                      0.9f - value * 0.7f,
                                      0.3f + value * 0.5f);
      if (kind == WorkloadKind::Transparency) material->transparency = 0.35f;
      draw->addChild(material);
      if (mutations) mutations->materials.push_back(material);
    }
    if (kind != WorkloadKind::FeatureRich) {
      SoCoordinate3 * coordinates = new SoCoordinate3;
      coordinates->point.setValues(0, 3, triangle);
      if (mutations) mutations->coordinates.push_back(coordinates);
      SoFaceSet * face = new SoFaceSet;
      face->numVertices.set1Value(0, 3);
      draw->addChild(coordinates);
      draw->addChild(face);
    }
    root->addChild(draw);
  }
  return root;
}

} // namespace coin_test
