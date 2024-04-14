/*********************************************************************************
* Derrick Lee, dehlee
* 2023 Winter CSE101 PA4
* List.c
* Contains implementation of List ADT
*********************************************************************************/

#include "List.h"

typedef struct NodeObj { //NodeObj struct definition
    char *URI;
    void *data;
    Node prev;
    Node next;
} NodeObj;

Node newNode(char *URI, void *x) {
    Node n = (Node) malloc(sizeof(NodeObj)); //Constructor for node
    assert(n != NULL);
    n->URI = URI;
    n->data = x;
    n->prev = NULL;
    n->next = NULL;
    return n;
}

void freeNode(Node *pN) { //frees the node
    if (pN != NULL && *pN != NULL) {
        free(*pN);
        *pN = NULL;
    }
}

typedef struct ListObj { //ListObj struct definition
    Node front;
    Node back;
    Node cursor;
    int length;
    int index;
} ListObj;

List newList(void) { //list constructor
    List L = (List) malloc(sizeof(ListObj));
    assert(L != NULL);
    L->front = NULL;
    L->back = NULL;
    L->length = 0;
    L->index = -1;
    L->cursor = NULL;
    return L;
}

void freeList(List *pL) { //frees memory allocated for instance of list
    clear(*pL);
    if (pL != NULL && *pL != NULL) {
        free(*pL);
        *pL = NULL;
    }
}

int length(List L) { //returns length of list
    return L->length;
}

int indexl(List L) { //returns index of cursor if defined, else -1
    if (L->cursor != NULL) {
        return L->index;
    } else {
        return -1;
    }
}

//PRECONDITION: list is non-empty
void *front(List L) { //returns element at front of list
    if (length(L) > 0) {
        return L->front->data;
    } else {
        fprintf(stderr, "List.c - front() - list cannot be empty\n");
        exit(1);
    }
}

//PRECONDITION: list is non-empty
void *back(List L) { //returns element at back of list
    if (length(L) > 0) {
        return L->back->data;
    } else {
        fprintf(stderr, "List.c - back() - list cannot be empty\n");
        exit(1);
    }
}

//PRECONDITIONS: list is non-empty; cursor is defined
void *get(List L) { //returns cursor element if defined, else -1
    if (length(L) > 0 && indexl(L) >= 0) {
        return L->cursor->data;
    } else {
        if (length(L) <= 0 && L->cursor == NULL) {
            fprintf(stderr, "List.c - get() - list cannot be empty; cursor cannot be undefined\n");
            exit(1);
        } else if (length(L) <= 0) {
            fprintf(stderr, "List.c - get() - list cannot be empty\n");
            exit(1);
        } else {
            fprintf(stderr, "List.c - get() - cursor cannot be undefined\n");
            exit(1);
        }
    }
}

char *get_URI(List L) { //returns cursor element if defined, else -1
    if (length(L) > 0 && indexl(L) >= 0) {
        return L->cursor->URI;
    } else {
        if (length(L) <= 0 && L->cursor == NULL) {
            fprintf(stderr, "List.c - get() - list cannot be empty; cursor cannot be undefined\n");
            exit(1);
        } else if (length(L) <= 0) {
            fprintf(stderr, "List.c - get() - list cannot be empty\n");
            exit(1);
        } else {
            fprintf(stderr, "List.c - get() - cursor cannot be undefined\n");
            exit(1);
        }
    }
}

void clear(List L) { //clears list back to original state and deletes nodes
    if (length(L) == 0) {
        return;
    } else if (length(L) == 1) {
        L->length -= 1;
        freeNode(&L->front);
    } else {
        Node n1 = L->front;
        while (length(L) > 1) {
            Node temp = n1;
            n1 = n1->next;
            L->length -= 1;
            freeNode(&temp);
        }
        L->length -= 1;
        freeNode(&n1);
    }
    L->cursor = NULL;
}

//PRECONDITIONS: list is non-empty; cursor is defined
void set(List L, void *x) { //sets element at cursor
    if (length(L) > 0 && indexl(L) >= 0) {
        L->cursor->data = x;
    } else {
        if (length(L) <= 0 && L->cursor == NULL) {
            fprintf(stderr, "List.c - set() - list cannot be empty; cursor cannot be undefined\n");
            exit(1);
        } else if (length(L) <= 0) {
            fprintf(stderr, "List.c - set() - list cannot be empty\n");
            exit(1);
        } else {
            fprintf(stderr, "List.c - set() - cursor cannot be undefined\n");
            exit(1);
        }
    }
}

void moveFront(List L) { //sets cursor under front node
    if (length(L) > 0) {
        L->cursor = L->front;
        L->index = 0;
    }
}

void moveBack(List L) { //sets cursor under back node
    if (length(L) > 0) {
        L->cursor = L->back;
        L->index = length(L) - 1;
    }
}

void movePrev(List L) { //moves cursor 1 step back if preconditions are met
    if (L->cursor == L->front) { //if defined and at front, cursor becomes undefined
        L->cursor = NULL;
        L->index = -1;
    } else { //if defined at not at front, move 1 step towards front
        L->cursor = L->cursor->prev;
        L->index -= 1;
    }
}

void moveNext(List L) {
    if (L->cursor == L->back) { //if defined and at back, cursor becomes undefined
        L->cursor = NULL;
        L->index = -1;
    } else { //if defined at not at back, move 1 step towards back
        L->cursor = L->cursor->next;
        L->index += 1;
    }
}

void append(List L, char *URI, void *x) { //inserts a node after the back
    Node n = newNode(URI, x);
    if (length(L) == 0) {
        L->front = n;
        L->back = n;
        L->length += 1;
    } else {
        L->back->next = n;
        n->prev = L->back;
        L->back = n;
        L->length += 1;
    }
}

//PRECONDITION: list is non-empty
void deleteFront(List L) { //deletes front node in a list
    if (length(L) > 0) {
        if (length(L) == 1) {
            freeNode(&L->front);
            L->length -= 1;
            L->index -= 1;
            return;
        }
        if (L->cursor == L->front) {
            L->cursor = NULL;
        }
        Node temp = L->front;
        L->front = L->front->next;
        L->front->prev = NULL;
        freeNode(&temp);
        L->length -= 1;
        L->index -= 1;
    } else {
        fprintf(stderr, "List.c - deleteFront() - list cannot be empty\n");
        exit(1);
    }
}

//PRECONDITION: list is non-empty
void deleteBack(List L) { //deletes back node in a list
    if (length(L) > 0) {
        if (length(L) == 1) {
            freeNode(&L->back);
            L->length -= 1;
            return;
        }
        if (L->cursor == L->back) {
            L->cursor = NULL;
        }
        Node temp = L->back;
        L->back = L->back->prev;
        L->back->next = NULL;
        freeNode(&temp);
        L->length -= 1;
    } else {
        fprintf(stderr, "List.c - deleteBack() - list cannot be empty\n");
        exit(1);
    }
}

//PRECONDITIONS: list is non-empty; cursor is defined
void delete (List L) { //delete cursor node
    if (length(L) > 0 && indexl(L) >= 0) {
        if (L->cursor == L->front) {
            deleteFront(L);
            L->index = -1;
            return;
        }
        if (L->cursor == L->back) {
            deleteBack(L);
            L->index = -1;
            return;
        } else {
            L->cursor->prev->next = L->cursor->next;
            L->cursor->next->prev = L->cursor->prev;
            L->length -= 1;
            L->index = -1;
            freeNode(&L->cursor);
        }
    } else {
        if (length(L) <= 0 && L->cursor == NULL) {
            fprintf(
                stderr, "List.c - delete() - list cannot be empty; cursor cannot be undefined\n");
            exit(1);
        } else if (length(L) <= 0) {
            fprintf(stderr, "List.c - delete() - list cannot be empty\n");
            exit(1);
        } else {
            fprintf(stderr, "List.c - delete() - cursor cannot be undefined\n");
            exit(1);
        }
    }
}
