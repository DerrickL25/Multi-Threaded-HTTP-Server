#include "asgn2_helper_funcs.h"
#include "queue.h"
#include "rwlock.h"
#include "List.h" //linked list from CSE101
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "regex.h"
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_BUF_LEN 2048

typedef struct {
    char RequestBuffer[2049]; //entire request line (including \r\n\r\n) + 1 for NULL
    char Method[9]; //max len of Method + 1 for NULL
    char URI[65]; //max len of URI + 1 for NULL
    char Version[9]; //max len of Version + 1 for NULL
    int content_length_exists;
    char Content_Length_Field[100];
    int request_id_exists;
    char Request_Id_Field[100];

} Request;

int process_connection(int socket);
void produce_response(int socket, int status_code, int valid_get, size_t content_length);
void parse_req_line(Request *request, int w1_so, int w1_eo, int w2_so, int w2_eo, int w3_so,
    int w3_eo, int w4_so, int w4_eo, int req_id_so, int req_id_eo);
int process_get_req(Request *request, int socket);
int process_put_req(Request *request, int socket, int msg_body_start, int read_bytes);
void graceful_shutdown(
    int socket, regex_t req_validity_re, regex_t put_validity_re, regex_t request_id_re);

//thread-safe queue and reader/writer lock for worker threads
queue_t *queue;
rwlock_t *rwlock;

//semaphores to keep track of workers available and workers busy
//sem_t workers_avail;
//sem_t workers_sem;

//lock for FileLockLL (shared resource)
pthread_mutex_t FileLockLL_lock;

//linked list to hold URI-rwlock pairs
List FileLockLL;

//linked list helper function to find URI - returns NULL if not found
void *find_URI(List L, char *URI) {
    if (length(L) == 0) { //if table is empty
        return NULL;
    }
    moveFront(L);
    if (length(L) == 1) { //if only 1 item in LL
        if (strcmp(get_URI(L), URI) == 0) { //found URI
            return get(L); //return rw_lock corresponding to URI
        } else {
            return NULL;
        }
    }
    while (indexl(L) < length(L) - 1) {
        if (strcmp(get_URI(L), URI) == 0) { //found URI
            return get(L); //return rw_lock corresponding to URI
        }
        moveNext(L);
    }
    return NULL;
}

void *worker_thread() {
    while (1) {
        //pop the next request off the queue (socket number)
        uintptr_t socket_ptr;
        int socket;
        queue_pop(queue, (void **) &socket_ptr); //should never pop from empty queue
        socket = (int) socket_ptr;

        //new regex objects for each connection
        regex_t req_validity_re;
        regex_t put_validity_re;
        regex_t request_id_re;
        size_t nmatch = 6;
        size_t nmatch2 = 1;
        size_t nmatch3 = 1;
        regmatch_t pmatch[nmatch]; //overall request fields
        regmatch_t pmatch2[nmatch2]; //content-length for put
        regmatch_t pmatch3[nmatch3]; //optional request_id

        //initialize regex patterns
        regcomp(&req_validity_re,
            "([a-zA-Z]{1,8}) (/[a-zA-Z0-9.-]{1,63}) (HTTP/[0-9].[0-9])\r\n([a-zA-Z0-9.-]{1,128}: [ "
            "-~]{0,128}\r\n)*\r\n",
            REG_EXTENDED);
        regcomp(&put_validity_re, "Content-Length: [0-9]+\r\n", REG_EXTENDED);
        regcomp(&request_id_re, "Request-Id: [0-9]+\r\n", REG_EXTENDED);

        //reads up to \r\n\r\n; this contans all of GET requests; leaves put requests for later
        Request request;
        request.request_id_exists = 0;
        request.content_length_exists = 0;

        memset(request.RequestBuffer, '\0', sizeof(request.RequestBuffer));
        ssize_t bytes_read = read_until(socket, request.RequestBuffer, MAX_BUF_LEN,
            "\r\n\r\n"); //will ONLY read n bytes if req doesnt contain \r\n\r\n
        //request.RequestBuffer[bytes_read] = '\0';

        //fprintf(stdout, "\n|BUFFER1|\n %s \n|ENDBUFFER1|\n", request.RequestBuffer);
        //regex matching
        int a = regexec(&req_validity_re, request.RequestBuffer, nmatch, pmatch,
            0); //validates entire request - GET and PUT are valid if a = 0
        if (a != 0) { //bad request and close connection
            produce_response(socket, 400, 0, 0);
            //sem_post(&workers_avail); //workers_avail.up()
            fprintf(stdout, "\nBUFFER\n %s \nENDBUFFER\n", request.RequestBuffer);
            //fprintf(stderr, "temp3\n");
            graceful_shutdown(socket, req_validity_re, put_validity_re, request_id_re);
            continue;
        }

        int b = regexec(&put_validity_re, request.RequestBuffer, nmatch2, pmatch2,
            0); //check if content length header exists
        if (b == 0) { //content_length field exists
            request.content_length_exists = 1;
        }
        int c = regexec(&request_id_re, request.RequestBuffer, nmatch3, pmatch3,
            0); //checks whether request_id field exists
        if (c == 0) { //request_id field exists
            request.request_id_exists = 1;
        }

        if (request.content_length_exists
            && request.request_id_exists) { //if content-length and request-id both exist
            parse_req_line(&request, pmatch[1].rm_so, pmatch[1].rm_eo, pmatch[2].rm_so,
                pmatch[2].rm_eo, pmatch[3].rm_so, pmatch[3].rm_eo, pmatch2[0].rm_so + 16,
                pmatch2[0].rm_eo - 2, pmatch3[0].rm_so + 12, pmatch3[0].rm_eo - 2);
        } else if (request.content_length_exists) { //if only content-length exists
            parse_req_line(&request, pmatch[1].rm_so, pmatch[1].rm_eo, pmatch[2].rm_so,
                pmatch[2].rm_eo, pmatch[3].rm_so, pmatch[3].rm_eo, pmatch2[0].rm_so + 16,
                pmatch2[0].rm_eo - 2, -1, -1);
        } else { //if only request-id exists
            parse_req_line(&request, pmatch[1].rm_so, pmatch[1].rm_eo, pmatch[2].rm_so,
                pmatch[2].rm_eo, pmatch[3].rm_so, pmatch[3].rm_eo, -1, -1, pmatch3[0].rm_so + 12,
                pmatch3[0].rm_eo - 2);
        }
        //fprintf(stdout, "REQUEST-ID: %s\n", request.Request_Id_Field);
        if (strcmp(request.Version, "HTTP/1.1") != 0) { //checking if version is 1.1
            produce_response(socket, 505, 0, 0);
            graceful_shutdown(socket, req_validity_re, put_validity_re, request_id_re);
            //sem_post(&workers_avail); //workers_avail.up()
            //fprintf(stdout, "worker done: %d\n", socket);
            continue;
        }

        pthread_mutex_lock(&FileLockLL_lock);
        void *value
            = find_URI(FileLockLL, request.URI); //try to find URI in FileLockLL - NULL if not found
        pthread_mutex_unlock(&FileLockLL_lock);

        if (strcmp(request.Method, "GET") == 0) { //checking if Method is exactly GET
            if (value != NULL) { //if rw_lock exists for this URI already
                //fprintf(stdout, "waiting in get 1\n");
                reader_lock((rwlock_t *) value);

                int res = process_get_req(&request, socket);
                if (res
                    != 0) { //only produce response if it was not successful (otherwise response has been sent already)
                    produce_response(socket, res, 0, 0);
                }

                reader_unlock((rwlock_t *) value);
                graceful_shutdown(socket, req_validity_re, put_validity_re, request_id_re);
            } else { //rw_lock does not exist for this URI
                //fprintf(stdout, "waiting in get 2\n");
                rwlock_t *new_rwlock = rwlock_new(N_WAY, 1); //make new rwlock
                pthread_mutex_lock(&FileLockLL_lock);
                append(FileLockLL, request.URI, new_rwlock);
                pthread_mutex_unlock(&FileLockLL_lock);

                reader_lock(new_rwlock);

                int res = process_get_req(&request, socket);
                if (res
                    != 0) { //only produce response if it was not successful (otherwise response has been sent already)
                    produce_response(socket, res, 0, 0);
                }

                reader_unlock(new_rwlock);
                graceful_shutdown(socket, req_validity_re, put_validity_re, request_id_re);
            }
            continue;
        }
        if (strcmp(request.Method, "PUT") == 0) { //checking if Method is exactly PUT
            if (value != NULL) { //if rw_lock exists for this URI already
                //fprintf(stdout, "waiting in put 1\n");
                if (!request
                         .content_length_exists) { //if no content-length header for put, bad request and close connection
                    produce_response(socket, 400, 0, 0);
                    graceful_shutdown(socket, req_validity_re, put_validity_re, request_id_re);
                } else {
                    writer_lock((rwlock_t *) value);

                    int res = process_put_req(&request, socket, pmatch[0].rm_eo, bytes_read);
                    produce_response(socket, res, 0, 0);

                    writer_unlock((rwlock_t *) value);
                    graceful_shutdown(socket, req_validity_re, put_validity_re, request_id_re);
                }
            } else {
                //fprintf(stdout, "waiting in put 2\n");
                rwlock_t *new_rwlock = rwlock_new(N_WAY, 1); //make new rwlock
                pthread_mutex_lock(&FileLockLL_lock);
                append(FileLockLL, request.URI, new_rwlock);
                pthread_mutex_unlock(&FileLockLL_lock);

                if (!request
                         .content_length_exists) { //if no content-length header for put, bad request and close connection
                    produce_response(socket, 400, 0, 0);
                    graceful_shutdown(socket, req_validity_re, put_validity_re, request_id_re);
                } else {
                    writer_lock(new_rwlock);

                    int res = process_put_req(&request, socket, pmatch[0].rm_eo, bytes_read);
                    produce_response(socket, res, 0, 0);

                    writer_unlock(new_rwlock);
                    graceful_shutdown(socket, req_validity_re, put_validity_re, request_id_re);
                }
            }
            //sem_post(&workers_avail); //workers_avail.up()
            continue;
        }
        produce_response(socket, 501, 0, 0);
        graceful_shutdown(socket, req_validity_re, put_validity_re, request_id_re);
        //sem_post(&workers_avail); //workers_avail.up()
    }
}

int main(int argc, char *argv[]) {
    int num_threads = 4; //default 4 worker threads
    int port;
    int opt;
    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
        case 't': num_threads = atoi(optarg); break;
        default: //? returned from getopt
            return -1;
        }
    }
    if (optind >= argc) {
        return -1;
    }
    port = atoi(argv[optind]);

    //creating socket and binding to a specified port
    if ((port == 0) | (port > 65535)) {
        fprintf(stderr, "Invalid Port\n");
        exit(1);
    }
    Listener_Socket sock;
    int succ = listener_init(&sock, port);
    if (succ == -1) {
        fprintf(stderr, "Invalid Port\n");
        exit(1);
    }

    //initialize thread-safe queue to hold connections
    queue = queue_new(num_threads);

    //initialize lock for FileLockLL, a shared resource (tracks rwlocks for each URI)
    pthread_mutex_init(&FileLockLL_lock, NULL);

    //initialize semaphores for dispatcher-worker scheme
    //sem_init(&workers_avail, 0, num_threads);
    //sem_init(&workers_sem, 0, 0);

    //initializing num_threads threads
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    //initialize file lock linked list
    FileLockLL = newList();

    //repeatedly accept new connections
    while (1) {
        uintptr_t socket = (uintptr_t) listener_accept(&sock);
        //if((int) socket == -1) break;

        //sem_wait(&workers_avail); //workers_avail.down()
        queue_push(queue, (void *) socket);
        //sem_post(&workers_sem); //workers_sem.up()
    }

    //free the LL, queue, and lock
    moveFront(FileLockLL);
    while (indexl(FileLockLL) < length(FileLockLL) - 1) {
        rwlock_delete(get(FileLockLL));
        moveNext(FileLockLL);
    }
    delete (FileLockLL);
    queue_delete(&queue);
    pthread_mutex_destroy(&FileLockLL_lock);
    return 0;
}

void graceful_shutdown(
    int socket, regex_t req_validity_re, regex_t put_validity_re, regex_t request_id_re) {
    regfree(&req_validity_re);
    regfree(&put_validity_re);
    regfree(&request_id_re);
    close(socket);
}

int process_put_req(Request *request, int socket, int msg_body_start, int read_bytes) {
    int content_length = atoi(request->Content_Length_Field);
    int file_exists = 0;
    int URI_fd;
    URI_fd = open(request->URI, O_WRONLY | O_TRUNC); //try to open existing file
    if (URI_fd != -1) { //file exists
        file_exists = 1;
    } else if (errno == EACCES) { //file exists but permission denied
        return 403;
    } else { //file doesn't exist
        URI_fd = open(request->URI, O_WRONLY | O_CREAT, 0666); //open new file
    }

    int total_bytes_written = 0;
    int remaining_bytes = read_bytes - msg_body_start;
    int cursor = msg_body_start;
    int bytes_written = write_n_bytes(URI_fd, request->RequestBuffer + cursor,
        remaining_bytes); //first write out all chopped off contents in buffer to location
    if (bytes_written == -1) {
        close(URI_fd);
        return 500;
    }
    total_bytes_written += bytes_written;

    pass_n_bytes(
        socket, URI_fd, content_length - total_bytes_written); //write out everything past buffer

    if (file_exists == 1) { //writing response
        close(URI_fd);
        if (request->request_id_exists) { //produce audit log entry
            fprintf(stderr, "PUT,%s,200,%s\n", request->URI, request->Request_Id_Field);
        } else {
            fprintf(stderr, "PUT,%s,200,0\n", request->URI);
        }
        return 200;
    } else {
        close(URI_fd);
        if (request->request_id_exists) { //produce audit log entry
            fprintf(stderr, "PUT,%s,201,%s\n", request->URI, request->Request_Id_Field);
            //fprintf(stderr, "%lu\n", strlen(request->Request_Id_Field));
        } else {
            fprintf(stderr, "PUT,%s,201,0\n", request->URI);
        }
        return 201;
    }
}

int process_get_req(Request *request, int socket) {
    int URI_fd = open(request->URI, O_RDONLY);
    if (URI_fd == -1) {
        if (errno == EACCES) {
            return 403;
        }
        if (request->request_id_exists) { //produce audit log entry
            fprintf(stderr, "GET,%s,404,%s\n", request->URI, request->Request_Id_Field);
        } else {
            fprintf(stderr, "GET,%s,404,0\n", request->URI);
        }
        return 404;
    }
    char temp[1]; //making sure URI is not a directory
    int test = read(URI_fd, temp, 0);
    if (test == -1) {
        close(URI_fd);
        return 403;
    }

    size_t content_length = 0;
    char temp_buf[MAX_BUF_LEN];
    while (1) { //read-only loop to find content_length
        int bytes_read = read(URI_fd, temp_buf, MAX_BUF_LEN);
        if (bytes_read == 0) {
            break;
        }
        content_length += bytes_read;
    }
    lseek(URI_fd, 0, SEEK_SET);
    produce_response(socket, 200, 1, content_length); //write out response before message_body

    pass_n_bytes(URI_fd, socket, content_length); //write everything after initial buffer to socket

    if (request->request_id_exists) { //produce audit log entry
        fprintf(stderr, "GET,%s,200,%s\n", request->URI, request->Request_Id_Field);
    } else {
        fprintf(stderr, "GET,%s,200,0\n", request->URI);
    }
    close(URI_fd);
    return 0;
}

void parse_req_line(Request *request, int w1_so, int w1_eo, int w2_so, int w2_eo, int w3_so,
    int w3_eo, int w4_so, int w4_eo, int req_id_so, int req_id_eo) { //helper function
    int n = w1_eo - w1_so;
    strncpy(request->Method, request->RequestBuffer + w1_so, n);
    request->Method[n] = '\0';

    n = w2_eo - (w2_so + 1);
    strncpy(request->URI, request->RequestBuffer + w2_so + 1, n);
    request->URI[n] = '\0';

    n = w3_eo - w3_so;
    strncpy(request->Version, request->RequestBuffer + w3_so, n);
    request->Version[n] = '\0';

    if (w4_so != -1) { //if there is a content length field
        n = w4_eo - w4_so;
        strncpy(request->Content_Length_Field, request->RequestBuffer + w4_so, n);
        request->Content_Length_Field[n] = '\0';
    }

    if (req_id_so != -1) { //if there is a request id field
        n = req_id_eo - req_id_so;
        strncpy(request->Request_Id_Field, request->RequestBuffer + req_id_so, n);
        request->Request_Id_Field[n] = '\0';
    }
}

void produce_response(
    int socket, int status_code, int valid_get, size_t content_length) { //helper function
    write(socket, "HTTP/1.1 ", 9);
    if (status_code == 200) {
        if (valid_get == 1) {
            write(socket, "200 OK\r\nContent-Length: ", 24);
            char cont_len_str[20];
            snprintf(cont_len_str, 20, "%zu", content_length);
            write(socket, cont_len_str, strlen(cont_len_str));
            write(socket, "\r\n\r\n", 4);

        } else {
            write(socket, "200 OK\r\nContent-Length: 3\r\n\r\nOK\n", 32);
        }
    }
    if (status_code == 201) {
        write(socket, "201 Created\r\nContent-Length: 8\r\n\r\nCreated\n", 42);
    }
    if (status_code == 400) {
        write(socket, "400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 51);
    }
    if (status_code == 403) {
        write(socket, "403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n", 47);
    }
    if (status_code == 404) {
        write(socket, "404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n", 47);
    }
    if (status_code == 500) {
        write(socket,
            "500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n", 71);
    }
    if (status_code == 501) {
        write(socket, "501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n", 59);
    }
    if (status_code == 505) {
        write(socket,
            "505 Version Not Supported\r\nContent-Length: 22\r\n\r\nVersion Not Supported\n", 71);
    }
}
