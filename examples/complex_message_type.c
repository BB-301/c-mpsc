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
    ===============================================
    Example: MPSC channel with complex message type
    ===============================================

    This is a simple example that aims to illustrate how to use
    the `mpsc_t` object to send messages that have more complex
    types, in case that is needed by a specific application.
    The approach taken here is to use the message's first byte
    to indicate the message type, so that the consumer knows
    how to interpret the remaining `n - 1` bytes. In reality,
    any serialization protocol would do, and the one implemented
    here is completely arbitrary, for illustration purposes.
*/

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpsc.h"

#define IGNORE_UNUSED(m) ((void)(m))
#define MY_TEXT_MESSAGE_BUFFER_SIZE (100)

static void my_consumer_callback(mpsc_consumer_t *consumer, void *data, size_t n, bool closed);
static void my_producer_thread_callback(mpsc_producer_t *producer);

enum my_message_type
{
    MY_MESSAGE_TYPE_NUMBER = 0x01,
    MY_MESSAGE_TYPE_TEXT = 0x02,
};

struct my_message_text
{
    char text[MY_TEXT_MESSAGE_BUFFER_SIZE];
};

struct my_message_number
{
    int number;
};

static size_t my_message_serializer(enum my_message_type message_type, void *data, void **serialized_message);

int main(void)
{
    mpsc_t *mpsc = mpsc_create((mpsc_create_params_t){
        .buffer_size = 1024,
        .n_max_producers = 1,
        .consumer_callback = my_consumer_callback,
        .error_handling_enabled = false,
        .consumer_error_callback = NULL,
        .create_and_join_thread_safety_disabled = false});

    assert(mpsc_register_producer(mpsc, my_producer_thread_callback, NULL) == MPSC_REGISTER_PRODUCER_ERROR_NONE);

    mpsc_join(mpsc);

    exit(EXIT_SUCCESS);
}

static void my_consumer_callback(mpsc_consumer_t *consumer, void *data, size_t n, bool closed)
{
    IGNORE_UNUSED(consumer);
    IGNORE_UNUSED(n);
    if (closed)
    {
        return;
    }
    enum my_message_type message_type = ((uint8_t *)data)[0];
    if (message_type == MY_MESSAGE_TYPE_NUMBER)
    {
        struct my_message_number message;
        memcpy(&message, (uint8_t *)data + 1, sizeof(struct my_message_number));
        fprintf(stdout, "[number] %i\n", message.number);
    }
    else if (message_type == MY_MESSAGE_TYPE_TEXT)
    {
        struct my_message_text message;
        memcpy(&message, (uint8_t *)data + 1, sizeof(struct my_message_text));
        fprintf(stdout, "[text] %s\n", message.text);
    }
    else
    {
        fprintf(stderr, "Error: unsupported message type\n");
        exit(EXIT_FAILURE);
    }
    free(data);
}

static void my_producer_thread_callback(mpsc_producer_t *producer)
{
    struct my_message_number message_1 = {.number = 1234};
    void *message_1_data = NULL;
    size_t n_1 = my_message_serializer(MY_MESSAGE_TYPE_NUMBER, &message_1, &message_1_data);
    assert(mpsc_producer_send(producer, message_1_data, n_1));
    free(message_1_data);

    struct my_message_text message_2;
    assert(sprintf(message_2.text, "My previous message contained the number %i.\n", message_1.number) < MY_TEXT_MESSAGE_BUFFER_SIZE);
    void *message_2_data = NULL;
    size_t n_2 = my_message_serializer(MY_MESSAGE_TYPE_TEXT, &message_2, &message_2_data);
    assert(mpsc_producer_send(producer, message_2_data, n_2));
    free(message_2_data);
}

static size_t my_message_serializer(enum my_message_type message_type, void *data, void **serialized_message)
{
    size_t n;
    switch (message_type)
    {
    case MY_MESSAGE_TYPE_NUMBER:
        n = sizeof(struct my_message_number) + 1;
        break;
    case MY_MESSAGE_TYPE_TEXT:
        n = sizeof(struct my_message_text) + 1;
        break;
    }
    *serialized_message = malloc(n);
    void *tmp = *serialized_message;
    if (tmp == NULL)
    {
        perror("malloc()");
        exit(EXIT_FAILURE);
    }
    ((uint8_t *)tmp)[0] = (uint8_t)message_type;
    memcpy((uint8_t *)tmp + 1, data, n - 1);
    return n;
}
