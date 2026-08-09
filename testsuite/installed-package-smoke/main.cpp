#include <Inventor/SoDB.h>
#include <Inventor/system/gl.h>

int
main()
{
  SoDB::init();
  SoDB::finish();
  return 0;
}
