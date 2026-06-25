#ifndef __RESOURCE_MANAGER_AGENT_H__
#define __RESOURCE_MANAGER_AGENT_H__

#include <stdlib.h>
#include <stdio.h>
#include <cola.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <time.h>
#include <sys/epoll.h>
#include <netinet/in.h>

#define MAX_NODOS 50 // max que podemos llegar a conocer.
#define MAX_MSG 1024
#define INTERVALO_SEG 3
#define TIEMPO_CAIDO 15
#define MAX_EVENTS 10 //cant eventos  epoll

#define CAIDO(x) ((time(NULL)-(x)->timestamp)>TIEMPO_CAIDO)


#define BROADCAST_IP "255.255.255.255"
#define PUERTO_BROADCAST 8888

//ANCHOR ESTRUCTURAS BASE DE DATOS:
typedef struct _TablaNodos{
    char IP[16];
    int puerto;
    char recursos[128];
    time_t timestamp;
} TablaNodos;


typedef struct _RecursosLocales{
    char nombre[16];
    int capacidadTotal;
    int cantidadDisponible;
    Cola solicitudesPendientes;
}RecursosLocales;

//ANCHOR - CREACION SOCKETS

//Crea un servidor en el puerto TCP pùblico (para otros nodos)
// retorna fd del socket.
int crear_servidor_tcp_publico(int puerto);

//Crea un servidor en el puerto TCP en localhost (para Elang)
// retorna fd del socket.
int crear_servidor_tcp_local(int puerto);

// Crea socket UDP
// recibe y envia ANNOUNCE.
// retorna fd del socket.
int crear_socket_broadcast(void);

// Inicia conexiòn TCP hacia el agente remoto.
// retorna fd del socket.
int crear_conexion_cliente(const char * ip_destino, int puerto_destino);

//ANCHOR -- eventos principales

//Crea el epoll y el loop de mensajes perioòdicamente.
//Manda periodicamente un mensaje con el formato ANNOUNCE <IP> <puerto> <recursos> por broadcast
void iniciar_event_loop(void);

// Envia un anuncio y espera 2s para recibir anuncios de otros nodos ya activos.,,.-- 
// EL socket UDP se agrega al conjunto de EPOLL - eventos EPOLLIN.
// Luego, atiende peticiones normales.
void ejecutar_arranque_inicial(int epoll_fd, int sock_udp_broadcast);


//ANCHOR - Validaciones y gestiones

//Valida que el mensaje sea uno de los vàlidos.
// RESERVE <job_id> <resource_name> <amount>
// GRANTED <job_id>
// DENIED <job_id>
// RELEASE <job_id> <resource_name> <amount>
int validar_mensajes_validos(char * mensaje);

/* 
Si recibe RESERVE :
    - si hay suficiente disponible, se descuenta y responde GRANTED
    - sino, se encola la solicitud.

Si recibe RELEASE:
    - se libera la cantidad, se descuenta del job y se atienden las encoladas por orden.
*/
void gestionar_recursos_locales(RecursosLocales * recursos, char * comando, int job_id, int amount);

//ANCHOR - Otros

//Inserta el nodo en la tablaNodos (si es que no existia)
// SI existia antes, actualizamos timestamp.
void insertar_en_tablaNodos(char * buffer);
#endif