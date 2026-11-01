#define NV_ANDROID 1
#include "../src/minipool.c"

static int matches(const char *input, const char *expected_code, const char *expected_tag) {
  char code[17] = "", tag[5] = "";
  return normalize_code(input, code, tag) && !strcmp(code, expected_code) && !strcmp(tag, expected_tag);
}

int main(void) {
  if (!matches("ABCD-EFGH-JKMN-PQRS", "EFGHJKMNPQRS", "ABCD")) return 1;
  if (!matches("abcd efgh jkmn pqrs", "EFGHJKMNPQRS", "ABCD")) return 2;
  if (!matches("ABCD-EFGH", "ABCDEFGH", "")) return 3;
  if (matches("ABCD-EFGH-JKMN", "", "")) return 4;
  if (matches("ABCD-EFGH-JKMN-PQRI", "", "")) return 5;
  return 0;
}
