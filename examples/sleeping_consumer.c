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
    ========================================================
    Example: Sleeping (i.e., blocking) the consumer callback
    ========================================================

    This example, which illustrates the behaviour of a blocking application
    defined consumer callback, was introduced as part of a revision of
    this library's original implementation, which revision was needed because
    the original version was making use of a "busy-waiting recursion" inside
    the `mpsc_producer_send` and `mpsc_producer_send_empty` functions, which,
    besides from being too aggressive of an approach, was also causing the application
    stack to "blow up", as pointed out by Reddit user skeeto [here](https://www.reddit.com/r/C_Programming/comments/1941qj4/comment/khe1ir3/?utm_source=share&utm_medium=web2x&context=3),
    in the presence of substantial blocking (e.g., `sleep(1)`) inside
    the consumer callback. So, the original implementation was modified
    to fix that problem, and this example was added as a means to show
    that the problem has indeed been fixed.
*/

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mpsc.h"

#define IGNORE_UNUSED(m) ((void)(m))

#define NUMBER_OF_EMPTY_MESSAGES (3)

static void my_consumer_callback_reader(mpsc_consumer_t *consumer, void *data, size_t n, bool closed);
static void my_producer_thread_callback_reader(mpsc_producer_t *producer);

int main(void)
{
    mpsc_t *mpsc_reader = mpsc_create((mpsc_create_params_t){
        .buffer_size = 0,
        .n_max_producers = 1,
        .consumer_callback = my_consumer_callback_reader,
        .consumer_error_callback = NULL,
        .error_handling_enabled = false,
        .create_and_join_thread_safety_disabled = false});

    assert(mpsc_register_producer(mpsc_reader, my_producer_thread_callback_reader, NULL) == MPSC_REGISTER_PRODUCER_ERROR_NONE);

    mpsc_join(mpsc_reader);

    return 0;
}

static void my_consumer_callback_reader(mpsc_consumer_t *consumer, void *data, size_t n, bool closed)
{
    static size_t counter = 0;
    IGNORE_UNUSED(consumer);
    IGNORE_UNUSED(data);
    counter += 1;
    if (closed)
    {
        fprintf(stdout, "[consumer][%zu] closed\n", counter);
        return;
    }
    assert(n == 0);
    fprintf(stdout, "[consumer][%zu] new message received; now sleeping for 1 second...\n", counter);
    sleep(1);
}

static void my_producer_thread_callback_reader(mpsc_producer_t *producer)
{
    size_t counter = 0;
    while (counter < (NUMBER_OF_EMPTY_MESSAGES))
    {
        counter += 1;
        fprintf(stdout, "[sender] sending empty message #%zu\n", counter);
        mpsc_producer_send_empty(producer);
    }
}
