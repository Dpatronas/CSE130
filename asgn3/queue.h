// Despina Patronas CSE130 Fall 2021 asgn3

#include <stdlib.h>
#include <ctype.h>

void queue_init(void);
void queue_deinit(void);

void queue_push(int connfd);
int  queue_pop(void);

void queue_clear(void);
