#include "resource_manager_agent.h"

//* ----- Variables globales para el manejo de mensajes y nodos. -----
char mensaje[MAX_MSG];
struct sockaddr_in srv_mensajeria_broadcast;

TablaNodos tabla_activos[MAX_NODOS];
int cantidad_nodos=0;

//*-------------------------------------------

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
    memset(&srv_mensajeria_broadcast, 0, sizeof(srv_mensajeria_broadcast));
    srv_mensajeria_broadcast.sin_family = AF_INET;
    srv_mensajeria_broadcast.sin_port = htons(PUERTO_BROADCAST);
    srv_mensajeria_broadcast.sin_addr.s_addr = inet_addr(BROADCAST_IP); 

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

void ejecutar_arranque_inicial(int epoll_fd, int socket_broadcast,char * ip, int puerto_tcp, char * recursos) {
    char buffer[MAX_MSG];
    struct sockaddr_in origen;
    socklen_t len = sizeof(origen);
    struct epoll_event events[MAX_EVENTS];

    printf("[ARRANQUE] Enviando primer anuncio...\n");
    
    // anuncio mi IP - Puerto TCP- Mis recursos disponibles
    snprintf(mensaje, sizeof(mensaje), "ANNOUNCE %s %d %s", ip, puerto_tcp, recursos);
   
    if (sendto(socket_broadcast, mensaje, strlen(mensaje), 0, (struct sockaddr*)&srv_mensajeria_broadcast, sizeof(srv_mensajeria_broadcast)) < 0) {
        perror("Error en sendto inicial");
    }

    printf("[ARRANQUE] Esperando 2 segundos para descubrir nodos...\n");

    int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 2000); 

    for (int i = 0; i < n; i++) {
        if (events[i].data.fd == socket_broadcast) {
            int nbytes = recvfrom(socket_broadcast, buffer, MAX_MSG -1, 0, (struct sockaddr*)&origen, &len);
            if (nbytes > 0) {
                buffer[nbytes] = '\0';
                printf("[ARRANQUE] Nodo activo descubierto: %s\n", buffer);
                
                insertar_en_tablaNodos(buffer);
                
            }
        }
    }

    printf("[ARRANQUE] Fase inicial completada.\n"); 
}

void iniciar_event_loop() {
    int socket_broadcast=crear_socket_broadcast();
    //crear_servidor_tcp_publico();
    //crear_servidor_tcp_local();

    //conf. de timerd para los anuncios:
    //clock_monotonic: no retrocede el reloj
    //tfd_nonblock: operaciones no bloqueantes
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
    

    //ejecutar_arranque_inicial(epoll_fd, socket_broadcast);

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
                sendto(socket_broadcast, mensaje, strlen(mensaje), 0, (struct sockaddr*)&srv_mensajeria_broadcast, sizeof(srv_mensajeria_broadcast));
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


//* --- MANEJO TABLA NODOS ----

void limpiar_nodos_caidos(){
    for(int x=0;x<cantidad_nodos;x++){
        if(CAIDO(&tabla_activos[x])){
            printf("[INFO-LIMPIEZA] Nodo caído detectado para ser eliminado: %s:%d\n",tabla_activos[x].IP,tabla_activos[x].puerto);
            tabla_activos[x]=tabla_activos[cantidad_nodos-1]; //Para no mover todo, simplemente lo pisamos con el ùltimo.
            cantidad_nodos--;
            x--; //Còmo agregamos el ùltimo, PUEDE PASAR que tambièn este caìdo...
            printf("[INFO-LIMPIEZA] Nodo caído eliminado. \n");
        }
    }
}

void insertar_en_tablaNodos(char * buffer){
    char comando[16];
    char ip_recibida[16];
    int puerto_recibido;
    char recursos_recibidos[128] = "";

    //lee hasta el primer espacio (comando - puerto - ip - lee el resto :recursos)
    int recibidos = sscanf(buffer,"%15s %15s %d %127[^\n]",comando,ip_recibida,&puerto_recibido,recursos_recibidos);

    //Verificacion ANUNCIAMIENTO vàlida.
    if(recibidos>=3 && strcmp(comando,"ANNOUNCE")==0){

        int existe=0;

        //nos fijamos si ya existe primero...
        // si existe actualizamos el timestmap, sino: lo insertamos.
        for(int x=0;x<cantidad_nodos && !existe;x++){
            if(strcmp(tabla_activos[x].IP,ip_recibida)==0 && tabla_activos[x].puerto==puerto_recibido){
                tabla_activos[x].timestamp=time(NULL); //hora actual
                existe=1;
                printf("[INFO] Nodo %s:%d actualizado.\n", ip_recibida,puerto_recibido);
            }
        }

        if(!existe){
            if(cantidad_nodos<MAX_NODOS){
                strncpy(tabla_activos[cantidad_nodos].IP,ip_recibida,15);
                tabla_activos[cantidad_nodos].IP[15]='\0';

                tabla_activos[cantidad_nodos].puerto=puerto_recibido;

                strncpy(tabla_activos[cantidad_nodos].recursos,recursos_recibidos,127);
                tabla_activos[cantidad_nodos].recursos[127]='\0';

                cantidad_nodos++;
                printf("[INFO] Nodo agregado a la tabla: %s:%d\n", ip_recibida, puerto_recibido);                
            }else printf("[INFO-WARNING] Tabla de nodos llena. No se pudo agregar: %s:%d.\n",ip_recibida,puerto_recibido);
        }

    }

}

