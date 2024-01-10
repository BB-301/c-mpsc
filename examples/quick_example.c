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
    ======================
    Example: Quick example
    ======================

    This is a quick "getting started" example used to illustrate
    the basic structure of a program using lib<mpsc>. This example
    is featured in the [repository](https://github.com/BB-301/c-mpsc)'s
    README.md file.
*/

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "mpsc.h"

#define IGNORE_UNUSED(m) ((void)(m))

#define TEXT_BUFFER_SIZE (100)
#define N_PRODUCERS (8)

static void my_consumer_callback(mpsc_consumer_t *consumer, void *data, size_t n, bool closed);
static void my_producer_thread_callback(mpsc_producer_t *producer);

struct my_message
{
    char text[TEXT_BUFFER_SIZE];
};

struct my_producer_thread_callback_context
{
    size_t id;
};

int main(void)
{
    mpsc_t *mpsc = mpsc_create((mpsc_create_params_t){
        .buffer_size = sizeof(struct my_message),
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
        assert(mpsc_register_producer(mpsc, my_producer_thread_callback, &contexts[i]) == MPSC_REGISTER_PRODUCER_ERROR_NONE);
    }

    mpsc_join(mpsc);

    exit(EXIT_SUCCESS);
}

static void my_consumer_callback(mpsc_consumer_t *consumer, void *data, size_t n, bool closed)
{
    IGNORE_UNUSED(consumer);
    static size_t counter = 0;
    counter += 1;
    if (closed)
    {
        fprintf(stdout, "[consumer:%zu] closed\n", counter);
        return;
    }
    if (n != sizeof(struct my_message))
    {
        fprintf(stderr, "[consumer] Error: Unexpected message size\n");
        exit(EXIT_FAILURE);
    }
    struct my_message *message = data;
    fprintf(stdout, "[consumer:%zu] %s\n", counter, message->text);
    free(data);
}

static void my_producer_thread_callback(mpsc_producer_t *producer)
{
    struct my_producer_thread_callback_context *ctx = mpsc_producer_context(producer);
    struct my_message message;
    sprintf(message.text, "Hello from producer #%zu!", ctx->id);
    assert(mpsc_producer_send(producer, &message, sizeof(struct my_message)));
}
