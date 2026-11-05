#define NV_ANDROID 1
#include "../src/minipool.c"

static int is_key(MiniMeta *meta, const char *expected) {
  char key[65] = "";
  return minipool_work_key(meta, key) && !strcmp(key, expected);
}

int main(void) {
  MiniMeta meta = {0};
  const char mrRobotS3E1[] = "4708f4853fffa33c2135f01e785bd8e355ceb8afd2ce71c2ba8098e4737650ee";
  snprintf(meta.imdb, sizeof meta.imdb, "%s", "tt4158110");
  meta.temporada = 3; meta.episodio = 1;
  if (!is_key(&meta, mrRobotS3E1)) return 1;

  // Some catalog paths preserve the episode suffix in imdb. It must hash to
  // the same base IMDb + season + episode key used by the server index.
  snprintf(meta.imdb, sizeof meta.imdb, "%s", "tt4158110:3:1");
  if (!is_key(&meta, mrRobotS3E1)) return 2;

  // Recover T/E from an episode-qualified IMDb ID when separate fields are 0.
  meta.temporada = 0; meta.episodio = 0;
  if (!is_key(&meta, mrRobotS3E1)) return 3;

  // Season zero is valid for a numbered special.
  snprintf(meta.imdb, sizeof meta.imdb, "%s", "tt4158110");
  meta.temporada = 0; meta.episodio = 1;
  if (!is_key(&meta, "b9f6854aea756c361928f86107690828d7f304831e17cebf459da5fe85d354a4")) return 4;

  meta.episodio = 0;
  if (!is_key(&meta, "b98c5f634ced77168a3ee31575a20cbb7de84efaf9910396ef1f37104d53a61e")) return 5;

  // Films use a separate domain tag and need only a valid IMDb identifier.
  snprintf(meta.imdb, sizeof meta.imdb, "%s", "tt1234567");
  meta.temporada = 0; meta.episodio = 0;
  if (!is_key(&meta, "bcef32d6b68d5295ecd712126d448f131d2f1ab3567b46049e614f219fe0f660")) return 6;

  // A film without IMDb identity cannot be assigned a work key.
  meta.imdb[0] = 0;
  if (minipool_work_key(&meta, (char[65]){0})) return 7;
  return 0;
}
