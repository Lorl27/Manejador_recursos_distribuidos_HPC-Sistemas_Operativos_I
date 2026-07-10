#Nombre ejecutable
TARGET = agente_recursos

# Compilador y flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -g

# Archivos que conforman el ejecutable
OBJ = main.o resource_manager_agent.o cola.o

#Regla general - lo compila
all: $(TARGET)

# Target depende de OBJ
# Equivalente a: gcc -Wall -Wextra -g -o agente_recursos main.o resource_manager_agent.o cola.o
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

# Regla para compilar .c en .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Limpieza
clean: 
	rm -f $(OBJ) $(TARGET)

# Limpia y recompila todo
rebuild:
	rm -f $(OBJ) $(TARGET)
	$(MAKE) all

# Declaramos que NO son archivos, sino reglas.
.PHONY: all clean rebuild