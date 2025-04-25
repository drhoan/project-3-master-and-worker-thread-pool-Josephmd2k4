// master-worker.c

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

int item_to_produce, curr_buf_size;
int total_items, max_buf_size, num_workers, num_masters;

int *buffer;

// circular‐buffer indices
int in = 0, out = 0;
// count of consumed items
long total_consumed = 0;

void print_produced(int num, int master)
{
    printf("Produced %d by master %d\n", num, master);
}

void print_consumed(int num, int worker)
{
    printf("Consumed %d by worker %d\n", num, worker);
}

// synchronization
pthread_mutex_t buf_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buf_not_full = PTHREAD_COND_INITIALIZER;
pthread_cond_t buf_not_empty = PTHREAD_COND_INITIALIZER;

// producer entry point
void *generate_requests_loop(void *data)
{
    int thread_id = *((int *)data);

    while (1)
    {
        pthread_mutex_lock(&buf_mutex);

        // no more items to produce?
        if (item_to_produce >= total_items)
        {
            pthread_mutex_unlock(&buf_mutex);
            break;
        }

        // wait if buffer is full
        while (curr_buf_size == max_buf_size)
        {
            pthread_cond_wait(&buf_not_full, &buf_mutex);
        }

        // produce next item
        int item = item_to_produce++;
        buffer[in] = item;
        in = (in + 1) % max_buf_size;
        curr_buf_size++;

        print_produced(item, thread_id);

        // wake up one consumer
        pthread_cond_signal(&buf_not_empty);
        pthread_mutex_unlock(&buf_mutex);
    }
    return NULL;
}

// consumer entry point
void *consume_requests_loop(void *data)
{
    int thread_id = *((int *)data);

    while (1)
    {
        pthread_mutex_lock(&buf_mutex);

        // if everything consumed and buffer empty, we're done
        if (total_consumed >= total_items && curr_buf_size == 0)
        {
            pthread_mutex_unlock(&buf_mutex);
            break;
        }

        // wait if buffer is empty
        while (curr_buf_size == 0)
        {
            // recheck exit condition to avoid deadlock
            if (total_consumed >= total_items)
            {
                pthread_mutex_unlock(&buf_mutex);
                return NULL;
            }
            pthread_cond_wait(&buf_not_empty, &buf_mutex);
        }

        // consume next item
        int item = buffer[out];
        out = (out + 1) % max_buf_size;
        curr_buf_size--;
        total_consumed++;

        print_consumed(item, thread_id);

        // wake up one producer
        pthread_cond_signal(&buf_not_full);
        pthread_mutex_unlock(&buf_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        fprintf(stderr,
                "Usage: %s <total_items> <max_buf_size> <num_workers> <num_masters>\n"
                " e.g. %s 10000 1000 4 3\n",
                argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }

    total_items = atoi(argv[1]);
    max_buf_size = atoi(argv[2]);
    num_workers = atoi(argv[3]);
    num_masters = atoi(argv[4]);

    buffer = malloc(sizeof(int) * max_buf_size);
    if (!buffer)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    item_to_produce = curr_buf_size = 0;

    // allocate thread handles & IDs
    pthread_t *masters = malloc(sizeof(pthread_t) * num_masters);
    int *master_id = malloc(sizeof(int) * num_masters);

    pthread_t *workers = malloc(sizeof(pthread_t) * num_workers);
    int *worker_id = malloc(sizeof(int) * num_workers);

    // spawn masters
    for (int i = 0; i < num_masters; i++)
    {
        master_id[i] = i;
        if (pthread_create(&masters[i], NULL,
                           generate_requests_loop,
                           &master_id[i]) != 0)
        {
            perror("pthread_create master");
            exit(EXIT_FAILURE);
        }
    }

    // spawn workers
    for (int i = 0; i < num_workers; i++)
    {
        worker_id[i] = i;
        if (pthread_create(&workers[i], NULL,
                           consume_requests_loop,
                           &worker_id[i]) != 0)
        {
            perror("pthread_create worker");
            exit(EXIT_FAILURE);
        }
    }

    // wait for all masters to finish
    for (int i = 0; i < num_masters; i++)
    {
        pthread_join(masters[i], NULL);
        printf("master %d joined\n", i);
    }

    // wake any consumers stuck waiting so they can exit
    pthread_mutex_lock(&buf_mutex);
    pthread_cond_broadcast(&buf_not_empty);
    pthread_mutex_unlock(&buf_mutex);

    // wait for all workers to finish
    for (int i = 0; i < num_workers; i++)
    {
        pthread_join(workers[i], NULL);
        printf("worker %d joined\n", i);
    }

    // clean up
    free(buffer);
    free(masters);
    free(master_id);
    free(workers);
    free(worker_id);

    return 0;
}
