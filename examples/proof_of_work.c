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
    ============================================
    Example: MPSC channel used for Proof-Of-Work
    ============================================

    This example, which illustrates how this library can be used to
    carry out Proof-Of-Work, draws inspiration from an article titled
    [Multithreading in Rust with MPSC (Multi-Producer, Single Consumer) channels](https://blog.softwaremill.com/multithreading-in-rust-with-mpsc-multi-producer-single-consumer-channels-db0fc91ae3fa),
    in which Rust's `std::sync::mpsc`'s facility is used by the
    "winning worker thread" to communicate its solution to the main
    thread. The current example is a little different, however. Notably,
    here, instead of casting the product (`base * value`) to as string
    and then to bytes before hashing, we encode the `__uint128_t` product
    as a 16-byte big-endian byte array and pass those 16 bytes directly
    to the OpenSSL based sha256 hasher. Then, to determine
    if a valid solution was found, we check the hash's trailing bytes for the
    number of trailing zeros specified by the `PROBLEM_DIFFICULTY` macro. This
    is also slightly different from what's done in the cited article, where the author
    converts the hash into an HEX string and uses Rust's `std::string::String::ends_with`
    method on the string representation of the hash; e.g. `"...f2c5b46000000".ends_with("00000")`.
*/

#include <assert.h>
#include <errno.h>
#include <openssl/sha.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "mpsc.h"

#define ASSERT_SHA_OPERATION(hash_result) assert(hash_result == 1)
#define ASSERT_PRODUCER_REGISTRATION(result) assert(result == MPSC_REGISTER_PRODUCER_ERROR_NONE)
#define ASSERT_GETTIMEOFDAY(result)   \
    {                                 \
        if (result != 0)              \
        {                             \
            perror("gettimeofday()"); \
            exit(EXIT_FAILURE);       \
        }                             \
    }

#define N_WORKERS (8)          // This value should match the number of CPUs on the system to be optimal.
#define PROBLEM_DIFFICULTY (7) // 7 trailing zeros is ok, but 8 takes substantial time for home the average home computer system
#define PROBLEM_BASE (158)
#define N_ITERS_BEFORE_CHANNEL_CHECK (1000)

typedef uint8_t byte_array_u128_t[16];
typedef uint8_t sha256_hash_t[SHA256_DIGEST_LENGTH];

struct my_time_tracker
{
    struct timeval start;
    struct timeval end;
};

static void my_time_tracker_start(struct my_time_tracker *self);
static void my_time_tracker_end(struct my_time_tracker *self);
static size_t my_time_tracker_elapsed_ms(struct my_time_tracker *self);

static void product_of_two_u64_values_to_u128_bit_byte_array(uint64_t a, uint64_t b, byte_array_u128_t dest);
static void sha256_hash_u128_bit_byte_array(byte_array_u128_t byte_array, sha256_hash_t hash);
static bool is_solution_valid(sha256_hash_t hash, uint8_t number_of_trailing_zeros);

struct my_solution
{
    sha256_hash_t hash;
    uint64_t value;
};

static void my_solution_print(struct my_solution *solution, FILE *stream);

struct my_producer_thread_callback_context
{
    size_t step;
    size_t start_at;
    size_t reset_counter_at;
    uint64_t base;
    uint8_t difficulty;
};

static void my_consumer_callback(mpsc_consumer_t *consumer, void *data, size_t n, bool closed);
static void my_producer_thread_callback(mpsc_producer_t *producer);

static struct my_solution *final_solution = NULL;

int main(void)
{
    struct my_time_tracker time_tracker;
    my_time_tracker_start(&time_tracker);

    mpsc_t *mpsc = mpsc_create((mpsc_create_params_t){
        .buffer_size = sizeof(struct my_solution),
        .n_max_producers = N_WORKERS,
        .consumer_callback = my_consumer_callback,
        .consumer_error_callback = NULL,
        .error_handling_enabled = false,
        .create_and_join_thread_safety_disabled = false,
    });

    struct my_producer_thread_callback_context contexts[N_WORKERS];
    for (size_t i = 0; i < N_WORKERS; i++)
    {
        contexts[i].base = PROBLEM_BASE;
        contexts[i].difficulty = PROBLEM_DIFFICULTY;
        contexts[i].reset_counter_at = N_ITERS_BEFORE_CHANNEL_CHECK;
        contexts[i].start_at = i;
        contexts[i].step = N_WORKERS;
        ASSERT_PRODUCER_REGISTRATION(mpsc_register_producer(mpsc, my_producer_thread_callback, &contexts[i]));
    }

    mpsc_join(mpsc);

    my_time_tracker_end(&time_tracker);

    assert(final_solution != NULL);

    fprintf(
        stdout,
        "\nProof-Of-Work (sha256) result for %zu workers, base = %llu, and difficulty = %llu (time elapsed: %zu ms):\n\n",
        (size_t)N_WORKERS,
        (uint64_t)PROBLEM_BASE,
        (uint64_t)PROBLEM_DIFFICULTY,
        my_time_tracker_elapsed_ms(&time_tracker));
    my_solution_print(final_solution, stdout);
    fprintf(stdout, "\n");

    free(final_solution);

    return 0;
}

static void my_consumer_callback(mpsc_consumer_t *consumer, void *data, size_t n, bool closed)
{
    if (closed)
    {
        return;
    }
    if (n != sizeof(struct my_solution))
    {
        fprintf(stderr, "Invalid message size\n");
        exit(EXIT_FAILURE);
    }
    if (final_solution != NULL)
    {
        free(data);
        return;
    }
    final_solution = data;
    mpsc_consumer_close(consumer);
}

static void my_producer_thread_callback(mpsc_producer_t *producer)
{
    struct my_producer_thread_callback_context *ctx = mpsc_producer_context(producer);
    size_t counter = 0;
    sha256_hash_t hash;
    byte_array_u128_t byte_array;
    for (uint64_t i = ctx->start_at; i < UINT64_MAX; i += ctx->step)
    {
        counter += 1;
        product_of_two_u64_values_to_u128_bit_byte_array(ctx->base, i, byte_array);
        sha256_hash_u128_bit_byte_array(byte_array, hash);
        if (is_solution_valid(hash, ctx->difficulty))
        {
            struct my_solution solution = {.hash = {0}, .value = i};
            memcpy(solution.hash, hash, SHA256_DIGEST_LENGTH);
            mpsc_producer_send(producer, &solution, sizeof(struct my_solution));
            break;
        }
        if (counter == ctx->reset_counter_at)
        {
            counter = 0;
            if (!mpsc_producer_ping(producer))
            {
                break;
            }
        }
    }
}

static void my_solution_print(struct my_solution *solution, FILE *stream)
{
    fprintf(stream, "struct my_solution {\n");
    fprintf(stream, "\t.hash = ");
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        fprintf(stream, "%02x", solution->hash[i]);
    }
    fprintf(stream, ",\n");
    fprintf(stream, "\t.value = %llu\n", solution->value);
    fprintf(stream, "}\n");
}

static void product_of_two_u64_values_to_u128_bit_byte_array(uint64_t a, uint64_t b, byte_array_u128_t dest)
{
    // NOTE: Looping is probably not as efficient has performing the 16
    // individual operations here, but I guess it's OK since this is just
    // an example.
    __uint128_t p = a * b;
    for (size_t i = 0; i < 16; i++)
    {
        size_t n = i * 8;
        dest[16 - i - 1] = (p & (0xff << n)) >> n;
    }
}

static void sha256_hash_u128_bit_byte_array(byte_array_u128_t byte_array, sha256_hash_t hash)
{
    SHA256_CTX sha256;
    ASSERT_SHA_OPERATION(SHA256_Init(&sha256));
    ASSERT_SHA_OPERATION(SHA256_Update(&sha256, byte_array, 16));
    ASSERT_SHA_OPERATION(SHA256_Final(hash, &sha256));
}

static bool is_solution_valid(sha256_hash_t hash, uint8_t number_of_trailing_zeros)
{
    uint8_t counter = 0;
    for (size_t i = SHA256_DIGEST_LENGTH; i > 1; i--)
    {
        uint8_t byte = hash[i - 1];
        if (byte == 0)
        {
            counter += 2;
            if (counter == number_of_trailing_zeros)
            {
                return true;
            }
        }
        else if (
            byte == 0x10 ||
            byte == 0x20 ||
            byte == 0x30 ||
            byte == 0x40 ||
            byte == 0x50 ||
            byte == 0x60 ||
            byte == 0x70 ||
            byte == 0x80 ||
            byte == 0x90 ||
            byte == 0xa0 ||
            byte == 0xb0 ||
            byte == 0xc0 ||
            byte == 0xd0 ||
            byte == 0xe0 ||
            byte == 0xf0)
        {
            counter += 1;
            if (counter == number_of_trailing_zeros)
            {
                return true;
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }
    }
    return false;
}

static void my_time_tracker_start(struct my_time_tracker *self)
{
    ASSERT_GETTIMEOFDAY(gettimeofday(&self->start, NULL));
}

static void my_time_tracker_end(struct my_time_tracker *self)
{
    ASSERT_GETTIMEOFDAY(gettimeofday(&self->end, NULL));
}

static size_t my_time_tracker_elapsed_ms(struct my_time_tracker *self)
{
    size_t diff = (self->end.tv_sec - self->start.tv_sec) * 1000;
    diff += (self->end.tv_usec - self->start.tv_usec) / 1000;
    return diff;
}
