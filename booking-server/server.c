#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define ERR_EXIT(a) \
    do {            \
        perror(a);  \
        exit(1);    \
    } while (0)

#define OBJ_NUM 3

#define FOOD_INDEX 0
#define CONCERT_INDEX 1
#define ELECS_INDEX 2
#define RECORD_PATH "./bookingRecord"
#define USER_ID_LOWER 902001
#define USER_ID_UPPER 902020
#define NUM_MAX 20  // id number max
#define ENTER_ID_REM \
    "Please enter your id (to check your booking state):\n"  // reminder
#define BOOKING_REM                                                      \
    "\nPlease input your booking command. (Food, Concert, Electronics. " \
    "Positive/negative value increases/decreases the booking amount.):\n"

// error part
#define INPUT_ERROR "[Error] Operation failed. Please try again.\n"

#define ERROR_NUM 3

#define BOOK_ERROR_LESS_ZERO \
    "[Error] Sorry, but you cannot book less than 0 items.\n"
#define BOOK_ERROR_GREATER_FIFTEEN \
    "[Error] Sorry, but you cannot book more than 15 items in total.\n"
#define EXIT_STRING "\n(Type Exit to leave...)\n"
#define LOCK_ERROR "Locked.\n"

static char *obj_names[OBJ_NUM] = {"Food", "Concert", "Electronics"};
typedef struct {
    char hostname[512];   // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;        // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;     // fd to talk with client
    char buf[512];   // data sent by/to client
    size_t buf_len;  // bytes used by buf
    int id;
    int user_id;
    int wait_for_write;  // used by handle_read to know if the header is read or
    // not.
} request;

server svr;                // server
request *requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list
struct flock file_lock;
int bookfd;

const char *accept_read_header = "ACCEPT_FROM_READ";
const char *accept_write_header = "ACCEPT_FROM_WRITE";
const unsigned char IAC_IP[3] = "\xff\xf4";

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request *reqP);
// initailize a request instance

static void free_request(request *reqP);
// free resources used by a request instance

typedef struct {
    int id;                     // 902001-902020
    int bookingState[OBJ_NUM];  // 1 means booked, 0 means not.
} record;
bool IsBeingWritten[NUM_MAX + 1] = {0};
int IsBeingRead[NUM_MAX + 1] = {0};

int handle_read(request *reqP) {
    /*  Return value:
     *      1: read successfully
     *      0: read EOF (client down)
     *     -1: read failed
     */
    int r;
    char buf[512];
    memset(buf, 0, sizeof(buf));
    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    char *p1 = strstr(buf, "\015\012");
    int newline_len = 2;
    if (p1 == NULL) {
        p1 = strstr(buf, "\012");
        if (p1 == NULL) {
            if (!strncmp(buf, IAC_IP, 2)) {
                // Client presses ctrl+C, regard as disconnection
                fprintf(stderr, "Client presses ctrl+C....\n");
                return 0;
            }
            ERR_EXIT("this really should not happen...");
        }
    }
    size_t len = p1 - buf + 1;
    memmove(reqP->buf, buf, len);
    reqP->buf[len - 1] = '\0';
    reqP->buf_len = len - 1;
    return 1;
}

bool not_number(const char num[]) {
    int len = strlen(num);
    int st = 0;
    if (num[0] == '-') st++;
    if ((len - 1) - st + 1 > 1 && num[st] == '0') return true;
    for (int i = st; i < len; i++) {
        if (!(num[i] >= '0' && num[i] <= '9')) return true;
    }
    return false;
}
bool rcdless0(const record *rcd) {
    return (rcd->bookingState[0] < 0 || rcd->bookingState[1] < 0 ||
            rcd->bookingState[2] < 0);
}
bool rcdgreater15(const record *rcd) {
    return (rcd->bookingState[0] + rcd->bookingState[1] + rcd->bookingState[2] >
            15);
}
bool invalid_uid(int user_id) {
    return (user_id < USER_ID_LOWER || user_id > USER_ID_UPPER);
}
int read_id_from_client(const request *requestP) {
    int user_id = -1;
    sscanf(requestP->buf, "%d", &user_id);
    return user_id;
}
void init_flock(int user_id, short type) {
    file_lock.l_type = type;
    file_lock.l_whence = SEEK_SET;
    file_lock.l_start = sizeof(record) * (user_id - USER_ID_LOWER);
    file_lock.l_len = sizeof(record);
}
int read_state(int user_id, record *rcdP) {
    lseek(bookfd, (user_id - USER_ID_LOWER) * sizeof(record), SEEK_SET);
    read(bookfd, rcdP, sizeof(record));
    return 0;
}
int write_state(record rcd) {
    lseek(bookfd, (rcd.id - USER_ID_LOWER) * sizeof(record), SEEK_SET);
    write(bookfd, &rcd, sizeof(record));
    return 0;
}
/* print record state to the buf */
void print_state2buf(char buf[], record rcd) {
    sprintf(buf,
            "Food: %d booked\nConcert: %d booked\nElectronics: %d booked\n",
            rcd.bookingState[FOOD_INDEX], rcd.bookingState[CONCERT_INDEX],
            rcd.bookingState[ELECS_INDEX]);
}
int read_from_client(request *reqP) {
    int ret =
        handle_read(reqP);  // parse data from client to requestP[conn_fd].buf
    fprintf(stderr, "ret = %d\n", ret);
    if (ret < 0) {
        fprintf(stderr, "bad request from %s\n", reqP->host);
    }
    return ret;
}

int main(int argc, char **argv) {
    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading
    char buf[512];
    int buf_len;
    bookfd = openat(AT_FDCWD, RECORD_PATH, O_RDWR);

    // Initialize server
    init_server((unsigned short)atoi(argv[1]));
    fd_set readfds;

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n",
            svr.hostname, svr.port, svr.listen_fd, maxfd);

    while (1) {
        // TODO: Add IO multiplexing
        FD_ZERO(&readfds);
        // add servr socket to set
        FD_SET(svr.listen_fd, &readfds);
        // add valid socket descriptor to read set
        for (int i = svr.listen_fd + 1; i < maxfd; i++) {
            if (requestP[i].conn_fd > 0) {
                FD_SET(requestP[i].conn_fd, &readfds);
            }
        }

        // Check activity of client  connection
        int activity = select(maxfd, &readfds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            fprintf(stderr, "select error");
        }
        // server fd change -> incoming new conection -> new cilent
        if (FD_ISSET(svr.listen_fd, &readfds)) {
            clilen = sizeof(cliaddr);
            conn_fd = accept(svr.listen_fd, (struct sockaddr *)&cliaddr,
                             (socklen_t *)&clilen);
            if (conn_fd < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;  // try again
                if (errno == ENFILE) {
                    (void)fprintf(
                        stderr,
                        "out of file descriptor table ... (maxconn %d)\n",
                        maxfd);
                    continue;
                }
                ERR_EXIT("accept");
            }
            requestP[conn_fd].conn_fd = conn_fd;
            strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
            fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd,
                    requestP[conn_fd].host);

            // Greeting the clinet
            write(requestP[conn_fd].conn_fd, ENTER_ID_REM,
                  strlen(ENTER_ID_REM));
        }
        // TODO: handle requests from clients
#ifdef READ_SERVER
        for (int i = svr.listen_fd + 1; i < maxfd; i++) {
            // not valid(no connection) or not activive, then continue
            if (requestP[i].conn_fd <= 0 ||
                !FD_ISSET(requestP[i].conn_fd, &readfds))
                continue;
            int state = read_from_client(&requestP[i]);
            if (state <= 0) {  // unexpected error or presses ctrl c
                if (requestP[i].id == 1) {  // "Exit" state
                    IsBeingRead[requestP[i].user_id - USER_ID_LOWER]--;
                    if (IsBeingRead[requestP[i].user_id - USER_ID_LOWER] == 0) {
                        // ensure no clinet is read the user id on the server
                        init_flock(requestP[i].user_id, F_RDLCK);
                        file_lock.l_type = F_UNLCK;
                        fcntl(bookfd, F_SETLK, &file_lock);
                    }
                }
                close(requestP[i].conn_fd);
                free_request(&requestP[i]);
                continue;
            }
            if (requestP[i].id == 0) {
                requestP[i].user_id = read_id_from_client(&requestP[i]);
                if (not_number(requestP[i].buf) ||
                    invalid_uid(requestP[i].user_id)) {
                    write(requestP[i].conn_fd, INPUT_ERROR,
                          strlen(INPUT_ERROR));
                    close(requestP[i].conn_fd);
                    free_request(&requestP[i]);
                    continue;
                }
                // after checking the id is valid, then lock it
                init_flock(requestP[i].user_id, F_RDLCK);
                int lock_ret = fcntl(bookfd, F_SETLK, &file_lock);
                if (lock_ret == -1) {
                    write(requestP[i].conn_fd, LOCK_ERROR, strlen(LOCK_ERROR));
                    close(requestP[i].conn_fd);
                    free_request(&requestP[i]);
                    continue;
                }
                // ensure no lock
                IsBeingRead[requestP[i].user_id - USER_ID_LOWER]++;

                record cur_state;
                read_state(requestP[i].user_id, &cur_state);
                print_state2buf(buf, cur_state);
                write(requestP[i].conn_fd, buf, strlen(buf));
                write(requestP[i].conn_fd, EXIT_STRING, strlen(EXIT_STRING));
                requestP[i].id++;
            } else {
                if (requestP[i].id != 1) {
                    fprintf(stderr, "WTF exceed expected number\n");
                }
                if (strcmp(requestP[i].buf, "Exit") == 0) {
                    // unlock
                    IsBeingRead[requestP[i].user_id - USER_ID_LOWER]--;
                    if (IsBeingRead[requestP[i].user_id - USER_ID_LOWER] == 0) {
                        // ensure no clinet is read the user id on the server
                        init_flock(requestP[i].user_id, F_RDLCK);
                        file_lock.l_type = F_UNLCK;
                        fcntl(bookfd, F_SETLK, &file_lock);
                    }
                    close(requestP[i].conn_fd);
                    free_request(&requestP[i]);
                }
            }
        }
#elif defined WRITE_SERVER
        for (int i = svr.listen_fd + 1; i < maxfd; i++) {
            if (requestP[i].conn_fd <= 0 ||
                !FD_ISSET(requestP[i].conn_fd, &readfds))
                continue;
            int state = read_from_client(&requestP[i]);
            if (state <= 0) {  // unexpected error or presses ctrl c
                if(requestP[i].id == 1) {
                    IsBeingWritten[requestP[i].user_id - USER_ID_LOWER] = false;
                    init_flock(requestP[i].user_id, F_WRLCK);
                    file_lock.l_type = F_UNLCK;
                    fcntl(bookfd, F_SETLK, &file_lock);
                }
                close(requestP[i].conn_fd);
                free_request(&requestP[i]);
                continue;
            }
            if (requestP[i].id == 0) {
                requestP[i].user_id = read_id_from_client(&requestP[i]);
                if (not_number(requestP[i].buf) ||
                    invalid_uid(requestP[i].user_id)) {
                    write(requestP[i].conn_fd, INPUT_ERROR,
                          strlen(INPUT_ERROR));
                    close(requestP[i].conn_fd);
                    free_request(&requestP[i]);
                    continue;
                }
                init_flock(requestP[i].user_id, F_WRLCK);
                int lock_ret = fcntl(bookfd, F_SETLK, &file_lock);
                fprintf(stderr, "lock return value is %d", lock_ret);
                if (lock_ret == -1 ||
                    IsBeingWritten[requestP[i].user_id - USER_ID_LOWER]) {
                    /* if (lock_ret != -1) {
                        file_lock.l_type = F_UNLCK;
                        fcntl(bookfd, F_SETLK, &file_lock);
                    } */
                    write(requestP[i].conn_fd, LOCK_ERROR, strlen(LOCK_ERROR));
                    close(requestP[i].conn_fd);
                    free_request(&requestP[i]);
                    continue;
                }
                IsBeingWritten[requestP[i].user_id - USER_ID_LOWER] = true;
                record cur_state;
                read_state(requestP[i].user_id, &cur_state);
                print_state2buf(buf, cur_state);
                write(requestP[i].conn_fd, buf, strlen(buf));
                requestP[i].id++;
                write(requestP[i].conn_fd, BOOKING_REM, strlen(BOOKING_REM));
            } else {
                if (requestP[i].id != 1) {
                    fprintf(stderr, "WTF exceed expected number\n");
                }
                char d0[4], d1[4], d2[4];  // delta1 2 3
                sscanf(requestP[i].buf, "%s %s %s", d0, d1, d2);
                // at least one of them not number
                if (not_number(d0) || not_number(d1) || not_number(d2)) {
                    init_flock(requestP[i].user_id, F_WRLCK);
                    file_lock.l_type = F_UNLCK;
                    fcntl(bookfd, F_SETLK, &file_lock);
                    IsBeingWritten[requestP[i].user_id - USER_ID_LOWER] = false;

                    write(requestP[i].conn_fd, INPUT_ERROR,
                          strlen(INPUT_ERROR));
                    close(requestP[i].conn_fd);
                    free_request(&requestP[i]);
                    continue;
                }
                record cur_state;
                read_state(requestP[i].user_id, &cur_state);
                // add
                cur_state.bookingState[0] += atoi(d0);
                cur_state.bookingState[1] += atoi(d1);
                cur_state.bookingState[2] += atoi(d2);
                if (rcdless0(&cur_state)) {
                    write(requestP[i].conn_fd, BOOK_ERROR_LESS_ZERO,
                          strlen(BOOK_ERROR_LESS_ZERO));
                } else if (rcdgreater15(&cur_state)) {
                    write(requestP[i].conn_fd, BOOK_ERROR_GREATER_FIFTEEN,
                          strlen(BOOK_ERROR_GREATER_FIFTEEN));
                } else {
                    write_state(cur_state);
                    sprintf(buf,
                            "Bookings for user %d are updated, the new booking "
                            "state is:\n",
                            requestP[i].user_id);
                    write(requestP[i].conn_fd, buf, strlen(buf));
                    // after updating the data , cur -> new
                    // read_state(requestP[i].user_id, &cur_state);
                    print_state2buf(buf, cur_state);
                    write(requestP[i].conn_fd, buf, strlen(buf));
                }
                IsBeingWritten[requestP[i].user_id - USER_ID_LOWER] = false;
                // unlock the file segment and close the connection
                init_flock(requestP[i].user_id, F_WRLCK);
                file_lock.l_type = F_UNLCK;
                fcntl(bookfd, F_SETLK, &file_lock);
                close(requestP[i].conn_fd);
                free_request(&requestP[i]);
            }
        }
#endif
    }
    close(bookfd);
    free(requestP);
    return 0;
}

// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void init_request(request *reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->id = 0;
    reqP->user_id = -1;
}

static void free_request(request *reqP) {
    /*if (reqP->filename != NULL) {
      free(reqP->filename);
      reqP->filename = NULL;
      }*/
    init_request(reqP);
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&tmp,
                   sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) <
        0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }

    // Get file descripter table size and initialize request table
    maxfd = getdtablesize();
    requestP = (request *)malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    return;
}
