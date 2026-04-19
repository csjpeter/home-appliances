#include "core/config.h"
#include "core/logger.h"
#include "domain/appliance_service.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Használat: %s <parancs> [opciók]\n"
        "\n"
        "Parancsok:\n"
        "  list       Hálózati scan és készülékek listázása\n"
        "  version    Verzió kiírása\n"
        "  help       Súgó megjelenítése\n",
        prog);
}

static int cmd_list(const Config *cfg)
{
    char broadcast[32];
    /* Egyszerű broadcast: hálózat utolsó oktetét .255-re cseréljük */
    const char *slash = strchr(cfg->network, '/');
    if (!slash) {
        fprintf(stderr, "Érvénytelen hálózati cím: %s\n", cfg->network);
        return -1;
    }
    int a, b, c;
    sscanf(cfg->network, "%d.%d.%d.", &a, &b, &c);
    snprintf(broadcast, sizeof(broadcast), "%d.%d.%d.255", a, b, c);

    ApplianceList list = {0};
    if (appliance_service_discover(broadcast, cfg->discovery_timeout_ms, &list) != 0) {
        fprintf(stderr, "Hiba a felfedezés során.\n");
        return -1;
    }

    appliance_service_print(&list);
    appliance_list_free(&list);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("home-appliances %s\n", HOME_APPLIANCES_VERSION);
        return 0;
    }

    logger_init(NULL, LOG_INFO);

    Config cfg = {0};
    if (config_load(&cfg) != 0) {
        fprintf(stderr, "Konfiguráció betöltési hiba.\n");
        return 1;
    }

    int ret = 0;
    if (strcmp(argv[1], "list") == 0) {
        ret = cmd_list(&cfg);
    } else {
        fprintf(stderr, "Ismeretlen parancs: %s\n", argv[1]);
        print_usage(argv[0]);
        ret = 1;
    }

    logger_close();
    return ret == 0 ? 0 : 1;
}
