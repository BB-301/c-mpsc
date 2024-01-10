<h1>lib&ltmpsc&gt</h1>

**A C POSIX multi-thread based multiple producers, single consumer (MPSC) channel library.**

## Introduction

`lib<mpsc>` is a C library that exposes a simple API of an attempt at a single process, multi-thread based multiple producers, single consumer (MPSC) channel implementation, which is based on [POSIX's pthread](https://en.wikipedia.org/wiki/Pthreads) facilities, making use of types such as `pthread_t`, `pthread_mutex_t`, and `pthread_cond_t` to ensure proper and efficient message synchronization.

### But why this library?

I created this library as a learning activity, while trying to see if I could implement something similar to Rust's [std::sync::mpsc](https://doc.rust-lang.org/std/sync/mpsc) module in C. The result turned out to be something that is not directly comparable to the Rust Standard Library's `mpsc` module, but nonetheless turned out to be an insightful as well as enjoyable exercise!

## A quick example

```c
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
```

**NOTE**: There are other examples in the [examples](./examples) directory.

## How it works

At a high level, a `mpsc_t` object (i.e., a channel) is instantiated using the `mpsc_create` function, which accepts a structure of type `mpsc_create_params_t` as argument, which contains the configurations for the new channel. The next step is to register producers, before finally "joining" (using `mpsc_join`) the channel. Note that, for most applications, the channel should be created an joined on the same thread (e.g., the main application thread).

Internally, a new thread is created for each registered producer, and an additional thread is created for the consumer. The consumer thread uses a `mutex` and a `condition variable` to wait on a message, which message is signaled by a producer by updating the condition variable's state (and possibly first updating the internal data buffer with data, if the message has data). The internal data buffer is protected by a mutex, and contains a memory independent copy of the message sent by the producer (in other words, a message sent by a producer, using `mpsc_producer_send`, has its data copied to the internal buffer).

When the internal consumer thread gets notified of a new message, it creates a copy of the message and passes that copy as argument to the consumer callback, which callback has been implemented by the application (note: that means that the consumer callback gets executed on the internal consumer thread; not the thread calling `mpsc_join`). But before calling the application defined consumer callback, the consumer thread first resets the condition variable and releases the mutex, so that other producers can send new messages.

A channel can be closed in two ways. First, the consumer callback function can itself request that the channel be closed by calling `mpsc_consumer_close` on the `mpsc_consumer_t` object it receives as argument. Alternatively, provided that `mpsc_join` has been called on the channel object, the channel will close as soon as all producer thread callback functions have returned. In either case, the application defined consumer callback will receive a last message with its `closed` argument set to `true`, and the callback will no longer be called for that particular channel object.

It should be noted, however, that the call to `mpsc_join` will hang, even when the channel closure has been requested by the consumer object, until all producer thread callback functions have returned. The `mpsc_producer_ping` function exists so that a producer that is used as a worker can periodically check to make sure the channel is still open, and, if not, return immediately. This is illustrated by both the [the_first_wins.c](./examples/the_first_wins.c) and the [proof_of_work.c](./examples/proof_of_work.c) examples.

Once the internal consumer thread and all the internal producer threads have been joined (which happens inside the `mpsc_join` call), the `mpsc_t` object will be cleaned up (i.e., destroyed), and the call to `mpsc_join` will return, marking the end of the channel's life cycle.

### Advanced details

For more details, you may have a look at the library's [official API documentation website](https://bb-301.github.io/c-mpsc-docs). I also encourage the interested reader to inspect the source code directly, by looking at [src/mpsc.c](src/mpsc.c).

## Files and directories explained

* [doxygen](./doxygen) — A directory that contains [Doxygen](https://github.com/doxygen/doxygen)-related stuff used to generate the [API documentation website](https://bb-301.github.io/c-mpsc-docs) for this library.
* [examples](./examples) — A directory that contains standalone examples illustrating how the library's different features can be used. The [Makefile](./Makefile) declares a recipe for each example. For instance, to run [examples/quick_example.c](examples/quick_example.c) simply run `make example_quick_example` (without the `.c` extension at the end of the file name).
* [include](./include) — A directory that contains the header file [mpsc.h](./include/mpsc.h); i.e., the declarations for the library's public API.
* [src](./src) — A directory that contains the implementation file [mpsc.c](./src/mpsc.c), in which all of the definitions for the functions and types declared in [mpsc.h](./include/mpsc.h) are provided.
* [LICENSE](./LICENSE) — A file containing the license and copyright information for this project.
* [Makefile](./Makefile) — A `Makefile` (for use with [GNU Make](https://www.gnu.org/software/make/)), which is provided as a convenience, and which can be used to automate operations such as building the library, running the examples, building the API documentation website, and installing/uninstalling the library on the target system. You may run `make` or `make help` for a list of all relevant recipes. **WARNING**: If you ever decide to use `make install`, please first make sure that `/usr/local/{lib|include|man}` are valid installation paths on your system, and, if not, make sure to adjust them first.
* [VERSION](./VERSION) — A simple text file that contains the library's current version. This is used by the [Makefile](./Makefile) to generate the documentation website and to "suffix" the library binaries with the current version number.

## Disclaimer

This is an experimental library, so please use with caution, and please feel free to let [me know](#contact) if you find anything wrong in my implementation.

## Roadmap

* I would like to identify more real life applications for which this library could be useful and add those as examples to the [examples](./examples) directory.
* I just realized, at the moment of publication of this first version of the project, that there is currently no way for a consumer callback to know for which `mpsc_t` object it is being executed (except than to use a callback for no more than only one `mpsc_t` instance by application). That means that, for an application using multiple channel instances, and for which knowledge about the channel object for which the callback is being executed would be required, that application would have to implement an individual consumer callback function for each channel, even if the callback implementation is the same across some of the channels. This could easily be solved by allowing to store arbitrary application data in the `mpsc_t` object at instantiation time, and by providing a simple function to be able to retrieve said data from inside the callback (e.g., `void *mpsc_consumer_channel_context(mpsc_consumer_t *self)`). A similar function to be used with the producer object could also be added (e.g., `void *mpsc_producer_channel_context(mpsc_producer_t *self)`).

## Contact

If you have any questions, if you find bugs, or if you have suggestions for this project, please feel free to contact me by opening an issue on the [repository](https://github.com/BB-301/c-mpsc/issues).

## License

This project is released under the [MIT License](./LICENSE).

## Copyright

Copyright (c) 2024 BB-301 (fw3dg3@gmail.com)