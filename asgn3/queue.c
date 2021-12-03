// Despina Patronas CSE130 Fall 2021 asgn3

// Sources:
/** 
    man queue:
    TAIL QUEUES
         A tail queue is headed by a structure defined by the TAILQ_HEAD macro.
         This structure contains a pair of pointers, 
            first element in the tail queue 
            last element in the tail queue.

         The elements are doubly linked so an arbitrary element can be removed without traversing the tail queue.
         New elements can be added to the tail queue:
            1) after an existing element
            2) before an existing element
            3) at the head of the tail queue
            4) at the end of the tail queue


         A TAILQ_HEAD structure is declared as follows:

              TAILQ_HEAD(HEADNAME, TYPE) head;

              - HEADNAME is the name of the structure to be defined, 
              - TYPE is the type of the elements to be linked into the tail queue.  

         A pointer to the head of the tail queue can later be declared as:

              struct HEADNAME *headp;
              
              -(The names head and headp are user selectable.)

         The macro TAILQ_HEAD_INITIALIZER evaluates to an initializer for the tail
         queue head.

**/

#include <sys/queue.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "queue.h"

TAILQ_HEAD(tailhead, connNode) head = TAILQ_HEAD_INITIALIZER(head);

struct connNode {
    int connfd;
    TAILQ_ENTRY(connNode) entries;     /* Tail queue. */
};

typedef sem_t Semaphore;

// Semaphores
Semaphore* sem;
Semaphore* lock;

static Semaphore *make_semaphore(int value){

    Semaphore *semaphore = (Semaphore *) malloc(sizeof(Semaphore));
    memset(&semaphore, 0, sizeof(semaphore));

    if ((semaphore = sem_open("/semaphore", O_CREAT, 0644, value)) == SEM_FAILED) {
        perror("Bad Semaphore init");
        exit(1);
    }
    sem_unlink("/semaphore");
    return semaphore;
}

static void semaphore_wait(Semaphore *semaphore){
    sem_wait(semaphore);
}

static void semaphore_signal(Semaphore *semaphore){
    sem_post(semaphore);
}

/* Initialize the queue. */
void queue_init(void) {
    sem = make_semaphore(0);    // not available
    lock = make_semaphore(1);   // available
    TAILQ_INIT(&head);
}

void queue_deinit(void) {
    if (sem)
    {
        sem_close(sem);
    }

    if (lock)
    {
        sem_close(lock);
    }

    if (!TAILQ_EMPTY(&head))
    {
        queue_clear();
    }

    free(sem); sem = 0;
    free(lock); lock = 0;
}

void queue_push(int connfd) {

    // atmoic
    semaphore_wait(lock);
    {
        struct connNode * node = (struct connNode *) malloc (sizeof(struct connNode));
        node->connfd = connfd;
        TAILQ_INSERT_TAIL(&head, node, entries);
    }
    semaphore_signal(lock);

    semaphore_signal(sem); // wake up threads
}

int queue_pop(void) {
    int n = -1;
    semaphore_wait(sem);

    // atomic
    semaphore_wait(lock);
    {
        struct connNode * node = TAILQ_FIRST(&head);
        n = node->connfd;
        TAILQ_REMOVE(&head, node, entries);
        free(node);
    }
    semaphore_signal(lock);

    return n;
}

void queue_clear(void) {
    semaphore_wait(lock);

    while(!TAILQ_EMPTY(&head))
    {
        struct connNode * node = TAILQ_FIRST(&head);
        TAILQ_REMOVE(&head, node, entries);
        free(node);
    }

    semaphore_signal(lock);
}
