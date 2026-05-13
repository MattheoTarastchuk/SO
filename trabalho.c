#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h> // shared memory
#include <semaphore.h>
#include <pthread.h>

// Estrutura Barreira

struct barreira {
	int count;
	int total;
	sem_t mutex;
	sem_t sem_barreira;
};

void init_barrier(struct barreira b, int n)
{
	b.count = 0;
	b.total = n;
	sem_init(&b.mutex, 1, 1);
	sem_init(&b.sem_barreira, 1, 0);
}	

void process_barrier(struct barreira b)
{
	printf("chegou");
	sem_wait(&b.mutex);
	b.count++;
	if (b.count == b.total)
	{
		b.count = 0;
		for(int i = 0; i < b.total;i++)
		{
			printf("liberando\n");
			sem_post(&b.sem_barreira);
			printf("liberando\n");
		}
		sem_post(&b.mutex);
	}
	else
	{
		sem_post(&b.mutex);
		sem_wait(&b.sem_barreira);
	}
}

//Estrutura FIFO

struct fila{
	struct nodo_t *inicio;
	struct nodo_t *fim;
	int livre;
	sem_t mutex_fila;
};

struct nodo_t{
	struct nodo_t *prox;
	sem_t *semaforo;
	int pid;
};

void init_fila(struct fila f)
{
	f.livre = 1;
	f.inicio = NULL;
	f.fim = NULL;
	sem_init(&f.mutex_fila, 1 ,1);
}

void enfileirar(struct fila f, struct nodo_t *nodo)
{
	if (f.inicio == NULL)
	{
		f.inicio = nodo;
		f.fim = nodo; 
	}
	else
	{
		f.fim->prox = nodo;
		f.fim = nodo;
	}
}
struct nodo_t *desenfileirar(struct fila f)
{
	struct nodo_t *nodo;

	if (f.inicio != NULL)
	{
		nodo = f.inicio;
		f.inicio = nodo->prox;
		if(f.inicio == NULL)
		{
			f.fim = NULL;
		}
	}
	return nodo;
}
void inicia_uso(int recurso, struct fila f)
{
	sem_wait(&f.mutex_fila); 
	if (f.livre)
	{
		f.livre = 0;
		sem_post(&f.mutex_fila);
	}
	else
	{
		struct nodo_t *nodo = malloc(sizeof(struct nodo_t));
		if (!nodo) return;
		nodo->pid = getpid();
		nodo->prox = NULL;
		sem_init(nodo->semaforo, 1, 0);
		enfileirar(f, nodo);
		sem_post(&f.mutex_fila);
		sem_wait(nodo->semaforo);

		free(nodo);
	}
}
void termina_uso(int recurso, struct fila f)
{

	sem_wait(&f.mutex_fila);

	struct nodo_t *proximo = desenfileirar(f);
	if (f.inicio != NULL)
	{
		sem_post(proximo->semaforo);
	}
	else
		f.livre = 1;
	sem_post(&f.mutex_fila);
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
	key_t key = ftok(".", 'A');

	shmid = shmget(key, sizeof(DadosCompartilhados), IPC_CREAT | 0666);
	if (shmid < 0) {perror("shmget"); exit(1);}
	DadosCompartilhados *dados = (DadosCompartilhados*) shmat(shmid, NULL, 0);
	if (dados == (void*) -1) {perror("shmat"); exit(1);}
	memset(dados, 0, sizeof(DadosCompartilhados));
	
	int recurso = rand() % 100;
	srand(time(NULL) ^ getpid());
	init_barrier(dados->b, total_proc);
	init_fila(dados->f);

	pid_t pid;
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
	}

	printf("PID: %d | PPID: %d | nProc %d\n", getpid(), getppid(), nProc);

	sleep(1);

	printf( "--Processo: %d chegando na barreira\n", nProc );
	process_barrier(dados->b);
	printf( "**Processo: %d saindo da barreira\n", nProc);

	int uso;
	int s;
	for (uso = 0; uso < 3; uso++)
	{
		//(A) Prolog o
		s = (rand() % (3 - 0 + 1)) + 0;
		printf( "Processo: %d Prologo: %d de %d segundos\n", nProc, uso, s );
		sleep(s);
		inicia_uso(recurso, dados->f);

		//(B) Recurso
		s = (rand() % (3 - 0 + 1)) + 0;
		printf( "Processo: %d USO: %d de %d segundos\n", nProc, uso, s );
		sleep(s);
		termina_uso(recurso, dados->f);

		//Epilogo
		s = (rand() % (3 - 0 + 1)) + 0;
		printf( "Processo: %d Epilogo: %d de %d segundos\n", nProc, uso, s );
		sleep(s);
	}
	printf( "--Processo: %d chegando novamente na barreira\n", nProc );
	process_barrier(dados->b);
	printf( "++Processo: %d saindo da barreira novamente\n", nProc );

	if (nProc == 0)
	{
		shmdt(dados);
		exit(EXIT_SUCCESS);
	}
	else
	{
		printf( "+++ Filho de número lógico %d terminou!\n", nProc );
		int status;
		while(wait(&status) > 0)
		{
			sem_destroy(&dados->b.mutex);
			sem_destroy(&dados->b.sem_barreira);
			sem_destroy(&dados->f.mutex_fila);

			shmdt(dados);
			shmctl(shmid, IPC_RMID, NULL);
			printf("fim do programa\n");
		}
	}
	return 0;
}
