/*
 * Retained alpha-test comparison helper.
 *
 * The caller supplies the alpha whose coverage semantics apply to the
 * producer. This module performs only the comparison; it does not decide
 * pass ordering, blending, or which producer alpha should be tested.
 */

const int COIN_ALPHA_TEST_NONE = 0;
const int COIN_ALPHA_TEST_NEVER = 1;
const int COIN_ALPHA_TEST_ALWAYS = 2;
const int COIN_ALPHA_TEST_LESS = 3;
const int COIN_ALPHA_TEST_LEQUAL = 4;
const int COIN_ALPHA_TEST_EQUAL = 5;
const int COIN_ALPHA_TEST_GEQUAL = 6;
const int COIN_ALPHA_TEST_GREATER = 7;
const int COIN_ALPHA_TEST_NOTEQUAL = 8;

bool coin_material_alpha_test_pass(float alpha, int function,
                                   float reference)
{
  if (function == COIN_ALPHA_TEST_NEVER) return false;
  if (function == COIN_ALPHA_TEST_ALWAYS) return true;
  if (function == COIN_ALPHA_TEST_LESS) return alpha < reference;
  if (function == COIN_ALPHA_TEST_LEQUAL) return alpha <= reference;
  if (function == COIN_ALPHA_TEST_EQUAL) return alpha == reference;
  if (function == COIN_ALPHA_TEST_GEQUAL) return alpha >= reference;
  if (function == COIN_ALPHA_TEST_GREATER) return alpha > reference;
  if (function == COIN_ALPHA_TEST_NOTEQUAL) return alpha != reference;
  return true;
}
