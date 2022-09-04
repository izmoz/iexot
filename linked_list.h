typedef struct Node {
    int cy;
    char *p;
    unsigned char *hl;
    struct Node *prev;
    struct Node *next;
} Node;
Node *get_tail(Node *);
Node *create_node(int ,char *, unsigned char*);
void push_back(Node *, Node **, Node **);
void list_print (Node *);
void list_free(Node *, Node *);