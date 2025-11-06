// src/actions/SoModernRenderAction.cpp

#include <Inventor/actions/SoModernRenderAction.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/elements/SoViewportRegionElement.h>

SO_ACTION_SOURCE(SoModernRenderAction);

// initClass: register with type system & action method table
void
SoModernRenderAction::initClass(void)
{
  SO_ACTION_INIT_CLASS(SoModernRenderAction, SoAction);

  // Install a default method for all nodes: renderNode()
  SoAction::addMethod(SoNode::getClassTypeId(),
                      SoModernRenderAction::renderNode);

  // Install specialized method for shapes: renderShape()
  SoAction::addMethod(SoShape::getClassTypeId(),
                      SoModernRenderAction::renderShape);
}

SoModernRenderAction::SoModernRenderAction(const SbViewportRegion & vp)
  : SoAction(), vpRegion(vp), camera(NULL)
{
  SO_ACTION_CONSTRUCTOR(SoModernRenderAction);
}

SoModernRenderAction::~SoModernRenderAction()
{
}

void
SoModernRenderAction::setViewportRegion(const SbViewportRegion & vp)
{
  this->vpRegion = vp;
}

void
SoModernRenderAction::setCamera(SoCamera * cam)
{
  this->camera = cam;
}

void
SoModernRenderAction::apply(SoNode * root)
{
  this->drawlist.clear();
  inherited::apply(root);
}

void
SoModernRenderAction::apply(SoPath * path)
{
  this->drawlist.clear();
  inherited::apply(path);
}

void
SoModernRenderAction::apply(const SoPathList & pathlist, SbBool obeysrules)
{
  this->drawlist.clear();
  inherited::apply(pathlist, obeysrules);
}

void
SoModernRenderAction::beginTraversal(SoNode * node)
{
  // Set viewport in state so nodes / elements can see it
  SoViewportRegionElement::set(this->state, this->vpRegion);
  // Later: set camera, view/projection matrices, etc.
  inherited::beginTraversal(node);
}

void
SoModernRenderAction::endTraversal(SoNode * node)
{
  // Could do post-processing on drawlist here (sorting, etc.),
  // but usually the backend will do that.
  inherited::endTraversal(node);
}

// Default method for generic nodes: behave as in a normal SoAction
void
SoModernRenderAction::renderNode(SoAction * a, SoNode * node)
{
  // For most non-shape nodes, the standard doAction() already
  // adjusts SoState elements (material, transform, etc.)
  node->doAction(a);
}

// Specialized method for SoShape and subclasses
void
SoModernRenderAction::renderShape(SoAction * a, SoNode * node)
{
  SoModernRenderAction * action = static_cast<SoModernRenderAction *>(a);
  SoShape * shape = static_cast<SoShape *>(node);

  // New entry point we’ll add on SoShape (see below)
  shape->render(action);
}
