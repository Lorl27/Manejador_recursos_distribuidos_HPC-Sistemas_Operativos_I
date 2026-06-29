# Descripción del trabajo práctico
Es un middleware distribuido para la gestión de recursos en un clúster: CPU, memoria y GPU.

El nodo utiliza 3 capas de comunicaciòn:
1. **UDP BROADCAST:** Envia anuncios (*ANNOUNCE*) cada 3 segundos, para que otros nodos se descubran dinàmicamente.
2. **TCP PÙBLICO:** Atiende las peticiones de otros agentes (*RESERVE / RELEASE*) de forma no bloqueante, con epoll. 
3. **TCP LOCAL (ERLANG):** Escucha peticiones locales (*JOB_REQUEST / JOB_RELEASE / JOB_STATUS / GET NODES*) que permite coordinar la comunicaciòn.

# Librerias necesarias previas 
```console
sudo apt update
sudo apt install build-essential libc6-dev
```


# Compilacion
Genera un binario llamado agente_recursos
```console
make
```

# Ejecuciòn
Requiere la IP de escucha - puerto pùblico - puerto local para Erlang y, una cadena con los recursos locales disponibles.
```console
./agente_recursos <IP> <PUERTO_PUBLICO> <PUERTO_LOCAL_ERLANG> "<RECURSOS>"
```

## Ejemplo de ejecuciòn:
```console
./agente_recursos 192.168.1.50 8100 9000 "cpu:4 mem:8192 gpu:1"
```

### Al ejecutarlo, veràs en la consola:
```console
[INFO-ARRANQUE] Enviando primer anuncio...
```

#### Flujo de operación

- Si se inicia otra instancia, ambos nodos se descubrirán mediante ```ANNOUNCE``` y se añadirán a la TablaNodos.

- Erlang ejecuta ``` GET NODES``` para obtener la tabla de nodos activos desde C, decide lanzar un job.
- Erlang envia ``` JOB_REQUEST 1001 @192.168.1.2:cpu:2```
- El agente de C realiza los sig. pasos: 
  - Busca el nodo en la tabla y crea la conexiòn asincrònica con el cliente
  - Registra el Job en TablaJobsActivos y, el evento EPOLLOUT para confirma la conexiòn
  - Envia ``` RESERVE 1001 cpu 2``` al nodo remoto
  - El nodo remoto recibe `` RESERVE```, verifica stock y responde ``` GRANTED 1001```
  - El nodo local recibe ``` GRANTED``` y,  tras actualizar TablaJobAcrtivos, le notifica a Erlang con ``` JOB_GRANTED 1001```
- Cuando Erlang termina el job, envia  ``` JOB_RELEASE 1001```
- El agente en C realiza los sig. pasos:
  - Consulta TablaJobActivos para ver a què IP les pidio los recursos, 
  - Abre una conexiòn TCP con cada nodo vinculado y envia ```RELEASE 1001 cpu 2 ```
  - FInalmente, limpia TablaJobActivos.

## Manejo de fallos
Si un nodo (por ejemplo 192.168.1.2) deja de enviar ```ANNOUNCE```, tras 15 segundos se considerará caído y se eliminará de la TablaNodos.