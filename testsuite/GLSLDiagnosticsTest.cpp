#include <Inventor/SoDB.h>
#include <Inventor/errors/SoDebugError.h>

#include "glue/glp.h"
#include "shaders/SoGLSLShaderObject.h"
#include "shaders/SoGLSLShaderProgram.h"
#include "support/GLTestContext.h"
#include "support/GLTestUtils.h"

#include <string>
#include <vector>

namespace {

using coin_test::check;
using coin_test::skip;

struct DiagnosticCapture {
  int warnings = 0;
  int infos = 0;
  std::vector<std::string> messages;
};

void capture_diagnostic(const SoError * error, void * data)
{
  DiagnosticCapture * capture = static_cast<DiagnosticCapture *>(data);
  if (!error->isOfType(SoDebugError::getClassTypeId())) return;

  const SoDebugError * debug = static_cast<const SoDebugError *>(error);
  if (debug->getSeverity() == SoDebugError::WARNING) ++capture->warnings;
  if (debug->getSeverity() == SoDebugError::INFO) ++capture->infos;
  capture->messages.push_back(debug->getDebugString().getString());
}

std::string messages_as_text(const DiagnosticCapture & capture)
{
  std::string result;
  for (std::vector<std::string>::const_iterator it = capture.messages.begin();
       it != capture.messages.end(); ++it) {
    if (!result.empty()) result += "\n";
    result += *it;
  }
  return result;
}

bool contains(const DiagnosticCapture & capture, const char * text)
{
  return messages_as_text(capture).find(text) != std::string::npos;
}

} // namespace

int main()
{
  SoDB::init();

  GLTestContextConfig config;
  config.profile = GLTestProfile::Core;
  config.major = 3;
  config.minor = 3;
  config.width = 16;
  config.height = 16;
  GLTestContext context;
  if (!context.initialize(config)) {
    SoDB::finish();
    return skip("core OpenGL test context is unavailable");
  }
  if (!context.makeCurrent()) {
    SoDB::finish();
    return skip("core OpenGL test context could not be made current");
  }

  const cc_glglue * glue = cc_glglue_instance(context.contextId());
  if (glue == NULL || glue->contextid == 0) {
    SoDB::finish();
    return skip("GL glue instance is unavailable");
  }

  SoErrorCB * previousCallback = SoDebugError::getHandlerCallback();
  void * previousData = SoDebugError::getHandlerData();
  int result = 1;

  do {
    DiagnosticCapture compileCapture;
    SoDebugError::setHandlerCallback(capture_diagnostic, &compileCapture);

    SoGLSLShaderObject brokenShader(glue->contextid);
    brokenShader.setShaderType(SoGLShaderObject::VERTEX);
    brokenShader.sourceHint = "broken.vert";
    brokenShader.load("#version 330 core\n"
                      "void main() { this is not valid GLSL; }\n");

    if (!check(!brokenShader.isLoaded(),
               "invalid shader unexpectedly compiled")) break;
    if (!check(compileCapture.warnings > 0,
               "shader compilation failure was not reported as a warning")) break;
    if (!check(contains(compileCapture, "vertex shader"),
               "shader diagnostic omitted its stage")) break;
    if (!check(contains(compileCapture, "broken.vert"),
               "shader diagnostic omitted its source identity")) break;
    if (!check(contains(compileCapture, "failed to compile"),
               "shader diagnostic omitted its failure description")) break;

    DiagnosticCapture linkCapture;
    SoDebugError::setHandlerCallback(capture_diagnostic, &linkCapture);

    SoGLSLShaderObject vertexShader(glue->contextid);
    vertexShader.setShaderType(SoGLShaderObject::VERTEX);
    vertexShader.sourceHint = "link.vert";
    vertexShader.load("#version 330 core\n"
                      "out vec3 varying_color;\n"
                      "void main() { varying_color = vec3(1.0); "
                      "gl_Position = vec4(0.0); }\n");

    SoGLSLShaderObject fragmentShader(glue->contextid);
    fragmentShader.setShaderType(SoGLShaderObject::FRAGMENT);
    fragmentShader.sourceHint = "link.frag";
    fragmentShader.load("#version 330 core\n"
                        "in vec4 varying_color;\n"
                        "out vec4 color;\n"
                        "void main() { color = varying_color; }\n");

    if (!check(vertexShader.isLoaded() && fragmentShader.isLoaded(),
               "link test shaders failed to compile")) break;

    {
      SoGLSLShaderProgram program;
      program.addShaderObject(&vertexShader);
      program.addShaderObject(&fragmentShader);
      program.enable(glue);
    }

    if (!check(linkCapture.warnings > 0,
               "program link failure was not reported as a warning")) break;
    if (!check(contains(linkCapture, "failed to link"),
               "program diagnostic omitted its failure description")) break;
    if (!check(contains(linkCapture, "vertex shader=link.vert"),
               "program diagnostic omitted the vertex source stage")) break;
    if (!check(contains(linkCapture, "fragment shader=link.frag"),
               "program diagnostic omitted the fragment source stage")) break;

    result = 0;
  } while (false);

  SoDebugError::setHandlerCallback(previousCallback, previousData);
  SoDB::finish();
  return result;
}
