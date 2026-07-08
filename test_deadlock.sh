#!/bin/bash
# Script de prueba para ejecutar el clúster de agentes de recursos


# Compilar el programa con make
echo "Compilando agente_recursos..."
make rebuild

# Verificar si la compilación fue éxitosa
if [ $? -ne 0 ]; then
    echo "Error en la compilación. Abortando."
    exit 1
fi


# Verificar si xterm está instalado
if ! command -v xterm &> /dev/null; then
    echo "xterm no está instalado. Instalándolo..."
    sudo apt-get update && sudo apt-get install -y xterm
fi

# Ejecuta dos nodos en terminales distintas (ALTERNATIVA SIN INSTALAR)
#gnome-terminal --tab --title="Nodo A" -- ./agente_recursos 127.0.0.1 8001  "cpu:2 mem:8"
#gnome-terminal --tab --title="Nodo B" -- ./agente_recursos 127.0.0.2 8001  "cpu:2 gpu:1"

# Ejecuta dos nodos en terminales distintas
xterm -hold -title "Nodo A" -e ./agente_recursos 127.0.0.1 8001  "cpu:2 mem:8" &
xterm -hold -title "Nodo B" -e ./agente_recursos 127.0.0.2 8002  "cpu:2 gpu:1" &

echo "Clúster de prueba iniciado."
echo "Cierre las ventanas de los nodos para finalizar el script."

wait