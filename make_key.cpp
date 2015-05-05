
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "key.h"

int
main(int argc, char *argv[])
{
    bool grid = false;

    if (argc > 1 && strcmp(argv[1], "-grid") == 0) {
        grid = true;
        argc--;
        argv++;
    }

    if (argc != 2) {
        printf("Usage: make_key [-grid] name\n");
        exit(EXIT_FAILURE);
    }

    char name[MAX_KEY_LENGTH];
    char key[MAX_KEY_LENGTH];

    strcpy(name, argv[1]);

    for (char *c = name; *c != '\0'; c++) {
        if (*c == ' ') {
            *c = '-';
        }
    }

    if (!make_key(name, key, grid)) {
        printf("Cannot make key\n");
        return 1;
    } else {
        printf("%s\n", key);
    }

    return 0;
}
