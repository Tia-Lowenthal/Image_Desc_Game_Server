/*
** This server was created for the comp30023 project 1 2019 by Tia Lowenthal
*/

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// constants
static char const * const HTTP_200_FORMAT = "HTTP/1.1 200 OK\r\n\
%s\
Content-Type: text/html\r\n\
Content-Length: %ld\r\n\r\n";
static char const * const HTTP_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_400_LENGTH = 47;
static char const * const HTTP_404 = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_404_LENGTH = 45;
static char const * const P_OPEN = "<p>";
static char const * const P_CLOSE = "</p>";
static int const EQUAL_ASCII = 61;
static int const BUFFER_SIZE = 5000;
static int const COOKIE_LEN = 15; // max string length of a cookie "usercookie=000\0"
static int const COOKIE_HEADER_LEN = 27; // "set-cookie: usercookie=000\0"
static int const MAX_GUESSES = 100;
static int MAX_PLAYERS = 100; 
static int cookie_counter = 0;
static int round = true; // boolean switch for game round


// represents the types of http requests
typedef enum{
    GET,
    POST,
    UNKNOWN
} METHOD;

// represents client statuses throughout the game
typedef enum{
    CONNECTED = 0,
    INTRO = 1,
    MAIN_MENU = 2,
    FIRST_TURN = 3,
    ACCEPTED = 4,
    DISCARDED = 5,
    ENDGAME = 6,
    GAME_OVER = 7,
    INVALID = -1
} STATUS;

// array of client guesses
typedef struct Guesses {
    char** arr;
    int num;
} Guesses;

// client data
typedef struct Client {
    char* cookie;
    int sockfd;
    int status;
    bool active;
    char* username;
    Guesses* guesses;
} Client;


/******************** methods ********************/

// returns the http request type
METHOD req_type(char* req){
    METHOD method = UNKNOWN;
    // return a 404 for a favicon request
    if(strstr(req, "favicon")){
        return method;
    } 
    else if (strncmp(req, "GET ", strlen("GET \0")) == 0){
        method = GET;
    }
    else if (strncmp(req, "POST ", strlen("POST \0")) == 0){
        method = POST;
    }    
    return method;
}


// checks whether the client has pressed the quit button
bool has_quit(char* req){
    if(strstr(req, "quit=Quit")){
        return true;
    }
    return false;
}


// reads a client request into a buffer (based on lab 6 code)
bool read_request(int sockfd, char* buffer){
    
    // try to read the request
    int n = read(sockfd, buffer, BUFFER_SIZE);
    if (n <= 0)
    {
        if (n < 0)
            perror("read");
        else
            printf("socket %d close the connection\n", sockfd);
        return false;
    }

    // terminate the string
    buffer[n] = 0;
    return true;
}


// gets a client's status based on their sockfd
STATUS get_status_from_sockfd(int sockfd, Client** clients){
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(clients[j]->sockfd == sockfd){
                return clients[j]->status;
            }   
        } 
    }     
    return INVALID;
}


// sends a 200 response to the client (based on lab 6 code)
bool send_response(int sockfd, char* filename, char* buffer, char* cookie){
    
    // get the size of the file
    struct stat st;
    stat(filename, &st);
    int n = sprintf(buffer, HTTP_200_FORMAT, cookie, st.st_size);
    
    // send the header first
    if (write(sockfd, buffer, n) < 0){
        perror("write");
        return false;
    }
    // send the file
    int filefd = open(filename, O_RDONLY);
    do{
        n = sendfile(sockfd, filefd, NULL, BUFFER_SIZE - 1);
    }
    while (n > 0);
    
    if (n < 0){
        perror("sendfile");
        close(filefd);
        return false;
    }
    close(filefd);
    return true;
}


// sends the main menu page with a client's username displayed
bool send_start_page(int sockfd, char* buffer, char* username){
    
    // get the size of the file
    struct stat st;
    stat("2_start.html", &st);
    
    // increase the file size by length of the html line to display the username
    // +1  for /0 at the end of the line
    int username_len = strlen(username) + strlen(P_OPEN) + strlen(P_CLOSE) + 1;
    long file_size = username_len + st.st_size;
    
    int n = sprintf(buffer, HTTP_200_FORMAT, "", file_size);
    
    // send the header first
    if (write(sockfd, buffer, n) < 0){
        perror("write");
        return false;
    }
    
    // read the contents of the html file
    int filefd = open("2_start.html", O_RDONLY);
    n = read(filefd, buffer, BUFFER_SIZE);
    if (n < 0)
    {
        perror("read");
        close(filefd);
        return false;
    }
    close(filefd);
    buffer[st.st_size] = '\0';
    
    // create the html line
    char* html_line = (char*)malloc((username_len + 1) * sizeof(char));
    strcat(html_line, P_OPEN);
    strcat(html_line, username);
    strcat(html_line, P_CLOSE);
    html_line[username_len - 1] = '\n';
    html_line[username_len] = '\0';
    
    // split the file where the username line will be inserted
    char* locn = strstr(buffer, "<form");
    int first_half_len = locn - buffer + 1;
    char* first_half = (char*)malloc(first_half_len*sizeof(char));
    strncpy(first_half, buffer, first_half_len - 1);
    first_half[first_half_len - 1] = '\0';
    
    int second_half_len = st.st_size - strlen(first_half) + 2;
    char* second_half = (char*) malloc (second_half_len * sizeof(char) );
    strncpy(second_half, locn, second_half_len - 1);
    second_half[second_half_len - 1] = '\0';
 
    // combine parts to form the output html file
    int output_size = strlen(first_half) + strlen(html_line) + strlen(second_half);
    char* output_file = (char*)calloc(output_size + 1, sizeof(char));
    strcat(output_file, first_half);
    strcat(output_file, html_line);
    strcat(output_file, second_half);
    output_file[output_size] = '\0';
    
    // send the file
    if (write(sockfd, output_file, output_size) < 0){
        perror("write");
        return false;
    }
    return true;
}


// sets a client's status based on their sockfd
bool set_client_status_from_sockfd(int sockfd, Client** clients, STATUS status){
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(clients[j]->sockfd == sockfd){
                clients[j]->status = status;
                return true;
            }   
        } 
    }     
    return false;    
}


// sets a client's status based on their cookie
bool set_status(Client** clients, char* cookie, STATUS status){
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(strcmp(clients[j]->cookie, cookie) == 0){
                clients[j]->status = status;
                return true;
            }   
        } 
    }     
    return false;
}


// extracts the username from a client's http request
char* extract_username(char* req){
    // find the username in the request
    char* pointer = strstr(req, "user=") + strlen("user=");
    
    // get the real length of the username since there are weird 
    // whitespace characters in the request
    int username_len = 0;
    for(int i = 0; i < strlen(pointer); i++){
        if(isalnum(pointer[i]) != 0){
            username_len++;
        }
        else{
            break;
        }
    }

    // create the username
    char* username = (char*)malloc((username_len + 1) * sizeof(char));
    strncpy(username, pointer, username_len);
    username[username_len] = '\0';
    return username;
}


// store a client's username
bool save_username(Client** clients, char* cookie, char* username){
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(strcmp(clients[j]->cookie, cookie) == 0){
                clients[j]->username = username;
                return true;
            }   
        } 
    }     
    return false;
}


// retrieves a client's username based on their cookie
char* get_username(Client** clients, char* cookie){
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(strcmp(clients[j]->cookie, cookie) == 0){
                return clients[j]->username;
            }   
        } 
    } 
    return "\0";
}


// generates a cookie
char* generate_cookie(){
    char* cookie = (char*)malloc(COOKIE_LEN * sizeof(char));
    snprintf(cookie, COOKIE_LEN - 1, "usercookie=%d", cookie_counter);
    cookie[COOKIE_LEN - 1] = '\0';
    cookie_counter++;
    return cookie;
}


// assigns a cookie to a client
bool assign_cookie(int sockfd, Client** clients, char* cookie){
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(clients[j]->sockfd == sockfd){
                clients[j]->cookie = cookie;
                return true;
            }   
        } 
    }        
    return false;  
}


// extracts the cookie from the client's request
char* get_cookie(char* req){

    char* pointer = strstr(req, "Cookie: ") + strlen("Cookie: \0");
    
    // ensure a cookie has been provided
    if(pointer == NULL){
        return "\0";
    }
    
    // get the real length of the cookie since there are weird whitespace characters
    int cookie_len = 0;
    for(int i = 0; i < strlen(pointer); i++){
        if(isalnum(pointer[i]) != 0 || (int)pointer[i] == EQUAL_ASCII){
            cookie_len++;
        }
        else{
            break;
        }
    }

    // create the cookie
    char* cookie = (char*)malloc((cookie_len + 1) * sizeof(char));
    strncpy(cookie, pointer, cookie_len);
    cookie[cookie_len] = '\0';
    return cookie;   
}


// checks if a cookie has been provided in the client's request 
bool cookie_in_req(char* req){
    if(strstr(req, "Cookie:")){
        return true;
    }
    return false;
}


// returns the guesses struct for a client
Guesses* get_client_guesses(Client** clients, char* cookie){
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(strcmp(clients[j]->cookie, cookie) == 0){
                return clients[j]->guesses;
            }   
        } 
    } 
    return NULL;     
}


// check if a guess has been guessed before
bool duplicate_guess(Client** clients, char* cookie, char* guess){
    // get the other player's cookie
    Guesses* other_player_guesses = NULL;
    char* other_player_cookie;
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(clients[j]->cookie != NULL && clients[j]->active){
                if(strcmp(clients[j]->cookie, cookie) != 0 && clients[j]->active){
                    other_player_cookie = clients[j]->cookie;
                } 
            }
        }   
    }    
    
    // get the other player's guesses struct
    other_player_guesses = get_client_guesses(clients, other_player_cookie);
    
    // check if they made the guess previously
    for(int i = 0; i < other_player_guesses->num; i++){
        if(strcmp(guess, other_player_guesses->arr[i]) == 0){
            return true;
        }
    }
    return false;
}


// adds a guess to a client's guesses struct
void add_guess(Client** clients, char* cookie, char* guess){
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(strcmp(clients[j]->cookie, cookie) == 0){
                clients[j]->guesses->arr[clients[j]->guesses->num] = guess;
                clients[j]->guesses->num++;
            }   
        } 
    }    
}


// get the status of a client
STATUS get_status(Client** clients, char* cookie){
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(strcmp(clients[j]->cookie, cookie) == 0){
                return clients[j]->status;
            }   
        } 
    } 
}


// gets the status of the other player to the cookie provided
STATUS other_player_status(Client** clients, char* cookie){
    // get the other player's cookie
    char* other_player_cookie;
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(clients[j]->cookie != NULL && clients[j]->active){
                if(strcmp(clients[j]->cookie, cookie) != 0 && clients[j]->active){
                    other_player_cookie = clients[j]->cookie;
                } 
            }
        }   
    }
    return get_status(clients, other_player_cookie);
}


// extracts the guess from a client's request
char* get_guess(char* req){
    // determine the start and end of the guess
    char* start = strstr(req, "keyword=") + strlen("keyword=");
    char* end = strstr(start, "&") -1;
    
    // create the guess
    int guess_len = end - start + 2;  
    char* guess = (char*)malloc(guess_len*sizeof(char));  
    strncpy(guess, start, guess_len);  
    guess[guess_len - 1] = '\0';  
    return guess;
}


// clears all of a client's guesses
void free_all_guesses(Client** clients, char* cookie){
    // get the player's guesses struct
    Guesses* player_guesses = NULL;
    for(int i = 0; i < MAX_PLAYERS; i++){
        if(clients[i] != NULL){
            if(strcmp(clients[i]->cookie, cookie) == 0){
                player_guesses = clients[i]->guesses;
            }  
        }  
    } 

    // free the guesses 
    for(int j = 0; j < player_guesses->num; j++){
        free(player_guesses->arr[j]);
    }
    player_guesses->num = 0;
}


// returns the length of the guesses for outputting to the html file
int guess_words_length(Client** clients, char* cookie){
    int sum = 0;
    Guesses* player_guesses = NULL;
    
    // get the player's guesses struct
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(strcmp(clients[j]->cookie, cookie) == 0){
                player_guesses = clients[j]->guesses;
            }   
        } 
    }
    
    // increment the sum
    for(int i = 0; i < player_guesses->num; i++){
        // +2 for the ", " between the words
        sum += strlen(player_guesses->arr[i]) + 2;
    }
    // -2 since there isn't ", " after the last word
    return sum - 2;
}


// sends the "accepted" page with the guesses displayed
bool send_guesses_page(int sockfd, char* filename, char* buffer, Client** clients, char* cookie){
    // get the size of the file
    struct stat st;
    stat(filename, &st);
    
    // increase file size by length of line to display username
    // +1 for \0 at the end of the line
    int guesses_len = guess_words_length(clients, cookie) + strlen(P_OPEN) + strlen(P_CLOSE) + 1;
    long file_size = guesses_len + st.st_size;
    
    int n = sprintf(buffer, HTTP_200_FORMAT, "", file_size);
    
    // send the header first
    if (write(sockfd, buffer, n) < 0){
        perror("write");
        return false;
    }
    
    // read the contents of the html file
    int filefd = open(filename, O_RDONLY);
    n = read(filefd, buffer, BUFFER_SIZE);
    if (n < 0)
    {
        perror("read");
        close(filefd);
        return false;
    }
    close(filefd);
    buffer[st.st_size] = '\0';
    
    // create the html line
    char* html_line = (char*)malloc((guesses_len + 1) * sizeof(char));
    Guesses* player_guesses = get_client_guesses(clients, cookie);
    strcat(html_line, P_OPEN);
    for(int i = 0; i < player_guesses->num - 1; i++){
        strcat(html_line, player_guesses->arr[i]);
        strcat(html_line, ", ");
    }
    strcat(html_line, player_guesses->arr[player_guesses->num - 1]);
    strcat(html_line, P_CLOSE);
    html_line[guesses_len - 1] = '\n';
    html_line[guesses_len] = '\0';
    
    // break up the file where the html line will be inserted
    char* locn = strstr(buffer, "<form");
    int first_half_len = locn - buffer + 1;
    char* first_half = (char*)malloc(first_half_len*sizeof(char));
    strncpy(first_half, buffer, first_half_len - 1);
    first_half[first_half_len - 1] = '\0';
    
    int second_half_len = st.st_size - strlen(first_half) + 2;
    char* second_half = (char*)malloc(second_half_len* sizeof(char));
    strncpy(second_half, locn, second_half_len - 1);
    second_half[second_half_len - 1] = '\0';
 
    // combine the parts to form the output html file
    int output_size = strlen(first_half) + strlen(html_line) + strlen(second_half);
    char* output_file = (char*)calloc(output_size + 1, sizeof(char));
    strcat(output_file, first_half);
    strcat(output_file, html_line);
    strcat(output_file, second_half);
    output_file[output_size] = '\0';
    
    // send the file
    if (write(sockfd, output_file, output_size) < 0){
        perror("write");
        return false;
    }
    return true;
}


// removes a client from the clients array
void remove_client_by_sockfd(Client** clients, int sockfd){
    for(int i = 0; i < MAX_PLAYERS; i++){
        if(clients[i] != NULL){
            if(clients[i]->sockfd == sockfd){
                free(clients[i]);
                clients[i] = NULL;
                break;
            }
        }
    }
}


// sets a player's sockfd to -1 if they quit
void reset_client_sockfd(Client** clients, char* cookie){
    for(int i = 0; i < MAX_PLAYERS; i++){
        if(clients[i] != NULL){
            if(strcmp(clients[i]->cookie, cookie) == 0){
                clients[i]->sockfd = -1;
            }
        }
    }   
}


// flips the activity status of a client (bool)
void flip_client_activity_status(Client** clients, char* cookie){
    for(int i = 0; i < MAX_PLAYERS; i++){
        if(clients[i] != NULL){
            if(strcmp(clients[i]->cookie, cookie) == 0){
                clients[i]->active = !clients[i]->active;
            }
        }
    }   
}


// gets the activity status of the other player
bool other_player_active(Client** clients, char* cookie){
    // try to find an active client
    for(int j = 0; j < MAX_PLAYERS; j++){
        if(clients[j] != NULL){
            if(clients[j]->cookie != NULL && clients[j]->active){
                if(strcmp(clients[j]->cookie, cookie) != 0 && clients[j]->active){
                    return true;
                } 
            }
        }   
    }
    return false;
}


// sets a client's sockfd
void set_client_sockfd(Client** clients, char* cookie, int new_sockfd){
    for(int i = 0; i < MAX_PLAYERS; i++){
        if(clients[i] != NULL){
            if(strcmp(clients[i]->cookie, cookie) == 0){
                clients[i]->sockfd = new_sockfd;
            }
        }
    }   
}



// handles an incoming request from a client
bool handle_http_request(int sockfd, Client** clients){

    // read request to a buffer
    char buffer[BUFFER_SIZE];   
    if(!read_request(sockfd, buffer)){
        return false;
    }
     printf("%s\n", buffer);
     fflush(stdout);   
    
    METHOD method = req_type(buffer);
    // sends a 404 to the client if a non standard HTTP GET or POST request received
    if(method == UNKNOWN){
        if (write(sockfd, HTTP_404, HTTP_404_LENGTH) < 0){
            perror("write");
            return false;
        }
        return true;
    }
    
    int client_status = get_status_from_sockfd(sockfd, clients);
  
    // if the client quits at any point
    if(has_quit(buffer)){
        char* cookie = get_cookie(buffer);
        send_response(sockfd, "7_gameover.html", buffer, "");
        set_status(clients, cookie, GAME_OVER);
        reset_client_sockfd(clients, cookie);
        flip_client_activity_status(clients, cookie);
        free_all_guesses(clients, cookie);
        return false;
    }  
    
    // no screen -> intro screen
    else if(client_status == CONNECTED && !cookie_in_req(buffer)){
        // generate and store a cookie for the client
        char* cookie = generate_cookie();
        assign_cookie(sockfd, clients, cookie);
        
        // generate the cookie header
        char* cookie_header = (char*)malloc(COOKIE_HEADER_LEN * sizeof(char));
        snprintf(cookie_header, COOKIE_HEADER_LEN, "Set-Cookie: %s\n", cookie);
     
        // send the intro screen
        if(send_response(sockfd, "1_intro.html", buffer, cookie_header)){
            set_status(clients, cookie, INTRO);
        }
        else{
            return false;
        }
    }

    // a returning client (they have a cookie) -> main menu screen
    else if(client_status == CONNECTED && cookie_in_req(buffer)){
        // remove duplicate client from array
        remove_client_by_sockfd(clients, sockfd); 
        
        // update the client's sockfd
        char* cookie = get_cookie(buffer);
        set_client_sockfd(clients, cookie, sockfd);
        
        // set the client to active
        // flip_client_activity_status(clients, cookie);
        
        // send the main menu screen with the username displayed
        if(send_start_page(sockfd, buffer, get_username(clients, cookie))){
            set_status(clients, cookie, MAIN_MENU);
        }
        else{
            return false;
        }         
    }
    
    // intro screen -> main menu screen
    else if(client_status == INTRO){
        // extract and store the username
        char* username = extract_username(buffer);
        char* cookie = get_cookie(buffer);
        save_username(clients, cookie, username);     
        
        // send the main menu screen with the username displayed
        if(send_start_page(sockfd, buffer, username)){
            set_status(clients, cookie, MAIN_MENU);
        }
        else{
            return false;
        }
    }
    
    // main menu screen -> first turn screen
    else if(client_status == MAIN_MENU && method == GET){
        char* player_cookie = get_cookie(buffer);
        flip_client_activity_status(clients, player_cookie);
        //round 1
        if(round){
            if(send_response(sockfd, "3_first_turn.html", buffer, "")){
                set_status(clients, player_cookie, FIRST_TURN);
            }
            else{
                return false;
            }        
        }
        // round 2
        else{
            if(send_response(sockfd, "3.2_first_turn.html", buffer, "")){
                set_status(clients, player_cookie, FIRST_TURN);
            }
            else{
                return false;
            }         
        }

    } 
    
    // if client is on first turn screen or "discarded" screen
    else if(client_status == FIRST_TURN || client_status == DISCARDED){
        // get the status of the other player
        char* player_cookie = get_cookie(buffer);
        
        // check if other player has quit
        if(!other_player_active(clients, player_cookie)){
            if(round){
                if(send_response(sockfd, "5_discarded.html", buffer, "")){
                    set_status(clients, player_cookie, DISCARDED);
                    return true;
                }
                else{
                    return false;
                }             
            }
            //round 2
            else{
                if(send_response(sockfd, "5.2_discarded.html", buffer, "")){
                    set_status(clients, player_cookie, DISCARDED);
                    return true;
                }
                else{
                    return false;
                } 
            }          
        }
        
        int other_players_status = other_player_status(clients, player_cookie);

        // if the other player has started the game
        if(other_players_status >= FIRST_TURN && other_players_status <= DISCARDED){           
            // check if the game is won
            char* guess = get_guess(buffer);
            if(duplicate_guess(clients, player_cookie, guess)){
                if(send_response(sockfd, "6_endgame.html", buffer, "")){
                    set_status(clients, player_cookie, ENDGAME);
                }
                else{
                    return false;
                }           
            }

            // store the guess and send "accepted" screen
            else{
                add_guess(clients, player_cookie, guess);
                // round 1
                if(round){
                    if(send_guesses_page(sockfd, "4_accepted.html", buffer, clients, player_cookie)){
                        set_status(clients, player_cookie, ACCEPTED);
                    }
                    else{
                        return false;
                    }
                }
                //round 2
                else{
                    if(send_guesses_page(sockfd, "4.2_accepted.html", buffer, clients, player_cookie)){
                        set_status(clients, player_cookie, ACCEPTED);
                    }
                    else{
                        return false;
                    }                
                }   
            }         
        }
    }
    
    // both players are playing the game
    else if(client_status == ACCEPTED){

        char* player_cookie = get_cookie(buffer);
        // check if the other player has quit
        if(!other_player_active(clients, player_cookie)){
            send_response(sockfd, "7_gameover.html", buffer, "");
            set_status(clients, player_cookie, GAME_OVER);
            reset_client_sockfd(clients, player_cookie);
            flip_client_activity_status(clients, player_cookie);
            free_all_guesses(clients, player_cookie);
            return false;           
        }
        
        int other_player_status_val = other_player_status(clients, player_cookie);
        char* guess = get_guess(buffer);

        // check if the game has been won
        if(other_player_status_val == 6 || duplicate_guess(clients, player_cookie, guess)){
            if(send_response(sockfd, "6_endgame.html", buffer, "")){
                set_status(clients, player_cookie, ENDGAME);
                free_all_guesses(clients, player_cookie);
            }
            else{
                return false;
            }           
        }
        
        // add the guess to the array and keep playing the game
        else{
            add_guess(clients, player_cookie, guess);
            //round 1
            if(round){
                if(!send_guesses_page(sockfd, "4_accepted.html", buffer, clients, player_cookie)){
                    return false;
                }  
            }
            //round 2
            else{
                if(!send_guesses_page(sockfd, "4.2_accepted.html", buffer, clients, player_cookie)){
                    return false;
                }            
            }
        }    
    }

    // endgame screen -> new round
    else if(client_status == ENDGAME){
        char* player_cookie = get_cookie(buffer);
        
        // if the other player has quit force quit this player
        if(!other_player_active(clients, player_cookie)){
            send_response(sockfd, "7_gameover.html", buffer, "");
            set_status(clients, player_cookie, GAME_OVER);
            reset_client_sockfd(clients, player_cookie);
            flip_client_activity_status(clients, player_cookie);
            free_all_guesses(clients, player_cookie);
            return false;           
        }
        
        // check if the other player has already started a new round
        if(other_player_status(clients, player_cookie) >= client_status){
            round = !round;
        }
        
        // round 1
        if(round){
            if(send_response(sockfd, "3_first_turn.html", buffer, "")){
                set_status(clients, player_cookie, FIRST_TURN);
            }
            else{
                return false;
            }       
        }
        //round 2
        else{
            if(send_response(sockfd, "3.2_first_turn.html", buffer, "")){
                set_status(clients, player_cookie, FIRST_TURN);
            }
            else{
                return false;
            }       
        }

    }
    
    return true;
}


// initialises a client
void initialise_client(Client* client, int sockfd){
    client->sockfd = sockfd;
    client->status = CONNECTED;
    client->active = false;
    client->cookie = NULL;
    client->username = NULL;
    client->guesses = (Guesses*)malloc(sizeof(Guesses));
    client->guesses->arr = (char**)malloc(MAX_GUESSES * sizeof(char*));
    client->guesses->num = 0;
}


// creates and initialiases a client (based on lab 6 code)
void create_client(Client** clients, int sockfd){
    int i = 0;
    while(clients[i] != NULL){
        i++;
    }
    clients[i] = (Client*)malloc(sizeof(Client));
    initialise_client(clients[i], sockfd);
}


int main(int argc, char * argv[]){
    
    if (argc < 3){
        fprintf(stderr, "usage: %s ip port\n", argv[0]);
        return 0;
    }
    
    Client* clients[MAX_PLAYERS];
    for(int i = 0; i < MAX_PLAYERS; i++){
        clients[i] = NULL;
    }

    // create TCP socket which only accept IPv4
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    

    // reuse the socket if possible
    int const reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0)
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // create and initialise address we will listen on
    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    // if ip parameter is not specified
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    // bind address to socket
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    
    // listen on the socket
    listen(sockfd, 5);

    // initialise an active file descriptors set
    fd_set masterfds;
    FD_ZERO(&masterfds);
    FD_SET(sockfd, &masterfds);
    // record the maximum socket number
    int maxfd = sockfd;

    while (1)
    {
        // monitor file descriptors
        fd_set readfds = masterfds;
        if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        // loop all possible descriptor
        for (int i = 0; i <= maxfd; ++i){
            // determine if the current file descriptor is active
            if (FD_ISSET(i, &readfds)){
                // create new socket if there is new incoming connection request
                if (i == sockfd){
                    struct sockaddr_in cliaddr;
                    socklen_t clilen = sizeof(cliaddr);
                    int newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &clilen);
                    if (newsockfd < 0)
                        perror("accept");
                    else{
                        // add the socket to the set
                        FD_SET(newsockfd, &masterfds);
                        // update the maximum tracker
                        if (newsockfd > maxfd)
                            maxfd = newsockfd;
                        
                        // create a client and add to clients array
                        create_client(clients, newsockfd);
                        
                        // print out the IP and the socket number
                        char ip[INET_ADDRSTRLEN];
                        printf(
                            "new connection from %s on socket %d\n",
                            // convert to human readable string
                            inet_ntop(cliaddr.sin_family, &cliaddr.sin_addr, ip, INET_ADDRSTRLEN),
                            newsockfd
                        );
                    }
                } 
                else if (!handle_http_request(i, clients)){
                    //remove_client(clients, i);
                    close(i);
                    FD_CLR(i, &masterfds);
                }              
            }
        }
    }

    return 0;
}