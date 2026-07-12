#ifndef __RESOURCE_MANAGER_AGENT_H__
#define __RESOURCE_MANAGER_AGENT_H__

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include "cola.h"
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>


#define MAX_NODOS 256 // Máximo de nodos que podemos llegar a conocer simultáneamente 
#define MAX_MSG 1024
#define MAX_EVENTS 64 // Máximo eventos epoll simultáneos 
#define MAX_PENDING 256 // Máximas peticiones simultáneas hacia otros nodos 
#define MAX_JOBS_ACTIVOS 256 // Cantidad máxima de jobs simultáneos
#define MAX_RECURSOS_POR_JOB 10 // Cantidad máxima de recursos distintos pedidos simultáneos por job

#define INTERVALO_SEG 3 // Frecuencia del envío de ANNOUNCE por Broadcast.
#define TIEMPO_CAIDO 15
#define TIMEOUT_JOB_SEG 120  //Tiempo máximo de espera para eliminar reservas inconclusas.

#define BROADCAST_IP "255.255.255.255"
#define LOCAL_IP "127.0.0.1" 

#define PUERTO_BROADCAST 8888

#define CAIDO(x) ((time(NULL)-(x)->timestamp)>TIEMPO_CAIDO)

//ANCHOR ESTRUCTURAS BASE DE DATOS:

//Uso: Registro de los nodos descubiertos dinámicamente mediante Broadcast
// Permite saber a qué IP/Puerto conectarnos cuando Erlang nos pide buscar recursos externos.
typedef struct _TablaNodos{
    char IP[16];
    int puerto;
    char recursos[128];
    time_t timestamp;
} TablaNodos;

//Uso: Para encolar las peticiones cuando no tenemos stock disponible.
typedef struct _SolicitudRecurso {
    int job_id;
    int amount;
    int fd_origen; // El socket al que le tenemos que mandar el GRANTED
} SolicitudRecurso;

//Uso: Para guardar en memoria quién fue el que pidió originalmente el recurso,
// luego de que Erlang nos hiciera la solicitud.
typedef struct _SolicitudRespuestaRecurso{
    int fd_remoto; //socket que se comunica con el otro nodo.
    int fd_erlang; //a quien hay que darle la respuesta (socket local) ó -1 si es RELEASE
    int job_id;
    int activo; //1: en uso ; 0:libre

    //Campos para que NO sea bloqueante:
    int conectando;  //1: esperando conexión ; 0: ya conectado.
    char recurso_name[16];
    int amount;

    //Para el envío de RESERVE/RELEASE:
    char ip[16];
    int puerto;
    int es_release; //1: Es release ; 0: Es reserve (No es release)
    time_t timestamp;
} SolicitudRespuestaRecurso;

//Uso: Para recuperar nuestros recursos si el cliente se desconectó de forma brusca
typedef struct _Asignacion {
    int fd_cliente;
    int amount;
    int job_id;
} Asignacion;

//Uso: Administra el stock del recurso local
typedef struct _RecursosLocales{
    char nombre[16];
    int capacidadTotal;
    int cantidadDisponible;
    Cola solicitudesPendientes;
    int cantidad_asignaciones;
    Asignacion asignaciones[MAX_PENDING]; //Para saber a quién le prestamos nuestros recursos.
}RecursosLocales;

// USO: Para registrar qué recursos nos han concedido otros nodos
typedef struct _RecursoConcedido {
    char ip[16];
    int puerto;
    char recurso_name[16];
    int amount;
    int fd_remoto; //Para mantener viva la conexión hasta recibir RELEASE
} RecursoConcedido;

typedef enum _TablaJobActivoEstado{
    LIBRE = 0,
    SOLICITANDO = 1,
    ACTIVO = 2
} TablaJobActivosEstado;

//Uso: Realiza el seguimiento de un Job solicitado por Erlang
// Controla cuántas respuestas llegaron de la red y define si la transacción fue exitosa o si requiere Rollback.
typedef struct _TablaJobActivos {
    int job_id;
    TablaJobActivosEstado estado_job; 
    int cantidad_recursos; //Recursos GRANTED exitosos guardados
    int cantidad_esperados; //Recursos pedidos en total en JOB_REQUEST
    int cantidad_respondidos; //Cuántos GRANTED/DENIED nos llegaron
    int cantidad_denegados; // si es >0 : La transacción global falló -> Se precisa rollback.

    RecursoConcedido recursos[MAX_RECURSOS_POR_JOB]; //Detalle de los recursos que nos prestaron
} TablaJobActivos;



//ANCHOR -- Inicialización

//Inicializa la estructura de RecursosLocales con los recursos pasados en formato: "cpu:4 mem:8192 gpu:1".
void inicializar_mis_recursos(const char * mis_recursos);

//ANCHOR - Funciones axuiliares cola

//Copia la solicitud de recurso.
void* copiar_solicitud(void* dato);

//Libera la memoria de la solicitud de recurso, destruyendola.
void destruir_solicitud(void* dato);

//Elimina las solicitudes pendientes de la Cola asociadas al cliente (fd_caido) que se ha desconectado.  
Cola purgar_solicitudes_por_fd(Cola cola, int fd_caido);

//ANCHOR - CREACION SERVIDORES

//Crea un servidor en el puerto TCP público (para otros nodos)
// retorna fd del socket.
int crear_servidor_tcp_publico(const char * ip_lan, int puerto);

//Crea un servidor  TCP en localhost ( Exclusivo para Erlang)
//Retorna fd del socket.
int crear_servidor_tcp_local(int puerto);

//ANCHOR - CREACION SOCKETS

// Crea socket UDP
// recibe y envia ANNOUNCE.
// retorna fd del socket.
int crear_socket_broadcast(void);

// Inicia conexión TCP hacia el agente remoto.
// Retorna fd del socket ó -1 si falla.
int crear_conexion_cliente(const char * ip_destino, int puerto_destino);

//ANCHOR -- Eventos principales

// Envia un anuncio y espera 2s para recibir anuncios de otros nodos ya activos.
void ejecutar_arranque_inicial(int epoll_fd, int socket_broadcast_recv, int socket_broadcast_send, int puerto_tcp, char * recursos);

/*
Inicializa el agente de recursos.
 
Crea:
    - Un Socket UDP exlusivo para envios asociado a la IP del nodo.
    - Un servidor TCP público asociado a la IP del nodo.
    - Un servidor TCP local asociado a 127.0.0.1.
    - Un socket UDP para recepción de anuncios por broadcast ((0.0.0.0)).
 
Ambos servidores TCP escuchan sobre el mismo puerto.
 
Configura epoll, registra todos los descriptores y envía periódicamente mensajes con el formato:
 ANNOUNCE <PUERTO> <RECURSOS>
 */
void iniciar_event_loop(char* mi_ip_lan, int mi_puerto_publico, char* mis_recursos);


//ANCHOR - Validaciones y gestiones

//Valida que el mensaje sea uno de los válidos.
// - RESERVE <job_id> <resource_name> <amount>
// - GRANTED <job_id>
// - DENIED <job_id>
// - RELEASE <job_id> <resource_name> <amount>
// - JOB_REQUEST <job_id> @<ip>:<recurso>:<cantidad> (Conexión entre Erlang y C)
// - JOB_RELEASE <job_id>
// - JOB_STATUS <job_id>
// - GET NODES (Conexión entre Erlang y C).
int validar_mensajes_validos(const char * mensaje);

/*
Gestiona una petición sobre un recurso local:

Si recibe RESERVE :
    - Si hay disponibilidad, se concede el recurso y responde GRANTED
    - Sino, se encola la solicitud.

Si recibe RELEASE:
    - Se libera el recurso y se atienden las solicitudes encoladas por orden.
*/
void gestionar_recursos_locales(RecursosLocales * recursos, const char * comando, int job_id, int amount,int fd_cliente);

//Retorna la cantidad de recursos solicitados en JOB_REQUEST
int contar_recursos_pedidos_Erlang(const char *mensaje_original);

// Limpia todas las reservas (y solicitudes encoladas) asociadas al socket que se desconectó.
//  Recupera todos los recursos que se le hubieran prestado.
void limpiar_recursos_por_desconexion(int fd);

//ANCHOR - Manejo de la Tabla de Nodos

// Si encontramos un nodo cuyo tiempo supero el de inactividad (15s), se elimina de la tabla.
// Se reemplaza con el último nodo disponible, y se vuelve a verificar que no se encuentre caído.
void limpiar_nodos_caidos();

// Inserta el nuevo nodo en TablaNodos
// Si el nodo ya existia, actualiza su timestamp.
void insertar_en_tablaNodos(const char * buffer, const char *ip_recibida);

//Envia la lista de nodos activos a Erlang (NODES ip:puerto:recursos;ip...)
void enviar_lista_nodos(int fd_erlang);

// Busca el puerto asociado a una IP en TablaNodos ó a mi IP Global
// Para mayor seguiridad: si hay varias instancias en una misma IP, verifica cuál de ellas anunció tener el recurso solicitado.
// retorna -1 si no lo encuentra.
int buscar_puerto_por_IP_y_recurso(const char * ip, const char * recurso);
//ANCHOR -  Solicitud Respuesta Recursos 

//Busca un hueco libre para guardar los datos relacionados a la solicitud pendiente (RESERVE/RELEASE).
//Devuelve el índice si tiene éxito ó -1 si la capacidad está llena.
int guardar_datos_solicitud_respuesta(int fd_remoto,int fd_erlang,int job_id,const char* recurso_name,int amount, const char* ip, int puerto, int es_release);

//ANCHOR - MANEJO TABLA DE JOB ACTIVOS 

/*
  Libera un job del sistema y limpia todas sus conexiones de red asociadas.
  Realiza el proceso en 3 fases:
  1. Limpia y cierra los sockets de solicitudes pendientes para evitar fugas de conexiones.
  2. Envía el mensaje RELEASE por los sockets ya establecidos de los recursos concedidos, y luego los cierra.
  3. Elimina el registro del job de la TablaJobActivos.
 */
void liberar_job(int job_id,int epoll_fd);

//Retorna el estado del job_id 
// -1 si no lo encuentra.
int conocer_estado_job(int job_id);

// Registra un nuevo recurso solicitado para un Job en la tabla
// Si ya estaba registrado, incrementa la capacidad registrada del mismo.
// Si no se pudo registrar retorna 0. Si pudo hacerlo: retorna 1.
int registrar_recurso_job(int job_id, const char* ip, int puerto, const char* recurso_name, int amount, int fd_remoto);

#endif