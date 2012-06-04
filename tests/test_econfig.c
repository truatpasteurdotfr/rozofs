#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>

#include "econfig.h"


int main(int argc, char **argv) {
    econfig_t config;
    //list_t *p;

    econfig_initialize(&config);
    econfig_read(&config, argv[1]);
    if (econfig_validate(&config) == 0) {
        printf("valid configuration.\n");
    } else {
        printf("invalid configuration: %s.\n", strerror(errno));
    }
    econfig_release(&config);
    return 0;
}
