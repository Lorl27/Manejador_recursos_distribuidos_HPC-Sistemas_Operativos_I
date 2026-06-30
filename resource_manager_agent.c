#include "resource_manager_agent.h"

//* ----- Variables globales para el manejo de mensajes, recursos y nodos. -----
char mensaje[MAX_MSG];
struct sockaddr_in srv_mensajeria_broadcast;

TablaNodos tabla_activos[MAX_NODOS];
int cantidad_nodos=0;

RecursosLocales mi_recurso_local[3]; //cpu - gpu - mem

SolicitudRespuestaRecurso solicitud_respuesta[MAX_PENDING];

TablaJobActivos tabla_jobs_activos[MAX_JOBS_ACTIVOS];

//*-------------------------------------------

//* --- INICIALIZACIÒN ---

void inicializar_mis_recursos(char * mis_recursos) {
    char copia[256];
    strncpy(copia, mis_recursos, 255);
    copia[255] = '\0';

    int x=0;
    // Cortamos por espacios
    char * token = strtok(copia, " ");
    
    while(token!=NULL && x<3){
        char nombre[16];
        int capacidad;
        
        // El %15[^:] lee todo hasta encontrar los dos puntos ':'
        if(sscanf(token,"%15[^:]:%d",nombre,&capacidad)== 2){
            strncpy(mi_recurso_local[x].nombre, nombre, 15);
            mi_recurso_local[x].nombre[15] = '\0';
            mi_recurso_local[x].capacidadTotal = capacidad;
            mi_recurso_local[x].cantidadDisponible = capacidad;
            mi_recurso_local[x].solicitudesPendientes = Cola_crear();
            
            printf("[INFO-INIT] Recurso cargado: %s con capacidad %d\n", mi_recurso_local[x].nombre, capacidad);
            x++;
        }
        token = strtok(NULL, " "); // Siguiente recurso
    }
    
    // Si alguno de los recursos no se paso, lo inicializamos por defecto en vacio. 
    //usa el valor de x anterior.
    for(;x<3;x++) {
        strcpy(mi_recurso_local[x].nombre, "");
        mi_recurso_local[x].capacidadTotal = 0;
        mi_recurso_local[x].cantidadDisponible = 0;
        mi_recurso_local[x].solicitudesPendientes = Cola_crear(); 
    }
}


//*  -- FUNCIONES AUXILIARES COLA --

void* copiar_solicitud(void* dato) {
    SolicitudRecurso * original=(SolicitudRecurso*) dato;
    SolicitudRecurso * copia=malloc(sizeof(SolicitudRecurso));
    copia->job_id=original->job_id;
    copia->amount=original->amount;
    copia->fd_origen=original->fd_origen;

    return copia;
}

void destruir_solicitud(void* dato) {
    free(dato);
}

//* --- CREACION DE SERVIDORES ---

int crear_servidor_tcp_publico(int puerto){
    int sock_srv;
    struct sockaddr_in srv_name;
    int opt = 1;
 
    if((sock_srv=socket(AF_INET, SOCK_STREAM,0))<0) {
        perror("[SERVER TCP PUBLICO-ERROR] Fallo la creacion del socket.\n");
        exit(EXIT_FAILURE);
    }

    //SO_REUSEADDR  nos permite reutilizar el puerto inmediatamente por si creashea/reinicia
    if(setsockopt(sock_srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("[SERVER TCP PUBLICO-ERROR] Fallo el setsockopt.\n");
        exit(EXIT_FAILURE);
    }

    // Configurar el socket como NO BLOQUEANTE
    int flags = fcntl(sock_srv, F_GETFL, 0);
    fcntl(sock_srv, F_SETFL, flags | O_NONBLOCK);
  
    srv_name.sin_family = AF_INET;
    srv_name.sin_addr.s_addr = INADDR_ANY; //acepta cualquiera.
    srv_name.sin_port = htons(puerto);

    if(bind(sock_srv, (struct sockaddr*)&srv_name, sizeof(srv_name)) < 0) {
        perror("[SERVER TCP PUBLICO-ERROR] Fallo el bind.\n");
        exit(EXIT_FAILURE);
    }

    //para recibir maximo que pueda
    if(listen(sock_srv, SOMAXCONN) < 0) {
        perror("[SERVER TCP PUBLICO-ERROR] Fallo el listen.\n");
        exit(EXIT_FAILURE);
    }
    
    printf("[SERVER TCP PUBLICO] Creacion realizada con exito. Escuchando en puerto: %d\n",puerto);
    
    return sock_srv;
}

int crear_servidor_tcp_local(int puerto){
    int sock_srv;
    struct sockaddr_in srv_name;
    int opt = 1;
 
    if((sock_srv=socket(AF_INET, SOCK_STREAM,0))<0) {
        perror("[SERVER TCP LOCAL-ERROR] Fallo en la creacion del socket.\n");
        exit(EXIT_FAILURE);
    }

    //SO_REUSEADOR nos permite reutilizar el puerto inmediatamente por si creashea/reinicia
    if(setsockopt(sock_srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("[SERVER TCP LOCAL-ERROR] Fallo el setsockopt.\n");
        exit(EXIT_FAILURE);
    }

    // Configurar el socket como NO BLOQUEANTE
    int flags = fcntl(sock_srv, F_GETFL, 0);
    fcntl(sock_srv, F_SETFL, flags | O_NONBLOCK);
  
    srv_name.sin_family = AF_INET;
    srv_name.sin_addr.s_addr = inet_addr(LOCAL_IP); //inet convierte str a formato red.
    srv_name.sin_port = htons(puerto);

    if(bind(sock_srv, (struct sockaddr*)&srv_name, sizeof(srv_name)) < 0) {
        perror("[SERVER TCP LOCAL-ERROR] Fallo el bind.\n");
        exit(EXIT_FAILURE);
    }

    //para ecibir maximo que pueda
    if(listen(sock_srv, SOMAXCONN) < 0) {
        perror("[SERVER TCP LOCAL-ERROR] Fallo el listen.\n");
        exit(EXIT_FAILURE);
    }
    
    printf("[SERVER TCP LOCAL] Creacion realizada con exito. Escuchando en puerto con IP: %d %s\n",puerto,LOCAL_IP);
    
    return sock_srv;
}

//* --- CREACION DE SOCKETS ----

int crear_socket_broadcast() {
    int sock;
    int broadcastEnable = 1;
    int opt=1;

    //crear socket upd
    if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("[BROADCAST-ERROR] Creando el socket UDP.\n");
        exit(EXIT_FAILURE);
    }

    //hacerlo broadcast
    if(setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("[BROADCAST-ERROR] Seteando el broadcast.\n");
        close(sock);
        exit(EXIT_FAILURE);
    }

    //^ ===== AGREGAR ESTO PARA PERMITIR MÚLTIPLES NODOS EN LOCALHOST
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[BROADCAST-ERROR] Seteando SO_REUSEADDR.\n");
    }
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("[BROADCAST-ERROR] Seteando SO_REUSEPORT.\n");
    }
    //^ =========================

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

    if(bind(sock, (struct sockaddr*)&recvAddr, sizeof(recvAddr)) < 0) {
        perror("[BROADCAST-ERROR] Error en el bind.\n");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("[BROADCAST] Creacion realizada con exito. Escuchando en puerto con IP: %d %s\n",PUERTO_BROADCAST,BROADCAST_IP);

    return sock;
}


int crear_conexion_cliente(const char * ip_destino, int puerto_destino){
    int sock = 0;
    struct sockaddr_in serv_addr;

    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("[CLIENTE-ERROR] Fallo la creacion del Socket cliente. \n");
        return -1;
    }

    //conf. no bloqueante
    int flags=fcntl(sock,F_GETFL,0); //las que ya tiene el socket
    fcntl(sock,F_SETFL,flags|O_NONBLOCK);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(puerto_destino);


    if(inet_pton(AF_INET, ip_destino, &serv_addr.sin_addr) <= 0) {
        printf("[CLIENTE-ERROR] Dirección invalida o no soportada. \n");
        return -1;
    }

    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        if(errno==EINPROGRESS){ //conectado por detràs , asisara cuando termine.
            printf("[CLIENTE-INFO] Iniciando conexion a %s:%d.\n",ip_destino,puerto_destino);
            return sock; //asi epoll lo espera.
        }
        else{
            printf("[CLIENTE-ERROR] Fallo la conexion. \n");
            return -1;
        }
    }

    printf("[CLIENTE] Conexion TCP establecida con exito a %s:%d\n", ip_destino, puerto_destino);

    return sock;
}

//* --- Main eventos ----

void ejecutar_arranque_inicial(int epoll_fd, int socket_broadcast,char * ip, int puerto_tcp, char * recursos) {
    char buffer[MAX_MSG];
    struct sockaddr_in origen;
    socklen_t len = sizeof(origen);
    struct epoll_event events[MAX_EVENTS];

    inicializar_mis_recursos(recursos);

    printf("[INFO-ARRANQUE] Enviando primer anuncio...\n");
    
    // anuncio mi IP - Puerto TCP- Mis recursos disponibles
    snprintf(mensaje, sizeof(mensaje), "ANNOUNCE %s %d %s.\n", ip, puerto_tcp, recursos);
    if (sendto(socket_broadcast, mensaje, strlen(mensaje), 0, (struct sockaddr*)&srv_mensajeria_broadcast, sizeof(srv_mensajeria_broadcast)) < 0) {
        perror("[ARRANQUE-ERROR] Error en sendto inicial.\n");
    }

    printf("[INFO-ARRANQUE] Esperando 2 segundos para descubrir nodos...\n");

    int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 2000); 

    for (int i = 0; i < n; i++) {
        if (events[i].data.fd == socket_broadcast) {
            int nbytes = recvfrom(socket_broadcast, buffer, MAX_MSG-1, 0, (struct sockaddr*)&origen, &len);
            if (nbytes > 0) {
                buffer[nbytes] = '\0';
                printf("[INFO-ARRANQUE] Nodo activo descubierto: %s\n", buffer);
                
                insertar_en_tablaNodos(buffer);
                
            }
        }
    }

    printf("[INFO-ARRANQUE] Fase inicial completada.\n"); 
}

void iniciar_event_loop(char* mi_ip_lan, int mi_puerto_publico, int mi_puerto_local, char* mis_recursos){
    
    int srv_public=crear_servidor_tcp_publico(mi_puerto_publico);
    int srv_local=crear_servidor_tcp_local(mi_puerto_local);

    int socket_broadcast=crear_socket_broadcast();
    struct sockaddr_in  cli_name;
    socklen_t cli_size;
    ssize_t nbytes;

    //conf. de timerd para los anuncios:
    //clock_monotonic: no retrocede el reloj
    //tfd_nonblock: operaciones no bloqueantes
    int timer = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer == -1) {
        perror("[ERROR] Fallo timerfd_create.\n");
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
        perror("[ERROR] Fallo epoll_create1.\n");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev, events[MAX_EVENTS];

    // agregar socket de escucha al epoll
    ev.events = EPOLLIN;
    ev.data.fd = socket_broadcast; 
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_broadcast, &ev)==-1){
        perror("[ERROR] Fallo epoll_ctl ADD socket broadcast.\n");
        exit(EXIT_FAILURE);
    }

    // agregar timer al epoll -para que sepa cuando debe enviar el announce
    ev.events = EPOLLIN;
    ev.data.fd = timer; 
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer, &ev)==-1){
            perror("[ERROR] fallo epoll_ctl ADD timer.\n");
        exit(EXIT_FAILURE);
    }

    // agregar srv publico al epoll
    ev.events = EPOLLIN;
    ev.data.fd = srv_public; 
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, srv_public, &ev)==-1){
        perror("[ERROR] fallo epoll_ctl ADD srv public.\n");
        exit(EXIT_FAILURE);
    }

    // agregar srv local al epoll
    ev.events = EPOLLIN;
    ev.data.fd = srv_local; 
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, srv_local, &ev)==-1){
        perror("[ERROR] fallo epoll_ctl ADD srv local.\n");
        exit(EXIT_FAILURE);
    }
    

    ejecutar_arranque_inicial(epoll_fd, socket_broadcast, mi_ip_lan, mi_puerto_publico, mis_recursos);

    printf("[Servidor iniciado correctamente.]\n");
    printf("[INFO-SERVIDOR] Iniciando envíos periódicos de ANNOUNCE por broadcast...\n");

    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            perror("[SERVIDOR-ERROR] fallo epoll_wait.\n");
            exit(EXIT_FAILURE);
        }

        for (int i=0;i<n;i++) {
            int fd = events[i].data.fd;
            if(fd==timer){
                uint64_t expira;
                read(timer, &expira, sizeof(expira)); // limpiar timer (leyendolo)

                // Armar y mandar el mensaje ANNOUNCE 
                snprintf(mensaje, sizeof(mensaje), "ANNOUNCE %s %d %s\n", mi_ip_lan, mi_puerto_publico, mis_recursos);
                sendto(socket_broadcast, mensaje, strlen(mensaje), 0, (struct sockaddr*)&srv_mensajeria_broadcast, sizeof(srv_mensajeria_broadcast));
                
                limpiar_nodos_caidos();
            }
            else if(fd==socket_broadcast){ //llego algo de otro nodo.
                cli_size=sizeof(cli_name);
                nbytes = recvfrom(socket_broadcast, mensaje, MAX_MSG-1, 0, (struct sockaddr *) &cli_name, &cli_size);  //donde recibo info.
                if(nbytes < 0) {
                    perror("[OTRO NODO - ERROR] Falló el recvfrom en broadcast.\n");
                    continue; //no bloqueante si falla.
                }

                mensaje[nbytes]='\0';
                printf("[OTRO NODO - INFO] Se recibio del nodo con IP %s el mensaje: %s.\n", inet_ntoa(cli_name.sin_addr),mensaje);

                insertar_en_tablaNodos(mensaje);
            }
            else if (fd==srv_local || fd==srv_public) {
                // Alguien nuevo se quiere conectar...:
                cli_size=sizeof(cli_name);
                int nuevo_cli = accept(fd, (struct sockaddr *) &cli_name, &cli_size);
                if (nuevo_cli < 0) {
                    perror("[SERVIDOR-ERROR] fallo accept cliente nuevo.\n");
                    continue; //no bloqueante si falla.
                }

                printf("[INFO-SERVIDOR] Nuevo cliente %d conectado en socket: %s.\n", nuevo_cli, (fd==srv_local)?"LOCAL":"PUBLICO");

                // Registrar cliente en epoll
                ev.events = EPOLLIN;
                ev.data.fd = nuevo_cli;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, nuevo_cli, &ev);
            }
            else {
                uint32_t eventos= events[i].events;

                //EPOLLOUT: nuestro socket async nos avisa que YA se conecto.
                //&: & a nivel de bits
                // lo que sucede es que esta veriricando si el bit correspondiente a EPOLLOUT esta encendido.
                if(eventos & EPOLLOUT){
                    int procesandose_conexion=0;
                    for(int x=0;x<MAX_PENDING && !procesandose_conexion;x++){
                        int conectando=solicitud_respuesta[x].activo && solicitud_respuesta[x].fd_remoto == fd && solicitud_respuesta[x].conectando;

                        if(conectando){
                            int error=0;
                            socklen_t error_len=sizeof(error);
                            //fue exitosa a nivel tcp? (el connect termino BIEN)
                            getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&error_len);

                            if(error!=0){
                                printf("[CLIENTE-ERROR] Fallo la conexion al nodo vecino.\n");

                                if(solicitud_respuesta[x].es_release) printf("[CLIENTE-ERROR] No se pudo enviar el RELEASE al nodo vecino, ya que fallo la conexion.\n");
                                else if(solicitud_respuesta[x].fd_erlang!=-1){
                                    //avisarle a erlang que fallo:
                                    snprintf(mensaje, sizeof(mensaje), "DENIED %d \n", solicitud_respuesta[x].job_id);
                                    
                                    ssize_t enviados =send(solicitud_respuesta[x].fd_erlang, mensaje, strlen(mensaje), 0);
                                    if(enviados==-1) perror("[CLIENTE-ERROR] Error en send() de DENIED");
                                }

                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev);
                                close(fd);
                                solicitud_respuesta[x].activo=0;
                            }
                            else{
                                if(solicitud_respuesta[x].es_release){
                                    printf("[CLIENTE-ASYNC] Conexion TCP establecida. Enviando RELEASE...\n");
                                    snprintf(mensaje, sizeof(mensaje), "RELEASE %d %s %d\n", solicitud_respuesta[x].job_id, solicitud_respuesta[x].recurso_name, solicitud_respuesta[x].amount);
                                    
                                    ssize_t enviados = send(fd, mensaje, strlen(mensaje), 0);
                                    if(enviados==-1) perror("[CLIENTE-ERROR] Error en send() de RELEASE");
                                        
                                    // Cerramos el socket y liberamos el hueco, ya terminamos el proceso.
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev);
                                    close(fd);
                                    solicitud_respuesta[x].activo = 0;
                                }
                                else{
                                    printf("[CLIENTE-ASYNC] Conexion TCP establecida. Enviando RESERVE...\n");

                                    // Armamos el mensaje usando la memoria guardada
                                    snprintf(mensaje, sizeof(mensaje), "RESERVE %d %s %d\n", solicitud_respuesta[x].job_id, solicitud_respuesta[x].recurso_name, solicitud_respuesta[x].amount);
                                    
                                    int enviados=send(fd, mensaje, strlen(mensaje), 0);
                                    if(enviados == -1) perror("[CLIENTE-ERROR] Error en send() de RESERVE");
                                        
                                    // Ya no queremos que epoll nos avise de EPOLLOUT (escritura), 
                                    // modificamos el socket para escuchar solo respuestas (EPOLLIN)
                                    solicitud_respuesta[x].conectando = 0;
                                    struct epoll_event ev_update;
                                    ev_update.events = EPOLLIN;
                                    ev_update.data.fd = fd;
                                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev_update);
                                }
                            }
                            procesandose_conexion=1;
                        }
                    }
                }

                //Si hay datos nuevos para leer... (evento lectura)
                if(eventos & EPOLLIN){

                    // Cliente ya conectado mandó algo
                    nbytes = recv(fd, mensaje, MAX_MSG-1, 0);
                    if(nbytes <= 0) {
                        printf("[INFO-SERVIDOR] Cliente  %d desconectado.\n", fd);
                        close(fd);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev);
                    } 
                    else{
                        mensaje[nbytes] = '\0';

                        if(!validar_mensajes_validos(mensaje)) continue;
                        
                        printf("[INFO-SERVIDOR] Mensaje recibido: %s del cliente %d.\n", mensaje,fd);

                        char comando[16];
                        int job_id=0;
                        char recursos_name[16] = "";
                        int recursos_tam=0;

                        sscanf(mensaje,"%15s %d %15s %d",comando,&job_id,recursos_name,&recursos_tam);

                        //Erlang nos pide la lista de nodos: GET NODES (MODO_ INTERMEDIARIO)
                        if(strncmp(comando,"GET",3)==0) enviar_lista_nodos(fd);

                        //Respuestas a nuestras reservas: (MODO: CLIENTE)
                        else if(strcmp(comando,"GRANTED")==0||strcmp(comando,"DENIED")==0){
                            
                            printf("[INFO-SERVIDOR] Se recibio: %s para el job %d. \n",comando,job_id);
                            
                            //Buscar datos originales de la solicud:
                            int encontrado=0;
                            SolicitudRespuestaRecurso *soli=NULL;
                            for(int y=0;y<MAX_PENDING && !encontrado;y++){
                                int solicitud_act=solicitud_respuesta[y].activo && solicitud_respuesta[y].fd_remoto==fd && solicitud_respuesta[y].job_id==job_id;
                                if(solicitud_act){
                                    soli=&solicitud_respuesta[y];
                                    encontrado=1;
                                }
                            }

                            if(soli!=NULL){
                                // RECIEN nos dijeron que SI, ACA lo registramos:
                                if(strcmp(comando,"GRANTED")==0){
                                    registrar_recurso_job(job_id, soli->ip, soli->puerto, soli->recurso_name, soli->amount);
                                    marcar_job_concedido(job_id);
                                } // Nos dijeron que NO: como no lo registramos nunca, solo lo borramos.
                                else eliminar_job(job_id);
                                
                                int fd_erlang= soli->fd_erlang;
                                soli->activo=0;

                                if(fd_erlang!=-1){
                                    printf("[INFO-RESPUESTA] Reenviando respuesta Erlang.\n");
                                    // le respondemos a Erlang con JOB_GRANTED o JOB_DENIED
                                    snprintf(mensaje, sizeof(mensaje), "JOB_%s %d\n", comando, job_id);
                                    ssize_t enviados =send(fd_erlang, mensaje, strlen(mensaje), 0);
                                    if(enviados==-1) perror("[ERROR-RESPUESTA] Error en send() respuesta erlang.");
                                }

                            }
                            else{
                                printf("[WARNING] NO pudimos encontrar a quien reenviarle %s...\n",comando);
                            }

                            //termino la transaccion. cerramos socket y sacamos de epoll.
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev);
                            close(fd);
                        } 

                        //si alguien nos pidio ò libero recursos a nosotros: (MODO:SERVIDORES)
                        else if(strcmp(comando,"RESERVE")==0||strcmp(comando,"RELEASE")==0){
                            RecursosLocales * recurso=NULL;

                            int encontrado=0;
                            for(int x=0;x<3 && !encontrado;x++){
                                if(strcmp(recursos_name,mi_recurso_local[x].nombre)==0){
                                    recurso=&mi_recurso_local[x];
                                    encontrado=1;
                                }
                            }

                            if(recurso!=NULL) gestionar_recursos_locales(recurso,comando,job_id,recursos_tam,fd);
                            else {
                                printf("[INFO-SERVIDOR-WARNING] Otro nodo pidio el recurso %s, el cual no tenemos.\n",recursos_name);

                                //le avisamos al nodo por su misma conexion
                                snprintf(mensaje, sizeof(mensaje), "DENIED %d \n", job_id);
                                ssize_t enviados =send(fd, mensaje, strlen(mensaje), 0);
                                if(enviados==-1) perror("[SERVIDOR-ERROR] Error en send() de DENIED");
                            }
                        } 
                        
                        //si erlang nos pidio buscar un recurso: (MODO:INTERMEDIARIO)
                        else if(strcmp(comando,"JOB_REQUEST")==0){
                            sscanf(mensaje,"%*s %d",&job_id); //obtener job_id

                            char copia_mensaje[MAX_MSG];
                            strcpy(copia_mensaje,mensaje); //strtok modifica el str.

                            //Salteamos JOB_REQUEST y JOB_ID
                            char * token= strtok(copia_mensaje," ");
                            token=strtok(NULL, " ");
                            token=strtok(NULL, " ");

                            int todos_exitosos=1;

                            while(token!=NULL){
                                char ip_destino[16];
                                // Parseamos: @192.168.1.2:cpu:2  (TOKEN ACTUAL)
                                if(sscanf(token, "@%15[^:]:%15[^:]:%d", ip_destino, recursos_name, &recursos_tam)==3){

                                    int puerto_destino=buscar_puerto_por_IP(ip_destino);

                                    if(puerto_destino!=-1){
                                        int fd_remoto=crear_conexion_cliente(ip_destino,puerto_destino);
                                        
                                        if(fd_remoto!=-1){
                                            if(guardar_datos_solicitud_respuesta(fd_remoto,fd,job_id,recursos_name,recursos_tam,ip_destino,puerto_destino,0)){
                                                //lo agregamos al epoll, para que nos avise(escuchar) cuando respondan GRANTED y, ademàs EPOLLOUT para saber cuadno conecto.
                                                struct epoll_event ev_remoto;
                                                ev_remoto.events=EPOLLIN|EPOLLOUT;
                                                ev_remoto.data.fd=fd_remoto;
                                                epoll_ctl(epoll_fd,EPOLL_CTL_ADD,fd_remoto,&ev_remoto);
                                            }else{
                                                //NO hay hueco.
                                                close(fd_remoto);
                                                todos_exitosos=0;
                                            }

                                            
                                        }else{
                                            printf("[WARNING] Fallo conexion con %s para recurso %s.\n", ip_destino, recursos_name);
                                            todos_exitosos=0;
                                        }
                                    }
                                    else{
                                        printf("[WARNING] Erlang pidio conectar a %s, pero no esta en la tabla de activos...\n", ip_destino);
                                        todos_exitosos=0;
                                    }
                                }else {
                                    printf("[WARNING] El comando actual %s, no es valido.\n",token);
                                    todos_exitosos=0;
                                }

                                token=strtok(NULL, " ");
                            }

                            if(!todos_exitosos){
                                //le avisamos a erlang que fallamos:
                                snprintf(mensaje, sizeof(mensaje), "JOB_DENIED %d \n", job_id);
                                ssize_t enviados =send(fd, mensaje, strlen(mensaje), 0);
                                if(enviados==-1) perror("[ERROR] Error en send() de JOB_DENIED");

                                //Le delegamos el rollback a Erlang, èste deberìa enviar JOB_RELEASE <JOB_ID> cuando cancela/termina un job (Cuando le mandamos JOB_DENIED <JOB_ID>)
                                // Al mandar eso, liberar_job recorre TablaJobActivos y libera (RELEASE) los nodos y recursos que podrìan haber quedado 'secuestrados'.
                            }
                            
                        }

                        //si erlang libera un job:
                        else if(strcmp(comando,"JOB_RELEASE")==0){
                            sscanf(mensaje,"%*s %d",&job_id);
                            printf("[ERLANG-C] Erlang solicita liberar el job %d. \n",job_id);

                            liberar_job(job_id,epoll_fd);
                        }

                        //si erlang consulta el estado de un job:
                        else if(strcmp(comando,"JOB_STATUS")==0){
                            sscanf(mensaje,"%*s %d",&job_id);
                            printf("[ERLANG-C] Erlang solicita conocer el estado del job %d. \n",job_id);

                            int estado=conocer_estado_job(job_id);

                            if(estado!=-1){
                                printf("[ERLANG-C] El estado del job %d es: %d\n", job_id,estado);
                                snprintf(mensaje, sizeof(mensaje), "JOB_STATUS %d \n", estado);
                                ssize_t enviados =send(fd, mensaje, strlen(mensaje), 0);
                                if(enviados==-1) perror("[ERLANG-C ; ERROR] Error en send() de JOB_STATUS");
                            }else{
                                printf("[ERLANG-C WARNING] NO se encontro estado para el job %d. \n",job_id);
                            }
                        }
                        
                    }
                }

            }
        }
    }
}


//* --- Validaciones y gestiones ----

int validar_mensajes_validos(char * mensaje){
    char comando[32];
    int job_id;
    char recursos_name[32] = "";
    char ip_destino[32] = "";
    char sub_comando[32] = "";
    int recursos_tam;

    int recibidos = sscanf(mensaje,"%31s",comando);

    if(recibidos==1){
        //PIDE RECURSOS
        if(strcmp(comando,"RESERVE")==0 ||strcmp(comando,"RELEASE")==0){
            if(sscanf(mensaje,"%*s %d %31s %d",&job_id,recursos_name,&recursos_tam)==3) return 1; // (IGNORADA) JOB_ID RECURSO CANTIDAD
        }

        //RESPONDE
        else if(strcmp(comando,"GRANTED")==0 || strcmp(comando,"DENIED")==0){
            //%*s lee la palabra pero la ignora, pasa a la sig.
            if(sscanf(mensaje,"%*s %d",&job_id)==1) return 1; //(IGNORADA) JOB_ID
        }

        //ERLANG PIDE LISTA NODOS
        else if(strcmp(comando,"GET")==0){
            if(sscanf(mensaje,"%*s %31s",sub_comando)==1){ // (IGNORADA) SUBCOMANDO
                if(strcmp(sub_comando,"NODES")==0) return 1; //(IGNORADA) NODES
            }
        }

        //ERLANG PIDE BUSCAR RECURSO AFUERA.
        else if(strcmp(comando,"JOB_REQUEST")==0){
            if(sscanf(mensaje, "%*s %d @%15[^:]:%15[^:]:%d",&job_id,ip_destino,recursos_name,&recursos_tam)==4) return 1; //(IGNORADO) JOB_REQUEST JOB_ID @IP:RECURSO:CANTIDAD (VALIDA POR LO MENOS EL PRIMER RECURSO)
        }

        //ERLANG GESTIONA UN JOB ACTIVO
        else if(strcmp(comando,"JOB_STATUS")==0 || strcmp(comando,"JOB_RELEASE")==0){
            if(sscanf(mensaje,"%*s %d",&job_id)==1) return 1; //(IGNORADA) JOB_ID
        }
        
    }

    printf("[INFO-WARNING] Mensaje recibido (%s) no valido.\n",mensaje);
    return 0;
}

void gestionar_recursos_locales(RecursosLocales * recurso, char * comando, int job_id, int amount, int fd_cliente){
    char respuesta[MAX_MSG];

    if(strcmp(comando,"RESERVE")==0){
        if(recurso->cantidadDisponible>=amount && amount<=recurso->capacidadTotal){
            recurso->cantidadDisponible-=amount;

            //Armamos y mandamos la respuesta TCP al agente que la pidió
            snprintf(respuesta, sizeof(respuesta), "GRANTED %d\n", job_id);
            ssize_t enviados =send(fd_cliente, respuesta, strlen(respuesta), 0);
            if(enviados==-1) perror("[COLA-ERROR] Error en send() de GRANTED");
            
            printf("[INFO-COLA] GRANTED enviado para job %d , con recurso: %s cant: %d.\n", job_id, recurso->nombre,amount);
        }
        else{ //no hay stock:
            SolicitudRecurso soli;
            soli.job_id = job_id;
            soli.amount = amount;
            soli.fd_origen = fd_cliente;

            recurso->solicitudesPendientes=Encolar(recurso->solicitudesPendientes,&soli,copiar_solicitud);

            printf("[INFO-COLA] Sin stock de %s. Solicitud %d agregada a la cola.\n", recurso->nombre, job_id);
        }
    }
    else if(strcmp(comando,"RELEASE")==0){
        recurso->cantidadDisponible+=amount;
        printf("[INFO-COLA] Liberados %d de %s. Disponible actual: %d\n", amount, recurso->nombre, recurso->cantidadDisponible);
        
        int exigencia_mayor=0;
        while(!isEmpty(recurso->solicitudesPendientes) && !exigencia_mayor){
            SolicitudRecurso * primero = (SolicitudRecurso *) Tope(recurso->solicitudesPendientes);

            //si tenemos suficiente disponible para el job...:
            if(recurso->cantidadDisponible>=primero->amount){
                recurso->cantidadDisponible-=primero->amount;

                snprintf(respuesta, sizeof(respuesta), "GRANTED %d\n", primero->job_id);
                ssize_t enviados =send(primero->fd_origen, respuesta, strlen(respuesta), 0);
                if(enviados==-1) perror("[COLA-ERROR] Error en send() de GRANTED");

                printf("[INFO-COLA] GRANTED enviado. Job: %d | Recurso: %s\n", primero->job_id, recurso->nombre);

                recurso->solicitudesPendientes=Desencolar(recurso->solicitudesPendientes,destruir_solicitud);
            }else exigencia_mayor=1;
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

                tabla_activos[cantidad_nodos].timestamp=time(NULL);

                cantidad_nodos++;
                printf("[INFO] Nodo agregado a la tabla: %s:%d\n", ip_recibida, puerto_recibido);                
            }else printf("[INFO-WARNING] Tabla de nodos llena. No se pudo agregar: %s:%d.\n",ip_recibida,puerto_recibido);
        }

    }

}

void enviar_lista_nodos(int fd_erlang){
    char buffer[MAX_MSG];
    strcpy(buffer, "NODES\n");

    for(int x=0;x<cantidad_nodos;x++){
        char nodo[256];
        snprintf(nodo, sizeof(nodo), "%s:%d:%s;",tabla_activos[x].IP,tabla_activos[x].puerto,tabla_activos[x].recursos);
        strncat(buffer, nodo, MAX_MSG-strlen(buffer)-1);
    }
    strncat(buffer,"\n",MAX_MSG-strlen(buffer)-1); //agregamos \n

    ssize_t enviados=send(fd_erlang, buffer, strlen(buffer), 0);
    if(enviados==-1) perror("[SERVIDOR-ERROR] Error en send()  para erlang");

    printf("[INFO-SERVIDOR] Lista de nodos enviada a Erlang.\n");
}

int buscar_puerto_por_IP(char * ip){
    for(int x=0;x<cantidad_nodos;x++){
        if(strcmp(tabla_activos[x].IP,ip)==0){
            return tabla_activos[x].puerto;
        }
    }

    return -1;
}

//* ----  Solicitud Respuesta Recursos  ----

int guardar_datos_solicitud_respuesta(int fd_remoto,int fd_erlang,int job_id,char* recurso_name,int amount, char* ip, int puerto, int es_release){
    for(int x=0;x<MAX_PENDING;x++){
        if(!solicitud_respuesta[x].activo){
            
            solicitud_respuesta[x].fd_erlang=fd_erlang;
            solicitud_respuesta[x].fd_remoto=fd_remoto;
            solicitud_respuesta[x].job_id=job_id;

            //guardamos los datos, para luego mandar el RESERVE.
            solicitud_respuesta[x].conectando=1;
            solicitud_respuesta[x].amount=amount;
            strncpy(solicitud_respuesta[x].recurso_name,recurso_name,15);
            solicitud_respuesta[x].recurso_name[15]='\0';

            strncpy(solicitud_respuesta[x].ip,ip,15);
            solicitud_respuesta[x].ip[15] ='\0';
            solicitud_respuesta[x].puerto =puerto;
            solicitud_respuesta[x].es_release=es_release;

            solicitud_respuesta[x].activo=1;

            printf("[SOLICITUD RESPUESTA] Se registro la relacion fd_remoto %d => fd_erlang %d con job_id %d.\n",fd_remoto,fd_erlang,job_id);
            return 1;
        }
    }
    printf("[SOLICITUD RESPUESTA ERROR] No se pudo guardar la relacion para jod_id %d, debido a que se encuentra llena la capacidad de solicitudes.\n",job_id);
    return 0;
}

//* -- MANEJO TABLA DE JOB ACTIVOS ---

void liberar_job(int job_id,int epoll_fd){
    int encontrado=0;
    char mensaje_job[MAX_MSG];

    for(int x=0;x<MAX_JOBS_ACTIVOS && !encontrado;x++){
        if(job_id==tabla_jobs_activos[x].job_id && tabla_jobs_activos[x].estado_job!=0){
            encontrado=1;
            
            //Recorremos todos los recursos del job:
            for(int y=0;y<tabla_jobs_activos[x].cantidad_recursos;y++){
                char * ip=tabla_jobs_activos[x].recursos[y].ip;
                int puerto=tabla_jobs_activos[x].recursos[y].puerto;
                char * recurso_name=tabla_jobs_activos[x].recursos[y].recurso_name;
                int cantidad=tabla_jobs_activos[x].recursos[y].amount;
 
                int fd_remoto=crear_conexion_cliente(ip,puerto);

                if(fd_remoto!=-1){
                    //lo guardamos con -1, ya que no hace falta una rta a Erlang por esto.
                    guardar_datos_solicitud_respuesta(fd_remoto, -1, job_id, recurso_name, cantidad, ip, puerto, 1);
                    
                    // Lo agregamos al epoll para que nos avise cuando conecte
                    struct epoll_event ev_remoto;
                    ev_remoto.events = EPOLLOUT; // Solo queremos saber cuando conecta
                    ev_remoto.data.fd = fd_remoto;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd_remoto, &ev_remoto);
                }else{
                    printf("[WARNING] No se pudo contactar a %s:%d para liberar el recurso %s.\n", ip, puerto, recurso_name);
                }

            }

            //Ahora, liberamos en la tabla nuestra:
            tabla_jobs_activos[x].estado_job=0;
            tabla_jobs_activos[x].cantidad_recursos=0;
            tabla_jobs_activos[x].job_id=0;
            printf("[INFO - TABLA JOBS] Se ha liberado el job %d.\n",job_id);
        }
    }

    if(!encontrado) printf("[INFO- TABLA JOBS] NO se encontro el job %d. No se pudo liberar.\n",job_id);
}


void eliminar_job(int job_id){
    for(int x=0; x<MAX_JOBS_ACTIVOS; x++){
        if(tabla_jobs_activos[x].job_id == job_id && tabla_jobs_activos[x].estado_job != 0){
            tabla_jobs_activos[x].estado_job = 0;
            tabla_jobs_activos[x].cantidad_recursos = 0;
            tabla_jobs_activos[x].job_id=0;
            printf("[INFO-TABLA JOBS] Job %d eliminado sin mandar releases.\n", job_id);
            return;
        }
    }
    printf("[ERROR-TABLA JOBS] No se encontró el job %d para eliminar.\n", job_id);
}


int conocer_estado_job(int job_id){
    for(int x=0;x<MAX_JOBS_ACTIVOS;x++){
        if(tabla_jobs_activos[x].job_id==job_id && tabla_jobs_activos[x].estado_job!=0){
            return tabla_jobs_activos[x].estado_job;
        }
    }
    return -1;
}

void marcar_job_concedido(int job_id){
    for(int x=0;x<MAX_JOBS_ACTIVOS;x++){
        if(tabla_jobs_activos[x].estado_job!=0 && tabla_jobs_activos[x].job_id==job_id){
            tabla_jobs_activos[x].estado_job=2;
            printf("[INFO-TABLA JOBS] Job %d marcado como CONCEDIDO.\n", job_id);
            return;
        }
    }
    printf("[ERROR-TABLA JOBS] Job %d inexistente. No se pudo marcar concedido.\n", job_id);
}

void registrar_recurso_job(int job_id, char* ip, int puerto, char* recurso_name, int amount){
    int indice=-1;
    int encontrado=0;

    for(int x=0;x<MAX_JOBS_ACTIVOS && !encontrado;x++){
        if(tabla_jobs_activos[x].estado_job!=0 && tabla_jobs_activos[x].job_id==job_id){
            indice=x;
            encontrado=1;
        }
    }

    //si no existe... : lo creamos
    if(indice==-1){
        encontrado=0;
        for(int x=0;x<MAX_JOBS_ACTIVOS && !encontrado;x++){
            if(tabla_jobs_activos[x].estado_job==0){
                indice=x;
                //Limpiamos
                memset(&tabla_jobs_activos[x],0,sizeof(TablaJobActivos)); // Rellena el bloque con 0 (sobreescribe los viejos)
                tabla_jobs_activos[x].job_id=job_id;
                tabla_jobs_activos[x].estado_job=1;
                tabla_jobs_activos[x].cantidad_recursos=0;
                encontrado=1;
            }
        }   
    }
    
    // Insertamos los datos.
    if(indice!=-1){ 
        int r=tabla_jobs_activos[indice].cantidad_recursos;
        
        //EVITAR DUPLICADOS -> Sumamos la cantidad al existente.
        int existe=0;

        for(int x=0;x<r && !existe;x++){
            int existia=strcmp(tabla_jobs_activos[indice].recursos[x].ip, ip)==0 && tabla_jobs_activos[indice].recursos[x].puerto ==puerto && strcmp(tabla_jobs_activos[indice].recursos[x].recurso_name, recurso_name)==0;

            if(existia){
                existe=1;
                tabla_jobs_activos[indice].recursos[x].amount+=amount;
                printf("[INFO - TABLA JOBS] El recurso %s en %s:%d ya estaba registrado para el job %d. Se sumo la cantidad. TOTAL: %d.\n",recurso_name,ip,puerto,job_id,tabla_jobs_activos[indice].recursos[x].amount);
            }
        }

        if(!existe){
            if(r<MAX_RECURSOS_POR_JOB){
                RecursoConcedido * tabla=&tabla_jobs_activos[indice].recursos[r];
                strncpy(tabla->ip, ip, 15);
                tabla->ip[15]='\0';
                
                tabla->puerto=puerto;
                
                strncpy(tabla->recurso_name,recurso_name,15);
                tabla->recurso_name[15] ='\0';
                
                tabla->amount=amount;
                
                tabla_jobs_activos[indice].cantidad_recursos++;
            }else{
                printf("[WARNING] El job %d superó el máximo de %d recursos permitidos.\n", job_id, MAX_RECURSOS_POR_JOB);
            }
        }

    }else{
        printf("[WARNING] Tabla de jobs llena. No se pudo registrar el job %d.\n", job_id);
    }
    
}
