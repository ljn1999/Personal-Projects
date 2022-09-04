#include "request.h"
#include "server_thread.h"
#include "common.h"
#define printf(...)

// declare mutex lock and cv
pthread_mutex_t lock;
pthread_cond_t full;
pthread_cond_t empty;

// declare global variables
int in;
int out;
void * start_routine_helper(void * sv);

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	/* add any other parameters you need */
    pthread_t ** threads_pool; // ** because it's an array of threads pointers
    int * request_buffer;
    struct ht_cache * cache;
    struct node * lru_head;
};

struct cell {
    struct file_data * file;
    int count;
    struct cell * next;
};

struct ht_cache {
    /* you can define this struct to have whatever fields you want. */
    struct cell ** ht;
    int empty_space;
};

struct node {
    char * file_name;
    struct node * next;
};
/* static functions */

void cache_remove(struct server * sv, struct cell * cell_to_remove);
void lru_remove(struct server * sv, char * file_name_to_remove);
unsigned long generate_hash_key(char *str);
struct cell * cache_lookup(struct server * sv, char * file_name);
int cache_evict(struct server * sv, int amount_to_evict);
int can_cache_insert(struct server * sv, struct file_data * file);
struct cell * create_cell(struct file_data * file);
struct cell * cache_insert(struct server * sv, struct file_data * file);
void lru_insert(struct server * sv, struct file_data * file);
void lru_update(struct server * sv, struct file_data * file);
void print_lru(struct server * sv);
void print_cache(struct server * sv);
int cache_size = 0;

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
    printf("do server request start\n");
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
    /* read file,
	 * fills data->file_buf with the file contents,
	 * data->file_size with file size. */
    // lab4: no cache
    int not_inserted = 1;
    if (sv->cache == NULL) {
        ret = request_readfile(rq);
        if (ret == 0) { /* couldn't read file */
            goto out;
        }
        /* send file to client */
        request_sendfile(rq);
        out:
        request_destroy(rq);
        file_data_free(data);
    } else { // lab5: has cache
        // firstly look up in cache
        printf("is able to get here\n");
        pthread_mutex_lock(&lock);
        struct cell * target_cell = cache_lookup(sv, data->file_name);
        pthread_mutex_unlock(&lock);

        // if found in cache, no need to read again
        if (target_cell != NULL) {
            // set the data and update lru
            request_set_data(rq, target_cell->file);
            pthread_mutex_lock(&lock);
            target_cell->count = target_cell->count + 1;
            if (sv->lru_head == NULL) {
                printf("problem? lru shouldn't be empty\n");
            } else {
                lru_update(sv, data);
            }
            pthread_mutex_unlock(&lock);
        } else { // if not found in cache, read the file
            ret = request_readfile(rq);
            if (ret == 0) { /* couldn't read file */
                request_destroy(rq);
                file_data_free(data);
                return;
            }
            // look up in cache again and check if it's in cache, do it in critical section
            pthread_mutex_lock(&lock);
            target_cell = cache_lookup(sv, data->file_name);
            // if found in cache, set the data and update lru
            if (target_cell != NULL) {
                request_set_data(rq, target_cell->file);
                target_cell->count = target_cell->count + 1;
                if (sv->lru_head == NULL) {
                    printf("problem? lru shouldn't be empty\n");
                } else {
                    lru_update(sv, data);
                }
            } else { // still not found in cache
                // try insert to cache
                target_cell = cache_insert(sv, data);
                // if not able to insert in cache (return NULL)
                if (target_cell == NULL) {
                    // get out
                    printf("cannot insert to cache\n");
                } else { // if able to insert in cache
                    // add in lru
                    not_inserted = 0;
                    lru_insert(sv, data);
                }
            }
            pthread_mutex_unlock(&lock);
        }
        // request sendfile
        request_sendfile(rq);
        // update the count in critical section
        pthread_mutex_lock(&lock);
        if (target_cell != NULL) {
            target_cell->count = target_cell->count - 1;
        }
        pthread_mutex_unlock(&lock);
        // destroy the data
        request_destroy(rq);
        // free the data if not inserted in cache, do it in critical section
        pthread_mutex_lock(&lock);
        if (not_inserted == 1) {
            file_data_free(data);
        }
        pthread_mutex_unlock(&lock);
    }
}

/* entry point functions */

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;
    printf("server init\n");
	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests;
	sv->exiting = 0;
    // initialize to NULL
    sv->request_buffer = NULL;
    sv->threads_pool = NULL;

//	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
	if (nr_threads > 0) {
		//TBD();
        /* Lab 4: create queue of max_request size when max_requests > 0 */
        // initialize mutex lock, cv and global variables
        in = 0;
        out = 0;
        pthread_cond_init(&full, NULL);
        pthread_cond_init(&empty, NULL);
        pthread_mutex_init(&lock, NULL);

        // initialize request buffer
        // max_request + 1 because considering a circular buffer,
        // a cell will always be left empty,
        // therefore we need an extra cell to accommodate the max_request requests
        if (max_requests > 0) {
            sv->request_buffer = malloc((max_requests+1)*sizeof(int));
        }

        /* Lab 4: create worker threads when nr_threads > 0 */
        // initialize thread pool
        sv->threads_pool = malloc((nr_threads)*sizeof(pthread_t *));
        for (int i=0; i<nr_threads; i++) {
            sv->threads_pool[i] = malloc(sizeof(pthread_t *));
            // set the start function routine
            pthread_create(sv->threads_pool[i], NULL, start_routine_helper, (void * )sv);
        }

	} else {
        sv->threads_pool = NULL;
    }

	/* Lab 5: init server cache and limit its size to max_cache_size */
    if (max_cache_size > 0) {
        printf("server init start\n");
        cache_size = 2*max_cache_size-1;
        sv->max_cache_size = max_cache_size;
        sv->cache = malloc(sizeof(struct ht_cache));
        sv->cache->empty_space = max_cache_size;
        sv->cache->ht = malloc(cache_size*(sizeof(struct cell *)));
        for (int i=0; i<max_cache_size; i++) {
            sv->cache->ht[i] = NULL;
        }
        sv->lru_head = NULL;//malloc(sizeof(struct node *));
    } else {
        sv->cache = NULL;
    }
    printf("return server init\n");
	return sv;
}

void * start_routine_helper(void * sv_void) {
    // "consumer"
    struct server * sv = sv_void; // typecast

    while(!sv->exiting) { // when the thread is not going to exit
        // acquire lock
        pthread_mutex_lock(&lock);
        // empty buffer case, a worker (here's a consumer) must wait
        while (in == out) {
            if (!sv->exiting) {
                // thread go to sleep (wait) because it's empty
                pthread_cond_wait(&empty, &lock);
            } else {
                // release lock and exit
                pthread_mutex_unlock(&lock);
                pthread_exit(0);
            }
        }

        // check again if is going to exit
        if (sv->exiting) {
            // release lock and exit
            pthread_mutex_unlock(&lock);
            pthread_exit(0);
        }

        // store element in buffer
        int elem = sv->request_buffer[out];

        // update out
        out = (out+1) % (sv->max_requests+1);

        // wake up the full list
        pthread_cond_broadcast(&full);

        // release lock and do server request
        pthread_mutex_unlock(&lock);
        do_server_request(sv, elem);
    }

    // if the thread is going to exit
    pthread_mutex_unlock(&lock);
    pthread_exit(0);
}

void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		//TBD();
        // "producer"
        // acquire lock
        pthread_mutex_lock(&lock);
        int size = sv->max_requests+1;
        // when the buffer is full, a worker (here's a producer) must wait
        while ((in-out+size) % size == size-1) {
            // thread go to sleep (wait) because it's full
            pthread_cond_wait(&full, &lock);
        }

        // write in the buffer
        sv->request_buffer[in] = connfd;
        // update in
        in = (in+1) % size;

        // wake up the empty list
        pthread_cond_broadcast(&empty);

        // release lock and return
        pthread_mutex_unlock(&lock);
        return;
	}
}

void
server_exit(struct server *sv) {
    /* when using one or more worker threads, use sv->exiting to indicate to
     * these threads that the server is exiting. make sure to call
     * pthread_join in this function so that the main server thread waits
     * for all the worker threads to exit before exiting. */
    sv->exiting = 1;

    /* make sure to free any allocated resources */
    //free(sv);

    // wake up the empty list
    pthread_cond_broadcast(&empty);

    // do thread join (wait)
    for (int thread_idx = 0; thread_idx < sv->nr_threads; thread_idx++) {
        pthread_t * wait_thread = sv->threads_pool[thread_idx];
        // have to dereference to get the actual thread
        pthread_join(*wait_thread, NULL);
    }

    // free the buffer
    if (sv->request_buffer) {
        free(sv->request_buffer);
        sv->request_buffer = NULL;
    }

    // free the threads_pool
    if (sv->threads_pool) {
        for (int thread_idx = 0; thread_idx < sv->nr_threads; thread_idx++) {
            // only free it if the thread is there
            if (sv->threads_pool[thread_idx]) {
                free(sv->threads_pool[thread_idx]);
            }
        }
        free(sv->threads_pool);
    }
    sv->threads_pool = NULL;

    // free the cache and lru
    if (sv->cache) {
        for (int i=0; i<cache_size; i++) {
            struct cell * curr_head = sv->cache->ht[i];
            if (curr_head != NULL) {
                struct cell * curr = sv->cache->ht[i];
                struct cell * next_ptr = sv->cache->ht[i];
                while (curr != NULL) {
                    next_ptr = curr->next;
                    lru_remove(sv, curr->file->file_name);
                    file_data_free(curr->file);
                    curr->next = NULL;
                    free(curr);
                    curr = next_ptr;
                }
            }
        }
        free(sv->lru_head);
        sv->lru_head = NULL;
        free(sv->cache->ht);
        sv->cache->ht = NULL;
        free(sv->cache);
        sv->cache = NULL;
    }
    free(sv);

    return;
}

unsigned long generate_hash_key(char *str) {
    unsigned long hash = 5381;
    int c;
    while ((int)(c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

struct cell * cache_lookup(struct server * sv, char * file_name) {
    // check empty cache
    if (sv->max_cache_size == 0 || sv->cache == NULL || sv->cache->ht == NULL) {
        return NULL;
    }
    printf("generate key in cache lookup\n");
    printf("cache lookup file name %s\n", file_name);
    unsigned long hash_key = generate_hash_key(file_name);
    int quotient = hash_key % cache_size;
    struct cell * ht_position = sv->cache->ht[quotient];

    if (ht_position == NULL) { // the file never appeared before and it's the head of the linked-list
        return NULL;
    } else {
        while (ht_position != NULL) { // check the linked-list and see if it's already there
            if (strcmp(file_name, ht_position->file->file_name) == 0){ // the word is found
                return ht_position;
            }
            ht_position = ht_position->next; // move pointer to the next
        }
        // not found in the linked-list
        return NULL;
    }
}

int cache_evict(struct server * sv, int amount_to_evict) {
    struct node * curr = sv->lru_head;
    printf("curr (head) file name: %s\n", curr->file_name);
    int count = 0;
    while (curr != NULL && amount_to_evict > sv->cache->empty_space) {
        printf("count number %d\n", count);
        count = count+1;
        // first find it in cache
        if (curr == NULL) {
            printf("curr is null\n");
        }
        printf("stop here?\n");
        if (curr->next == NULL) {
            printf("curr->next is null\n");
        }
        struct cell * curr_in_cache = cache_lookup(sv, curr->file_name);
        // NULL or there are other processes using it, try the next in list
        if (curr_in_cache == NULL || curr_in_cache->count != 0) {
            curr = curr->next;
        } else { // if there's no other process currently accessing it
            sv->cache->empty_space = sv->cache->empty_space + curr_in_cache->file->file_size;
            struct node * copy = curr->next;

            // remove the curr_cache in cache
            cache_remove(sv, curr_in_cache);
            // remove the curr in lru
            lru_remove(sv, curr->file_name);

            curr = copy;
        }
    }
    // after eviction, check the size again
    // if still not enough space
    if (amount_to_evict > sv->cache->empty_space) {
        return 0;
    } else {
        return 1;
    }
}

int can_cache_insert(struct server * sv, struct file_data * file) {
    // check file size with cache limit
    if (file->file_size > sv->max_cache_size) {
        return 0;
    }

    // check file size with the cache rest space
    if (file->file_size <= sv->cache->empty_space) {
        return 1;
    } else { // need to do cache evict, return cache evict result
        return cache_evict(sv, file->file_size);
    }
}

struct cell * create_cell(struct file_data * file) {
    struct cell * new_cell = malloc(sizeof(struct cell));
    //memset(new_cell, 0, sizeof(struct cell));
    new_cell->count = 1;
    struct file_data * file_copy = malloc(sizeof(struct file_data));
    memcpy(file_copy, file, sizeof(struct file_data));
    new_cell->file = file_copy;
    //new_cell->file = file;
    new_cell->next = NULL;
    return new_cell;
}

// return NULL if can not insert (no enough size)
// return the inserted cell if can do insert
struct cell * cache_insert(struct server * sv, struct file_data * file) {
    if (can_cache_insert(sv, file) == 1) { // evict is already done in can_cache_insert if needed
        // insert into the cache
        char * file_name = file->file_name;
        printf("generate key in cache insert\n");
        unsigned long hash_key = generate_hash_key(file_name);
        int quotient = hash_key % cache_size;
        struct cell * ht_position = sv->cache->ht[quotient];

        if (ht_position == NULL) { // the file never appeared before and it's the head of the linked-list
            // insert as the head of the linked-list
            sv->cache->ht[quotient] = create_cell(file);
            sv->cache->empty_space = sv->cache->empty_space - file->file_size;
            return sv->cache->ht[quotient];
        } else {
            // insert at the end of the linked-list
            while (ht_position->next != NULL) { // check the linked-list and see if it's already there
                ht_position = ht_position->next; // move pointer to the next
            }
            ht_position->next = create_cell(file);
            sv->cache->empty_space = sv->cache->empty_space - file->file_size;
            return ht_position->next;
        }
    } else { // can not insert to cache
        return NULL;
    }
}

void cache_remove(struct server * sv, struct cell * cell_to_remove) {
    // check empty cache
    if (sv->max_cache_size == 0 || sv->cache == NULL || sv->cache->ht == NULL) {
        return;
    }
    char * file_name = cell_to_remove->file->file_name;
    printf("generate key in cache remove\n");
    unsigned long hash_key = generate_hash_key(file_name);
    int quotient = hash_key % cache_size;
    struct cell * ht_position = sv->cache->ht[quotient];

    if (ht_position == NULL) { // the file never appeared before and it's the head of the linked-list
        printf("there must be some problem in cache evict\n");
        return;
    } else {
        struct cell * prev_cell = ht_position;
        while (ht_position != NULL) { // check the linked-list and see if it's already there
            if (strcmp(file_name, ht_position->file->file_name) == 0){ // the word is found
                // if it's the head of the linked list
                if (sv->cache->ht[quotient] == ht_position) {
                    sv->cache->ht[quotient] = ht_position->next;
                    file_data_free(ht_position->file);
                    ht_position->next = NULL;
                    ht_position->file = NULL;
                    free(ht_position);
                    return;
                }
                // if it's not the head
                prev_cell->next = ht_position->next;
                file_data_free(ht_position->file);
                ht_position->next = NULL;
                ht_position->file = NULL;
                free(ht_position);
                return;
            }
            prev_cell = ht_position;
            ht_position = ht_position->next; // move pointer to the next
        }
        // not found in the linked-list
        printf("there must be some problem in cache evict\n");
        return;
    }
}

void lru_remove(struct server * sv, char * file_name_to_remove) {
    struct node * temp = sv->lru_head;
    struct node * prev = sv->lru_head;
    printf("print lru before remove\n");
    //print_lru(sv);
    // if head is the node to remove
    if (temp != NULL && strcmp(temp->file_name, file_name_to_remove) == 0) {
        sv->lru_head = temp->next;
        temp->next = NULL;
        free(temp);
        temp = NULL;
        printf("print lru after remove\n");
        //print_lru(sv);
        return;
    }

    while (temp != NULL && strcmp(temp->file_name, file_name_to_remove) != 0) {
        prev = temp;
        temp = temp->next;
    }
    if (temp == NULL) {
        printf("not found in lru");
        return;
    }
    prev->next = temp->next;
    temp->next = NULL;
    free(temp);
    temp = NULL;
    printf("print lru after remove\n");
    //print_lru(sv);
}

void lru_insert(struct server * sv, struct file_data * file) {
    // initialize the node to insert
    struct node * new_node = malloc(sizeof(struct node));
    new_node->file_name = malloc(strlen(file->file_name) + 1);
    strcpy(new_node->file_name, file->file_name);
    new_node->next = NULL;
    // insert the new node into the tail of linked-list
    // (head) least recent used -> -> -> most recent used (tail)
    struct node * last = sv->lru_head;
    if (sv->lru_head == NULL) {
        sv->lru_head = new_node;
        return;
    }
    while (last->next != NULL) {
        last = last->next;
    }
    last->next = new_node;
    return;
}
void print_lru(struct server * sv) {
    struct node * ptr = sv->lru_head;
    int count_lru = 0;
    while (ptr != NULL) {
        printf("lru item %s ", ptr->file_name);
        count_lru = count_lru+1;
        ptr = ptr->next;
    }
    printf("lru count %d\n", count_lru);
    printf("\n");
}
void lru_update(struct server * sv, struct file_data * file) {
    char *name_to_update = file->file_name;
    struct node *temp = sv->lru_head;
    struct node *prev = sv->lru_head;
    printf("print lru before update\n");
    //print_lru(sv);
    //print_cache(sv);
    // detach the node of interest from the linked-list
    // if head is the node to update
    if (temp != NULL && strcmp(temp->file_name, name_to_update) == 0) {
        sv->lru_head = temp->next;
        temp->next = NULL;
        prev = sv->lru_head;
    } else { // head is not the node to update
        printf("file name to update %s\n", name_to_update);
        printf("start print lru\n");
        //print_lru(sv);
        if (temp == NULL) {
            printf("temp is null from beginning\n");
        }
        while (temp != NULL && strcmp(temp->file_name, name_to_update) != 0) {
            prev = temp;
            temp = temp->next;
        }
        if (temp == NULL) {
            printf("temp is null\n");
        }
        if (temp != NULL) {
            prev->next = temp->next;
            temp->next = NULL;
        }
    }

    // move it to the end of list
    while (prev->next != NULL) {
        prev = prev->next;
    }
    prev->next = temp;
    printf("print lru after update\n");
    //print_lru(sv);
    //print_cache(sv);
    return;
}

void print_cache(struct server * sv) {
    int count_cache = 0;
    for (int i=0; i<cache_size; i++) {
        struct cell * curr_head = sv->cache->ht[i];
        if (curr_head != NULL) {
            struct cell * curr = sv->cache->ht[i];
            while (curr != NULL) {
                printf("cache item %s ", curr->file->file_name);
                count_cache++;
                curr = curr->next;
            }
        }
    }
    printf("cache count is %d\n",count_cache);
}
