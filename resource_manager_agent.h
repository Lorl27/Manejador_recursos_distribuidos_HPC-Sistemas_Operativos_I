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
#include <linux/time.h>
#include <fcntl.h>
#include <asm-generic/socket.h>
#include <errno.h>


#define MAX_NODOS 50 // maximo de nodos que podemos llegar a conocer (simultaneas)
#define MAX_MSG 1024
#define MAX_EVENTS 64 //cant eventos epoll (simultaneos)
#define MAX_PENDING 100 // maximo de peticiones hacia otros nodos (simultaneas)

#define INTERVALO_SEG 3
#define TIEMPO_CAIDO 15

#define BROADCAST_IP "255.255.255.255"
#define LOCAL_IP "127.0.0.1" 

#define PUERTO_BROADCAST 8888

#define CAIDO(x) ((time(NULL)-(x)->timestamp)>TIEMPO_CAIDO)

//ANCHOR ESTRUCTURAS BASE DE DATOS:
typedef struct _TablaNodos{
    char IP[16];
    int puerto;
    char recursos[128];
    time_t timestamp;
} TablaNodos;

//Uso: Para gestionar nuestros recursos, cuando nos lo piden.
typedef struct _SolicitudRecurso {
    int job_id;
    int amount;
    int fd_origen; // El socket al que le tenemos que mandar el GRANTED
} SolicitudRecurso;

//Uso: Para guardar en memoria quièn fue el que pidio originalmente el recurso, luego de pedirle a Erlang el mismo.
typedef struct _SolicitudRespuestaRecurso{
    int fd_remoto; //socket que se comunica con el otro nodo.
    int fd_erlang; //a quien hay que darle la respuesta (socket local)
    int job_id;
    int activo; //1: en uso - 0:libre
    //campos para que NO sea bloqueante:
    int conectando;  //1: esperando conexion - 0: ya conectado.
    char recurso_name[16];
    int amount;
} SolicitudRespuestaRecurso;

typedef struct _RecursosLocales{
    char nombre[16];
    int capacidadTotal;
    int cantidadDisponible;
    Cola solicitudesPendientes;
}RecursosLocales;

//ANCHOR -- Inicializaciòn

//Inicializa los recursos pasados (formato: "cpu:4 mem:8192 gpu:1") en el main.c ,
// para poder utilizarlos.
void inicializar_mis_recursos(char * mis_recursos);

//ANCHOR - Funciones axuiliares cola

//copia la solicitud de recurso.
void* copiar_solicitud(void* dato);

//libera la memoria de la solicitud de recurso, destruyendola.
void destruir_solicitud(void* dato);

//ANCHOR - CREACION SERVIDORES

//Crea un servidor en el puerto TCP pùblico (para otros nodos)
// retorna fd del socket.
int crear_servidor_tcp_publico(int puerto);

//Crea un servidor en el puerto TCP en localhost (para Elang)
// retorna fd del socket.
int crear_servidor_tcp_local(int puerto);

//ANCHOR - CREACION SOCKETS

// Crea socket UDP
// recibe y envia ANNOUNCE.
// retorna fd del socket.
int crear_socket_broadcast(void);

// Inicia conexiòn TCP hacia el agente remoto.
// retorna fd del socket ò -1 si fallo.
int crear_conexion_cliente(const char * ip_destino, int puerto_destino);

//ANCHOR -- Eventos principales

// Envia un anuncio y espera 2s para recibir anuncios de otros nodos ya activos.
void ejecutar_arranque_inicial(int epoll_fd, int sock_udp_broadcast,char * ip, int puerto_tcp, char * recursos);

/*
Crea sockets TCP public y local ademàs UPD Broadcats, utilizando datos de la computadora para la configuraciòn.
anuncia mensajes periodicos, con el formato ANNOUNCE <IP> <puerto> <recursos> por broadcast
Crea y registra eventos en epoll para luego atenderlos.
*/
void iniciar_event_loop(char* mi_ip_lan, int mi_puerto_publico,int mi_puerto_local, char* mis_recursos);


//ANCHOR - Validaciones y gestiones

//Valida que el mensaje sea uno de los vàlidos.
// RESERVE <job_id> <resource_name> <amount>
// GRANTED <job_id>
// DENIED <job_id>
// RELEASE <job_id> <resource_name> <amount>
// JOB_REQUEST <job_id> @<ip>:<recurso>:<cantidad> (Conexion entre Erlang y C)
// JOB_RELEASE <job_id>
// JOB_STATUS <job_id>
// GET NODES (Conexion entre Erlang y C).
int validar_mensajes_validos(char * mensaje);

/* 
Si recibe RESERVE :
    - si hay suficiente disponible, se descuenta y responde GRANTED
    - sino, se encola la solicitud.

Si recibe RELEASE:
    - se libera la cantidad, se descuenta del job y se atienden las encoladas por orden.
*/
void gestionar_recursos_locales(RecursosLocales * recursos, char * comando, int job_id, int amount,int fd_cliente);

//ANCHOR - Manejo de la Tabla de Nodos

// Si encontramos un nodo cuyo tiempo supero los 15s, se elimina de la tabla.
// Se reemplaza con el ùltimo nodo disponible, y se vuelve a verificar que no se encuentre caìdo.
void limpiar_nodos_caidos();

//Inserta el nodo en la tablaNodos (si es que no existia)
// SI existia antes, actualizamos timestamp.
void insertar_en_tablaNodos(char * buffer);

//Envia la lista de nodos a erlang (NODES ip:puerto:recursos;ip..)
void enviar_lista_nodos(int fd_erlang);

// Busca el puerto de una IP en tablaNodos
// retorna -1 si no lo encuentra.
int buscar_puerto_por_IP(char * ip);

//ANCHOR -  Solicitud Respuesta Recursos 

//Busca un hueco libre para guardar los datos relacionados.
void guardar_datos_solicitud_respuesta(int fd_remoto,int fd_erlang,int job_id,char* recurso_name,int amount);

// Busca el socket de erlang, libera el hueco y devuelve el mismo.
// devuelve -1 si no lo encuentra.
int obtener_socket_respuesta(int fd_remoto,int job_id);

#endif