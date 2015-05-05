
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "key.h"

static void
remove_whitespace(char const **s, int *length)
{
    while (isspace(**s)) {
        (*s)++;
        (*length)--;
    }

    while (*length > 0 && isspace((*s)[*length - 1])) {
        (*length)--;
    }
}

static void
copy_to_uppercase(char *dst, char const *src)
{
    while (*src != '\0') {
        *dst++ = toupper(*src++);
    }

    *dst = '\0';
}

static unsigned int
get_checksum(char const *name, int length, bool is_grid)
{
    int checksum = 173;
    static int primes[] = { 11, 17, 31, 37, 51 };
    static const int num_primes = sizeof(primes) / sizeof(primes[0]);

    if (is_grid) {
        checksum = 199;
    }

    for (int i = 0; i < length; i++) {
        checksum = checksum*primes[i % num_primes] + toupper(name[i]);
    }

    return checksum;
}

bool
key_is_valid(char const *key)
{
    /* key is of the form "name/checksum" where "name" is some
       identifiable thing about the buyer, such as their name.
       The checksum is a number that's based on the name, a
       non-trivial hash. the case of the name is ignored and
       whitespace around the edges of the key is ignored.

       the name has to be at least MIN_NAME_LENGTH letters long.
    */

    int key_length = strlen(key);
    remove_whitespace(&key, &key_length);

    char *slash = strrchr(key, '/');
    if (slash == NULL) {
        return false;
    }

    int name_length = slash - key;
    if (name_length < MIN_NAME_LENGTH) {
        return false;
    }

    char *checksum_end;
    unsigned int key_checksum = strtoul(slash + 1, &checksum_end, 16);

    if (checksum_end != key + key_length) {
        return false;
    }

    unsigned int actual_checksum = get_checksum(key, name_length, false);

    return key_checksum == actual_checksum;
}

bool
make_key(char const *name, char key[MAX_KEY_LENGTH], bool is_grid)
{
    // returns false if it couldn't make the key

    int name_len = strlen(name);

    remove_whitespace(&name, &name_len);

    if (name_len < MIN_NAME_LENGTH || name_len > MAX_KEY_LENGTH - 15) {
        return false;
    }

    unsigned int checksum = get_checksum(name, name_len, is_grid);

    copy_to_uppercase(key, name);
    sprintf(key + strlen(key), "/%08X", checksum);

    return true;
}
