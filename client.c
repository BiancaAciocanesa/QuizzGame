#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <signal.h>
#include <gtk/gtk.h>
#define port 2914 /* portul de conectare la server*/
extern int errno; /* codul de eroare returnat de anumite apeluri */
int sd; // descriptorul de socket
struct sockaddr_in server; // structura folosita pentru conectare
//gcc client.c -o client `pkg-config --cflags gtk+-3.0` `pkg-config --libs gtk+-3.0`

GtkWidget *window, *grid, *start_message, *send_name, *username, *enter_username, *see_ranking, *timer;
GtkWidget *answer_A, *answer_B, *answer_C, *answer_D; //butoanele pentru trimiterea raspunsurilor la intrebare

int exit_was_made = 0, in_game = 0,ranking_was_sent=0;
char player_name[20] = "", msg[1024]="",answer[10]="";
int remaining_time = 5;
int timer_id;

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

int next_question()
{
   // trimitem serverului raspunsul
  if (write(sd, &answer, sizeof(answer)) <= 0)
  {
    perror("[client]Eroare la write() spre server.\n");
    return errno;
  }
    
  bzero(msg,1024);
  
  //si citim urmatoarea intrebare
  if (read(sd, &msg, sizeof(msg)) < 0)
  {
    perror("[client]Eroare la read() de la server.\n");
    return errno;
  }

  printf("Received from server:%s\n",msg);
  fflush(stdout);
  gtk_label_set_text(GTK_LABEL(start_message), msg);

  if(strstr(msg,"RANKING")) //daca serverul a trimis rankingul -> final de joc
  {
      printf("Received from server:%s\n",msg);
      fflush(stdout);
      in_game=0;
      ranking_was_sent=1;

      g_source_remove(timer_id);  //oprim timerul care era pentru intrebari   
      gtk_widget_hide(see_ranking);   
      gtk_widget_hide(timer);
      return 1;

  }if(strstr(msg,"Quizz")) //se primeste de la server scorul
  {
    gtk_widget_show(see_ranking); //apare optinea de a astepta pentru clasament
    gtk_widget_hide(answer_A);  //si eliminam butoanele care erau pe ecran  
    gtk_widget_hide(answer_B);
    gtk_widget_hide(answer_C);
    gtk_widget_hide(answer_D);
  }
  remaining_time = 5; //se reseteaza timpul pentru raspuns la intrebare
  return 1;
}

int send_A()
{
  strcpy(answer,"A");
  int ok=next_question();
  return 1;
}
int send_B()
{
  strcpy(answer,"B");
  int ok=next_question();
  return 1;
}
int send_C()
{
  strcpy(answer,"C");
  int ok=next_question();
  return 1;
}
int send_D()
{
  strcpy(answer,"D");
  int ok=next_question();
  return 1;
}

int verify_timer() 
{
  if (remaining_time > 0)
  {
    gtk_label_set_text(GTK_LABEL(timer), itoa(remaining_time));
    remaining_time--;
  } 
  else 
  {
    gtk_label_set_text(GTK_LABEL(timer), "0");
    strcpy(answer,"E"); //daca timpul s-a scurs, se trimite catre server o litera gresita de raspuns
    int ok = next_question();
  }
  return 1;
}

int exit_client() // cand clientul tasteaza "exit" sa paraseasca jocul
{
  if(ranking_was_sent)
  return 0;
  if(in_game) //daca jucatorul era in runda, trebuie anuntat serverul
  {
    char exit[10] = "exit";
    if (write(sd, &exit, strlen(exit) + 1) <= 0)
    {
      perror("[client]Eroare la write() in trimiterea comenzii exit");
      return errno;
    }
    // asteptam de la server confirmarea ca a fost deconectat
    char disconnect[100];
    bzero(disconnect,100);
    if (read(sd, &disconnect, sizeof(disconnect)) < 0)
    {
      perror("[client]Eroare la read() de la server.\n");
      return errno;
    }
    printf("Received from server:%s\n",disconnect);
    fflush(stdout);
    close(sd); //conexiunea se incheie
    return 0;
  }

  return 1;
}

void handle_signal()
{
  printf("If you want to quit try the exit button\n");
}

void prepare_structs()
{
  /* umplem structura folosita pentru realizarea conexiunii cu serverul */
  server.sin_family = AF_INET;         /* familia socket-ului */
  server.sin_addr.s_addr = INADDR_ANY; /* adresa IP a serverului */
  server.sin_port = htons(port);       /* portul de conectare */
}

int create_socket()
{
  /* cream socketul */
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("Eroare la socket().\n");
    return errno;
  }

  /* ne conectam la server */
  if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
  {
    perror("[client]Eroare la connect().\n");
    return errno;
  }

  return 0;
}

int say_hello()
{
  //jucatorul introduce numele, care apoi este trimis catre server -> se citeste prima intrebare
  char buffer[100];
  gtk_widget_hide(send_name);
  gtk_widget_hide(enter_username);
  gtk_widget_hide(username);

  char *entered = (char *)gtk_entry_get_text(GTK_ENTRY(enter_username));
  strcpy(player_name, entered);


  prepare_structs();
  int created = create_socket();

  if (write(sd, &player_name, strlen(player_name) + 1) <= 0) // trimitem numele
  {
    perror("[client]Eroare la write() in trimiterea numelui jucatorului");
    return errno;
  }
  in_game=1;

  if (read(sd, &msg, sizeof(msg)) < 0)
    {
      perror("[client]Eroare la read() de la server.\n");
      return errno;
    }

  gtk_widget_set_size_request(start_message, 955, 205);
  gtk_label_set_text(GTK_LABEL(start_message), msg);
  gtk_label_set_text(GTK_LABEL(timer), "");
  gtk_widget_show(answer_A);
  gtk_widget_show(answer_B);
  gtk_widget_show(answer_C);
  gtk_widget_show(answer_D);
  timer_id = g_timeout_add_seconds(1, verify_timer, NULL);
  return 0;
}

int main(int argc, char *argv[])
{
  signal(SIGINT, handle_signal);
  signal(SIGTSTP, handle_signal);
  signal(SIGQUIT, handle_signal);
  signal(SIGTERM, handle_signal);

  //fereastra de start pentru GUI
  gtk_init(&argc, &argv);
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  gtk_window_set_default_size(GTK_WINDOW(window), 955, 495);

  grid = gtk_grid_new();
  gtk_container_add(GTK_CONTAINER(window), grid);

  PangoAttrList *attr_list = pango_attr_list_new();
  PangoAttribute *attr = pango_attr_size_new(20 * PANGO_SCALE); // Set font size to 20
  pango_attr_list_insert(attr_list, attr);

  start_message = gtk_label_new("WELCOME TO THE QUIZZ");
  gtk_grid_attach(GTK_GRID(grid), start_message, 0, 0, 2, 1);
  gtk_widget_set_size_request(start_message, 955, 155);
  gtk_label_set_attributes(GTK_LABEL(start_message), attr_list);

  username = gtk_label_new("USERNAME:");
  gtk_grid_attach(GTK_GRID(grid), username, 0, 1, 1, 1);
  gtk_widget_set_size_request(username, 300, 150);
  gtk_label_set_attributes(GTK_LABEL(username), attr_list);

  // casuta pentru introducerea numelui
  enter_username = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid), enter_username, 1, 1, 1, 1);
  gtk_widget_set_size_request(enter_username, 655, 150);
  gtk_entry_set_attributes(GTK_ENTRY(enter_username), attr_list);

  send_name = gtk_button_new_with_label("START");
  gtk_grid_attach(GTK_GRID(grid), send_name, 0, 2, 2, 1);
  gtk_widget_set_size_request(send_name, 955, 190);

  answer_A = gtk_button_new_with_label("A");
  gtk_grid_attach(GTK_GRID(grid), answer_A, 0,1, 1, 1);
  gtk_widget_set_size_request(answer_A,gtk_widget_get_allocated_width(answer_A),150);

  answer_B = gtk_button_new_with_label("B");
  gtk_grid_attach(GTK_GRID(grid), answer_B, 1,1, 1, 1);
  gtk_widget_set_size_request(answer_B,gtk_widget_get_allocated_width(answer_B),150);

  answer_C = gtk_button_new_with_label("C");
  gtk_grid_attach(GTK_GRID(grid), answer_C, 0,2, 1, 1);
  gtk_widget_set_size_request(answer_C,gtk_widget_get_allocated_width(answer_C),150);

  answer_D = gtk_button_new_with_label("D");
  gtk_grid_attach(GTK_GRID(grid), answer_D, 1,2, 1, 1);
  gtk_widget_set_size_request(answer_D,gtk_widget_get_allocated_width(answer_D),150);

  see_ranking = gtk_button_new_with_label("SEE RANKING");
  gtk_grid_attach(GTK_GRID(grid), see_ranking, 0,1, 2, 1);
  gtk_widget_set_size_request(see_ranking,gtk_widget_get_allocated_width(see_ranking),150);

  timer = gtk_label_new("-after you press start you will be in waiting room-");
  gtk_grid_attach(GTK_GRID(grid), timer, 0, 3, 2, 1);
  //gtk_widget_set_size_request(timer, 150, 150);
  gtk_label_set_attributes(GTK_LABEL(timer), attr_list);

  g_signal_connect(send_name, "clicked", G_CALLBACK(say_hello), NULL);
  g_signal_connect(answer_A, "clicked", G_CALLBACK(send_A), NULL);
  g_signal_connect(answer_B, "clicked", G_CALLBACK(send_B), NULL);
  g_signal_connect(answer_C, "clicked", G_CALLBACK(send_C), NULL);
  g_signal_connect(answer_D, "clicked", G_CALLBACK(send_D), NULL);
  g_signal_connect(see_ranking, "clicked", G_CALLBACK(send_A), NULL);
  g_signal_connect(window, "delete-event", G_CALLBACK(exit_client), NULL);

  gtk_widget_show_all(window);

  gtk_widget_hide(answer_A);
  gtk_widget_hide(answer_B);
  gtk_widget_hide(answer_C);
  gtk_widget_hide(answer_D);
  //gtk_widget_hide(timer);

  gtk_widget_hide(see_ranking);

  gtk_main();

  return 0;
}