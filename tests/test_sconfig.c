#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "sconfig.h"

int main(int argc, char **argv) {
    sconfig_t config;
    list_t *p;

    sconfig_initialize(&config);
    sconfig_read(&config, argv[1]);
    printf("layout: %d\n", config.layout);
    list_for_each_forward(p, &config.storages) {
        storage_config_t *sc = list_entry(p, storage_config_t, list);
        printf("%d, %s\n", sc->sid, sc->root);
    }
    if (sconfig_validate(&config) == 0) {
        printf("config OK\n");
    } else {
        printf("config KO\n");
    }
    sconfig_release(&config);

    return 0;
}
