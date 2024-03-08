#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include "sqlite3.h"
#include <time.h>

time_t waiting_room_start_time;
time_t  waiting_room_end_time;
int started = 0;
int first_player = 0;

#define PORT 2914
#define MAX_CLIENTS 100 
extern int errno; /* codul de eroare returnat de anumite apeluri */

#define TIME 10 //timpul alocat pt a raspunde la o intrebare
int no_questions=0; //numarul de intrebari se va incrementa dupa citirea din database

pthread_t th[MAX_CLIENTS]; //Identificatorii thread-urilor care se vor crea
int i=0; //ID-urile threadurilor

struct sockaddr_in server;	
struct sockaddr_in client;	

typedef struct thread_client{
	int idThread; //id-ul thread-ului tinut in evidenta de acest program
	int cl; //descriptorul intors de accept
}thread_client;

//stocarea intrebarilor extrase din baza de date
struct quizz_questions{
    char q[100]; //intrebarea
    char ans_A[100]; //varianta a de raspuns 
    char ans_B[100]; //varianta b de raspuns
    char ans_C[100]; //varianta c de raspuns
    char ans_D[100]; //varianta d de raspuns 
    char correct_ans; //litera corespunzatoare raspunsului corect
}questions[20];

//stocarea informatiilor despre clientii participanti
struct info_quizz{
    char name[20]; //numele jucatorului
    int score; //scorul obtinut
    int done; //variabila care retine daca jucatorul a terminat quizz-ul
    int out; //variabila care retine daca jucatorul a parasit quizz-ul
    int descriptor; //descriptorul de socket asociat clientului
    char answers[20]; //literele de raspuns primite de la client
}clients[MAX_CLIENTS],aux;

//retinerea informatiilor legate de starea jocului
struct status_game{
    int players_number; //numarul de jucatori participanti
    int players_done; //cati dintre acestia au terminat quizz-ul
    int players_out; //cati au parasit jocul
    char ranking[MAX_CLIENTS]; //clasamentul care va fi realizat la final de joc
}status;

char* itoa(int x);

int callback(void *data, int argc, char **argv, char **column_name)
{
    //argc = nr de coloane din tabel
    strcpy(questions[no_questions].q,argv[1]);
    strcpy(questions[no_questions].ans_A,argv[2]);
    strcpy(questions[no_questions].ans_B,argv[3]);
    strcpy(questions[no_questions].ans_C,argv[4]);
    strcpy(questions[no_questions].ans_D,argv[5]);
    questions[no_questions].correct_ans=argv[6][0];
    printf("%s\n   %s\n   %s\n   %s\n   %s\n   %c\n", questions[no_questions].q,
     questions[no_questions].ans_A, questions[no_questions].ans_B,
     questions[no_questions].ans_C, questions[no_questions].ans_D, questions[no_questions].correct_ans);
    no_questions++;
	return 0;
}

int read_from_database(char *dbname)
{
    sqlite3 *db;
    int dbhandle; 

    int statement_handle; 
    char *errorMsg = 0; 
    const char *query = "SELECT * FROM QUESTIONS";

    // Open database
    dbhandle = sqlite3_open(dbname, &db);

    if(dbhandle == SQLITE_OK) {
        printf("Database opened successfully\n");
    }
    else {		
        perror("Could not open database:\n");
        return errno;
    }

    statement_handle = sqlite3_exec(db, query, callback, 0, &errorMsg);

    if (statement_handle == SQLITE_OK) {
        printf("Query successful\n");
    }
    else {
        perror("Could not run query:\n");
        return errno;
    }

    sqlite3_close(db);
    
    return 0;
}

void reset() //de fiecare data cand termina toti jucatorii quizz-ul -> ar incepe un nou meci, serverul o ia de la 0
{
    //reset info_quizz
    for(int i=0;i<status.players_number;i++)
    {
        bzero(clients[i].name,20);
        clients[i].score=0;
        clients[i].done=0;
        clients[i].out=0;
        clients[i].descriptor=0;
        bzero(clients[i].answers,no_questions);
    }

    //reset status_game
    status.players_number=0;
    status.players_done=0;
    status.players_out=0;
    bzero(status.ranking,MAX_CLIENTS);

    //reset threads_number
    i=0;
}

void create_ranking()
{
    //create ranking -> jucatorii sunt sortati descrescator dupa nr de puncte
    int i,j;
    if(status.players_number!=status.players_out) //daca mai are sens sa fie calculat clasamentul
    {
        for(i=0;i<status.players_number-1;i++)
        for(j=i+1;j<status.players_number;j++)
            if(clients[i].score<clients[j].score)
            {
                aux=clients[i];
                clients[i]=clients[j];
                clients[j]=aux;
            }
    //for(int i=0;i<status.players_number;i++)
       // printf("RANK %d -> %s\n",i+1,clients[i].name);

    /* pentru moment clientul primeste doar mesajul de mai jos, nu clasamentul real*/
    //char send_ranking[1024]="RANKING:\n         AI PRIMIT CLASAMENTUL\n\0";

    char send_ranking[1024]="";
    bzero(send_ranking,1024);
    strcat(send_ranking, "RANKING:\n");

    int rank=0,last_score=-1;
    for(int i=0;i<status.players_number;i++)
        if(clients[i].out==0)
        {
            if(clients[i].score!=last_score) rank++;
            strcat(send_ranking, "RANK ");
            strcat(send_ranking, itoa(rank));
            strcat(send_ranking, ": ");
            strcat(send_ranking, clients[i].name);
            strcat(send_ranking, "\n");
            last_score=clients[i].score;

        }

    strcat(send_ranking, "\0");

    //send ranking
    for(int i=0;i<status.players_number;i++)
        if(clients[i].done==1 && clients[i].out==0)
        {
            if (write(clients[i].descriptor,&send_ranking,strlen(send_ranking)+1)<= 0) 
            {
                perror ("Eroare la write() catre client la ranking.\n");
            }
            else 
            {
                printf("S-a trimis rankigul catre clientul %d\n",i);
                fflush(stdout);
            }
            //inchei conexiunea cu clientul
            printf("Am incheiat conexiunea cu clientul %d\n",i);
            close(clients[i].descriptor);
        }

    }

    reset(); //final de joc -> cand alti jucatori vor intra, va fi un nou meci
}

void prepare_structs()
{
    status.players_out=0;
    status.players_number=0;
    status.players_done=0;

    /* pregatirea structurilor de date */
    bzero (&server, sizeof (server));
    bzero (&client, sizeof (client));      

    /* umplem structura folosita de server */
    server.sin_family = AF_INET;  /* stabilirea familiei de socket-uri */
    server.sin_addr.s_addr = htonl (INADDR_ANY); /* acceptam orice adresa - from host byte order to network byte order*/
    server.sin_port = htons (PORT);   /* utilizam un port utilizator */
}

int sd;		//descriptorul de socket 
int create_socket()
{
    /* crearea socket-ului*/
    if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1) //domeniu tip protocol
    {
      perror ("[server]Eroare la socket().\n");
      return errno;
    }

    /* utilizarea optiunii SO_REUSEADDR */
    int on=1;
    setsockopt(sd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));

    /* atasam socketul */
    if (bind (sd, (struct sockaddr *) &server, sizeof (struct sockaddr)) == -1) //asociaza adresa cu socketul
    {
      perror ("[server]Eroare la bind().\n");
      return errno;
    }
}

static void *treat(void *); /* functia executata de fiecare thread ce realizeaza comunicarea cu clientii */
void raspunde(void *);

int exit_client(int descriptor)
{
  //trimitem clientului confirmarea ca a fost deconectat
  char disconnect[100]="You left the quizz.\n";
  if (write (descriptor, &disconnect,strlen(disconnect)+1) <= 0) 
  {
    perror ("[client]Eroare la write() catre client pentru exit.\n");
    return errno;
  }
  else printf("[server] Un client a fost deconectat\n.");
  //dupa putem incheia conexiunea
  close (descriptor);
  return 1;
}

int main ()
{
    prepare_structs();
    char *dbname = "test.db";
    int ok=read_from_database(dbname);
    int created=create_socket();


    /* punem serverul sa asculte daca vin clienti sa se conecteze */
    if (listen (sd, MAX_CLIENTS) == -1)
    {
      perror ("[server]Eroare la listen().\n");
      return errno;
    }

    /* servim in mod concurent clientii...folosind thread-uri */
    while (1)
    {

        int client;
        thread_client * td; //parametru functia executata de thread     
        int length = sizeof (client);

        /* acceptam un client (stare blocanta pina la realizarea conexiunii) */
        if ( (client = accept (sd, (struct sockaddr *) &client, &length)) < 0)
        {
            perror ("[server]Eroare la accept().\n");
            continue;
        }
	
        /* s-a realizat conexiunea, se astepta mesajul */
        // if(first_player==0)
        // {
        //     first_player=1;
        // }
        // //playerul asteapta pana cand incepe runda noua
        // if(first_player)
        while(started){}
        

        status.players_number++; //inca un jucator
        //printf("---------------%d------------------\n",status.players_number);
        if(status.players_number==1)
            printf("\nRUNDA NOUA DE JOC\n");

        printf ("[server]CLIENT NOU\n");
        printf ("------------------CATI JUCATORI SUNT ACUM: %d\n",status.players_number-status.players_out);
        fflush (stdout);

        td=(struct thread_client*)malloc(sizeof(struct thread_client));	
        td->idThread=i++;
        td->cl=client;

        pthread_create(&th[i], NULL, &treat, td);     				
	}//while    
}; //main

static void *treat(void * arg)
{		
		struct thread_client tdL; 
		tdL= *((struct thread_client*)arg);	
		printf ("[thread] S-a creat thread-ul %d\n", tdL.idThread);
		fflush (stdout);		 
		pthread_detach(pthread_self());		
		raspunde((struct thread_client*)arg);
		/* am terminat cu acest client, inchidem conexiunea */
		close ((intptr_t)arg);
		return(NULL);			
};

char* itoa(int x)
{
    static char sir[10]="";
    int p=1,copie=x,nr=0;
    char c;
    if(copie==0) 
    {
        sir[0]='0';
        nr++;
    }
    else
    {
        while(copie)
        {
            p=p*10;
            copie/=10;
        }
        p=p/10;
        
        while(p)
        {
            c=x/p+'0';
            sir[nr]=c;
            nr++;
            x=x%p;
            p=p/10;
        } 

    }

    sir[nr]='\0';
    return sir;
}

void raspunde(void *arg)
{
    int nr, i=0,exit_was_made=0;
    char answer[10],player_name[20];
	struct thread_client tdL; 
	tdL= *((struct thread_client*)arg);
    bzero(player_name, 20);

    clients[tdL.idThread].score=0; //scorul este 0 pentru inceput
    clients[tdL.idThread].descriptor=tdL.cl;
    i=tdL.idThread;

    //NOU

    time_t rawtime;
    struct tm *timeinfo;// = (struct tm*)malloc(sizeof(struct tm));;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    printf("Time player %d has joined: %s\n", tdL.idThread, asctime(timeinfo));
    //printf("VERIFIC CAT ESTE ASTA:%d\n", tdL.idThread);
    if(i == 0){

       // printf("AM INTRAT IN IF\n");
        //waiting_room_start_time = (time_t )malloc(sizeof(time_t));
        //waiting_room_end_time = (time_t )malloc(sizeof(time_t));

        waiting_room_start_time = time(&rawtime);
        waiting_room_end_time = time(&rawtime) + 20;


       // printf("The Game will start at: %s\n", asctime(localtime(&waiting_room_end_time)));
        fflush(stdout);

    }
    printf("CLIENT IS IN WAITING ROOM, WILL START AT %s\n", asctime(localtime(&waiting_room_end_time)));
    fflush(stdout);

    while(1){
        
        time_t actual;
        struct tm *tt;
        time(&actual);
        tt = localtime(&actual);

        //if(difftime(mktime(tt), waiting_room_end_time) >= 0)
        if(difftime(time(&actual), waiting_room_end_time) >= 0)
            break;
    }
    printf("CLIENT LEFT THE WAITING ROOM\n");
    started = 1;
    //NOU

    /*---------SERVERUL PRIMESTE NUMELE CLIENTULUI-----------------*/
    if (read (tdL.cl, &player_name,sizeof(player_name)) <= 0) 
    {
        printf("[Thread %d]\n",tdL.idThread);
        perror ("Eroare la read() de la client.\n");
    }
    strcpy(clients[tdL.idThread].name,player_name);

    /*----------------SI TRIMITE MESAJUL DE START----------------*/
    // char hello_msg[100]="Hello ";
    // strcat(hello_msg,player_name);
    // strcat(hello_msg,". Press enter to start:\n\0");
    // printf("%s",hello_msg);
    // fflush(stdout); 

    // if (write(tdL.cl,&hello_msg,strlen(hello_msg)+1)<= 0) 
    // {
    //     printf("[Thread %d]\n",tdL.idThread);
    //     perror ("Eroare la write() catre client pentru mesajul de start.\n");
    // }
    // bzero(hello_msg,100);

    // /*-----------SERVERUL TRIMITE INTREBARILE DUPA CE CITESTE ENTER DE LA CLIENT------------*/
    // char enter[2];
    //  if (read (tdL.cl, &enter,sizeof(enter)) <= 0) 
    // {
    //     printf("[Thread %d]\n",tdL.idThread);
    //     perror ("Eroare la read() de la client.\n");
    // }  
    /*---------SERVERUL TRIMITE INTREBARILE---------------*/
    for(int j=0;j<no_questions;j++)
    {
        char msg[1024];
        strcpy(msg,questions[j].q);
        strcat(msg,"\nA)");
        strcat(msg, questions[j].ans_A);
        strcat(msg,"\nB)");
        strcat(msg, questions[j].ans_B);
        strcat(msg,"\nC)");
        strcat(msg, questions[j].ans_C);
        strcat(msg,"\nD)");
        strcat(msg, questions[j].ans_D);
        /* trimitem intrebarea clientului */
        if (write (tdL.cl, &msg, strlen(msg)+1) <= 0)
        {
            printf("[Thread %d] ",tdL.idThread);
            perror ("[Thread]Eroare la write() catre client in for.\n");
        }
        else
            printf ("[Thread %d]Mesajul a fost transmis cu succes -> question number %d.\n",tdL.idThread, j);

         bzero(msg,1024);

        /*citim raspunsul primit*/
        if (read (tdL.cl, &answer,sizeof(answer)) <= 0)
                {
                printf("[Thread %d]\n",tdL.idThread);
                perror ("Eroare la read() de la client.\n");
                }
        if(strcmp("exit",answer)==0)
        {
            printf("S-a dat un exit\n");
            clients[i].done=1;
            status.players_done++;
            status.players_out++;
            clients[i].out=1;
            exit_was_made=1;
            int done=exit_client(tdL.cl);
            break;
        }
        clients[tdL.idThread].answers[j]=answer[0]; //punem in struct-ul corespunzator clientului raspunsurile
    }

    if(exit_was_made==0)
    {
        /*--------DUPA CE TOATE INTREBARILE AU FOST TRIMISE, SE CALCULEAZA SCORUL-----------------*/
        for(int j=0;j<no_questions;j++)
        {
            //printf("%c %c\n",);
            if(clients[tdL.idThread].answers[j]==questions[j].correct_ans)clients[tdL.idThread].score+=20;

        }
        
        /*------------JUCATORUL PRIMESTE SCORUL OBTINUT------------------------------*/
        char * char_number;
        char points[100]="Quizz is over. You got ";
        char_number=itoa(clients[tdL.idThread].score);
        strcat(points,char_number);
        strcat(points,"/100 points.\n\0");

        if (write(tdL.cl, &points, strlen(points)+1) <= 0)
        {
            printf("[Thread %d]\n",tdL.idThread);
            perror ("Eroare la write() catre client la nr de puncte.\n");
        }
        else
        {
            printf("Client %d: scorul este %d \n", tdL.idThread,clients[tdL.idThread].score);
            fflush(stdout);
            char maybe_exit[10];
            //verificam daca clientul asteapta clasamentul sau nu
            if (read (tdL.cl, &maybe_exit,sizeof(maybe_exit)) <= 0) 
            {
                printf("[Thread %d]\n",tdL.idThread);
                perror ("Eroare la read() de la client.\n");
            }  
            else
            {
                status.players_done++;
                clients[tdL.idThread].done=1;
                //printf("%s",maybe_exit);
                if(strcmp("exit",maybe_exit)==0)
                {
                    printf("S-a dat un exit\n");
                    int done=exit_client(tdL.cl);
                    exit_was_made=1;
                    //clients[i].done=0;
                    clients[i].out=1;
                    status.players_out++;
                    //status.players_done--;
                    //status.players_number--;
                }
            }
        }
    }

    /*-------------VEDEM DACA ACUM TOTI JUCATORII AU TERMINAT-----------------*/
    printf("---------------CATI JUCATORI SUNT GATA ACUM: %d/%d\n",status.players_done,status.players_number);
    if(status.players_done==status.players_number) 
    {
        printf("AU TERMINAT TOTI: INCEPEM CLASAMENTUL\n");
        create_ranking(); 
        started = 0;
    }
}