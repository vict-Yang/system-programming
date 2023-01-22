#include <asm-generic/errno-base.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "status.h"
#define LEFT 0
#define RIGHT 1
#define READ 0
#define WRITE 1
#define BUF_MAX 64
pid_t childs_pid[2];
int child_player_id_table[14][2] = {
    {-1, -1}, {-1, -1}, {-1, 14}, {-1, -1}, {-1, -1}, {-1, -1}, {0, 1},
    {2, 3},   {4, 5},   {6, 7},   {-1, 10}, {-1, 13}, {8, 9},   {11, 12}};
char *child_battle_id_table[14] = {"BC", "DE", "F0", "GH", "IJ", "KL", "00",
                                   "00", "00", "00", "M0", "N0", "00", "00"};
char *parent_id_table = "0AABBCDDEEFFKL";
Attribute battle_attr_table[14] = {FIRE,  GRASS, WATER, WATER, FIRE,
                                   FIRE,  FIRE,  GRASS, WATER, GRASS,
                                   GRASS, GRASS, FIRE,  WATER};

int pipefd[2][2][2];
char battle_id;
pid_t parent_process_id, process_id;
char global_buf[BUF_MAX];
FILE *logfile_fd;
char child_battle_id[2];
int child_player_id[2];
char parent_id;
Status player_status[2];
Attribute battle_attr;
int passing_mode;
int winner_id, loser_id;
void init_battle(char *argv[]);
void err_sys(const char *x);
// dup src to dest and close the src file descriptor
void mydup2(int srcfd, int destfd);
// get the id attack first
int first_id();
int buf_ATK(int id);
void write_log(int child_id, int is_parent, char *type);
void myread(int fd, Status *msg_ptr, char *error);
void mywrite(int fd, Status *msg_ptr, char *error);
void myclose(int fd);
int main(int argc, char *argv[]) {
    // TODO
    init_battle(argv);
#ifdef DEBUG
    fprintf(stderr, "battle_id = %c ", battle_id);
    fprintf(stderr, "process_id = %d ", process_id);
    fprintf(stderr, "parent_process_id = %d\n", parent_process_id);
#endif
    ((childs_pid[LEFT] = fork()) && (childs_pid[RIGHT] = fork()));
    if (childs_pid[LEFT] == 0) {  // left children
        // close the pipe used by right child
        myclose(pipefd[RIGHT][WRITE][0]);
        myclose(pipefd[RIGHT][READ][0]);
        myclose(pipefd[RIGHT][WRITE][1]);
        myclose(pipefd[RIGHT][READ][1]);
        // close itself unused pipe
        myclose(pipefd[LEFT][WRITE][1]);
        myclose(pipefd[LEFT][READ][0]);
        // dup it to stdin/stdout
        mydup2(pipefd[LEFT][WRITE][0], STDIN_FILENO);
        mydup2(pipefd[LEFT][READ][1], STDOUT_FILENO);
        char arg1[BUF_MAX], arg2[BUF_MAX];
        sprintf(arg2, "%d", process_id);
        if (child_player_id[LEFT] >= 0) {
            sprintf(arg1, "%d", child_player_id[LEFT]);
#ifdef DEBUG
            fprintf(stderr, "execute player %s\n", arg1);
#endif
            execl("./player", "./player", arg1, arg2, NULL);
        } else {
            sprintf(arg1, "%c", child_battle_id[LEFT]);
            execl("./battle", "./battle", arg1, arg2, NULL);
        }
    } else if (childs_pid[RIGHT] == 0) {  // right children
        // close the pipe used by left child
        myclose(pipefd[LEFT][WRITE][0]);
        myclose(pipefd[LEFT][READ][0]);
        myclose(pipefd[LEFT][WRITE][1]);
        myclose(pipefd[LEFT][READ][1]);
        // close itself unused pipe
        myclose(pipefd[RIGHT][WRITE][1]);
        myclose(pipefd[RIGHT][READ][0]);
        // dupt it to stdin/stdout
        mydup2(pipefd[RIGHT][WRITE][0], STDIN_FILENO);
        mydup2(pipefd[RIGHT][READ][1], STDOUT_FILENO);
        char arg1[BUF_MAX], arg2[BUF_MAX];
        sprintf(arg2, "%d", process_id);
        if (child_player_id[RIGHT] >= 0) {
            sprintf(arg1, "%d", child_player_id[RIGHT]);
#ifdef DEBUG
            fprintf(stderr, "execute player %s\n", arg1);
#endif
            execl("./player", "./player", arg1, arg2, NULL);
        } else {
            sprintf(arg1, "%c", child_battle_id[RIGHT]);
            execl("./battle", "./battle", arg1, arg2, NULL);
        }
    } else {  // parent (current process)
        myclose(pipefd[LEFT][WRITE][0]);
        myclose(pipefd[RIGHT][WRITE][0]);
        myclose(pipefd[LEFT][READ][1]);
        myclose(pipefd[RIGHT][READ][1]);
        while (1) {
            if (!passing_mode) {  // playing mode
                // read from children
                myread(pipefd[LEFT][READ][0], &player_status[LEFT],
                       "read from left child error");
                write_log(LEFT, 0, "from");
                myread(pipefd[RIGHT][READ][0], &player_status[RIGHT],
                       "read from right child error");
                write_log(RIGHT, 0, "from");
                int first = first_id();  // first attack
                int second = 1 - first;  // second attack
                player_status[second].HP -= buf_ATK(first);
                if (player_status[second].HP > 0)
                    player_status[first].HP -= buf_ATK(second);
                // if the battle is ended
                if (player_status[second].HP <= 0 ||
                    player_status[first].HP <= 0) {
                    player_status[first].battle_ended_flag =
                        player_status[second].battle_ended_flag = 1;
                    // set winner id and loser id
                    winner_id = (player_status[first].HP > 0 ? first : second);
                    loser_id = 1 - winner_id;
#ifdef DEBUG
                    fprintf(stderr, "battle%c winner is %d\n",battle_id, player_status[winner_id].real_player_id);
#endif
                    passing_mode = 1;
                }
                // modify it's current_battle_id
                player_status[LEFT].current_battle_id =
                    player_status[RIGHT].current_battle_id = battle_id;
                // write it back
                write_log(LEFT, 0, "to");
                mywrite(pipefd[LEFT][WRITE][1], &player_status[LEFT],
                        "write to left child error");
                write_log(RIGHT, 0, "to");
                mywrite(pipefd[RIGHT][WRITE][1], &player_status[RIGHT],
                        "write to right child error");
                if (passing_mode) {  // wait the loser
                    waitpid(childs_pid[loser_id], NULL, 0);
                }
                if (passing_mode && battle_id == 'A') {  // last battle no passing mode
                    printf("Champion is P%d\n",
                           player_status[winner_id].real_player_id);
                    waitpid(childs_pid[winner_id], NULL, 0);
                    break;
                }
            } else {  // passing mode
                // read from child
                myread(pipefd[winner_id][READ][0], &player_status[winner_id],
                       "read from winner child error");
                write_log(winner_id, 0, "from");
                // and passing it to the parent
                write_log(winner_id, 1, "to");
                mywrite(STDOUT_FILENO, &player_status[winner_id],
                        "write to parent error");
                // read back from parent
                myread(STDIN_FILENO, &player_status[winner_id],
                       "read from parent error");
                write_log(winner_id, 1, "from");
                // send it back to child
                write_log(winner_id, 0, "to");
                mywrite(pipefd[winner_id][WRITE][1], &player_status[winner_id],
                        "write to winner child error");
                if (player_status[winner_id].battle_ended_flag) {
                    if (player_status[winner_id].HP <= 0 ||
                        player_status[winner_id].current_battle_id == 'A') {
                        waitpid(childs_pid[winner_id], NULL, 0);
                        break;
                    }
                }
            }
        }
    }
    _exit(0);
}
void init_battle(char *argv[]) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    sscanf(argv[1], "%c", &battle_id);
    sscanf(argv[2], "%d", &parent_process_id);
    process_id = getpid();
    sprintf(global_buf, "log_battle%c.txt", battle_id);
    logfile_fd = fopen(global_buf, "a+");
    setbuf(logfile_fd, NULL);
    child_player_id[LEFT] = child_player_id_table[battle_id - 'A'][LEFT];
    child_player_id[RIGHT] = child_player_id_table[battle_id - 'A'][RIGHT];
    child_battle_id[LEFT] = child_battle_id_table[battle_id - 'A'][LEFT];
    child_battle_id[RIGHT] = child_battle_id_table[battle_id - 'A'][RIGHT];
    parent_id = parent_id_table[battle_id - 'A'];
    battle_attr = battle_attr_table[battle_id - 'A'];
    passing_mode = 0;
    winner_id = -1;
    loser_id = -1;
    if (pipe(pipefd[LEFT][READ]) < 0 || pipe(pipefd[LEFT][WRITE]) < 0 ||
        pipe(pipefd[RIGHT][READ]) < 0 || pipe(pipefd[RIGHT][WRITE]) < 0) {
        err_sys("pipe error");
    }
}
void err_sys(const char *x) {
    perror(x);
    exit(1);
}
void mydup2(int srcfd, int destfd) {
    if (dup2(srcfd, destfd) != destfd) {
        err_sys("dup2 error to destfd");
    }
    myclose(srcfd);
}
int first_id() {
    if (player_status[LEFT].HP < player_status[RIGHT].HP) return LEFT;
    if (player_status[LEFT].HP > player_status[RIGHT].HP) return RIGHT;
    if (player_status[LEFT].real_player_id <
        player_status[RIGHT].real_player_id)
        return LEFT;
    return RIGHT;
}
int buf_ATK(int id) {
    return (player_status[id].attr == battle_attr ? 2 * player_status[id].ATK
                                                  : player_status[id].ATK);
}
void write_log(int id, int is_parent, char *type) {
    if (is_parent) {
        fprintf(logfile_fd, "%c,%d pipe %s %c,%d %d,%d,%c,%d\n", battle_id,
                process_id, type, parent_id, parent_process_id,
                player_status[id].real_player_id, player_status[id].HP,
                player_status[id].current_battle_id,
                player_status[id].battle_ended_flag);
    } else {
        if (child_player_id[id] >= 0) {  // child is player
            fprintf(logfile_fd, "%c,%d pipe %s %d,%d %d,%d,%c,%d\n", battle_id,
                    process_id, type, child_player_id[id], childs_pid[id],
                    player_status[id].real_player_id, player_status[id].HP,
                    player_status[id].current_battle_id,
                    player_status[id].battle_ended_flag);
        } else {  // child is battle
            fprintf(logfile_fd, "%c,%d pipe %s %c,%d %d,%d,%c,%d\n", battle_id,
                    process_id, type, child_battle_id[id], childs_pid[id],
                    player_status[id].real_player_id, player_status[id].HP,
                    player_status[id].current_battle_id,
                    player_status[id].battle_ended_flag);
        }
    }
}
void myread(int fd, Status *msg_ptr, char *error) {
    int ret = 0;
    if ((ret = read(fd, msg_ptr, sizeof(Status))) != sizeof(Status)) {
#ifdef DEBUG
        fprintf(stderr, "ret = %d, battle%c %s\n",ret,  battle_id, error);
#endif
    }
    if(errno == EPIPE) {
#ifdef DEBUG
        fprintf(stderr, "pipe has close\n");
#endif
    }
}
void mywrite(int fd, Status *msg_ptr, char *error) {
    if (write(fd, msg_ptr, sizeof(Status)) != sizeof(Status)) {
#ifdef DEBUG
        fprintf(stderr, "battle%c %s\n", battle_id, error);
#endif
    }
}
void myclose(int fd) {
    if(close(fd)) {
#ifdef  DEBUG
        fprintf(stderr, "battle%c close error\n", battle_id);
#endif
    }
}
