# Manejador de Recursos Distribuidos para HPC (Agente C)
Es un middleware distribuido para la gestión de recursos (CPU,Memoria y GPU) en un clúster. Implementa concurrencia asincrónica mediante `epoll` para coordinar el acceso a los recursos sin bloquear el procesamiento de múltiples conexiones simultáneas.

El agente utiliza 3 capas de comunicación simultáneas:

1. **UDP BROADCAST:** Envía anuncios (`ANNOUNCE`) cada 3 segundos para el descubrimiento dinámico de otros nodos.
Implementa una arquitectura de **doble socket UDP**: utiliza un socket de **recepción** bindeado a `0.0.0.0`para escuchar la red, y un socket de **envío** dedicado bindeado explícitamente a la `IP local` del nodo.
2. **TCP PÚBLICO:** Atiende las peticiones de otros agentes (`RESERVE / RELEASE`) de forma no bloqueante, con epoll.
3. **TCP LOCAL (ERLANG):** Interfaz TCP exclusiva para peticiones locales del planificador de Erlang (`JOB_REQUEST / JOB_RELEASE / JOB_STATUS / GET NODES`) que permiten coordinar la comunicación.

Todas las conexiones TCP utilizadas entre agentes permanecen abiertas mientras el recurso permanezca asignado al job correspondiente. Esto permite que la orden `RELEASE` sea enviada posteriormente utilizando la misma conexión, evitando pérdidas de estado en las colas FIFO del nodo remoto.

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
- `IP:` Dirección IP (**) de la interfaz de escucha pública (*)
- `PUERTO:` Puerto TCP utilizado simultáneamente para:
  - Comunicación entre agentes (Mediante socket asociado a IP Pública del nodo)
  - Comunicación local exclusiva con Erlang (Mediante socket asociado a `IP_LOCAL = 127.0.0.1`).
- `RECURSOS:` Cadena de texto con los recursos locales disponibles.
```markdown
./agente_recursos <IP> <PUERTO>  "<RECURSOS>"
```

> (*) Para pruebas multi-nodos en una misma máquina, cada instancia necesita una IP distinta (Ej `127.0.0.2` y `127.0.0.3`) ya que el agente usa su propia IP tanto para identificarse a sí mismo como para resolver sus propios recursos dentro del mismo mecanismo que usa para pedir recursos remotos (`@host:recurso:cantidad`).
> (**) Debe ser la que te devuelve `hostname -I` , si no, `bind()` falla con `Cannot assign requested address`.

#### Ejemplo de ejecución
```bash
# 192.168.1.50 es ILUSTRATIVO: reemplazalo por el resultado de `hostname -I`,
# o por una IP de loopback (127.0.0.x) si es para pruebas locales con varios nodos.
./agente_recursos 192.168.1.50 8100 "cpu:4 mem:8192 gpu:1"
```

### Ejecución para pruebas automáticas
1. Dar permisos de ejecución al script:
 ```bash
  chmod +x Scripts/test_deadlock.sh
 ```
2. Iniciar el script automático
 ```bash
  ./Scripts/test_deadlock.sh
 ```

Este script orquesta de forma autónoma la creación de dos nodos, aguarda la sincronización de la topología vía UDP Broadcast, y simula al planificador Erlang inyectando peticiones cruzadas (Hold and Wait). Al ejecutarlo, se puede observar en las consolas resultantes **dos casos** dependiendo del timing exacto:
   **A.** El sistema entra en interbloqueo y resuelve la colisión automáticamente mediante el mecanismo de Timeout y Rollback implementado en C.
   **B.** Un Job resultó ser más rápido que el otro , resultando en que uno obtiene todo de inmediato y el otro: se encola en la cola FIFO y éste mismo se resolverá apenas se libere el primer Job.

### Ejecución para pruebas locales (Simulando Erlang con NetCat)

1. Dar permisos de ejecución e iniciar el clúster local de prueba : levantará Nodo A (origen/local) y B (remoto) en ventanas separadas: 
 ```bash
    chmod +x Scripts/test_deadlock_manual.sh
    ./Scripts/test_deadlock_manual.sh
```
Se imprimirá por pantalla el mensaje:`[INFO-ARRANQUE] Enviando primer anuncio...`. Ambos intercambiarán automáticamente mensajes `ANNOUNCE` y se descubrirán mutuamente, añadiéndose a su respectiva `TablaNodos`.

2. En una terminal nueva, para simular el planificador Erlang del Nodo A, conectarse por TCP local con:  
 ```bash
  nc 127.0.0.1 8001 # Si desea del Nodo B: nc 127.0.0.1 8002
```
3. Ejecutar los siguientes comandos en la terminal de NetCat para validar el flujo:
   1. `GET NODES`: Consulta nodos vivos.
   El Nodo A registrará el comando recibido y responderá con la lista de nodos activos (`NODES IP:PUERTO:RECURSOS;`). 
   En la terminal de Erlang, aparecerá dicha lista. 
   (`NODES 127.0.0.3:8002:cpu:2:gpu:1;`)
   Tener en cuenta la IP listada para el siguiente paso. 
   2. `JOB_REQUEST <job_id> <@IP>:<recurso>:<cantidad_recurso>`: Solicita `<recurso>` al nodo con IP `<@IP> `.
   Por ejemplo: `JOB_REQUEST 1001 @127.0.0.3:gpu:1` solicita `gpu` al Nodo B.
   Este comando provocará que el Nodo A busque la IP en  `TablaNodos` y establezca una conexión TCP con el Nodo remoto. Luego le enviará `RESERVE 1001 gpu 1` al Nodo B y este verificará la `<cantidad_recurso>` (En este caso: `1`).
         - Si tiene capacidad enviará `GRANTED 1001`. El Nodo A recibe la respuesta y tras actualizar `TablaJobActivos`, le notificará a Erlang `JOB_GRANTED 1001`.
         - Si no tiene capacidad, la solicitud se encolará (ver sección de Cola FIFO) hasta que exista disponibilidad.
         -  En caso de que la operación deba abortarse enviará `DENIED 1001` y el Nodo A realizará el rollback automático liberando los recursos obtenidos parcialmente y, finalmente, le notificará a Erlang  `JOB_DENIED 1001`.
       - Si no encuentra la IP: el Nodo A realizará el rollback liberando los recursos obtenidos parcialmente y, finalmente, le notificará a Erlang  `JOB_DENIED 1001`, indicando que el nodo solicitado no está registrado en `TablaNodos`. 
     1. `JOB_RELEASE <job_id>`: Libera todos los recursos asociados a `<job_id>`
    Por ejemplo `JOB_RELEASE 1001` provocará que el Nodo A consulte en `TablaJobActivos` los nodos cuyos recursos se le hayan asignado al job, para luego utilizar la conexión TCP persistente asociada a cada recurso e ir enviando `RELEASE <job_id> <recurso> <cantidad_recurso>` ( En este caso`RELEASE 1001 gpu 1`). 
    Por último, limpia el registro en `TablaJobActivos` y cierra las conexiones pendientes.
    (Nota: Si se intenta liberar un Job inexistente, el agente registrará una advertencia interna sin afectar el sistema).
     2. `JOB_STATUS <job_id>`: Devuelve el estado del job.
    Por ejemplo: `JOB_STATUS 1001` provocará que el Nodo A revise `TablaJobActivos` y:
          - Si existe: envie a Erlang `JOB_STATUS <estado>`
          - Si no existe: tire la advertencia `[ERLANG-C WARNING] NO se encontró estado para el job <job_id>`

## Funcionamiento de la Cola FIFO de solicitudes
La implementación garantiza una gestión justa de los recursos cuando la demanda supera el stock actual de un nodo.

*  **Encolamiento automático:** Si Erlang solicita un recurso mediante `JOB_REQUEST 1002 @127.0.0.2:gpu:1` y el Nodo B no tiene `gpu` disponible en ese momento, éste agregará la petición a la cola de espera e informará con : `[INFO-COLA] Sin stock de gpu. Solicitud 1002 agregada a la cola.`.
*  **Liberación y reasignación:** Cuando el Job anterior termine y Erlang envíe `JOB_RELEASE 1001`, provocará que el Nodo A busque el `job` en `TablaJobActivos` para encontrar a qué Nodo se le pidió el recurso `gpu`. Luego, le enviará `RELEASE 1001 gpu 1` a ese mismo Nodo. Entonces, el Nodo B liberará la `gpu`. Luego, éste revisará su cola FIFO, detectará que el primer elemento de la cola pertenece al `job 1002` (estaba esperando), le asignará el recurso y enviará `GRANTED 1002`. Finalmente, en la terminal de Erlang, recibimos `JOB_GRANTED 1002`.
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

> Nota: Un `JOB_REQUEST` apuntado a la propia IP del nodo se resuelve conectándose a sí mismo por TCP y pasando por el mismo camino `RESERVE/GRANTED` que cualquier pedido remoto.

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
* **Time-Outs:** Las reservas a otros nodos que queden trabadas ó jobs en estado de solicitud (1) que no logren completarse en 120 segundos (2 minutos) son abortadas automáticamente, enviando al planificador de Erlang : `JOB_TIMEOUT`.


# Autora
Antonella Grassi

> Testeado en Ubuntu 24.04.2 LTS
