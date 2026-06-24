#include "resource_manager_agent.h"


//var globales para el manejo de mensajes.
char mensaje[MAX_MSG];
struct sockaddr_in broadcastAddr;

int crear_socket_broadcast() {
    int sock;
    int broadcastEnable = 1;

    //crear socket upd
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error creando socket UDP");
        exit(EXIT_FAILURE);
    }

    //hacerlo broadcast
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("Error seteando broadcast");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // configura la dirección de destino para los envíos
    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(PUERTO_BROADCAST);
    broadcastAddr.sin_addr.s_addr = inet_addr(BROADCAST_IP); 

    // Bind  - permite escuchar msg que llegan al puerto
    struct sockaddr_in recvAddr;
    memset(&recvAddr, 0, sizeof(recvAddr));
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(PUERTO_BROADCAST);
    recvAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&recvAddr, sizeof(recvAddr)) < 0) {
        perror("Error en bind del socket UDP");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;
}

void ejecutar_arranque_inicial(int epoll_fd, int sock_udp_broadcast) {
    char buffer[MAX_MSG];
    struct sockaddr_in origen;
    socklen_t len = sizeof(origen);
    struct epoll_event events[MAX_EVENTS];

    printf("[ARRANQUE] Enviando primer anuncio...\n");
    
    // Envíar anuncio inmediatamente 
    snprintf(mensaje, sizeof(mensaje), "ANNOUNCE %s %d %s", "127.0.0.1", 8888, "cpu:4");
   if (sendto(sock_udp_broadcast, mensaje, strlen(mensaje), 0, 
           (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr)) < 0) {
        perror("Error en sendto inicial");
    }

    printf("[ARRANQUE] Esperando 2 segundos para descubrir nodos...\n");

    //2s de espera.
    int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 2000); 

    for (int i = 0; i < n; i++) {
        if (events[i].data.fd == sock_udp_broadcast) {
            int nbytes = recvfrom(sock_udp_broadcast, buffer, MAX_MSG, 0, 
                                 (struct sockaddr*)&origen, &len);
            if (nbytes > 0) {
                buffer[nbytes] = '\0';
                printf("[ARRANQUE] Nodo activo descubierto: %s\n", buffer);
                //TODO pasear buffer y guardar nodo en TablaNodos
            }
        }
    }

    printf("[ARRANQUE] Fase inicial completada. Pasando a peticiones normales.\n"); // 
}

void iniciar_event_loop() {
    int socket_broadcast=crear_socket_broadcast();
    //crear_servidor_tcp_publico();
    //creaR_servidor_tcp_local();

    //conf. de timerd para los anuncios:
    int timer = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer == -1) {
        perror("Falló timerfd_create");
        exit(EXIT_FAILURE);
    }

    struct itimerspec ts;
    ts.it_interval.tv_sec = INTERVALO_SEG; // frec. repeticion
    ts.it_interval.tv_nsec = 0;
    ts.it_value.tv_sec = INTERVALO_SEG;    // cuando ocurre primer lanzamiento
    ts.it_value.tv_nsec = 0;
    timerfd_settime(timer, 0, &ts, NULL);

    //crear epoll
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("Falló epoll_create1");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev, events[MAX_EVENTS];

    // agregar socket de escucha al epoll
    ev.events = EPOLLIN;
    ev.data.fd = socket_broadcast; 
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_broadcast, &ev)==-1){
        perror("fallo epoll_ctl ADD socket broadcast");
        exit(EXIT_FAILURE);
    }

    // agregar timer al epoll
    ev.events = EPOLLIN;
    ev.data.fd = timer; // asi sabe cuando debera enviar el anuncio
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer, &ev)==-1){
            perror("fallo epoll_ctl ADD timer");
        exit(EXIT_FAILURE);
    }
    

    ejecutar_arranque_inicial(epoll_fd, socket_broadcast);

    printf("[Servidor iniciado correctamente.]\n");
    printf("Iniciando envíos periódicos de ANNOUNCE por broadcast...\n");


    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            perror("fallo epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if(fd==timer){
                uint64_t expira;
                read(timer, &expira, sizeof(expira)); // limpiar timer (leyendolo)

                // Armar y mandar el mensaje ANNOUNCE 
                snprintf(mensaje, sizeof(mensaje), "ANNOUNCE %s %d %s\n", "127.0.0.1", 8888, "cpu:4");
                sendto(socket_broadcast, mensaje, strlen(mensaje), 0, 
                       (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
            }
           else if (fd == socket_broadcast) {
                //TODO - llego algo de oto nodo- recvfrom
                

            } else {
                //TODO - accept conexiones tcp
                //TODO - mensajes cleintes ya conectados - recv send
            }
        }
    }
}