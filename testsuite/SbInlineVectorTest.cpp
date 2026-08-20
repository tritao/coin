#include <Inventor/SbInlineVector.h>

#include <utility>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main()
{
  SbInlineVector<int, 2> values;
  CHECK(values.empty());

  values.push_back(10);
  values.push_back(20);
  CHECK(values.size() == 2);
  CHECK(values[0] == 10 && values[1] == 20);
  const int * inlineData = values.data();

  values.push_back(30);
  CHECK(values.size() == 3);
  CHECK(values.data() != inlineData);
  CHECK(values[0] == 10 && values[1] == 20 && values[2] == 30);

  SbInlineVector<int, 2> copy = values;
  values[0] = 40;
  CHECK(copy[0] == 10);

  SbInlineVector<int, 2> moved = std::move(copy);
  CHECK(moved.size() == 3);
  CHECK(moved[0] == 10 && moved[1] == 20 && moved[2] == 30);

  values.clear();
  CHECK(values.empty());
  values.push_back(50);
  CHECK(values.size() == 1 && values[0] == 50);

  int sum = 0;
  for (int value : moved) sum += value;
  CHECK(sum == 60);

  moved.resize(5);
  CHECK(moved.size() == 5);
  moved.resize(1);
  CHECK(moved.size() == 1 && moved[0] == 10);
  moved.resize(0);
  CHECK(moved.empty());
  moved.push_back(60);
  CHECK(moved.size() == 1 && moved[0] == 60);
  return 0;
}
