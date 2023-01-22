#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#include "header.h"

movie_profile *movies[MAX_MOVIES];
unsigned int num_of_movies = 0;
unsigned int num_of_reqs = 0;

// Global request queue and pointer to front of queue
// TODO: critical section to protect the global resources
request *reqs[MAX_REQ];
filtered *filtered_movies;
int front = -1;

/* Note that the maximum number of processes per workstation user is 512. *
 * We recommend that using about <256 threads is enough in this homework. */
pthread_t tid[MAX_CPU][MAX_THREAD];  // tids for multithread

#ifdef PROCESS
pid_t pid[MAX_CPU][MAX_THREAD];  // pids for multiprocess
#endif

// mutex
pthread_mutex_t lock;

void initialize(FILE *fp);
request *read_request();
int pop();

int pop() {
    front += 1;
    return front;
}
double dot(double *a, double *b) {
    double ret = 0;
    for (int i = 0; i < NUM_OF_GENRE; i++) ret += a[i] * b[i];
    return ret;
}
// the function will fill the array pts and filtered_movie
void filter_movie(int reqId) {
    int cnt = 0;
    for (int i = 0; i < num_of_movies; i++) {
        if (reqs[reqId]->keywords[0] == '*' ||
            strstr(movies[i]->title, reqs[reqId]->keywords)) {
#ifdef THREAD
            // filtered_movies[reqId].title[cnt] = (char *)malloc(MAX_LEN);
            filtered_movies[reqId].title[cnt] = movies[i]->title;
#endif
#ifdef PROCESS
            filtered_movies[reqId].title[cnt] =
                (char *)mmap(NULL, MAX_LEN, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
            memcpy(filtered_movies[reqId].title[cnt], movies[i]->title,
                   strlen(movies[i]->title) + 1);
#endif
            filtered_movies[reqId].pts[cnt] =
                dot(movies[i]->profile, reqs[reqId]->profile);
            cnt++;
        }
    }
    filtered_movies[reqId].len = cnt;
}
int greater(double pts1, double pts2, char *name1, char *name2) {
    if (pts1 > pts2) return 1;
    if (pts1 < pts2) return 0;
    // equal
    if (strcmp(name1, name2) <= 0) return 1;
    return 0;
}
void merge(merge_sort_args *ptr) {
    int l = ptr->left, r = ptr->right, reqId = ptr->reqId;
    int mid = (l + r) >> 1;
    double *pts = (double *)malloc(sizeof(double) * (r - l + 1));
    char **title = (char **)malloc(sizeof(char *) * (r - l + 1));
    int lp = l, rp = mid + 1, idx = 0;  // left pointer, right pointer
    while (lp <= mid && rp <= r) {
        if (greater(filtered_movies[reqId].pts[lp],
                    filtered_movies[reqId].pts[rp],
                    filtered_movies[reqId].title[lp],
                    filtered_movies[reqId].title[rp])) {
            // if (filtered_movies[reqId].pts[lp] >
            // filtered_movies[reqId].pts[rp] ||
            //     (filtered_movies->pts[lp] == filtered_movies[reqId].pts[rp]
            //     &&
            //      strcmp(filtered_movies[reqId].title[lp],
            //             filtered_movies[reqId].title[rp]) <= 0)) {
            title[idx] = filtered_movies[reqId].title[lp];
            pts[idx] = filtered_movies[reqId].pts[lp];
            lp++;
        } else {
            title[idx] = filtered_movies[reqId].title[rp];
            pts[idx] = filtered_movies[reqId].pts[rp];
            rp++;
        }
        idx++;
    }
    while (lp <= mid) {
        title[idx] = filtered_movies[reqId].title[lp];
        pts[idx] = filtered_movies[reqId].pts[lp];
        lp++;
        idx++;
    }
    while (rp <= r) {
        title[idx] = filtered_movies[reqId].title[rp];
        pts[idx] = filtered_movies[reqId].pts[rp];
        rp++;
        idx++;
    }
    memcpy(filtered_movies[reqId].title + l, title,
           (r - l + 1) * sizeof(char *));
    memcpy(filtered_movies[reqId].pts + l, pts, (r - l + 1) * sizeof(double));
    free(title);
    free(pts);
}
void *mergeSort(void *args) {
    merge_sort_args data = *((merge_sort_args *)args);
    int l = data.left, r = data.right, reqId = data.reqId, depth = data.depth,
        id = data.id;
    // printf("(%d, %d, %d)\n", l, r, depth);
    int lid = (id << 1), rid = ((id << 1) | 1);
    if (l <= r) {
        int m = ((l + r) >> 1);
        if (depth == MAX_DEPTH || (r - l + 1) <= MIN_MERGE_SORT_LEN) {
            // printf("into normal sort\n");
#ifdef THREAD
            // sort(filtered_movies[reqId].title + l,
            //      filtered_movies[reqId].pts + l, (m - l + 1));
            // if (l != r)
            //     sort(filtered_movies[reqId].title + m + 1,
            //          filtered_movies[reqId].pts + m + 1, (r - m));
            // merge((merge_sort_args *)args);
            sort(filtered_movies[reqId].title + l,
                 filtered_movies[reqId].pts + l, (r - l + 1));
#endif
#ifdef PROCESS
            char **title_temp = (char **)malloc(sizeof(char *) * (r - l + 1));
            double *pts_temp = (double *)malloc(sizeof(double) * (r - l + 1));
            for (int i = 0; i <= r - l; i++) {
                title_temp[i] = (char *)malloc(MAX_LEN);
                memcpy(title_temp[i], filtered_movies[reqId].title[i + l],
                       strlen(filtered_movies[reqId].title[i + l]) + 1);
                pts_temp[i] = filtered_movies[reqId].pts[i + l];
                // strcpy(title_temp[i], filtered_movies[reqId].title[i + l]);
            }
            sort(title_temp, pts_temp, r - l + 1);
            for (int i = 0; i < (r - l) + 1; i++) {
                memcpy(filtered_movies[reqId].title[i + l], title_temp[i],
                       strlen(title_temp[i]) + 1);
                filtered_movies[reqId].pts[i + l] = pts_temp[i];
                free(title_temp[i]);
            }
            free(title_temp);
            free(pts_temp);
#endif

        } else {
            merge_sort_args larg = {.reqId = reqId,
                                    .left = l,
                                    .right = m,
                                    .depth = depth + 1,
                                    .id = lid},
                            rarg = {.reqId = reqId,
                                    .left = m + 1,
                                    .right = r,
                                    .depth = depth + 1,
                                    .id = rid};
            // printf("create at %d %d\n", lid, rid);
#ifdef THREAD
            pthread_create(&tid[reqId][lid], NULL, mergeSort, &larg);
            pthread_create(&tid[reqId][rid], NULL, mergeSort, &rarg);
            pthread_join(tid[reqId][lid], NULL);
            pthread_join(tid[reqId][rid], NULL);
#endif
#ifdef PROCESS
            (pid[reqId][lid] = fork()) && (pid[reqId][rid] = fork());
            if (pid[reqId][lid] == 0) {
                mergeSort(&larg);
            } else if (pid[reqId][rid] == 0) {
                mergeSort(&rarg);
            } else {
                waitpid(pid[reqId][lid], NULL, 0);
                waitpid(pid[reqId][rid], NULL, 0);
            }
#endif
            merge((merge_sort_args *)args);
        }
    }
#ifdef THREAD
    pthread_exit(NULL);
#endif
#ifdef PROCESS
    exit(0);
#endif
}
void *handle_request(void *arg) {
    char buf[64];
    int reqId = 0;
    if (arg != NULL) reqId = *((int *)arg);
    merge_sort_args args = {.reqId = reqId,
                            .depth = 0,
                            .left = 0,
                            .right = filtered_movies[reqId].len - 1,
                            .id = 1};
#ifdef THREAD
    while (1) {
        pthread_mutex_lock(&lock);
        pop();
        if (front >= num_of_reqs) {
            pthread_mutex_unlock(&lock);
            break;
        }
        reqId = front;
        filter_movie(reqId);
        merge_sort_args targs = {.reqId = reqId,
                                 .depth = 0,
                                 .left = 0,
                                 .right = filtered_movies[front].len - 1,
                                 .id = 1};
        // printf("hello %d\n",front);
        pthread_create(&tid[reqId][1], NULL, mergeSort, &targs);
        pthread_mutex_unlock(&lock);
        pthread_join(tid[reqId][1], NULL);
        sprintf(buf, "%dt.out", reqs[reqId]->id);
        FILE *fptr = fopen(buf, "w+");
        for (int i = 0; i < filtered_movies[reqId].len; i++) {
            // fprintf(fptr, "%s %lf\n", filtered_movies[reqId].title[i],
            //         filtered_movies[reqId].pts[i]);
            fprintf(fptr, "%s\n", filtered_movies[reqId].title[i]);
        }
    }
    pthread_exit(NULL);
#endif
#ifdef PROCESS
    pid[reqId][1] = fork();
    if (pid[reqId][1] == 0) {
        mergeSort(&args);
    } else {
        waitpid(pid[reqId][1], NULL, 0);
    }
    sprintf(buf, "%dp.out", reqs[reqId]->id);
    FILE *fptr = fopen(buf, "w+");
    for (int i = 0; i < filtered_movies[reqId].len; i++) {
        fprintf(fptr, "%s\n", filtered_movies[reqId].title[i]);
    }
    exit(0);
#endif
}

int main(int argc, char *argv[]) {
    if (argc != 1) {
#ifdef PROCESS
        fprintf(stderr, "usage: ./pserver\n");
#elif defined THREAD
        fprintf(stderr, "usage: ./tserver\n");
#endif
        exit(-1);
    }

    FILE *fp;

    if ((fp = fopen("./data/movies.txt", "r")) == NULL) {
        ERR_EXIT("fopen");
    }
    pthread_mutex_init(&lock, NULL);
    initialize(fp);
    assert(fp != NULL);
    fclose(fp);
    int reqIds[MAX_REQ] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                           11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                           22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
#ifdef THREAD
    filtered_movies = (filtered *)malloc(sizeof(filtered) * num_of_reqs);
#endif
#ifdef PROCESS
    filtered_movies = (filtered *)mmap(NULL, sizeof(filtered) * num_of_reqs,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
#endif
#ifdef THREAD
    for (int i = 0; i < MAX_PARALLEL_REQ; i++) {
        // printf("i = %d\n", i);
        pthread_create(&tid[i][0], NULL, handle_request, NULL);
    }
    for (int i = 0; i < MAX_PARALLEL_REQ; i++) {
        pthread_join(tid[i][0], NULL);
    }
#endif
#ifdef PROCESS
    for (int i = 0; i < num_of_reqs; i++) {
        filter_movie(i);
        pid[i][0] = fork();
        if (pid[i][0] == 0) {
            handle_request(&reqIds[i]);
        }
    }
    for (int i = 0; i < num_of_reqs; i++) {
        waitpid(pid[i][0], NULL, 0);
    }
#endif
    return 0;
}

/**=======================================
 * You don't need to modify following code *
 * But feel free if needed.                *
 =========================================**/

request *read_request() {
    int id;
    char buf1[MAX_LEN], buf2[MAX_LEN];
    char delim[2] = ",";

    char *keywords;
    char *token, *ref_pts;
    char *ptr;
    double ret, sum;

    scanf("%u %254s %254s", &id, buf1, buf2);
    keywords = malloc(sizeof(char) * strlen(buf1) + 1);
    if (keywords == NULL) {
        ERR_EXIT("malloc");
    }

    memcpy(keywords, buf1, strlen(buf1));
    keywords[strlen(buf1)] = '\0';

    double *profile = malloc(sizeof(double) * NUM_OF_GENRE);
    if (profile == NULL) {
        ERR_EXIT("malloc");
    }
    sum = 0;
    ref_pts = strtok(buf2, delim);
    for (int i = 0; i < NUM_OF_GENRE; i++) {
        ret = strtod(ref_pts, &ptr);
        profile[i] = ret;
        sum += ret * ret;
        ref_pts = strtok(NULL, delim);
    }

    // normalize
    sum = sqrt(sum);
    for (int i = 0; i < NUM_OF_GENRE; i++) {
        if (sum == 0)
            profile[i] = 0;
        else
            profile[i] /= sum;
    }

    request *r = malloc(sizeof(request));
    if (r == NULL) {
        ERR_EXIT("malloc");
    }

    r->id = id;
    r->keywords = keywords;
    r->profile = profile;

    return r;
}

/*=================initialize the dataset=================*/
void initialize(FILE *fp) {
    char chunk[MAX_LEN] = {0};
    char *token, *ptr;
    double ret, sum;
    int cnt = 0;

    assert(fp != NULL);

    // first row
    if (fgets(chunk, sizeof(chunk), fp) == NULL) {
        ERR_EXIT("fgets");
    }

    memset(movies, 0, sizeof(movie_profile *) * MAX_MOVIES);

    while (fgets(chunk, sizeof(chunk), fp) != NULL) {
        assert(cnt < MAX_MOVIES);
        chunk[MAX_LEN - 1] = '\0';

        const char delim1[2] = " ";
        const char delim2[2] = "{";
        const char delim3[2] = ",";
        unsigned int movieId;
        movieId = atoi(strtok(chunk, delim1));

        // title
        token = strtok(NULL, delim2);
        char *title = malloc(sizeof(char) * strlen(token) + 1);
        if (title == NULL) {
            ERR_EXIT("malloc");
        }

        // title.strip()
        memcpy(title, token, strlen(token) - 1);
        title[strlen(token) - 1] = '\0';

        // genres
        double *profile = malloc(sizeof(double) * NUM_OF_GENRE);
        if (profile == NULL) {
            ERR_EXIT("malloc");
        }

        sum = 0;
        token = strtok(NULL, delim3);
        for (int i = 0; i < NUM_OF_GENRE; i++) {
            ret = strtod(token, &ptr);
            profile[i] = ret;
            sum += ret * ret;
            token = strtok(NULL, delim3);
        }

        // normalize
        sum = sqrt(sum);
        for (int i = 0; i < NUM_OF_GENRE; i++) {
            if (sum == 0)
                profile[i] = 0;
            else
                profile[i] /= sum;
        }

        movie_profile *m = malloc(sizeof(movie_profile));
        if (m == NULL) {
            ERR_EXIT("malloc");
        }

        m->movieId = movieId;
        m->title = title;
        m->profile = profile;

        movies[cnt++] = m;
    }
    num_of_movies = cnt;

    // request
    scanf("%d", &num_of_reqs);
    assert(num_of_reqs <= MAX_REQ);
    for (int i = 0; i < num_of_reqs; i++) {
        reqs[i] = read_request();
    }
}
/*========================================================*/
