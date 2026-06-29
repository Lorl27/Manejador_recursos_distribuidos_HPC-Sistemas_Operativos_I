TARGET = agente_recursos

# Compilador y flags
CC = gcc
CFLAGS = -Wall -Wextra -g

OBJ = main.o resource_manager_agent.o cola.o

#Regla general
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

# Regla para compilar .c
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Limpieza
clean: 
	rm -f $(OBJ) $(TARGET)

# Rebuild
rebuild:
	rm -f $(OBJ) $(TARGET)
	$(MAKE) all


.PHONY: all clean