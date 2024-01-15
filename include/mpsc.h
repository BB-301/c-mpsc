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

/**
 * @file
 */

#ifndef _MPSC_H_
#define _MPSC_H_

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief The type returned by \ref mpsc_register_producer (as well as by its
 * aliases; i.e., \ref mpsc_producer_register_producer and \ref mpsc_consumer_register_producer )
 * when trying to register a new producer on a \ref mpsc_t instance, and which indicates whether an error
 * occurred.
 * @see mpsc_register_producer, mpsc_consumer_register_producer, mpsc_producer_register_producer
 */
typedef enum
{
    /**
     * @brief The producer was successfully registered.
     */
    MPSC_REGISTER_PRODUCER_ERROR_NONE = 0,
    /**
     * @brief The producer could not be registered because the \ref mpsc_t instance
     * has internally been marked as `closed` (i.e., the channel has been closed).
     */
    MPSC_REGISTER_PRODUCER_ERROR_CLOSED = 1,
    /**
     * @brief The producer could not be registered because the maximum number of
     * producers allowed (i.e., `n_max_producers`) in the \ref mpsc_t instance has
     * been reached.
     */
    MPSC_REGISTER_PRODUCER_ERROR_N_MAX_PRODUCERS_REACHED = 2,
    /**
     * @brief The producer could not be registered because a \ref EAGAIN error was
     * observed when, internally, while trying to create a new thread using \ref pthread_create .
     * @note When \ref mpsc_create_params_t 's `error_handling_enabled` is set to `false`,
     * this error will not be returned and will instead result in the process being terminated.
     */
    MPSC_REGISTER_PRODUCER_ERROR_EAGAIN = 3
} mpsc_register_producer_error_t;

/**
 * @brief An opaque data type used as a container for the MPSC channel data.
 * @see mpsc_create, mpsc_join
 * @par Example:
 * @include examples/quick_example.c
 */
typedef struct mpsc_s mpsc_t;

/**
 * @brief An opaque data type used as a container for the MPSC channel's
 * consumer.
 */
typedef struct mpsc_consumer_s mpsc_consumer_t;

/**
 * @brief An opaque data type used as a container for a MPSC channel's
 * producer.
 */
typedef struct mpsc_producer_s mpsc_producer_t;

/**
 * @brief The signature of the producer thread callback function, to be declared and
 * implemented by the application, which is passed as a parameter to the \ref mpsc_register_producer
 * function when registering a producer for a given \ref mpsc_t instance.
 * @param producer A pointer to a \ref mpsc_producer_t instance for which the callback
 * is being executed.
 * @see mpsc_register_producer, mpsc_consumer_register_producer, mpsc_producer_register_producer
 */
typedef void(mpsc_producer_thread_callback_t)(mpsc_producer_t *producer);

/**
 * @brief The signature of the consumer callback function, to be declared and
 * implemented by the application, which is passed as a parameter to the \ref mpsc_create function
 * when instantiating a new channel.
 * @param consumer A pointer to a \ref mpsc_consumer_t instance for which the callback
 * is being executed.
 * @param data A pointer to dynamically allocated memory containing the message sent
 * by a producer to the consumer.
 * @param n The size (in bytes) of \p data .
 * @param closed A boolean value indicating whether the channel (i.e., the \ref mpsc_t instance)
 * has been marked as closed, in which case the callback won't be called again for the
 * given \ref mpsc_t instance.
 * @see mpsc_create, mpsc_create_params_t
 * @warning When \p n is non-zero, \p data refers to dynamically allocated memory that
 * is the responsibility of the callback. In other words, as soon as \p data is no longer
 * needed, it should be freed using \ref free , else memory will be leaked.
 * @note - There are two scenarios that can cause the \p closed argument to be `true`: (1) the
 * \ref mpsc_consumer_close function was called on \p consumer from inside the callback or (2) the
 * \ref mpsc_join function has been called on the channel object and all producer threads have
 * returned.
 * @note - The callback should return as quick as possible to avoid blocking the consumer thread.
 Blocking tasks, if required, should be offloaded to other threads and the callback should return
 as quick as possible.
 * @note - When \p n is set to 0, \p data will be set to \ref NULL .
 */
typedef void(mpsc_consumer_callback_t)(mpsc_consumer_t *consumer, void *data, size_t n, bool closed);

/**
 * @brief The signature of an optional consumer error callback function, to be declared and
 * implemented by the application, which is passed as a parameter to the \ref mpsc_create function when
 * instantiating a new channel.
 * @see mpsc_create, mpsc_create_params_t
 * @note - This callback is ignored when \ref mpsc_create_params_t 's
 * `error_handling_enabled` is set to `false`, in which case, when an error occurs,
 * information about that error is printed to \ref stderr and the process is terminated.
 * @note - When executed for \p consumer (i.e., the specific \ref mpsc_consumer_t instance),
 * the application should look at \ref errno for information about the encountered error. Currently,
 * the only possible error is \ref ENOMEM, which arises from a failed internal call to \ref malloc .
 */
typedef void(mpsc_consumer_error_callback_t)(mpsc_consumer_t *consumer);

/**
 * @brief The structure that must be passed to \ref mpsc_create to instantiate
 * a new \ref mpsc_t object.
 * @see mpsc_create
 */
typedef struct
{
    /**
     * @brief The size (in bytes) of the internal buffer used to
     * transfer a message between a producer and the consumer.
     * @note - This acts as the upper bound of a message size for the
     * \ref mpsc_t instance. Calling \ref mpsc_producer_send with
     * a message of size exceeding this value will cause the process
     * to be terminated, regardless of `error_handling_enabled`'s value.
     * @note - This value can be set to 0 if the application only requires
     * empty messages, or if it does not need to send messages at all.
     * @warning In this context, the argument name `buffer_size` should not
     * be confused with the term buffer in the sense of a "buffered channel",
     * for which the term could correspond to the number of messages that can
     * be held inside an internal message queue to be delivered to the consumer.
     * Currently, this library only allows sending one message at the time, in
     * a blocking manner. In other words, the \ref mpsc_producer_send and
     * \ref mpsc_producer_send_empty functions are blocking functions.
     */
    size_t buffer_size;
    /**
     * @brief The maximum number of producers that can be registered
     * on the \ref mpsc_t instance.
     * @note - This value must be greater than 0, else the process will
     * be terminated.
     * @note - Calling \ref mpsc_register_producer when this value has
     * been reached will result in a \ref mpsc_register_producer_error_t
     * error of type \ref MPSC_REGISTER_PRODUCER_ERROR_N_MAX_PRODUCERS_REACHED .
     */
    size_t n_max_producers;
    /**
     * @brief The application defined consumer callback function to be
     * used to received messages for the \ref mpsc_t instance.
     */
    mpsc_consumer_callback_t *consumer_callback;
    /**
     * @brief An optional, application defined producer thread callback function
     * used, when `error_handling_enabled = true`, to deliver potential internal errors
     * to the application, in which case \ref errno should be checked for more information.
     * @note As explained in the \ref mpsc_consumer_error_callback_t documentation, the
     * only possible such error at the moment is \ref ENOMEM , which could arise from a failed
     * internal call to \ref malloc to allocate space where to copy the message from the internal
     * buffer before passing that new memory to the \p consumer_callback function's \p data argument.
     */
    mpsc_consumer_error_callback_t *consumer_error_callback;
    /**
     * @brief A boolean value indicating whether (`true`) error handling should be handed over
     * to the application or whether (`false`) errors should be printed to \ref stderr and the
     * process be terminated.
     * @note Not all errors can be handled, but those that can may occur at three different levels:
     * 1. When calling \ref mpsc_create (the returned value could be the \ref NULL pointer, in
     * which case \ref errno should be checked for details about the error, which will be related
     * to resources exhaustion; i.e., \ref ENOMEM or \ref EAGAIN ).
     * 2. When calling \ref mpsc_register_producer, as well as the \ref mpsc_consumer_register_producer
     * and \ref mpsc_producer_register_producer aliases (which will allow the \ref mpsc_register_producer_error_t
     * error of type \ref MPSC_REGISTER_PRODUCER_ERROR_EAGAIN to be returned, instead of terminating the process).
     * 3. As a result of the internal consumer thread not being able to allocate memory needed to hold a copy of
     * the message temporarily stored inside the internal buffer by a producer for delivery to the consumer
     * callback. Such an error will be communicated to the application through the \p consumer_error_callback
     * when `error_handling_enabled = true`, else will result in the process being terminated.
     */
    bool error_handling_enabled;
    /**
     * @brief A boolean value that can be used to disable the safety feature
     * that prevents \ref mpsc_create and \ref mpsc_join to be called on
     * distinct threads for the same \ref mpsc_t instance.
     */
    bool create_and_join_thread_safety_disabled;
} mpsc_create_params_t;

/**
 * @brief The function used to create a new channel instance (i.e., a \ref mpsc_t instance).
 * @param params The instance's configurations. (Note: See documentation for \ref mpsc_create_params_t
 * for the details).
 * @return \ref mpsc_t* A pointer to the created \ref mpsc_t object, or the \ref NULL pointer if an
 * error occurred during instantiation.
 * @note If an error occurs when \ref mpsc_create_params_t 's `error_handling_enabled = false`,
 * information about the error will be printed to \ref stderr and the process will be terminated,
 * in which case the returned value doesn't need assertion. If, on the other hand, `error_handling_enabled = true`,
 * then \ref NULL will be returned and a handleable error will be available on \ref errno . It should be noted, however,
 * that not all errors can be handled by the application: some errors will always, regardless of `error_handling_enabled`'s value,
 * result in the process being terminated. Currently, the only errors that can be handled by the application are
 * those related to resources exhaustion (i.e., \ref ENOMEM or \ref EAGAIN ), which, internally, can occur when calling
 * \ref malloc , \ref pthread_mutex_init , \ref pthread_cond_init , or \ref pthread_create .
 */
mpsc_t *mpsc_create(mpsc_create_params_t params);

/**
 * @brief The function that must be called on \p self to wait for the channel close.
 * @note - Internally, this function will join the internal consumer thread. Once joined,
 * it will set the internal channel state to closed, and then will join all registered
 * producer threads. Once all internal threads have been joined, \p self 's internal
 * resources will be destroyed and the memory freed.
 * @note - In most applications, this function should be called on the same thread as
 * the thread that was used to instantiate \p self (i.e., the \ref mpsc_t object).
 * @param self A pointer to the \ref mpsc_t instance to be joined.
 */
void mpsc_join(mpsc_t *self);

/**
 * @brief The function used to register a new producer for \p self .
 * @param self A pointer to the \ref mpsc_t instance for which to register a new producer.
 * @param callback An application defined thread callback function, which conforms to the \ref mpsc_producer_thread_callback_t
 * interface, to be used by the producer.
 * @param context An application defined context object that can be retrieved from inside \p callback
 * by calling the \ref mpsc_producer_context function on the \p callback 's \ref mpsc_producer_t argument.
 * @return \ref mpsc_register_producer_error_t A value used to report a potential error with the call. Please
 * read the documentation for \ref mpsc_register_producer_error_t for more information about the potential errors.
 * A successful call will return \ref MPSC_REGISTER_PRODUCER_ERROR_NONE .
 * @see mpsc_consumer_register_producer, mpsc_producer_register_producer
 */
mpsc_register_producer_error_t mpsc_register_producer(mpsc_t *self, mpsc_producer_thread_callback_t callback, void *context);

/**
 * @brief An alias for \ref mpsc_register_producer , but which is used on an object of
 * type \ref mpsc_consumer_t , to try to register a producer for \p self 's parent channel object.
 * @param self A pointer to the \ref mpsc_consumer_t object for whose parent (i.e., a \ref mpsc_t instance)
 * to register a new producer.
 * @param callback An application defined thread callback function, which conforms to the \ref mpsc_producer_thread_callback_t
 * interface, to be used by the producer.
 * @param context An application defined context object that can be retrieved from inside \p callback
 * by calling the \ref mpsc_producer_context function on the \p callback 's \ref mpsc_producer_t argument.
 * @return \ref mpsc_register_producer_error_t A value used to report a potential error with the call. Please
 * read the documentation for \ref mpsc_register_producer_error_t for more information about the potential errors.
 * A successful call will return \ref MPSC_REGISTER_PRODUCER_ERROR_NONE .
 * @see mpsc_register_producer, mpsc_producer_register_producer
 * @note This function exists for situations in which a consumer would like to register other producers to the same
 * channel.
 */
mpsc_register_producer_error_t mpsc_consumer_register_producer(mpsc_consumer_t *self, mpsc_producer_thread_callback_t callback, void *context);

/**
 * @brief A function that can be used (from inside the application defined consumer callback
 * implementing \ref mpsc_consumer_callback_t ) on the consumer object \p self to notify the channel's
 * internal consumer thread that it should return.
 * @param self A pointer to a \ref mpsc_consumer_t instance whose parent object (a \ref mpsc_t instance)
 * should be marked as closed.
 * @note - Before returning, the internal consumer thread will call the \ref mpsc_consumer_callback_t one
 * last time with the `closed` argument marked as `true`.
 * @note - The \ref mpsc_consumer_callback_t could also receive a call with `closed = true` if all producer
 * threads have returned and \ref mpsc_join has been called.
 */
void mpsc_consumer_close(mpsc_consumer_t *self);

/**
 * @brief A function that can be used from inside a producer thread callback to check whether
 * the channel to which \p self belongs is still opened.
 * @param self A pointer to the \ref mpsc_producer_t instance for which to check whether the
 * channel is still open.
 * @return \ref bool A boolean value indicating whether the underlying channel is still open (`true`)
 * or whether it has been marked as closed (`false`).
 * @note A producer thread callback function should return when its channel has been marked as closed,
 * to avoid making the application's call to \ref mpsc_join hang.
 * @see mpsc_producer_send, mpsc_producer_send_empty
 */
bool mpsc_producer_ping(mpsc_producer_t *self);

/**
 * @brief The function used (from inside a producer thread callback function) to send a
 * message to the channel's consumer.
 * @param self A pointer to the \ref mpsc_producer_t instance for which to send a message
 * down the underlying channel, to be delivered to the consumer.
 * @param data A pointer to arbitrary bytes ( \p n  bytes) to be sent to the channel's consumer.
 * @param n the message size, in bytes.
 * @return \ref bool A boolean value indicating whether the message was accepted or not. `false`
 * means that the channel has been marked as closed and that, as a consequence, the message could not be delivered.
 * `true` means that the message was successfully copied to the internal buffer and will eventually be picked up
 * and copied by the internal consumer thread for delivery to the consumer callback.
 * @note - The message size (i.e., \p n ) should never be greater than \ref mpsc_create_params_t 's
 * the `buffer_size` parameter value (which was specified when creating the channel using \ref mpsc_create ).
 * If `n > buffer_size`, an error message will be printed to \ref stderr and the process will be
 * terminated.
 * @note - It is possible to send en empty message using `data = NULL` and `n = 0`, although there
 * is a function called \ref mpsc_producer_send_empty that can be used for that purpose.
 * @note - The \p n bytes from \p data are temporarily copied to the internal buffer. So, if the
 * producer thread callback function implementation uses dynamic memory allocation for \p data ,
 * it must call \ref free() on that memory when it's no longer needed to avoid a leak.
 * @note - In some applications, a producer will not need to send messages to the consumer, and will
 * simply perform a job and return. Once all producer thread callback functions have returned for
 * a particular \ref mpsc_t object, the consumer callback (i.e., \ref mpsc_consumer_callback_t ) will be
 * called one last time with its `closed` argument set to `true`, and the call to \ref mpsc_join will
 * return.
 * @see mpsc_producer_ping, mpsc_producer_send_empty
 */
bool mpsc_producer_send(mpsc_producer_t *self, void *data, size_t n);

/**
 * @brief Similar to \ref mpsc_producer_send , except that this function is used (from
 * inside a producer thread callback function) to send an empty message.
 * @param self A pointer to the \ref mpsc_producer_t instance for which to send a message
 * down the underlying channel, to be delivered to the consumer.
 * @return \ref bool A boolean value indicating whether the message was accepted or not. `false`
 * means that the channel has been marked as closed and that, as a consequence, the message could not be delivered.
 * `true` means that the message was accepted and will eventually be picked up
 * by the internal consumer thread for delivery to the consumer callback.
 * @note In some applications, a producer will not need to send messages to the consumer, and will
 * simply perform a job and return. Once all producer thread callback functions have returned for
 * a particular \ref mpsc_t object, the consumer callback (i.e., \ref mpsc_consumer_callback_t ) will be
 * called one last time with its `closed` argument set to `true`, and the call to \ref mpsc_join will
 * return.
 * @see mpsc_producer_ping, mpsc_producer_send
 */
bool mpsc_producer_send_empty(mpsc_producer_t *self);

/**
 * @brief A function that can be used from inside the producer thread callback function
 * to retrieve the application defined context object passed to \ref mpsc_register_producer
 * when the producer was registered.
 * @param self A pointer to the \ref mpsc_producer_t instance for which to retrieve the application
 * defined context (i.e., the user context).
 * @return \ref void* A pointer to arbitrary memory defined by the application, which contains the
 * "user context object".
 */
void *mpsc_producer_context(mpsc_producer_t *self);

/**
 * @brief An alias for \ref mpsc_register_producer , but which is used on an object of
 * type \ref mpsc_producer_t , to try to register a producer for \p self 's parent channel object.
 * @param self A pointer to the \ref mpsc_producer_t object for whose parent (i.e., a \ref mpsc_t instance)
 * to register a new producer.
 * @param callback An application defined thread callback function, which conforms to the \ref mpsc_producer_thread_callback_t
 * interface, to be used by the producer.
 * @param context An application defined context object that can be retrieved from inside \p callback
 * by calling the \ref mpsc_producer_context function on the \p callback 's \ref mpsc_producer_t argument.
 * @return \ref mpsc_register_producer_error_t A value used to report a potential error with the call. Please
 * read the documentation for \ref mpsc_register_producer_error_t for more information about the potential errors.
 * A successful call will return \ref MPSC_REGISTER_PRODUCER_ERROR_NONE .
 * @see mpsc_register_producer, mpsc_producer_register_producer
 * @note This function exists for situations in which a producer would like to register other producers to the same
 * channel.
 */
mpsc_register_producer_error_t mpsc_producer_register_producer(mpsc_producer_t *self, mpsc_producer_thread_callback_t callback, void *context);

#endif
