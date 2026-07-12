#!/bin/bash

# Moverse automáticamente al directorio raíz del proyecto
cd "$(dirname "$0")/.." || exit 1


echo "======================================================="
echo " Iniciando Prueba Automatizada de Prevención de Deadlock"
echo " (Usando IPs Loopback Dedicadas: 127.0.0.2 y 127.0.0.3)"
echo "======================================================="

#  Limpieza de procesos zombies anteriores por seguridad
killall agente_recursos 2>/dev/null

#  Levantar los nodos en terminales separadas (usando la IP dinámica)
echo "[*] Iniciando Nodo A (127.0.0.2:8001) (cpu:2 mem:8)..."
xterm -hold -title "Nodo A (127.0.0.2:8001)" -e "./agente_recursos 127.0.0.2 8001 \"cpu:2 mem:8\"" &

echo "[*] Iniciando Nodo B (127.0.0.3:8002) (cpu:2 gpu:1)..."
xterm -hold -title "Nodo B (127.0.0.3:8002)" -e "./agente_recursos 127.0.0.3 8002 \"cpu:2 gpu:1\"" &

#  Dar tiempo al Broadcast UDP para que se descubran mutuamente
echo "[*] Esperando 5 segundos para el descubrimiento UDP..."
sleep 5

#  Disparar el Deadlock cruzado, capturando las respuestas (sin perder la simultaneidad)
echo "[*] Disparando JOB_REQUEST 1001 al Nodo A (Pide 2 CPU de A y 1 GPU de B)..."
echo "JOB_REQUEST 1001 @127.0.0.2:cpu:2 @127.0.0.3:gpu:1" | nc -w 2 127.0.0.1 8001 > /tmp/resp1.txt &
PID1=$!

echo "[*] Disparando JOB_REQUEST 1002 al Nodo B (Pide 1 GPU de B y 2 CPU de A)..."
echo "JOB_REQUEST 1002 @127.0.0.3:gpu:1 @127.0.0.2:cpu:2" | nc -w 2 127.0.0.1 8002 > /tmp/resp2.txt &
PID2=$!

wait $PID1 $PID2
RESP1=$(cat /tmp/resp1.txt)
RESP2=$(cat /tmp/resp2.txt)

echo "======================================================="
echo " Job1 -> ${RESP1:-<<sin respuesta inmediata, probablemente encolado>>}"
echo " Job2 -> ${RESP2:-<<sin respuesta inmediata, probablemente encolado>>}"
echo "======================================================="

#  Si alguno ganó la carrera, lo liberamos para demostrar la resolución completa vía cola FIFO
if [[ "$RESP1" == *"JOB_GRANTED"* ]]; then
    echo "[*] Job1 ganó la carrera (Caso B). Liberándolo para desbloquear a Job2..."
    echo "JOB_RELEASE 1001" | nc -w 2 127.0.0.1 8001
    sleep 1
    echo "[*] Estado final de Job2:"
    echo "JOB_STATUS 1002" | nc -w 2 127.0.0.1 8002
elif [[ "$RESP2" == *"JOB_GRANTED"* ]]; then
    echo "[*] Job2 ganó la carrera (Caso B). Liberándolo para desbloquear a Job1..."
    echo "JOB_RELEASE 1002" | nc -w 2 127.0.0.1 8002
    sleep 1
    echo "[*] Estado final de Job1:"
    echo "JOB_STATUS 1001" | nc -w 2 127.0.0.1 8001
else
    echo "[*] Ninguno se concedió de inmediato -> interbloqueo circular real (Caso A)."
    echo "[*] Esperando resolución automática por TIMEOUT_JOB_SEG (podés bajarlo a 15 en el .h para no esperar 120s)..."
fi

# NOTA: En el caso A, habrán advertencias de Bad File descriptor , al intentar hacer send().
# Esto es algo esperado que sucede al simular Erlang con nc
# (ya que el planificador de Erlang mantendría la conexión persistente)
# Para inyectar los comandos de forma no bloqueante usamos `nc -w 2` (timeout de 2 seg).
# Dado que la resolución del interbloqueo por rollback tarda más tiempo (TIMEOUT_JOB_SEG),
# para cuando el agente C intenta enviar el 'JOB_TIMEOUT' o el 'JOB_GRANTED' diferido, 
# NetCat ya cerró la conexión TCP por su cuenta.
# Debido a SIGPIPE, captura el fallo del socket cerrado sin crashear.
