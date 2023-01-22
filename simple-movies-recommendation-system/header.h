#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// error handling
#define ERR_EXIT(a) \
    do {            \
        perror(a);  \
        exit(1);    \
    } while (0)

// max number of movies
#define MAX_MOVIES 70000

// max length of each movies
#define MAX_LEN 0xff

// number of requests
#define MAX_REQ 64

// number of genre
#define NUM_OF_GENRE 19

// number of l1,l2 threads
#define MAX_CPU 64
#define MAX_THREAD 0x200 
#define MAX_DEPTH 4
#define MAX_PARALLEL_REQ 8
#define MIN_MERGE_SORT_LEN 1000
void sort(char** movies, double* pts, int size);

typedef struct request {
    int id;
    char* keywords;
    double* profile;
} request;

typedef struct movie_profile {
    int movieId;
    char* title;
    double* profile;
} movie_profile;
typedef struct merge_sort_args {
    int left, right, depth, reqId, id;
} merge_sort_args;
typedef struct filtered {
    double pts[MAX_MOVIES];
    char* title[MAX_MOVIES];
    int len;
} filtered;
