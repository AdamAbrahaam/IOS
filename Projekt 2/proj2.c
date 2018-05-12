/*
 * Autor: Adam Abrahám
 * Login: xabrah04
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <time.h>
#include <signal.h>
#include <sys/shm.h>

//stderr Errors
#define ARGERR "Nevhodny argument."
#define FILERR "Chyba pri otvoreni suboru."
#define MEMERR "Chyba so zdielanou pamatou."
#define SEMERR "Neuspesne vytvorenie semaforu."
#define FORKERR "Neuspesny fork."
#define MALLERR "Chyba pri alokacii."

//Konstanty
#define LOCKED 0
#define UNLOCKED 1
#define MAX 1000

//Semaphores
sem_t *s_board = NULL;
sem_t *s_arrived = NULL;
sem_t *s_full = NULL;
sem_t *s_enter = NULL;
sem_t *s_ridFinish = NULL;

//Zdielane premenne
int pocet, kapacita, art, abt;
FILE *file;
pid_t prcsID;
pid_t genID;
pid_t *riders;

int *cisloA = NULL;
int *naZastavke = NULL;
int *pocetShared = NULL;
int *kapacitaShared = NULL;
int *isBoarding = NULL;
int cisloCounter = 0;
int zastavkaCounter = 0;
int pocetCounter = 0;
int kapacitaCounter = 0;
int boardingCounter = 0;


void prcsBus(int kapacita, int abt) {
    int jazda = (rand() % (abt + 1 - 0) + 0)*1000; //Random cislo z intervalu * prevod na mikro
    int startBoarding = 0;
    fprintf(file, "%d\t\t: BUS\t\t: start\n", (*cisloA)++);
    fflush(file);
    
    while((*pocetShared) > 0) {
        (*kapacitaShared) = 0;
        fprintf(file, "%d\t\t: BUS\t\t: arrival\n", (*cisloA)++);
        fflush(file);
        (*isBoarding) = 1;

        if ((*naZastavke) > 0) {
            
            if ((*naZastavke) > kapacita){
                startBoarding = kapacita;
            }else{
                startBoarding = *naZastavke;
            }
            fprintf(file, "%d\t\t: BUS\t\t: start boarding: %d\n", (*cisloA)++, startBoarding);
            fflush(file);
            sem_post(s_board);

            sem_wait(s_full);
            fprintf(file, "%d\t\t: BUS\t\t: end boarding: %d\n", (*cisloA)++, *naZastavke);
            fflush(file);
        }
        (*isBoarding) = 0;
        
        fprintf(file, "%d\t\t: BUS\t\t: depart\n", (*cisloA)++);
        fflush(file);
        
        if (abt)
            usleep(jazda);
        
        fprintf(file, "%d\t\t: BUS\t\t: end\n", (*cisloA)++);
        fflush(file);

        while((*kapacitaShared) > 0) {
            (*kapacitaShared)--;
            sem_post(s_ridFinish);
        }
    }

    fprintf(file, "%d\t\t: BUS\t\t: finish\n", (*cisloA)++);
    fflush(file);

    exit(1);
}

void prcsRid(int kapacita, int ID) {
    fprintf(file, "%d\t\t: RID %d\t\t: start\n", (*cisloA)++, ID);
    fflush(file);

    //Cakanie ked je bus na zastavke
    while(*isBoarding);

    (*naZastavke)++;
    fprintf(file, "%d\t\t: RID %d\t\t: enter: %d\n", (*cisloA)++, ID, *naZastavke);
    fflush(file);
  
    sem_wait(s_board);
    fprintf(file, "%d\t\t: RID %d\t\t: boarding\n", (*cisloA)++, ID);
    fflush(file);
    
    (*naZastavke)--;
    (*pocetShared)--;
    (*kapacitaShared)++;
    
    if (((*kapacitaShared) == kapacita) || (*naZastavke == 0)) {
        sem_post(s_full);
    }else if (((*kapacitaShared) < kapacita) || (*naZastavke > 0)){
        sem_post(s_board);
    }
        
    fflush(file);
    sem_wait(s_ridFinish);
    fprintf(file, "%d\t\t: RID %d\t\t: finish\n", (*cisloA)++, ID);
    fflush(file);
}

void genRiders(int kapacita, int pocet, int art) {
    int delay = (rand() % (art + 1 - 0) + 0)*1000;  //Random cislo z intervalu * prevod na mikrosec
	for (int ridID = 0; ridID < pocet; ridID++) {
        if ((riders[ridID] = fork()) < 0) {
            //Kill childs
            for(int i = 0; i < ridID; i++) {
                kill(riders[i], SIGTERM);
            }

            //Kill prcsID
            kill(prcsID, SIGTERM);
            fprintf(stderr, "%s\n", FORKERR);
            break;
        }

        if (riders[ridID] == 0) {
            //Child
            prcsRid(kapacita, ridID + 1);
            exit(0);
        }else {
            //Parent
            if (art)
                usleep(delay);
        }
    }

    wait(NULL);
    exit(2);
}

void dealokacia() {
    sem_close(s_board);
    sem_close(s_arrived);
    sem_close(s_full);
    sem_close(s_enter);
    sem_close(s_ridFinish);

    sem_unlink("/xabrah04.board");
    sem_unlink("/xabrah04.arrived");
    sem_unlink("/xabrah04.full");
    sem_unlink("/xabrah04.enter");
    sem_unlink("/xabrah04.ridFinish");
    
    shmctl(cisloCounter, IPC_RMID, NULL);
    shmctl(kapacitaCounter, IPC_RMID, NULL);
    shmctl(pocetCounter, IPC_RMID, NULL);
    shmctl(zastavkaCounter, IPC_RMID, NULL);
    shmctl(boardingCounter, IPC_RMID, NULL);
}

void alokacia() {
    //Zdielane premenne
    if ((cisloCounter = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1){
        fprintf(stderr, "%s\n", MEMERR);
        dealokacia();
        exit(2);
    }
    if ((zastavkaCounter = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1){
        fprintf(stderr, "%s\n", MEMERR);
        dealokacia();
        exit(2);
    }
    if ((pocetCounter = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1){
        fprintf(stderr, "%s\n", MEMERR);
        dealokacia();
        exit(2);
    }
    if ((kapacitaCounter = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1){
        fprintf(stderr, "%s\n", MEMERR);
        dealokacia();
        exit(2);
    }
    if ((boardingCounter = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1){
        fprintf(stderr, "%s\n", MEMERR);
        dealokacia();
        exit(2);
    }

    if ((cisloA = shmat(cisloCounter, NULL, 0)) == NULL){
        fprintf(stderr, "%s\n", MEMERR);
        dealokacia();
        exit(2);
    }
    if ((naZastavke = shmat(zastavkaCounter, NULL, 0)) == NULL){
        fprintf(stderr, "%s\n", MEMERR);
        dealokacia();
        exit(2);
    }
    if ((pocetShared = shmat(pocetCounter, NULL, 0)) == NULL){
        fprintf(stderr, "%s\n", MEMERR);
        dealokacia();
        exit(2);
    }
    if ((kapacitaShared = shmat(kapacitaCounter, NULL, 0)) == NULL){
        fprintf(stderr, "%s\n", MEMERR);
        dealokacia();
        exit(2);
    }
    if ((isBoarding = shmat(boardingCounter, NULL, 0)) == NULL){
        fprintf(stderr, "%s\n", MEMERR);
        dealokacia();
        exit(2);
    }

    //Semaphores
    s_board = sem_open("/xabrah04.board", O_CREAT | O_EXCL, 0666, LOCKED); // 1 LOCKED
    s_arrived = sem_open("/xabrah04.arrived", O_CREAT | O_EXCL, 0666, UNLOCKED); // 0 UNLOCKED
    s_full = sem_open("/xabrah04.full", O_CREAT | O_EXCL, 0666, LOCKED); // 1 LOCKED
    s_enter = sem_open("/xabrah04.enter", O_CREAT | O_EXCL, 0666, UNLOCKED); // 1 LOCKED
    s_ridFinish = sem_open("/xabrah04.ridFinish", O_CREAT | O_EXCL, 0666, LOCKED); // 1 LOCKED

    if((s_board == SEM_FAILED) || (s_arrived == SEM_FAILED) || (s_full == SEM_FAILED) || (s_enter == SEM_FAILED) || (s_ridFinish == SEM_FAILED)) {
        fprintf(stderr, "%s\n", SEMERR);
        dealokacia();
        exit(2);
    }
}

int main(int argc, char** argv) {
    dealokacia();

    //Kontrola argumentov
    if (argc == 5) {
        char *error;
        pocet = (int) strtol(argv[1], &error, 10);
        kapacita = (int) strtol(argv[2], &error, 10);
        art = (int) strtol(argv[3], &error, 10);
        abt = (int) strtol(argv[4], &error, 10);

        if (*error != 0 || pocet <= 0 || kapacita <= 0 || (art < 0 || art > MAX) || (abt < 0 || abt > MAX)) {
            fprintf(stderr, "%s\n", ARGERR);
            return 1;
        }
    }else {
        fprintf(stderr, "%s\n", ARGERR);
        return 1;
    }

    //Kontrola suboru
    if((file = fopen("proj2.out","w")) == NULL){
        fprintf(stderr, "%s\n", FILERR);
        return 2;
    }

    //Vytvorenie premennych a semaforov
    alokacia();

    //Alokacia pre pole riders
    riders = malloc(sizeof(pid_t)*pocet);
    if(riders == NULL) {
        fprintf(stderr, "%s\n", MALLERR);
        fclose(file);
        return 2;
    }

    srand(time(NULL));
    *cisloA = 1;
    *pocetShared = pocet;
    *kapacitaShared = 0;
    *isBoarding = 0;

    int pid = fork();
    if (pid == 0){
        //Child
        prcsBus(kapacita, abt);
        exit(0);
    }else if(pid > 0) {
        //Parent
        prcsID = pid;
        pid = fork();

        if (pid == 0) {
            genRiders(kapacita, pocet, art);
            exit(0);
        }else if (pid > 0) {
            genID = pid;
        }else {
            fprintf(stderr, "%s\n", FORKERR);
            exit(2);
            }
    } else {
        fprintf(stderr, "%s\n", FORKERR);
        exit(2);
    }

    //Cakanie na koniec
    waitpid(prcsID, NULL, 0);
    waitpid(genID, NULL, 0);

    //Unlink
    dealokacia();
    fclose(file);

    return 0;
}
