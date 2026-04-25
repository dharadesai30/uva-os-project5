/* useful for "pacing" the launches of multpile jobs ...*/

#include "user.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s <seconds>\n", argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);
    if (n > 0) {
        sleep(n * 1000);
    }

    return 0;
}