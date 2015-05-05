
#include <stdio.h>

int main()
{
    int width = 720;
    int height = 720;

    FILE *f = fopen("stripped.ppm", "w");
    fprintf(f, "P6 %d %d 255\n", width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int color;
            if (y % 2 == 0) {
                color = 255;
            } else {
                color = 0;
            }

            fprintf(f, "%c%c%c", color, color, color);
        }
    }

    fclose(f);

    return 0;
}
