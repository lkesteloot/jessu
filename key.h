
#ifndef __KEY_H__
#define __KEY_H__

#define MAX_KEY_LENGTH          128
#define MIN_NAME_LENGTH         5

bool key_is_valid(char const *key);
bool make_key(char const *name, char key[MAX_KEY_LENGTH], bool is_grid);

#endif // __KEY_H__
