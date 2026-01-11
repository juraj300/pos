#include "common/map.h"
#include <stdio.h>

int load_map_obstacles(int obstacles[GRID_H][GRID_W], const char *path) {
      FILE *f = fopen(path, "r");
      if (!f) return -1;

    for (int y = 0; y < GRID_H; y++) {
                 for (int x = 0; x < GRID_W; x++) {
                     int c = fgetc(f);
                     while (c == '\r') c = fgetc(f);
            if (c == EOF) { fclose(f); return -1; }

            obstacles[y][x] = (c == '#') ? 1 : 0;
        }

        int c = fgetc(f);
                 while (c != '\n' && c != EOF) c = fgetc(f);
    }

    fclose(f);
    return 0;
}

