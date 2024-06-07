#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>    // fork()
#include <sys/shm.h>   // memóriakezelés
#include <semaphore.h> // nevesített szemafor
#include <fcntl.h>     // jogosultságok, engedélyek
#include <string.h>    // strcpy()
#include <wait.h>      // wait()
/*

A Krikett célja, hogy a szektorokat 15-20-ig, valamint a bullt lezárjuk, mielőtt ellenfelünk teszi ezt
meg. Egy szám lezárásához háromszor kell eltalálni az adott számot (a dupla kettőt, a tripla hármat
ér). Ha az egyik játékosnak le van zárva egy szám, amit újra eltalál, és ellenfelének még nincs lezárva
ugyanaz a szám, akkor a játékos a szektornak megfelelő pontot kapja. Az a játékos nyer, aki a 20 kör
letelte előtt lezárta az összes számot 15-20-ig és a bullt, valamint több ponttal rendelkezik, mint
ellenfele.


Készítsünk szimulációt a következők szerint: A játékvezető (Szülő folyamat) fogja jegyzetelni két
játékos (2 gyerekfolyamat) pontjait. A szülő folyamat létrehozza a gyerekfolyamatokat, elkészíti a
gépeket (Ezt csupán egy várakozás paranccsal szimuláljuk), majd jelzést (Signal) küld a játékosoknak,
akik ezután bemutatkoznak (A nevük tetszőleges lehet), és elkezdik a játékot. A játék során két
különböző Darts gépet használnak, így mind a ketten egyszerre tudnak dobni. A dobást a
következőképpen szimuláljuk: 13-21 közötti véletlen szám, valamint 1-3 közötti véletlen szám. A 13
14-es számok esetén semmi pontot nem kapnak, 15-20 között az adott számot, a második 1-3 közötti
szorzóval, 21 esetén bull [ha a második szám 3-as, akkor dupla, 2-es és 1-es esetén szimpla] { 25
pontot ér a szimpla }. Minden dobás között véletlen időnyit várnak. A játékvezető minden kör elején
szemaforral jelzi hogy mikor kezdhetnek, és a játékosok is szemaforral jelzik mikor végeztek. Ezután a
játékvezető szemaforral jelzi az első játékosnak hogy visszaadhatja a pontjait, ezeket lejegyzeteli
(Amennyiben még nem találta el az adott számot háromszor, feljegyezzük hogy hányszor volt meg,
ellenkező esetben pontértéket kap, ha az ellenfélnek még nincs meg), majd a másik játékosnak is
jelez szemaforral, és ő is elküldi csövön a pontjait (Így akár ez lehet ugyanaz a cső is). Ezután jelzi
hogy indulhat az újabb kör. Közben mindig írjuk a konzolra hogy mi történt. A játék 20 körig tart, vagy
amíg valaki nem nyert (Minden szám lezárva és több ponttal jelentkezik a másik játékosnál). Ezután
eredményhirdetés (A program kiírja a képernyőre a játék végeredményét).


A programot C nyelven készítsük el. Kötelező elemek: fork és pipe parancsok, szignál és szemafor.
(Nem pusztán a szimuláció megírása a feladat egy folyamatban C nyelven, hanem az operációs
rendszerek órán tanult eszközök használata is)
*/
int helpVar;
sem_t *introductionFinished;

void setSemToZero(sem_t *sem)
{
    int value;
    sem_getvalue(sem, &value);
    if (value > 0)
    {
        for (value; value > 0; sem_getvalue(sem, &value))
        {
            sem_wait(sem);
        }
    }
    else
    {
        for (value; value < 0; sem_getvalue(sem, &value))
        {
            sem_post(sem);
        }
    }
}
typedef struct Points
{
    int hits[9];
} Points;
typedef struct Throw
{
    int score;
    int multiplier;
} Throw;

typedef struct Player
{
    pid_t process;
    int score;
} Player;

// Signal handlers
void introduction(int pid)
{
    char name[50];
    printf("Enter the name of the player: ");
    fgets(name, sizeof(name), stdin);
    printf("My name is %s\n", name);
    sem_post(introductionFinished);
}
void endProcess(int pid)
{
    exit(1);
}

typedef struct Game
{
    sem_t *rdyToThrow[2];
    sem_t *finishedThrow[2];

    Player players[2];
    Points points[2];

    int player1PointsChannel[2]; // csővezeték
    int player2PointsChannel[2];
    int roundCounter;
    int roundMax;
} Game;

void printSem(Game *game)
{
    sem_getvalue(game->rdyToThrow[0], &helpVar);
    printf("Value rdyToThrow 0 : %i\n", helpVar);
    sem_getvalue(game->rdyToThrow[1], &helpVar);
    printf("Value rdyToThrow 1 : %i\n", helpVar);
    sem_getvalue(game->finishedThrow[0], &helpVar);
    printf("Value finishedThrow 0: %i\n", helpVar);
    sem_getvalue(game->finishedThrow[1], &helpVar);
    printf("Value finishedThrow 1: %i\n", helpVar);
    sem_getvalue(introductionFinished, &helpVar);
    printf("Value introductionFinished: %i\n", helpVar);
}
void setup(Game *game)
{
    // Szemaforok
    introductionFinished = sem_open("introductionFinished", O_CREAT, S_IWUSR | S_IRUSR, 0);
    game->rdyToThrow[0] = sem_open("rdyToThrow0", O_CREAT, S_IWUSR | S_IRUSR, 0);
    game->rdyToThrow[1] = sem_open("rdyToThrow1", O_CREAT, S_IWUSR | S_IRUSR, 0);
    game->finishedThrow[0] = sem_open("finishedThrow0", O_CREAT, S_IWUSR | S_IRUSR, 0);
    game->finishedThrow[1] = sem_open("finishedThrow1", O_CREAT, S_IWUSR | S_IRUSR, 0);
    if (game->finishedThrow[0] == SEM_FAILED || game->finishedThrow[1] == SEM_FAILED || game->rdyToThrow[0] == SEM_FAILED ||
        game->rdyToThrow[1] == SEM_FAILED || introductionFinished == SEM_FAILED)
    {
        printf("Hiba a szemaforok létrehozásakor.\n");
        exit(0);
    }
    setSemToZero(introductionFinished);
    setSemToZero(game->rdyToThrow[0]);
    setSemToZero(game->rdyToThrow[1]);
    setSemToZero(game->finishedThrow[0]);
    setSemToZero(game->finishedThrow[1]);

    // Csővezeték
    if (pipe(game->player1PointsChannel) == -1 || pipe(game->player2PointsChannel) == -1)
    {
        printf("Hiba a csővezeték létrehozásakor.\n");
        exit(0);
    }
    // max körök száma
    game->roundCounter = 0;
    game->roundMax = 20;
    // Gépek nullázása
    for (int i = 0; i < (sizeof(game->points[0].hits) / sizeof(int)); ++i)
    {
        game->points[0].hits[i] = 0;
        game->points[1].hits[i] = 0;
    }
    // Pontok nullázása
    game->players[0].score = 0;
    game->players[1].score = 0;
}

int end(Game *game)
{
    bool condition1 = (game->roundCounter >= game->roundMax);
    bool condition2 = false;
    int i = 0;
    while (condition2 == false && i < 2)
    {
        int j = 0;
        while (game->points[i].hits[j] >= 3 && j < (sizeof(game->points[i].hits) / sizeof(int)))
        {
            ++j;
        }
        if (j >= (sizeof(game->points[i].hits) / sizeof(int)))
        {
            condition2 = true;
        }
        ++i;
    }
    return (condition1 || condition2);
}

void addPoints(Game *game, int player, Throw *throw)
{
    int otherPlayer = player == 0 ? 1 : 0;
    for (int i = throw->multiplier; i > 0; i--)
    {
        if (game->points[player].hits[throw->score - 13] < 3)
        {
            game->points[player].hits[throw->score - 13] += 1;
        }
        else
        {
            if (throw->score > 14)
            {
                if (throw->score == 21)
                {
                    if (game->points[otherPlayer].hits[8] < 3)
                    {
                        game->players[player].score += (25 * i);
                        i = 0;
                    }
                }
                else if(game->points[otherPlayer].hits[throw->score - 13] < 3)
                    {
                        game->players[player].score += (throw->score * i);
                        i = 0;
                    }
            }
        }
    }
}
void printScores(Game *game)
{
    printf("________________\nENDGAME SCORE: \n\n");
    for (int i = 0; i < 2; i++)
    {
        printf("%i. player's score: %i \n\n", i + 1, game->players[i].score);
        for (int j = 0; j < 9; j++)
        {
            printf("|Sector: %i, Required: %i", (j + 13), (3 - game->points[i].hits[j]));
            if (j % 3 == 2)
            {
                printf("\n");
            }
        }
        printf("\n\n");
    }
}
void throwing(Game *game, int playerID)
{
    int sleepTime = rand() % 10 + 1;
    usleep(sleepTime * 200000);
    Throw h;
    h.score = rand() % 9 + 13;
    h.multiplier = rand() % 3 + 1;
    if (playerID == 0)
    {
        write(game->player1PointsChannel[1], &h, sizeof(Throw));
    }
    else
    {
        write(game->player2PointsChannel[1], &h, sizeof(Throw));
    }
}
void run(Game *game)
{
    setup(game);
    signal(SIGALRM, introduction);
    signal(SIGABRT, endProcess);
    int state;
    game->players[0].process = fork();
    if (game->players[0].process > 0)
    {
        game->players[1].process = fork();
    }
    if (game->players[0].process < 0 || game->players[1].process < 0)
    {
        printf("Error: Creation of a thread is failed.\n");
        exit(0);
    }
    if (game->players[0].process > 0 && game->players[1].process > 0)
    {
        // Signal küldése gyerekeknek h mutatkozanak be
        printSem(game);
        printf("Introduction: \n");
        kill(game->players[0].process, SIGALRM);
        sem_wait(introductionFinished);
        kill(game->players[1].process, SIGALRM);
        sem_wait(introductionFinished);

        while (!end(game))
        {
            printf("_________________\nround : %i\n\n", game->roundCounter + 1);
            sem_post(game->rdyToThrow[0]);
            sem_post(game->rdyToThrow[1]);

            sem_wait(game->finishedThrow[0]);
            Throw throwPlayer1;
            read(game->player1PointsChannel[0], &throwPlayer1, sizeof(Throw));
            printf("1st Player's throw: %i-%i\n", throwPlayer1.score, throwPlayer1.multiplier);
            addPoints(game, 0, &throwPlayer1);

            sem_wait(game->finishedThrow[1]);
            Throw throwPlayer2;
            read(game->player2PointsChannel[0], &throwPlayer2, sizeof(Throw));
            printf("2nd Player's throw: %i-%i\n", throwPlayer2.score, throwPlayer2.multiplier);
            addPoints(game, 1, &throwPlayer2);

            game->roundCounter++;
        }
        printScores(game);

        kill(game->players[0].process, SIGABRT);
        kill(game->players[1].process, SIGABRT);

        waitpid(game->players[0].process, &state, 0);
        waitpid(game->players[1].process, &state, 0);

        sem_destroy(introductionFinished);
        sem_unlink("introductionFinished");
        sem_destroy(game->rdyToThrow[0]);
        sem_unlink("rdyToThrow0");
        sem_destroy(game->rdyToThrow[1]);
        sem_unlink("rdyToThrow1");
        sem_destroy(game->finishedThrow[0]);
        sem_unlink("finishedThrow0");
        sem_destroy(game->finishedThrow[1]);
        sem_unlink("finishedThrow1");
        fflush(NULL);
    }
    else
    {
        sleep(5);
        sleep(5);
        int playerID = 0;
        if (game->players[1].process == 0)
        {
            playerID = 1;
        }
        srand(time(NULL) + playerID * 1000);
        while (true)
        {
            sem_wait(game->rdyToThrow[playerID]);
            throwing(game, playerID);
            sem_post(game->finishedThrow[playerID]);
        }
    }
}

int main()
{
    Game game;
    run(&game);
}
