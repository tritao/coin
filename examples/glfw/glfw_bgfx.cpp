/* Simple example that demonstrates how to render with Coin3D and GLFW.
 * 
 * Note: This example uses GLFW, so you do not need to have any of the 
 * SoGUI libraries installed.
 */

#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoSceneManager.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoRotationXYZ.h>
#include <Inventor/nodes/SoImage.h>

#include <bx/bx.h>
#include <bgfx/bgfx.h>

#include <cstdlib>

static bool useEGL = true;
static bool useGL = false;
static bool useGLES = false;
static bool useGLCompatibilityProfile = false;

#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>

#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_OSX
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

// ----------------------------------------------------------------------

GLFWwindow* window;
SoSceneManager* sceneManager;

static char scene_iv[] = {
  "#Inventor V2.1 ascii\n\n"
  "Separator {\n"
  "  ShaderProgram {\n"
  "    shaderObject [\n"
  "      VertexShader {\n"
  "        sourceProgram \"perpixel_vertex.glsl\"\n"
  "      }\n"
  "      FragmentShader {\n"
  "        sourceProgram \"perpixel_fragment.glsl\"\n"
  "      }\n"
  "    ]\n"
  "  }\n"
  "  Cube { }\n"
  "}\n"
};

// ----------------------------------------------------------------------

typedef struct cc_glglue cc_glglue;
const cc_glglue * sogl_glue_instance(const SoState * state);
SbBool sogl_compatibility_profile(const SoState * state);

// Redraw on scenegraph changes.
void redrawCallback(void * user, SoSceneManager * manager)
{
#if !defined(COIN_USE_BGFX_RENDERER)
  const SoState* state = manager->getGLRenderAction()->getState();
  if (sogl_compatibility_profile(state))
  {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
  }
#endif

  sceneManager->render();

  if (useGL || useGLES) {
    glfwSwapBuffers(window);
  }
}

// Redraw on expose events.
void exposeCallback(void)
{
#if !defined(COIN_USE_BGFX_RENDERER)
  const SoState* state = sceneManager->getGLRenderAction()->getState();
  if (sogl_compatibility_profile(state))
  {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
  }
#endif

  sceneManager->render();

  if (useGL || useGLES) {
    glfwSwapBuffers(window);
  }
}

// Reconfigure on changes to window dimensions.
void framebufferSizeCallback(GLFWwindow* window,int w, int h)
{
  sceneManager->setWindowSize(SbVec2s(w, h));
  //sceneManager->setSize(SbVec2s(w, h));
  //sceneManager->setViewportRegion(SbViewportRegion(w, h));
  sceneManager->scheduleRedraw();
}

// Process the internal Coin queues when idle. Necessary to get the
// animation to work.
void idleCallback(void)
{
  SoDB::getSensorManager()->processTimerQueue();
  SoDB::getSensorManager()->processDelayQueue(TRUE);
}

// ----------------------------------------------------------------------

SoSeparator* createScene();
unsigned char * img;
const int IMGWIDTH = 256;
const int IMGHEIGHT = 256;
static void mandel(double sr, double si, double width, double height,
        int bwidth, int bheight, int mult, unsigned char * bmp, int n);

int main(void)
{
    glfwSetErrorCallback([](int error, const char* description) {
      fprintf(stderr, "Error: %s\n", description);
    });

    SoDB::init();

    if (!glfwInit())
        return EXIT_FAILURE;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

/*
    if (useEGL) {
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    }

    // Needs at least OpenGL 4.3 for KHR_debug to be available (on Linux)
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    if (useGLES) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    }

    if (useGLCompatibilityProfile) {
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    }
*/

    //glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);

    window = glfwCreateWindow(1920, 1080, "Coin3D", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // Initialize bgfx after window is created and before main loop
    bgfx::Init init_object;
#if BX_PLATFORM_LINUX
    init_object.platformData.ndt = glfwGetX11Display();
    init_object.platformData.nwh = reinterpret_cast<void *>(glfwGetX11Window(window));
#elif BX_PLATFORM_OSX
    init_object.platformData.nwh = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
    init_object.platformData.nwh = glfwGetWin32Window(window);
#endif
    //init_object.type = bgfx::RendererType::OpenGL;

    int32_t width, height;
    glfwGetWindowSize(window, &width, &height);
    init_object.resolution.width = static_cast<uint32_t>(width);
    init_object.resolution.height = static_cast<uint32_t>(height);
    init_object.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init_object))
    {
        return 1;
    }

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
  
    if (useGL || useGLES) {
      glfwMakeContextCurrent(window);
    }

    img = new unsigned char[IMGWIDTH * IMGHEIGHT];
    mandel(-0.5, 0.6, 0.025, 0.025, IMGWIDTH, IMGHEIGHT, 1, img, 256);

    SoSeparator* root = createScene();

    //int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    framebufferSizeCallback(window, width, height);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        idleCallback();
        exposeCallback();
    }

    root->unref();
    delete sceneManager;
    delete[] img;
  
    bgfx::shutdown();
    glfwTerminate();
    return 0;
}

// ----------------------------------------------------------------------

SoSeparator* createScene()
{
    auto root = new SoSeparator;
    root->ref();
    SoPerspectiveCamera * camera = new SoPerspectiveCamera;
    root->addChild(camera);
    root->addChild(new SoDirectionalLight);


    SoInput in;
    in.setBuffer(scene_iv, strlen(scene_iv));
    
#if 0
    SoSeparator* result = SoDB::readAll(&in);
    if (result == nullptr) {
      fprintf(stderr, "Could not load scene graph from text");
      exit(EXIT_FAILURE); 
    }
    root->addChild(result);
#endif

#if 0
    SoImage * nimage = new SoImage;
    nimage->vertAlignment = SoImage::HALF;
    nimage->horAlignment = SoImage::CENTER;
    nimage->image.setValue(SbVec2s(IMGWIDTH, IMGHEIGHT), 1, img);
    root->addChild(nimage);
#endif

    root->addChild(new SoCube);

    sceneManager = new SoSceneManager;
    sceneManager->setRenderCallback(redrawCallback, (void *)1);
    sceneManager->setBackgroundColor(SbColor(0.8f, 0.2f, 0.2f));
    sceneManager->activate();
    camera->viewAll(root, sceneManager->getViewportRegion());
    sceneManager->setSceneGraph(root);

    return root;
}

static void mandel(double sr, double si, double width, double height,
        int bwidth, int bheight, int mult, unsigned char * bmp, int n)
{
  double zr, zr_old, zi, cr, ci;
  int w;

  for (int y=0; y<bheight; y++)
    for (int x=0; x<bwidth; x++) {
      cr = ((double)(x)/(double)(bwidth))*width+sr;
      ci = ((double)(y)/(double)(bheight))*height+si;
      zr = zi = 0.0;
      for (w = 0; (w < n) && (zr*zr + zi*zi)<n; w++) {
        zr_old = zr;
        zr = zr*zr - zi*zi + cr;
        zi = 2*zr_old*zi + ci;
      }
      bmp[y*bwidth+x] = w*mult;
    }
}