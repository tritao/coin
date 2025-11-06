// include/Inventor/actions/SoModernRenderAction.h

#ifndef COIN_SOMODERNRENDERACTION_H
#define COIN_SOMODERNRENDERACTION_H

#include <Inventor/actions/SoAction.h>
#include <Inventor/SbViewportRegion.h>
#include "rendering/SoModernIR.h" // internal for now

class SoPath;
class SoPathList;
class SoCamera;

/*!
  \class SoModernRenderAction SoModernRenderAction.h
  \brief New-style render action that builds an API-agnostic draw list (IR).
*/
class COIN_DLL_API SoModernRenderAction : public SoAction {
  typedef SoAction inherited;
  SO_ACTION_HEADER(SoModernRenderAction);

public:
  static void initClass(void);

  SoModernRenderAction(const SbViewportRegion & vp);
  virtual ~SoModernRenderAction();

  void setViewportRegion(const SbViewportRegion & vp);
  const SbViewportRegion & getViewportRegion(void) const { return this->vpRegion; }

  void setCamera(SoCamera * camera);
  SoCamera * getCamera(void) const { return this->camera; }

  // Standard entry points, mirroring SoGLRenderAction
  virtual void apply(SoNode * root);
  virtual void apply(SoPath * path);
  virtual void apply(const SoPathList & pathlist, SbBool obeysrules = FALSE);

  // Access to generated IR for the current frame
  const SoDrawList & getDrawList(void) const { return this->drawlist; }

  // (later) hooks for backend:
  // uint32_t getCacheContext(void) const;
  // void setCacheContext(uint32_t ctx);

protected:
  virtual void beginTraversal(SoNode * node);
  virtual void endTraversal(SoNode * node);

  // static dispatchers installed in SoAction's method table
  static void renderNode(SoAction * action, SoNode * node);
  static void renderShape(SoAction * action, SoNode * node);

  // For shapes to add commands
  friend class SoShape;
  SoDrawList & getMutableDrawList() { return this->drawlist; }

private:
  SbViewportRegion vpRegion;
  SoCamera *       camera;

  SoDrawList       drawlist;
};

#endif // COIN_SOMODERNRENDERACTION_H
