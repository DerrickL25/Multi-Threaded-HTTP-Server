/*********************************************************************************
* Derrick Lee, dehlee
* 2023 Winter CSE101 PA4
* List.h
* Header file for List ADT
*********************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>

typedef struct NodeObj *Node;

Node newNode(char *URI, void *x);

void freeNode(Node *pN);

typedef struct ListObj *List;

List newList(void);

void freeList(List *pL);

int length(List l);

int indexl(List l);

//PRECONDITION: list is non-empty
void *front(List l);

//PRECONDITION: list is non-empty
void *back(List l);

//PRECONDITIONS: list is non-empty; cursor is defined
void *get(List l);

char *get_URI(List L);

void clear(List l);

//PRECONDITIONS: list is non-empty; cursor is defined
void set(List l, void *x);

void moveFront(List l);

void moveBack(List l);

void movePrev(List l);

void moveNext(List l);

void prepend(List l, void *x);

void append(List l, char *URI, void *x);

//PRECONDITIONS: list is non-empty; cursor is defined
void insertBefore(List l, void *x);

//PRECONDITIONS: list is non-empty; cursor is defined
void insertAfter(List l, void *x);

//PRECONDITION: list is non-empty
void deleteFront(List l);

//PRECONDITION: list is non-empty
void deleteBack(List l);

//PRECONDITIONS: list is non-empty; cursor is defined
void delete (List l);

void printList(FILE *out, List l);
