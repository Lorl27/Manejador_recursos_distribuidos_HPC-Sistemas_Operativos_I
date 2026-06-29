#include "resource_manager_agent.h"

int main(int argc, char *argv[]) {
    //  Validamos que el usuario nos pase los 4 parámetros que necesitamos
    if (argc != 5) {
        printf("[MAIN-WARNING] Faltan parámetros de configuración.\n");
        printf("[MAIN] Uso: %s <IP_LAN> <PUERTO_PUBLICO> <PUERTO_LOCAL_ERLANG> \"<RECURSOS>\" \n", argv[0]);
        printf("[MAIN] Ejemplo: %s 192.168.1.50 8100 9000 \"cpu:4 mem:8192 gpu:1\"\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *mi_ip_lan = argv[1];
    int mi_puerto_publico = atoi(argv[2]); //atoi convierte el texto a int
    int mi_puerto_local = atoi(argv[3]);
    char *mis_recursos = argv[4];

    printf("[MAIN] Iniciando Agente en IP: %s | Puerto: %d\n", mi_ip_lan, mi_puerto_publico);
    printf("[MAIN] Puerto Local (Erlang): %d\n", mi_puerto_local);
    printf("[MAIN] Recursos locales: %s\n", mis_recursos);

    iniciar_event_loop(mi_ip_lan, mi_puerto_publico, mi_puerto_local, mis_recursos);

    return 0;
}