#include "linked_list.h"
#include <stdlib.h>
#include <stdio.h>

void list_print (Node *head) {
    Node *n;
    for(n=head;n;n=n->next) {
        printf("%d\n", n->cy);
        if(n->next == head) break;
    }
}
Node *get_tail(Node *head) {
    Node *p = NULL;
    for(p = head;p && p->next != head;p = p->next);
    return p;
}

Node *create_node(int cy,char *p) {
    Node *n = malloc(sizeof(Node));
    if(!n) return NULL;
    n->cy = cy;
    n->p = p;
    n->next = n->prev = NULL;
    return n;
}
void push_back(Node *n, Node **head, Node **tail) {
    if(!n) return;
    if(!*head) {
        *head = malloc(sizeof(Node));
        *tail = malloc(sizeof(Node));
        if(!head || !tail) return;
        *head = n;
        *tail = *head;
        return;
    } else {
        n->next=*head;
        (*head)->prev = n;

        n->prev=*tail;
        (*tail)->next = n;
    }
    *tail = n;
}
void list_free(Node *head, Node *tail) {
    Node *n;
    Node *p = NULL;
    for(n=tail;n;n=n->prev) {
        if(p) {
            p->prev=p->next=NULL;
            free(p);
        }
        p=n;
    }
}