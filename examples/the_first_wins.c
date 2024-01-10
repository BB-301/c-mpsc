/*
    Copyright (c) 2024 BB-301 <fw3dg3@gmail.com>
    [Official repository](https://github.com/BB-301/c-mpsc)

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the “Software”), to deal in the Software without restriction,
    including without limitation the rights to use, copy, modify, merge,
    publish, distribute, sublicense, and/or sell copies of the Software,
    and to permit persons to whom the Software is furnished to do so,
    subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

/*
    ====================================================
    Example: Closing the channel after the first message
    ====================================================

    This example illustrates how the `mpsc_t` channel can
    be used to send a structured message from a producer to
    the consumer using the `mpsc_producer_send` function.
    Furthermore, this example illustrates how the `mpsc_producer_ping`
    function can be used to periodically check whether the channel has
    been marked as closed, in which case the producer callback
    thread should stop what it is doing and return to avoid making
    the call to `mpsc_join` hang.
*/

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "mpsc.h"

#define PRODUCER_ACCEPTED(result) assert(result == MPSC_REGISTER_PRODUCER_ERROR_NONE)

#define N_PRODUCERS (4)
#define RANDOM_SLEEP_UPPER_BOUND_MS (10000)
#define WAKE_INTERVAL_MS (50) // max resolution

static size_t my_get_random_sleep_ms(void);
static size_t my_ms_sleeper(void);

static void my_consumer_callback(mpsc_consumer_t *consumer, void *data, size_t n, bool closed);
static void my_producer_thread_callback(mpsc_producer_t *producer);

struct my_game_result
{
    size_t time_elapsed_ms;
    size_t player_id;
};

static void my_game_result_print(struct my_game_result *game_result, FILE *stream);

struct my_producer_thread_callback_context
{
    size_t id;
    size_t must_sleep_ms;
    size_t total_slept_ms;
};

static struct my_game_result final_game_result = {.player_id = 0, .time_elapsed_ms = 0};

int main(void)
{
    srand(time(NULL));

    mpsc_t *mpsc = mpsc_create((mpsc_create_params_t){
        .buffer_size = sizeof(struct my_game_result),
        .n_max_producers = N_PRODUCERS,
        .consumer_callback = my_consumer_callback,
        .consumer_error_callback = NULL,
        .error_handling_enabled = false,
        .create_and_join_thread_safety_disabled = false,
    });

    struct my_producer_thread_callback_context contexts[N_PRODUCERS];

    for (size_t i = 0; i < N_PRODUCERS; i++)
    {
        contexts[i].id = i + 1;
        contexts[i].must_sleep_ms = my_get_random_sleep_ms();
        contexts[i].total_slept_ms = 0;
        // NOTE: Technically, producers registered first have a slight advantage,
        // but its ok for this example I guess.
        PRODUCER_ACCEPTED(mpsc_register_producer(mpsc, my_producer_thread_callback, &contexts[i]));
    }

    mpsc_join(mpsc);

    // NOTE: Here, technically, the message structure could have
    // only contained the producer ID and we could have used that
    // value to retrieve the `total_slept_ms` value directly into
    // the appropriate context structure, using `contexts[id - 1].total_slept_ms`.
    fprintf(stdout, "We have a winner (out of %zu players)!\n", (size_t)N_PRODUCERS);
    my_game_result_print(&final_game_result, stdout);

    return 0;
}

static void my_consumer_callback(mpsc_consumer_t *consumer, void *data, size_t n, bool closed)
{
    if (closed)
    {
        return;
    }
    if (n != sizeof(struct my_game_result))
    {
        fprintf(stderr, "Unexpected message size");
        exit(EXIT_FAILURE);
    }
    if (final_game_result.player_id > 0)
    {
        return;
    }
    struct my_game_result *game_result = data;
    final_game_result.player_id = game_result->player_id;
    final_game_result.time_elapsed_ms = game_result->time_elapsed_ms;
    mpsc_consumer_close(consumer);
}

static void my_producer_thread_callback(mpsc_producer_t *producer)
{
    struct my_producer_thread_callback_context *ctx = mpsc_producer_context(producer);
    while (true)
    {
        ctx->total_slept_ms += my_ms_sleeper();
        if (ctx->must_sleep_ms > ctx->total_slept_ms)
        {
            if (!mpsc_producer_ping(producer))
            {
                break;
            }
        }
        else
        {
            struct my_game_result message = {
                .player_id = ctx->id,
                .time_elapsed_ms = ctx->total_slept_ms};
            // NOTE: At this point, we don't care about whether this call to the `mpsc_producer_send`
            // function returns `true` or `false`: we break out of the loop and return anyway because
            // we are done.
            mpsc_producer_send(producer, &message, sizeof(struct my_game_result));
            break;
        }
    }
}

static void my_game_result_print(struct my_game_result *game_result, FILE *stream)
{
    fprintf(stream, "my_game_result {\n");
    fprintf(stream, "\t.player_id = %zu,\n", game_result->player_id);
    fprintf(stream, "\t.time_elapsed_ms = %zu\n", game_result->time_elapsed_ms);
    fprintf(stream, "}\n");
}

static size_t my_get_random_sleep_ms(void)
{
    return rand() % RANDOM_SLEEP_UPPER_BOUND_MS;
}

static size_t my_ms_sleeper(void)
{
    struct timespec spec = {.tv_sec = 0, .tv_nsec = 1000000 * (WAKE_INTERVAL_MS)};
    if (nanosleep(&spec, NULL) != 0)
    {
        // NOTE: Here we don't bother handling errors that could be caused
        // by signal interrupts, since this is just an example.
        //  [read more](https://man7.org/linux/man-pages/man2/nanosleep.2.html)
        perror("nanosleep()");
        exit(EXIT_FAILURE);
    }
    return WAKE_INTERVAL_MS;
}
