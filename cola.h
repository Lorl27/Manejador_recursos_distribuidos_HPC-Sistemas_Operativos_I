#ifndef __COLA_H__
#define __COLA_H__

#include <stdlib.h>
#include <netinet/in.h>

typedef void (*FuncionDestructora)(void *dato);
typedef void *(*FuncionCopia)(void *dato);
typedef int (*FuncionComparadora)(void *a, void *b);

typedef struct _GNode{
    void * dato;
    struct _GNode * sig;
    int job_id;
    struct sockaddr_in origen;
}GNode;

typedef struct _Cola {
    GNode *inicio; // Por aquí salen (dequeue)
    GNode *fin;    // Por aquí entran (enqueue)
} * Cola;

/*
* Crea la Cola
*/
Cola Cola_crear();

/*
Devuelve 1 si la Cola pasada esta vacia y, 0 en caso contrario
*/
int isEmpty(Cola cola);

// Devuelve el primer elemento de la COla
void * Tope(Cola cola);

/*
Inserta el elemento al final de la Cola, utilizando la funcion copia pasada por paràmetro
*/
Cola  Encolar(Cola cola, void * elemento, FuncionCopia copy);


/*
Elimina el elemento al inicio de la Cola, utilizando la funcion destroy pasada por paràmetro
*/
Cola  Desencolar(Cola cola, FuncionDestructora destroy);

/* 
Destruye toda la COla
*/
Cola cola_destruir(Cola cola, FuncionDestructora destroy);

//retorna 1  si el elemento esta en la Cola, 0 si no.
int cola_contiene(Cola cola, void *dato_buscado, FuncionComparadora comp, FuncionCopia copy, FuncionDestructora destroy);


#endif