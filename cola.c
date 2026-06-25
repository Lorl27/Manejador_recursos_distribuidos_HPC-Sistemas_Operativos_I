#include "cola.h"

Cola Cola_crear(){
    Cola cola = malloc(sizeof(struct _Cola));
    if(cola!=NULL){
        cola->inicio=NULL;
        cola->fin=NULL;
    }
    return cola;
}

int isEmpty(Cola cola){
    return cola==NULL || cola->inicio==NULL;
}

void * Tope(Cola cola){
    if (isEmpty(cola)) return NULL;
    
    return cola->inicio->dato;
}

Cola  Encolar(Cola cola, void * elemento, FuncionCopia copy){
    if (cola == NULL) return NULL;

    GNode * nuevoNodo=malloc(sizeof(GNode));
    nuevoNodo->dato=copy(elemento);
    nuevoNodo->sig=NULL;

    if(isEmpty(cola)){
        //no tiene datos
        cola->inicio=nuevoNodo;
        cola->fin=nuevoNodo;
        
        return cola;
    }

    cola->fin->sig=nuevoNodo;
    cola->fin=nuevoNodo; //actualizamos puntero.
    
    return cola;
}


Cola  Desencolar(Cola cola, FuncionDestructora destroy){
    if(isEmpty(cola)) return cola;

    GNode * tmp= cola->inicio;
    cola->inicio=cola->inicio->sig;

    // si sacamos el ùltimo...!
    if(cola->inicio==NULL) cola->fin=NULL;

    destroy(tmp->dato);
    free(tmp);
    
    return cola;
}

Cola cola_destruir(Cola cola, FuncionDestructora destroy){
    if (cola == NULL) return NULL;

    while(!isEmpty(cola)){
        cola=Desencolar(cola,destroy);
    }

    free(cola);
    return NULL;
}

int cola_contiene(Cola cola, void *dato_buscado, FuncionComparadora comp, FuncionCopia copy, FuncionDestructora destroy) {
    if (isEmpty(cola)) return 0;

    int encontrado = 0;
    Cola aux = Cola_crear();

    while (!isEmpty(cola)) {
        void *frente = Top(cola);
        if (comp(frente, dato_buscado) == 0) encontrado = 1;
    
        aux=Encolar(aux, frente, copy);
        cola = Desencolar(cola, destroy);
    }

    while (!isEmpty(aux)) {
        cola=Encolar(cola, Top(aux), copy);
        aux = Desencolar(aux, destroy);
    }

    aux=cola_destruir(aux, destroy);
    return encontrado;
}