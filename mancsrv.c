#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80  /* maximum permitted name size, not including \0 */
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* maximum permitted message size */

int port = 30000;
// set up some useful variables
int listenfd;
fd_set all_fds;
char welcome_msg[MAXMESSAGE] = "Welcome to Mancala. What is your name?\n";
char turn_announcement[MAXMESSAGE] = "";

struct player {
    int fd;
    char name[MAXNAME+1]; 
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits 
                        // pits[NPITS] is the end pit
    //other stuff undoubtedly needed here
    struct player *next;
    int entering_name; //waiting for the user to enter their name
    int player_turn; //indicates whether its this player's turn or not
    int removed; // indicates whether a player is being removed
};
struct player *playerlist = NULL;

extern void disconnect_player(struct player *p);
extern void find_player_turn(struct player *p);
extern void player_turn(struct player *p);
extern void move_pebbles(struct player *origin, struct player *p, int n, int num_of_pebbles);
extern void generate_game_board(struct player *playerlist);
extern int find_newline(const char *value);
extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char *s, int player_fd);  /* you need to write this one */
extern int accept_new_player(int fd);

int main(int argc, char **argv) {
    char msg[MAXMESSAGE];
    parseargs(argc, argv);
    makelistener();
    // initialize the set of fds
    int max_fd = listenfd;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);

    while (!game_is_over()) {
	fd_set readfds = all_fds;
	int nready = select(max_fd + 1, &readfds, NULL, NULL, NULL);
	if (nready == -1) {
	    perror("server: select");
	    exit(1);
	}
	if (FD_ISSET(listenfd, &readfds)) { // a new player is trying to connect
	    // accept the connection
	    int player_fd = accept_new_player(listenfd);
	    if (player_fd > max_fd) {
		max_fd = player_fd;
	    }
	    // add player's fd to the set and print welcome message
	    FD_SET(player_fd, &all_fds);
	    printf("A new player just connected\n");
	    write(player_fd, welcome_msg, strlen(welcome_msg));
	}

	for (struct player *p = playerlist; p; p = p->next) { // an existing player performed an action
	    if (p->fd > -1 && FD_ISSET(p->fd, &readfds)) {
		char buffer[MAXMESSAGE];
		if (read(p->fd, buffer, MAXMESSAGE) == 0) { // if read returns 0 disconnect the player
		    disconnect_player(p);
		} else {
		    if (p->entering_name == 1){ // a player is entering their name
		        char value[MAXNAME+1] = "";
		        char buf[MAXNAME+1] = "";
		        int duplicate = 0;
		        int empty = 0;
			strncat(buf, buffer, strlen(buffer));
			while(!find_newline(buf)) { // keep readiing player input until a newline character is found
			    read(p->fd, value, MAXNAME);
			    strncat(buf, value, strlen(value));
			}
			if (strlen(buf) < MAXNAME) { // make sure the name is not empty or too long
		            buf[strcspn(buf, "\r\n")] = 0; // remove trailing newline characters
			    if (!strcmp(buf, "")) { // make sure the name is not an empty string
				empty = 1;
			    }
			    for (struct player *p2 = playerlist; p2; p2=p2->next) { 
			        // make sure the name isnt already taken
			        if (!p2->entering_name && !strcmp(buf,p2->name)) {
				    duplicate = 1;
			        }
			    }
			    if (!duplicate && !empty) {
				int have_ready_players = 0;
			        // set new player's name to value
			        strcpy(p->name, buf);
			        p->entering_name = 0;
			        printf("A player just entered their name: %s\n", p->name);
			        // broadcast new player entry to all players
			        char new_player_announcement[MAXMESSAGE];
			        sprintf(new_player_announcement, "%s has joined the game\n", p->name);
			        broadcast(new_player_announcement, p->fd);
			        // broadcast the updated game board to all players
			        generate_game_board(playerlist);
			        // find the player who's current turn it is
				for (struct player *p2 = playerlist; p2; p2=p2->next) {
				    if (p2->player_turn == 1) { 
					// if there is a player's turn already, continue the game as is
					have_ready_players = 1;
				    }
				}
				if (!have_ready_players) { // if there is no players whose turn it is
				    // give the turn to the current player
				    player_turn(p);
				}
			    } else { // the player entered a name that is already taken or an empty string
				printf("Player entered a duplicate name or empty string\n");
			        disconnect_player(p);
			    }
		        } else { // the player entered a name that is too long
			    printf("Player entered a name that is too long\n");
			    disconnect_player(p);
			}
		    }
		       else if (p->player_turn) { //if it is the current player's turn to make a move
		            char *ptr;
		            long move = strtol(buffer, &ptr, 10);
		            if (move < NPITS) { // make sure the player chose a valid pit
		                int num_of_pebbles = p->pits[move];
			        if (num_of_pebbles != 0) { // make sure the player chose a non empty pit
			            // remove all pebbles from the selected pit
		                    p->pits[move] = 0;
			            // distribute the pebbles and broadcast the updated game board to all players
		                    move_pebbles(p, p, move, num_of_pebbles);
		                    printf("%s's turn, they entered %ld\n", p->name, move);
			            sprintf(msg, "%s chose pit %ld\n", p->name, move);
			            broadcast(msg, p->fd);
			            generate_game_board(playerlist);
			            if ((move + num_of_pebbles) == NPITS) { 
				        // if the last pebble ends in the player's end pit they go again
				        player_turn(p);
			            } else {
				        // find the next player who's turn it is
				        p->player_turn = 0;
				        find_player_turn(p);
			            }
			        } else { // player chose a pit with no pebbles
			            char *invalid = "PLease choose a non-empty pit\n";
			            write(p->fd, invalid, strlen(invalid));
			            player_turn(p);
			        }
		            } else { // player choose a pit that doesnt exist
			        char *invalid = "Please choose a valid pit number\n";
			        write(p->fd, invalid, strlen(invalid));
			        player_turn(p);
		            }

		    } else { // it is not the current player's turn to make a move
		            write(p->fd, "It is not your move\n", strlen("It is not your move\n"));
		            printf("A player just entered: %s\n", buffer);
		    }
		}
	    }
	}
    }

    broadcast("Game over!\r\n", -1);
    printf("Game over!\n");
    for (struct player *p = playerlist; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }
        printf("%s has %d points\r\n", p->name, points);
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        broadcast(msg, -1);
    }

    return 0;
}

void disconnect_player(struct player *dp) {
    // function to disconnect a player from the game
    dp->removed = 1;
    if (dp->player_turn) { // if it is the current player's turn set it to the next player's turn
	dp->player_turn = 0;
	find_player_turn(dp);
    }
    if (playerlist->fd == dp->fd) { // if the player is at the front of the playerlist remove them from the list
	playerlist = playerlist->next;
    }
    for (struct player *p = playerlist; p; p=p->next) { //loop through playerlist and disconnect removed players in the middle
	if (p->next != NULL && (p->next)->fd == dp->fd) {
	    p->next = dp->next;
	}
    }
    // notify other players that a player left
    char disconnect_msg[MAXMESSAGE];
    if (dp->entering_name == 0) { // the player was in game
        sprintf(disconnect_msg, "%s has left the game\n", dp->name);
	broadcast(disconnect_msg, dp->fd);
	printf("%s left the game\n", dp->name);
    } else { // the player did not finish entering their name
	printf("A player was removed from the game\n");
    }
    // close the player's fd and remove them from all_fds set
    close(dp->fd);
    FD_CLR(dp->fd, &all_fds);
    dp->fd = -1;
    free(dp);
}
void find_player_turn(struct player *p) {
    // function to determines who is next to move in the game
    struct player *next_player = p->next;
    while(next_player != NULL && next_player->entering_name == 1) { 
	// while the next player in the list is still entering their name
	// skip to the player after that
	next_player = next_player->next;
    }
    if (next_player != NULL) {
	// if the player is not NULL, it is this player's turn
	player_turn(next_player);
    } else {
	// if the player is NULL, the end of the list has been reached
	next_player = playerlist;
	// start looping from the beginning of the list
	// and find the first player that's done entering their name
	while(next_player->entering_name == 1) {
	    next_player = next_player->next;
	}
	player_turn(next_player);
    }

}
void player_turn(struct player *p) {
    // function to set the turn to a player
    p->player_turn = 1;
    // send the player a message telling them it is their turn
    write(p->fd, "Your move?\n", strlen("Your move?\n"));
    // broadcast whose turn it is to the rest of the players
    sprintf(turn_announcement, "It is %s's turn\n", p->name);
    broadcast(turn_announcement, p->fd);
}
void move_pebbles(struct player *origin, struct player *p, int n, int num_of_pebbles) {
    // function to move the pebbles in pit n into the subsequence pits
    int i=1;
    while (i <= num_of_pebbles && n+i <=NPITS) {
	if (n+i == NPITS && origin->fd != p->fd) { //if n+i reached another players end pit, skip it
	    n ++;
	} else { // add 1 pebble to each pit
	    p->pits[n+i] += 1;
	    i++;
	}
    }

    if (n+i > NPITS) { // if the pebbles go over the players own pits
	int remaining = num_of_pebbles - i;
	struct player *next_player = p->next;
        // loop until the next in game player (ie.a player not in the process of entering their name)
	while(next_player != NULL && next_player->entering_name == 1) {
	    next_player = next_player->next;
	}
	if (next_player != NULL) {
	    // call move_pebbles on the next player with the remaining pebbles
	    move_pebbles(origin, next_player, -1, remaining+1);
	} else {
	    next_player = playerlist;
	    while(next_player->entering_name == 1){
		next_player = next_player->next;
	    }
	    // call move_pebbles on the next player with the remaining pebbles
	    move_pebbles(origin, next_player, -1, remaining+1);
	}
    }
}

void generate_game_board(struct player *playerlist) {
    // function to generate a string representation of the current game board
    for (struct player *p = playerlist; p; p=p->next) { //  loop through every player in the game
	if (!p->entering_name) { //  make sure the player is done entering their name
	    char game_board[MAXMESSAGE] = "";
	    // add the player's name to the string
	    strcat(game_board, p->name);
	    strcat(game_board, ": ");
	    // loop through the player's pits and add each of them to the string
	    for (int i = 0; i < NPITS; i ++) {
	        char pits_state[MAXMESSAGE];
	        sprintf(pits_state, " [%d]%d", i, p->pits[i]);
	        strcat(game_board, pits_state);
	    }
	    // add the player's endpit to the string and broadcast it to all players
	    char endpit_state[MAXMESSAGE];
	    sprintf(endpit_state, "  [end pit]%d\n", p->pits[NPITS]);
	    strcat(game_board, endpit_state);
	    broadcast(game_board, -1);
        }
    }
}
int find_newline(const char *value) {
    // function to find newline characters in a given string
    // return 1 if a newline characer was found
    // return 0 otherwise
    for (int i = 0; i < strlen(value); i++) {
	if ((value[i] == '\r') || (value[i] == '\n')) {
	    return 1;
	}
	i ++;
    }
    return 0;
}

void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);  
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}


void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}



/* call this BEFORE linking the new player in to the list */
int compute_average_pebbles() { 
    // returns the average number of pebbles to give the new player
    struct player *p;
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }
    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
        nplayers++;
        for (i = 0; i < NPITS; i++) {
            npebbles += p->pits[i];
        }
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}


int game_is_over() { /* boolean */
    // returns 1 if one of the players have no more pebbles in their sockets
    int i;

    if (!playerlist) {
       return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = playerlist; p; p = p->next) {
        int is_all_empty = 1;
        for (i = 0; i < NPITS; i++) {
            if (p->pits[i]) {
                is_all_empty = 0;
            }
        }
        if (is_all_empty) {
            return 1;
        }
    }
    return 0;
}

void broadcast(char *s, int player_fd) {
    // function that writes a message to all active players except the player with player_fd
    for (struct player *p = playerlist; p; p = p->next) {
	if (p->entering_name == 0) { // make sure the player is in game
	    if (p->fd != player_fd) { // make sure not to send message to the specified player
	        write(p->fd, s, strlen(s));
	    }
	}
    }
}

int accept_new_player(int fd) {
    // function that accepts a new player and return their fd
    int player_fd = accept(fd, NULL, NULL);
    if (player_fd < 0) {
	perror("server: accept");
	close(fd);
	exit(1);
    }
    // create new player struct and initialize its values
    struct player *new_p = malloc(sizeof(struct player));
    new_p->fd = player_fd;
    new_p->next = playerlist;
    new_p->entering_name = 1;
    new_p->player_turn = 0;
    new_p->removed = 0;
    // put pebbles into the players pits
    int avg_pebbles = compute_average_pebbles();
    for (int i = 0; i < NPITS; i++) {
	new_p->pits[i] = avg_pebbles;
    }
    // put the player at the head of playerlist
    playerlist = new_p;
    return player_fd;
}

