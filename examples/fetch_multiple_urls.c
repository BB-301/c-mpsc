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
    ================================================
    Example: Using the MPSC channel without messages
    ================================================

    This example illustrates how the `mpsc_t` channel facility
    can be used to perform multiple tasks concurrently, on different
    threads (since each producer executes on its own thread). In this
    example, we use `libcurl` to fetch a list of HTTP URLs and we
    manually count the number of bytes returned by each resource's remote
    server, along with the response status code. In this case, the
    channel's consumer end is not used, so this application would not really
    require this library and could simply use pthreads directly to achieve
    similar results, but I thought the example was nonetheless interesting
    since it still illustrates the structure of an application using this
    library. Besides, this example could serve as a starting point for a
    similar application that would require notifying the consumer.
*/

#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "mpsc.h"

#define IGNORE_UNUSED(m) ((void)(m))
#define ASSERT_CURL_OK(curl_code) assert(curl_code == CURLE_OK)
#define PRODUCER_ACCEPTED(result) assert(result == MPSC_REGISTER_PRODUCER_ERROR_NONE)

static size_t my_writer_function(void *data, size_t size, size_t nmemb, void *user_data);
static void my_consumer_callback(mpsc_consumer_t *self, void *data, size_t n, bool closed);
static void my_producer_thread_callback(mpsc_producer_t *producer);

struct my_time_diff
{
    struct timeval start;
    struct timeval end;
};

static void my_time_diff_start(struct my_time_diff *self);
static void my_time_diff_end(struct my_time_diff *self);
static double my_time_diff_ms(struct my_time_diff *self);

struct my_producer_thread_callback_context
{
    char *url;
    size_t content_length;
    long status_code;
    struct my_time_diff time_diff;
};

void my_producer_thread_callback_context_print(struct my_producer_thread_callback_context *ctx, FILE *stream);

int main(void)
{
    char *my_urls[] = {
        "https://en.wikipedia.org/wiki/Pthreads",
        "https://man7.org/linux/man-pages/man7/pthreads.7.html",
        "https://www.cs.cmu.edu/afs/cs/academic/class/15492-f07/www/pthreads.html",
        "https://pubs.opengroup.org/onlinepubs/7908799/xsh/pthread.h.html",
        "https://linux.die.net/man/7/pthreads"};
    size_t n_urls = sizeof(my_urls) / sizeof(my_urls[0]);

    struct my_producer_thread_callback_context contexts[n_urls];

    ASSERT_CURL_OK(curl_global_init(CURL_GLOBAL_ALL));

    mpsc_t *mpsc = mpsc_create((mpsc_create_params_t){
        .buffer_size = 0,
        .n_max_producers = n_urls,
        .consumer_callback = my_consumer_callback,
        .consumer_error_callback = NULL,
        .create_and_join_thread_safety_disabled = false,
        .error_handling_enabled = false});

    for (size_t i = 0; i < n_urls; i++)
    {
        contexts[i].url = my_urls[i];
        contexts[i].content_length = 0;
        contexts[i].status_code = 0;
        PRODUCER_ACCEPTED(mpsc_register_producer(mpsc, my_producer_thread_callback, &contexts[i]));
    }

    mpsc_join(mpsc);

    for (size_t i = 0; i < n_urls; i++)
    {
        my_producer_thread_callback_context_print(&contexts[i], stdout);
    }

    curl_global_cleanup();

    return 0;
}

static size_t my_writer_function(void *data, size_t size, size_t nmemb, void *user_data)
{
    IGNORE_UNUSED(data);
    struct my_producer_thread_callback_context *ctx = user_data;
    size_t n = size * nmemb;
    ctx->content_length += n;
    return n;
}

static void my_consumer_callback(mpsc_consumer_t *self, void *data, size_t n, bool closed)
{
    IGNORE_UNUSED(self);
    IGNORE_UNUSED(data);
    IGNORE_UNUSED(n);
    if (!closed)
    {
        fprintf(stderr, "Error: consumer not expecting any messages\n");
        exit(EXIT_FAILURE);
    }
}

static void my_producer_thread_callback(mpsc_producer_t *producer)
{
    struct my_producer_thread_callback_context *ctx = mpsc_producer_context(producer);
    my_time_diff_start(&ctx->time_diff);
    CURL *curl = curl_easy_init();
    assert(curl != NULL);
    ASSERT_CURL_OK(curl_easy_setopt(curl, CURLOPT_URL, ctx->url));
    ASSERT_CURL_OK(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, my_writer_function));
    ASSERT_CURL_OK(curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx));
    ASSERT_CURL_OK(curl_easy_perform(curl));
    ASSERT_CURL_OK(curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ctx->status_code));
    curl_easy_cleanup(curl);
    my_time_diff_end(&ctx->time_diff);
}

void my_producer_thread_callback_context_print(struct my_producer_thread_callback_context *ctx, FILE *stream)
{
    fprintf(stream, "struct my_producer_thread_callback_context {\n");
    fprintf(stream, "\t.url = %s,\n", ctx->url);
    fprintf(stream, "\t.content_length = %zu,\n", ctx->content_length);
    fprintf(stream, "\t.status_code = %lu,\n", ctx->status_code);
    fprintf(stream, "\t.time_elapsed = %0.0f\n", my_time_diff_ms(&ctx->time_diff));
    fprintf(stream, "}\n");
}

static void my_time_diff_start(struct my_time_diff *self)
{
    gettimeofday(&self->start, NULL);
}

static void my_time_diff_end(struct my_time_diff *self)
{
    gettimeofday(&self->end, NULL);
}

static double my_time_diff_ms(struct my_time_diff *self)
{
    double time_elapsed_sec = (self->end.tv_sec - self->start.tv_sec) * 1000.0;
    time_elapsed_sec += (self->end.tv_usec - self->start.tv_usec) / 1000.0;
    return time_elapsed_sec;
}
