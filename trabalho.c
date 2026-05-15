#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h> 
#include <semaphore.h>
#include <pthread.h>

#define MAX_PROC 100

// Estrutura Barreira

struct barreira {
	int count;
	int total;
	sem_t mutex;
	sem_t sem_chegada;
	sem_t sem_saida;
};

void init_barrier(struct barreira *b, int n)
{
	b->count = 0;
	b->total = n;
	sem_init(&b->mutex, 1, 1);
	sem_init(&b->sem_chegada, 1, 0);
	sem_init(&b->sem_saida, 1, 1);
}	

void process_barrier(struct barreira *b)
{
	sem_wait(&b->mutex);

	b->count++;
	if (b->count == b->total)
	{
		sem_wait(&b->sem_saida);
		sem_post(&b->sem_chegada);
	}

	sem_post(&b->mutex);

	sem_wait(&b->sem_chegada);
	sem_post(&b->sem_chegada);

	sem_wait(&b->mutex);

	b->count--;

	if (b->count == 0)
	{
		sem_wait(&b->sem_chegada);
		sem_post(&b->sem_saida);
	}

	sem_post(&b->mutex);

	sem_wait(&b->sem_saida);
	sem_post(&b->sem_saida);
}

//Estrutura FIFO

struct fila {

	int fila[MAX_PROC];

	int head;
	int tail;

	int livre;

	sem_t mutex_fila;

	sem_t semaforos[MAX_PROC];
};

struct nodo_t{
	struct nodo_t *prox;
	sem_t semaforo;
	int pid;
};

void init_fila(struct fila *f)
{
	f->livre = 1;
	f->head = 0;
	f->tail = 0;
	sem_init(&f->mutex_fila, 1 ,1);

	for (int i = 0; i < MAX_PROC; i++)
		sem_init(&f->semaforos[i], 1, 0);
}

void enfileirar(struct fila *f, int proc)
{
	f->fila[f->tail] = proc;

	f->tail = (f->tail + 1) % MAX_PROC;
}
int desenfileirar(struct fila *f)
{
	int proc = f->fila[f->head];

	f->head = (f->head + 1) % MAX_PROC;

	return proc;
}

void inicia_uso(int Pi, int recurso, struct fila *f)
{
	sem_wait(&f->mutex_fila);
	if (f->livre && (f->head == f->tail))
	{
		f->livre = 0;

		sem_post(&f->mutex_fila);

		return;
	}
	enfileirar(f, Pi);

	sem_post(&f->mutex_fila);

	sem_wait(&f->semaforos[Pi]);
}

void termina_uso(int recurso, struct fila *f)
{
	sem_wait(&f->mutex_fila);

	if (f->head != f->tail)
	{
		int prox = desenfileirar(f);

		sem_post(&f->semaforos[prox]);
	}
	else
	{
		f->livre = 1;
	}

	sem_post(&f->mutex_fila);
}

int main(int argc, char *argv[]){
	if (argc != 2)
		return -1;

	typedef struct {
		struct barreira b;
		struct fila f;
	}DadosCompartilhados;

	int n_filhos = atoi(argv[1]);
	int total_proc = n_filhos+1;
	int shmid;
	key_t key = IPC_PRIVATE;
	if (key == -1)
	{
		perror("ftok");
		exit(1);	
	}

	shmid = shmget(key, sizeof(DadosCompartilhados), IPC_CREAT | 0666);
	if (shmid == -1) {perror("shmget"); exit(1);}
	DadosCompartilhados *dados = (DadosCompartilhados*) shmat(shmid, NULL, 0);
	if (dados == (void*) -1) {perror("shmat"); exit(1);}
	memset(dados, 0, sizeof(DadosCompartilhados));

	int recurso = rand() % 100;
	init_barrier(&dados->b, total_proc);
	init_fila(&dados->f);

	pid_t pid;
	pid_t filhos[MAX_PROC];
	int nProc = 0;
	for (int i = 1; i <= n_filhos; i++)
	{
		pid = fork();
		if (pid < 0) {perror("fork"); exit(1);}
		if (pid == 0)
		{
			nProc = i;
			break;
		}
		else
		{
			filhos[i] = pid;
		}
	}

	srand(time(NULL) ^ getpid());

	printf("PID: %d | PPID: %d | nProc %d\n", getpid(), getppid(), nProc);
	fflush(stdout);

	sleep(1);

	printf( "--Processo: %d chegando na barreira\n", nProc );
	fflush(stdout);
	process_barrier(&dados->b);
	printf( "**Processo: %d saindo da barreira\n", nProc);
	fflush(stdout);

	sleep(1);

	int uso;
	int s;
	for (uso = 0; uso < 3; uso++)
	{
		//(A) Prolog o
		s = (rand() % (3 - 0 + 1)) + 0;
		printf( "Processo: %d Prologo: %d de %d segundos\n", nProc, uso, s );
		fflush(stdout);
		sleep(s);
		inicia_uso(nProc, recurso, &dados->f);

		//(B) Recurso
		s = (rand() % (3 - 0 + 1)) + 0;
		printf( "Processo: %d USO: %d de %d segundos\n", nProc, uso, s );
		fflush(stdout);
		sleep(s);
		termina_uso(recurso, &dados->f);

		//Epilogo
		s = (rand() % (3 - 0 + 1)) + 0;
		printf( "Processo: %d Epilogo: %d de %d segundos\n", nProc, uso, s );
		fflush(stdout);
		sleep(s);
	}

	sleep(1);
	printf( "--Processo: %d chegando novamente na barreira\n", nProc );
	fflush(stdout);
	process_barrier(&dados->b);
	printf( "++Processo: %d saindo da barreira novamente\n", nProc );
	fflush(stdout);

	sleep(1);

	if (nProc == 0)
	{
    	int status;
		pid_t terminou;

    	while((terminou = wait(&status)) > 0)
    	{
			for (int i = 1; i <= n_filhos; i++)
			{
				if (filhos[i] == terminou)
				{
					printf( "+++ Filho de número lógico %d e pid %d terminou!\n", i, terminou);
					fflush(stdout);
					break;
				}
			}
		}
		sem_destroy(&dados->b.mutex);
		sem_destroy(&dados->b.sem_chegada);
		sem_destroy(&dados->b.sem_saida);
		sem_destroy(&dados->f.mutex_fila);

    	shmdt(dados);
    	shmctl(shmid, IPC_RMID, NULL);
	}
	else
	{
    	shmdt(dados);
    	exit(EXIT_SUCCESS);
	}
	return 0;
}
