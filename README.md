# Descripción del trabajo práctico
Es un middleware distribuido para la gestión de recursos en un clúster: CPU, memoria y GPU.

El nodo utiliza 3 capas de comunicaciòn:
1. **UDP BROADCAST:** Envia anuncios (*ANNOUNCE*) cada 3 segundos, para que otros nodos se descubran dinàmicamente.
2. **TCP PÙBLICO:** Atiende las peticiones de otros agentes (*RESERVE / RELEASE*) de forma no bloqueante, con epoll. 
3. **TCP LOCAL (ERLANG):** Escucha peticiones locales (*JOB_REQUEST / JOB_RELEASE / JOB_STATUS / GET NODES*) que permite coordinar la comunicaciòn.

## Librerias necesarias previas 
```console
sudo apt update
sudo apt install build-essential libc6-dev
```


## Compilacion
Genera un binario llamado agente_recursos
```console
make
```

## Ejecuciòn
Requiere la IP de escucha - puerto pùblico - puerto local para Erlang y, una cadena con los recursos locales disponibles.
```console
./agente_recursos <IP> <PUERTO_PUBLICO> <PUERTO_LOCAL_ERLANG> "<RECURSOS>"
```

#### Ejemplo de ejecuciòn:
```console
./agente_recursos 192.168.1.50 8100 9000 "cpu:4 mem:8192 gpu:1"
```

Al ejecutarlo, veràs en la consola: ```
[INFO-ARRANQUE] Enviando primer anuncio...```

### Flujo de operación

- Si se inicia otra instancia, ambos nodos se descubrirán mediante ```ANNOUNCE``` y se añadirán a la TablaNodos.

- Erlang ejecuta ``` GET NODES``` para obtener la tabla de nodos activos desde C, decide lanzar un job.
- Erlang envia ``` JOB_REQUEST 1001 @192.168.1.2:cpu:2```
- El agente de C realiza los sig. pasos: 
  - Busca el nodo en la tabla y crea la conexiòn asincrònica con el cliente
  - Registra el Job en TablaJobActivos y, el evento EPOLLOUT para confirma la conexiòn
  - Envia ```RESERVE 1001 cpu 2``` al nodo remoto
  - El nodo remoto recibe ```RESERVE```, verifica stock y responde ```GRANTED 1001```
  - El nodo local recibe ```GRANTED``` y,  tras actualizar TablaJobActivos, le notifica a Erlang con ```JOB_GRANTED 1001```
- Cuando Erlang termina el job, envia  ```JOB_RELEASE 1001```
- El agente en C realiza los sig. pasos:
  - Consulta TablaJobActivos para ver a què IP les pidio los recursos, 
  - Abre una conexiòn TCP con cada nodo vinculado y envia ```RELEASE 1001 cpu 2```
  - FInalmente, limpia TablaJobActivos.

### Manejo de fallos
Si un nodo (por ejemplo 192.168.1.2) deja de enviar ```ANNOUNCE```, tras 15 segundos se considerará caído y se eliminará de la TablaNodos.

## Ejecucion para testeo ràpida:
1. Ejecutar en una terminal: ```./test_deadlock.sh ```. Se abriran 2 ventanas (Nodo A y Nodo B), veràs el arranque inicial en ambos y como estàn en localhost se iràn pasando los nodos entre sì.
2. Luego de eso ejecutar en otra terminal ```nc 127.0.0.1 9001```, simulara ser Erlang.
3. En la terminal de Erlang, escribir ```GET NODES```, aparecera la lista de nodos activos. Nodo A comunicarà que recibio el comando y, le envio la lista a Erlang.
4. En la terminal de Erlang, escribir ```JOB_REQUEST 1001 @127.0.0.2:gpu:1```. Nodo A procesa el comando, busca en la tabla la IP y, lanza la conexiòn al puerto del Nodo B (Envia ```RESERVE 1001 gpu 1```). El Nodo B recibe el ```RESERVE```, verifica si tiene la capacidad del recurso y envia (en caso afirmativo: ```GRANTED 1001``` - En caso negativo: ```DENIED 1001```). Luego, en la terminal de Erlang recibiremos ```JOB_GRANTED 1001``` Ò ```JOB_DENIED 1001```.
5. Para testear el funcionamiento de la Cola, luego de realizar lo de arriba escribir en la terminar de Erlang: ```JOB_REQUEST 1002 @127.0.0.2:gpu:1```, la terminal quedarà esperando . En Nodo B , al no tener stock, lanzara el mensaje ```[INFO-COLA] Sin stock de gpu. Solicitud 1002 agregada a la cola.```. 
6.  Para que Erlang termine su trabajo: escribir en su terminal ```JOB_RELEASE 1001```, el NodoA buscara en TablaJobActivos y encontrarà que le pidio el recurso (GPU) al Nodo B, por lo que enviara ```RELEASE 1001 gpu 1``` a èste. El Nodo B libera el recurso (GPU) y...
    - Si tenia a un job en la cola: Se da cuenta que el ```Job 1002``` estaba en la cola esperando ese recurso, manda ```GRANTED 1002```  y en la terminal de Erlang recibimos ```JOB_GRANTED 1002```
NOTA: En caso de que se intente liberar uno ya liberado / no exista el job, Nodo A avisara con un mensaje en pantalla.
1. Para conocer el estado de un job, en Erlang : ```JOB_STATUS 1002```, El Nodo A informara el estado si es que existe y enviara a Erlang ```JOB_STATUS status```. Si no existe el Nodo A tirara un warning.
