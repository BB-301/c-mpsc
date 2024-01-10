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
    =========================================
    Example: MPSC channel with empty messages
    =========================================

    This example illustrates how to create a mpsc_t channel
    object and use it to receive empty messages (i.e., messages
    without payloads). Each registered producer has a maximum number
    of messages that it can send (which number is chosen at random),
    after which its thread callback function returns. The producer
    thread callback will alternatively return upon failure to send,
    which will mean that the consumer has requested the channel's
    closure, which will happen here if the number of messages
    received by the consumer reaches `MESSAGE_THRESHOLD`. Note
    that a message that was received by the channel before the request
    to close will be delivered before the channel closes, such that
    it is possible for a consumer to receive another message even
    after having called `mpsc_consumer_close`.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "mpsc.h"

#define UNUSED_VARIABLE(m) ((void)(m))
#define PRODUCER_ACCEPTED(result) assert(result == MPSC_REGISTER_PRODUCER_ERROR_NONE)

#define N_PRODUCERS (4)
#define MESSAGE_THRESHOLD (20)
#define RANDOM_INTEGER_UPPER_BOUND (16)

static void my_consumer_callback(mpsc_consumer_t *self, void *data, size_t n, bool closed);
static void my_producer_thread_callback(mpsc_producer_t *producer);

struct my_producer_thread_callback_context
{
    size_t id;
    size_t n_max_iter;
    size_t counter;
};

static void my_producer_thread_callback_context_print(
    struct my_producer_thread_callback_context *ctx,
    FILE *stream);

static size_t consumer_callback_message_counter = 0;

int main(void)
{
    mpsc_t *mpsc = mpsc_create((mpsc_create_params_t){
        .buffer_size = 0,
        .n_max_producers = N_PRODUCERS,
        .consumer_error_callback = NULL,
        .error_handling_enabled = false,
        .create_and_join_thread_safety_disabled = true,
        .consumer_callback = my_consumer_callback});

    srand(time(NULL));

    struct my_producer_thread_callback_context contexts[N_PRODUCERS];
    for (size_t i = 0; i < N_PRODUCERS; i++)
    {
        struct my_producer_thread_callback_context *ctx = &contexts[i];
        ctx->id = i + 1;
        ctx->counter = 0;
        ctx->n_max_iter = (size_t)((rand() % (RANDOM_INTEGER_UPPER_BOUND - 1)) + 1); // Generate integer between 1 and `RANDOM_INTEGER_UPPER_BOUND`
        PRODUCER_ACCEPTED(mpsc_register_producer(mpsc, my_producer_thread_callback, ctx));
    }

    mpsc_join(mpsc);

    size_t total_messages_sent = 0;
    for (size_t i = 0; i < N_PRODUCERS; i++)
    {
        total_messages_sent += contexts[i].counter;
        my_producer_thread_callback_context_print(&contexts[i], stdout);
    }
    assert(total_messages_sent == consumer_callback_message_counter);

    return 0;
}

static void my_consumer_callback(mpsc_consumer_t *self, void *data, size_t n, bool closed)
{
    UNUSED_VARIABLE(data);
    assert(n == 0);
    if (closed)
    {
        fprintf(stdout, "[consumer:closed]\n");
    }
    else
    {
        consumer_callback_message_counter += 1;
        fprintf(stdout, "[consumer:%02zu] new message\n", consumer_callback_message_counter);
        if (MESSAGE_THRESHOLD == consumer_callback_message_counter)
        {
            fprintf(stdout, "[consumer:%02zu] threshold reached, so requesting channel closure...\n", consumer_callback_message_counter);
            mpsc_consumer_close(self);
        }
    }
}

static void my_producer_thread_callback(mpsc_producer_t *producer)
{
    struct my_producer_thread_callback_context *ctx = mpsc_producer_context(producer);
    while (true)
    {
        if (!mpsc_producer_send_empty(producer))
        {
            break;
        }
        ctx->counter += 1;
        if (ctx->n_max_iter == ctx->counter)
        {
            break;
        }
    }
    fprintf(stdout, "[producer #%zu] exiting thread...\n", ctx->id);
}

static void my_producer_thread_callback_context_print(
    struct my_producer_thread_callback_context *ctx,
    FILE *stream)
{
    fprintf(stream, "struct my_producer_thread_callback_context {\n");
    fprintf(stream, "\t.id = %zu,\n", ctx->id);
    fprintf(stream, "\t.n_max_iter = %zu,\n", ctx->n_max_iter);
    fprintf(stream, "\t.counter = %zu\n", ctx->counter);
    fprintf(stream, "}\n");
}
