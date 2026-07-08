# Manejador de Recursos Distribuidos para HPC (Agente C)
Es un middleware distribuido para la gestión de recursos (CPU,Memoria y GPU) en un clúster. Implementa concurrencia asincrónica mediante `epoll` para coordinar el acceso a los recursos sin bloquear el procesamiento de múltiples conexiones simultáneas.

El agente utiliza 3 capas de comunicación simultáneas:

1. **UDP BROADCAST:** Envía anuncios (`ANNOUNCE`) cada 3 segundos, para el descubrimiento dinámico de otros nodos.
2. **TCP PÚBLICO:** Atiende las peticiones de otros agentes (`RESERVE / RELEASE`) de forma no bloqueante, con epoll.
3. **TCP LOCAL (ERLANG):** Interfaz TCP exclusiva para peticiones locales del planificador de Erlang (`JOB_REQUEST / JOB_RELEASE / JOB_STATUS / GET NODES`) que permiten coordinar la comunicación.

## Requisitos previos
```console
sudo apt update
sudo apt install build-essential libc6-dev xterm
```

## Compilación
```bash
 make rebuild # Genera un binario ejecutable llamado agente_recursos
```

## Ejecución
Requiere:
- `IP:` Dirección IP de la interfaz de escucha pública 
- `PUERTO:` Puerto TCP utilizado simultáneamente para:
  - Comunicación entre agentes (Mediante socket asociado a IP Pública del nodo)
  - Comunicación local exclusiva con Erlang (Mediante socket asociado a `IP_LOCAL = 127.0.0.1`).
- `RECURSOS:` Cadena de texto con los recursos locales disponibles.
```markdown
./agente_recursos <IP> <PUERTO>  "<RECURSOS>"
```

#### Ejemplo de ejecución
```console
./agente_recursos 192.168.1.50 8100 "cpu:4 mem:8192 gpu:1"
```

### Ejecución para pruebas locales (Simulando Erlang con NetCat)
1. Iniciar el clúster local de prueba : levantará Nodo A (origen/local) y B (remoto) en ventanas separadas: 
 ```bash
    ./test_deadlock.sh
```
Se imprimirá por pantalla el mensaje:`[INFO-ARRANQUE] Enviando primer anuncio...` y como ambos agentes se ejecutan en la misma máquina, intercambiarán automáticamente mensajes `ANNOUNCE` y se descubrirán mutuamente, añadiéndose a su respectiva `TablaNodos`.

2. En una terminal nueva, para simular el planificador Erlang del Nodo A, conectarse por TCP local con:  
 ```bash
  nc 127.0.0.1 8001 # Si desea del Nodo B: nc 127.0.0.1 8002
```
3. Ejecutar los siguientes comandos en la terminal de NetCat para validar el flujo:
   1. `GET NODES`: Consulta nodos vivos.
   El Nodo A registrará el comando recibido y responderá con la lista de nodos activos (`NODES IP:PUERTO:RECURSOS;`). 
   En la terminal de Erlang, aparecerá dicha lista.
   2. `JOB_REQUEST <job_id> <@IP>:<recurso>:<cantidad_recurso>`: Solicita `<recurso>` al nodo con IP `<@IP> `.
   Por ejemplo: `JOB_REQUEST 1001 @127.0.0.2:gpu:1` solicita `gpu` al Nodo B.
   Este comando provocará que el Nodo A busque la IP en  `TablaNodos` y abra una conexión TCP con el Nodo remoto. Luego le enviará `RESERVE 1001 gpu 1` al Nodo B y este verificará la `<cantidad_recurso>` (En este caso: `1`).
         - Si tiene capacidad enviará `GRANTED 1001`. El Nodo A recibe la respuesta y tras actualizar `TablaJobActivos`, le notificará a Erlang `JOB_GRANTED 1001`.
         - Si no tiene capacidad, la solicitud se encolará (ver sección de Cola FIFO) hasta que exista disponibilidad. En caso de que la operación deba abortarse enviará `DENIED 1001` y el Nodo A realizará el rollback liberando los recursos obtenidos parcialmente y, finalmente, le notificará a Erlang  `JOB_DENIED 1001`.
       - Si no encuentra la IP: el Nodo A realizará el rollback liberando los recursos obtenidos parcialmente y, finalmente, le notificará a Erlang  `JOB_DENIED 1001`, indicando que el nodo solicitado no está registrado en `TablaNodos`. 
     1. `JOB_RELEASE <job_id>`: Libera todos los recursos asociados a `<job_id>`
    Por ejemplo `JOB_RELEASE 1001` provocará que el Nodo A consulte en `TablaJobActivos` los nodos cuyos recursos se le hayan asignado al job, para luego abrir una conexión TCP con cada uno de ellos e ir enviando `RELEASE <job_id> <recurso> <cantidad_recurso>` ( En este caso`RELEASE 1001 gpu 1`). 
    Por último, limpia el registro en `TablaJobActivos`.
    (Nota: Si se intenta liberar un Job inexistente, el agente registrará una advertencia interna sin afectar el sistema).
     2. `JOB_STATUS <job_id>`: Devuelve el estado del job.
    Por ejemplo: `JOB_STATUS 1001` provocará que el Nodo A revise `TablaJobActivos` y:
          - Si existe: envie a Erlang `JOB_STATUS <estado>`
          - Si no existe: tire la advertencia `[ERLANG-C WARNING] NO se encontro estado para el job <job_id>`

## Funcionamiento de la Cola FIFO de solicitudes
La implementación garantiza una gestión justa de los recursos cuando la demanda supera el stock actual de un nodo.

*  **Encolamiento automático:** Si Erlang solicita un recurso mediante `JOB_REQUEST 1002 @127.0.2:gpu:1` y el Nodo B no tiene `gpu` disponible en ese momento, éste agregará la petición a la cola de espera e informará con : `[INFO-COLA] Sin stock de gpu. Solicitud 1002 agregada a la cola.`.
*  **Liberación y reasignación:** Cuando el Job anterior termine y Erlang envie `JOB_RELEASE 1001`, provocará que el Nodo A busque el `job` en `TablaJobActivos` para encontrar a qué Nodo se le pidió el recurso `gpu`. Luego, le enviará `RELEASE 1001 gpu 1` a ese mismo Nodo. Entonces, el Nodo B liberará la `gpu`. Luego, éste revisará su cola FIFO, detectará que el primer elemento de la cola pertenece al `job 1002` (estaba esperando), le asignará el recurso y enviará `GRANTED 1002`. Finalmente, en la terminal de Erlang, recibimos `JOB_GRANTED 1002`.
*  **Manejo de transacciones parciales:** Si un `JOB_REQUEST` incluye múltiples recursos (Por ejemplo: `JOB_REQUEST 1003 @192.168.1.2:cpu:2 @192.168.1.3:gpu:1`) y uno de ellos falla o es denegado (`DENIED`): el Nodo A aplicará un rollback automático, enviando `RELEASE` a los recursos que había concedido parcialmente. Esto evita que se produzcan `DEADLOCKS` por retención de recursos bloqueados.

Esto permite que un mismo job solicite recursos distribuidos en distintos nodos garantizando consistencia: el job obtiene todos los recursos solicitados o ninguno de ellos, manteniendo además el orden FIFO para las solicitudes pendientes.

## Estructuras internas
El agente mantiene tres estructuras principales:

- **TablaNodos:** registra los nodos descubiertos mediante `ANNOUNCE`, junto con sus recursos anunciados y la marca temporal (timestamp) del último anuncio recibido.

- **TablaJobActivos:** mantiene los recursos concedidos a cada job, permitiendo consultar su estado y liberar posteriormente todas las reservas asociadas.

- **Colas FIFO por recurso:** cada recurso local (`CPU`, `GPU` y `MEM`) mantiene una cola independiente de solicitudes pendientes, que se utiliza cuando la capacidad disponible resulta insuficiente.

## Protocolo de mensajes

#### Entre Erlang y el agente
`
GET NODES |
JOB_REQUEST |
JOB_RELEASE |
JOB_STATUS |
JOB_TIMEOUT
`

#### Entre agentes
`
ANNOUNCE |
RESERVE |
RELEASE |
GRANTED |
DENIED 
`

#### Diagrama de la arquitectura :
```
            UDP Broadcast
        +-------------------+
        |                   |
+---------------+    +---------------+
| Agente C (A)  |<-->| Agente C (B)  |
+---------------+    +---------------+
        ^                    ^
        |                    |
   TCP Local            TCP Local
        |                    |
+-------------+        +-------------+
|  Erlang A   |        |  Erlang B   |
+-------------+        +-------------+
```

## Manejo de fallos

El agente cuenta con un sistema de auto-limpieza para garantizar la estabilidad del clúster:
* **Nodos caídos:** Si un nodo deja de enviar `ANNOUNCE`, tras 15 segundos se considerará caído y se eliminará de la `TablaNodos`, dejando de estar disponibles para futuras reservas.
* **Caídas de red:** Si un cliente TCP se desconecta inesperadamente, los recursos que se le habían asignado se recuperan de inmediato.
* **Time-Outs:** Las reservas a otros nodos que queden trabadas y no se completen en 10 segundos son abortadas automáticamente, enviando al planificador de Erlang : `JOB_TIMEOUT`.


# Autora
Antonella Grassi

> Testeado en Ubuntu 24.04.2 LTS
