#include "rendering/SoRenderPlan.h"

#include <iostream>

namespace {

bool check(bool condition, const char * message)
{
  if (!condition) std::cerr << "FAIL: " << message << std::endl;
  return condition;
}

}

int
main()
{
  SoDrawList drawlist;
  SoRenderCommand first;
  SoRenderCommand second;
  SoRenderCommand third;
  first.userData = &first;
  second.userData = &second;
  third.userData = &third;
  drawlist.addCommand(first);
  drawlist.addCommand(second);
  drawlist.addCommand(third);

  const uint32_t generation = drawlist.getGeneration();
  const void * const firstData = drawlist.getCommand(0).userData;
  const void * const secondData = drawlist.getCommand(1).userData;
  const void * const thirdData = drawlist.getCommand(2).userData;

  SoRenderPlanner planner;
  SoRenderPlan plan;
  SoDrawList empty;
  planner.build(empty, plan);
  bool result = check(plan.getNumDraws() == 0,
                      "empty DrawList did not produce an empty plan");

  planner.build(drawlist, plan);

  result = check(plan.getNumDraws() == 3,
                 "planner did not retain every command") &&
    check(plan.getDraw(0).commandIndex == 0 &&
          plan.getDraw(1).commandIndex == 1 &&
          plan.getDraw(2).commandIndex == 2,
          "planner changed insertion order") &&
    check(drawlist.getGeneration() == generation &&
          drawlist.getCommand(0).userData == firstData &&
          drawlist.getCommand(1).userData == secondData &&
          drawlist.getCommand(2).userData == thirdData,
          "planning modified the retained DrawList");

  drawlist.truncate(2);
  planner.build(drawlist, plan);
  result = check(plan.getNumDraws() == 2 &&
                 plan.getDraw(0).commandIndex == 0 &&
                 plan.getDraw(1).commandIndex == 1,
                 "planner did not rebuild from the current DrawList") && result;
  drawlist.clear();
  planner.build(drawlist, plan);
  result = check(plan.getNumDraws() == 0,
                 "rebuilding an empty DrawList left stale plan operations") &&
    result;
  drawlist.addCommand(first);
  planner.build(drawlist, plan);
  result = check(plan.getNumDraws() == 1 &&
                 plan.getDraw(0).commandIndex == 0,
                 "reusing a cleared plan did not rebuild its operations") &&
    result;

  SoDrawList transparencyDrawList;
  SoRenderCommand opaque;
  SoRenderCommand transparentNear;
  SoRenderCommand transparentFar;
  opaque.viewMatrix.makeIdentity();
  opaque.modelMatrix.makeIdentity();
  transparentNear.viewMatrix.makeIdentity();
  transparentNear.modelMatrix.makeIdentity();
  transparentFar.viewMatrix.makeIdentity();
  transparentFar.modelMatrix.makeIdentity();
  transparentNear.opacityClass = SO_OPACITY_TRANSPARENT;
  transparentFar.opacityClass = SO_OPACITY_TRANSPARENT;
  transparentFar.modelMatrix.setTranslate(SbVec3f(0.0f, 0.0f, -2.0f));
  transparencyDrawList.addCommand(opaque);
  transparencyDrawList.addCommand(transparentNear);
  transparencyDrawList.addCommand(transparentFar);
  planner.build(transparencyDrawList, plan);
  result = check(plan.getNumDraws() == 3 &&
                 plan.getDraw(0).commandIndex == 0 &&
                 plan.getDraw(1).commandIndex == 2 &&
                 plan.getDraw(2).commandIndex == 1,
                 "planner did not schedule transparent commands back-to-front") &&
    result;

  return result ? 0 : 1;
}
