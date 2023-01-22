#include "status.h"
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define BUF_MAX 512
int player_id, parent_process_id;
pid_t process_id;
// file descriptor to the fifo file
int fifo_fd;
int lose = 0;
// log file pointer
FILE *logfile_fd;
// player_status.txt  pointer
FILE *player_status_fd;
Status player_info;
int init_hp;
char global_buf[BUF_MAX]; // used as temp buf
char parent[] = "GGHHIIJJMMKNNLC";
char agent_battle_id[] = "GIDHJEB";
bool file_exists(char *filename);
void err_sys(const char *x);
void init_player(char *argv[]);
// read from particular line, 1 based and store in global_buf
void read_line(FILE *fd, int line_number);
// after battle ended, recover  HP
int recover_hp(int lose, int endhp);
int get_agent_id(char lose_battle_id);
int main(int argc, char *argv[]) {
    // TODO
    init_player(argv);

#ifdef DEBUG    
    fprintf(stderr,
            "PLAYER_ID = %d, REAL_PLAYER_ID = %d, HP = %d, ATK = %d, ATTR = %d, "
            "BATTLE = %c, END_FLAG = %d\n",
            player_id, player_info.real_player_id, player_info.HP,
            player_info.ATK, player_info.attr, player_info.current_battle_id,
            player_info.battle_ended_flag);
#endif  

    while (1) {
        // pip to the parent
        fprintf(logfile_fd, "%d,%d pipe to %c,%d %d,%d,%c,%d\n", player_id,
                process_id, parent[player_id], parent_process_id,
                player_info.real_player_id, player_info.HP,
                player_info.current_battle_id, player_info.battle_ended_flag);
        if (write(STDOUT_FILENO, &player_info, sizeof(Status)) != sizeof(Status))
            perror("write to parent failed");

        // receive result from parent
        Status next_player_info;
        if (read(STDIN_FILENO, &next_player_info, sizeof(Status)) != sizeof(Status))
            perror("read from parent failed");
        fprintf(logfile_fd, "%d,%d pipe from %c,%d %d,%d,%c,%d\n", player_id,
                process_id, parent[player_id], parent_process_id,
                next_player_info.real_player_id, next_player_info.HP,
                next_player_info.current_battle_id,
                next_player_info.battle_ended_flag);
        // copy the next state to the current state
        player_info = next_player_info;
        if (player_info.battle_ended_flag) { // battle is ended
            lose = (player_info.HP <= 0);
            // recover the HP and reset the battle_ended_flag to false
            player_info.HP = recover_hp(lose, player_info.HP);
            player_info.battle_ended_flag = 0;
            if (lose || player_info.current_battle_id == 'A')// lose or it is the final winner
                break;
        }
    }
    if (lose && player_id <= 7 &&
            get_agent_id(player_info.current_battle_id) >
            0) { // lose and he is not the agent player
                 // and it has corresponding agent player
        int agent_id = get_agent_id(player_info.current_battle_id);
        fprintf(logfile_fd, "%d,%d fifo to %d %d,%d\n", player_id, process_id,
                agent_id, player_info.real_player_id, player_info.HP);
        sprintf(global_buf, "player%d.fifo", agent_id);
#ifdef DEBUG
        fprintf(stderr, "try to write to fifo file %s\n", global_buf);
#endif
        // pass data to agent player

        while (!file_exists(global_buf)) {
#ifdef DEBUG
            fprintf(stderr, "wait fifo to be created\n");
#endif
        }
        fifo_fd = openat(AT_FDCWD, global_buf, O_WRONLY);
        if (write(fifo_fd, &player_info, sizeof(Status)) != sizeof(Status)) {
            perror("write to agent player failed\n");
        }
    }
    return 0;
}
void init_player(char *argv[]) {
    // set printf to unbuffered
    setbuf(stdin, NULL);
    sscanf(argv[1], "%d", &player_id);
    sscanf(argv[2], "%d", &parent_process_id);
    process_id = getpid();
    if (player_id > 7) { // is agent player
#ifdef DEBUG
        fprintf(stderr, "I am agent player!\n");
#endif
        sprintf(global_buf, "player%d.fifo", player_id);
        if (mkfifoat(AT_FDCWD, global_buf, 0666) < 0) {
            err_sys("mkfifoat error\n");
        }
        fifo_fd = openat(AT_FDCWD, global_buf, O_RDONLY);
        if (read(fifo_fd, &player_info, sizeof(Status)) != sizeof(Status)) {
            perror("read error, cannot read from sibiling\n");
        }
        Status temp = player_info;
        char attribure[12] = {0};
        // read the init_hp
        player_status_fd = fopen("./player_status.txt", "r");
        read_line(player_status_fd, temp.real_player_id + 1);// remember to use real player id
        sscanf(global_buf, "%d %d %s %c %d", &player_info.HP, &player_info.ATK,
                attribure, &player_info.current_battle_id,
                &player_info.battle_ended_flag);

        init_hp = player_info.HP;
        player_info = temp;
        // open the log file
        sprintf(global_buf, "log_player%d.txt", player_info.real_player_id);
        logfile_fd = fopen(global_buf, "a+");
        fprintf(logfile_fd, "%d,%d fifo from %d %d,%d\n", player_id, process_id,
                player_info.real_player_id, player_info.real_player_id,
                player_info.HP);
        return;
    }
    // open the player_status.txt
    player_status_fd = fopen("./player_status.txt", "r");
    read_line(player_status_fd, player_id + 1);
    char attribure[12] = {0};
    // and set the player status
    sscanf(global_buf, "%d %d %s %c %d", &player_info.HP, &player_info.ATK,
            attribure, &player_info.current_battle_id,
            &player_info.battle_ended_flag);
    player_info.real_player_id = player_id;
    init_hp = player_info.HP;
    if (strcmp("FIRE", attribure) == 0)
        player_info.attr = FIRE;
    else if (strcmp("GRASS", attribure) == 0)
        player_info.attr = GRASS;
    else
        player_info.attr = WATER;
    // create the log file
    sprintf(global_buf, "log_player%d.txt", player_info.real_player_id);
    logfile_fd = fopen(global_buf, "a+");
}
void read_line(FILE *fd, int line_number) {
    // make sure it is at the file begining
    fseek(fd, 0, SEEK_SET);
    int end = 0;
    for (int i = 0; i < line_number; i++) {
        if (fgets(global_buf, sizeof(global_buf), fd) == 0) {
            end = 1;
            break;
        }
    }
    if (end) {
#ifdef DEBUG
        fprintf(stderr, "out of file !!!\n");
#endif
    } else {
#ifdef DEBUG
        fprintf(stderr, "%s", global_buf);
#endif
    }
}
void err_sys(const char *x) {
    perror(x);
    exit(1);
}
int recover_hp(int lose, int endhp) {
    if (lose)
        return init_hp;
    return endhp + (init_hp - endhp) / 2;
}
int get_agent_id(char lose_battle_id) {
    for (int i = 8; i <= 14; i++) {
        if (agent_battle_id[i - 8] == lose_battle_id)
            return i;
    }
    return -1;
}
bool file_exists(char *filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}
