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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpsc.h"

#ifndef MPSC_SRC_FILE_NAME
#define MPSC_SRC_FILE_NAME "mpsc.c"
#endif

static void my_thread_join(pthread_t id);
static void my_mutex_set_lock_state(pthread_mutex_t *mutex, bool state);
static void my_condition_variable_signal(pthread_cond_t *condition_variable);
static void my_condition_variable_wait(pthread_cond_t *condition_variable, pthread_mutex_t *mutex);
static bool my_mutex_init(pthread_mutex_t *mutex, bool handle_errors);
static void my_mutex_destroy(pthread_mutex_t *mutex);
static bool my_condition_variable_init(pthread_cond_t *condition_variable, bool handle_errors);
static void my_condition_variable_destroy(pthread_cond_t *condition_variable);
static bool my_thread_create(pthread_t *id, void *(callback)(void *context), void *context, bool handle_errors);
static void *my_malloc(size_t n, bool handle_errors);
static void my_free(void *pointer);

static void *my_producer_thread_callback(void *context);
static void *my_consumer_thread_callback(void *context);

typedef enum
{
    MPSC_HANDLE_CREATION_FAILURE_NONE = 0,
    MPSC_HANDLE_CREATION_FAILURE_COND_VAR_INIT = 1,
    MPSC_HANDLE_CREATION_FAILURE_MUTEX_INIT = 2,
    MPSC_HANDLE_CREATION_FAILURE_THREAD_CREATE = 3,
} mpsc_handle_creation_failure_type_t;

static void *mpsc_handle_creation_failure(mpsc_t *self, mpsc_handle_creation_failure_type_t type, ssize_t producer_cond_var_index);
static void mpsc_destroy(mpsc_t *self);
static void mpsc_create_params_validate(mpsc_create_params_t *params);
static void mpsc_producer_done(mpsc_producer_t *self);
static size_t mpsc_producer_subscribe_to_wait_queue(mpsc_producer_t *self);
static void mpsc_shift_producer_wait_queue(mpsc_t *self);

struct mpsc_consumer_s
{
    mpsc_t *mpsc;
};

struct mpsc_producer_s
{
    mpsc_t *mpsc;
    void *application_context;
    bool done;
    mpsc_producer_thread_callback_t *callback;
};

struct mpsc_s
{
    size_t buffer_size;
    size_t n_max_producers;
    void *buffer;
    size_t n;
    bool pending_message;
    bool joined;
    bool closed;
    size_t n_producers_closed;
    bool error_handling_enabled;
    bool create_and_join_thread_safety_disabled;
    pthread_t parent_thread_id;
    size_t n_producers_waiting;
    ssize_t next_waiting_producer_id;

    pthread_mutex_t mutex;
    pthread_cond_t condition_variable;

    pthread_t consumer_thread_id;
    mpsc_consumer_callback_t *consumer_callback;
    mpsc_consumer_error_callback_t *consumer_error_callback;
    mpsc_consumer_t consumer;

    pthread_t *producer_thread_ids;
    mpsc_producer_t *producers;
    size_t producer_count;
    pthread_cond_t *producer_condition_variables;
    size_t *producer_waiting_ids_queue;
};

mpsc_t *mpsc_create(mpsc_create_params_t params)
{
    mpsc_create_params_validate(&params);
    mpsc_t *self = my_malloc(sizeof(mpsc_t), params.error_handling_enabled);
    if (self == NULL)
    {
        return mpsc_handle_creation_failure(self, MPSC_HANDLE_CREATION_FAILURE_NONE, -1);
    }
    self->parent_thread_id = pthread_self();
    self->buffer_size = params.buffer_size;
    self->n_max_producers = params.n_max_producers;
    self->consumer_callback = params.consumer_callback;
    self->consumer_error_callback = params.consumer_error_callback;
    self->create_and_join_thread_safety_disabled = params.create_and_join_thread_safety_disabled;
    self->buffer = my_malloc(params.buffer_size, params.error_handling_enabled);
    if (self->buffer == NULL)
    {
        return mpsc_handle_creation_failure(self, MPSC_HANDLE_CREATION_FAILURE_NONE, -1);
    }
    self->n = 0;
    self->n_producers_closed = 0;
    self->producer_thread_ids = my_malloc(sizeof(pthread_t) * params.n_max_producers, params.error_handling_enabled);
    if (self->producer_thread_ids == NULL)
    {
        return mpsc_handle_creation_failure(self, MPSC_HANDLE_CREATION_FAILURE_NONE, -1);
    }
    self->producers = my_malloc(sizeof(mpsc_producer_t) * params.n_max_producers, params.error_handling_enabled);
    if (self->producers == NULL)
    {
        return mpsc_handle_creation_failure(self, MPSC_HANDLE_CREATION_FAILURE_NONE, -1);
    }
    self->producer_condition_variables = my_malloc(sizeof(pthread_cond_t) * params.n_max_producers, params.error_handling_enabled);
    if (self->producer_condition_variables == NULL)
    {
        return mpsc_handle_creation_failure(self, MPSC_HANDLE_CREATION_FAILURE_NONE, -1);
    }
    self->producer_waiting_ids_queue = my_malloc(sizeof(size_t) * params.n_max_producers, params.error_handling_enabled);
    if (self->producer_waiting_ids_queue == NULL)
    {
        return mpsc_handle_creation_failure(self, MPSC_HANDLE_CREATION_FAILURE_NONE, -1);
    }
    self->n_producers_waiting = 0;
    self->next_waiting_producer_id = -1;
    self->consumer.mpsc = self;
    self->producer_count = 0;
    self->joined = false;
    self->closed = false;
    self->pending_message = false;
    self->error_handling_enabled = params.error_handling_enabled;
    //  NOTE: The follow three calls' order is expected by `mpsc_handle_creation_failure`.
    if (!my_condition_variable_init(&self->condition_variable, params.error_handling_enabled))
    {
        return mpsc_handle_creation_failure(self, MPSC_HANDLE_CREATION_FAILURE_COND_VAR_INIT, -1);
    }
    for (size_t i = 0; i < params.n_max_producers; i++)
    {
        if (!my_condition_variable_init(&self->producer_condition_variables[i], params.error_handling_enabled))
        {
            return mpsc_handle_creation_failure(self, MPSC_HANDLE_CREATION_FAILURE_COND_VAR_INIT, i);
        }
    }
    if (!my_mutex_init(&self->mutex, params.error_handling_enabled))
    {
        return mpsc_handle_creation_failure(self, MPSC_HANDLE_CREATION_FAILURE_MUTEX_INIT, -1);
    }

    if (!my_thread_create(&self->consumer_thread_id, my_consumer_thread_callback, self, params.error_handling_enabled))
    {
        return mpsc_handle_creation_failure(self, MPSC_HANDLE_CREATION_FAILURE_THREAD_CREATE, -1);
    }
    return self;
}

void mpsc_join(mpsc_t *self)
{
    my_mutex_set_lock_state(&self->mutex, true);
    if (!self->create_and_join_thread_safety_disabled)
    {
        pthread_t current_thread_id = pthread_self();
        if (current_thread_id != self->parent_thread_id)
        {
            fprintf(
                stderr,
                "%s:%i %s [Fatal Error] 'create_and_join_thread_safety_disabled = false' requires mpsc instance to be created and joined on the same thread.\n",
                MPSC_SRC_FILE_NAME, __LINE__, __func__);
            abort();
        }
    }
    if (self->joined)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] can only be called once per instance\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__);
        abort();
    }
    self->joined = true;
    if (self->producer_count == 0)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] expecting at least one producer\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__);
        abort();
    }
    if (self->producer_count == self->n_producers_closed)
    {
        self->closed = true;
        my_condition_variable_signal(&self->condition_variable);
    }
    my_mutex_set_lock_state(&self->mutex, false);
    my_thread_join(self->consumer_thread_id);
    my_mutex_set_lock_state(&self->mutex, true);
    self->closed = true;
    my_mutex_set_lock_state(&self->mutex, false);
    for (size_t i = 0; i < self->producer_count; i++)
    {
        my_thread_join(self->producer_thread_ids[i]);
    }
    mpsc_destroy(self);
}

mpsc_register_producer_error_t mpsc_register_producer(mpsc_t *self, mpsc_producer_thread_callback_t callback, void *context)
{
    my_mutex_set_lock_state(&self->mutex, true);
    if (self->n_max_producers == self->producer_count)
    {
        my_mutex_set_lock_state(&self->mutex, false);
        return MPSC_REGISTER_PRODUCER_ERROR_N_MAX_PRODUCERS_REACHED;
    }
    if (self->closed)
    {
        my_mutex_set_lock_state(&self->mutex, false);
        return MPSC_REGISTER_PRODUCER_ERROR_CLOSED;
    }
    size_t i = self->producer_count;
    mpsc_producer_t *producer = &self->producers[i];
    producer->mpsc = self;
    producer->application_context = context;
    producer->done = false;
    producer->callback = callback;
    pthread_t *thread_id = &self->producer_thread_ids[i];
    if (!my_thread_create(thread_id, my_producer_thread_callback, producer, self->error_handling_enabled))
    {
        my_mutex_set_lock_state(&self->mutex, false);
        return MPSC_REGISTER_PRODUCER_ERROR_EAGAIN;
    }
    self->producer_count += 1;
    my_mutex_set_lock_state(&self->mutex, false);
    return MPSC_REGISTER_PRODUCER_ERROR_NONE;
}

mpsc_register_producer_error_t mpsc_consumer_register_producer(mpsc_consumer_t *self, mpsc_producer_thread_callback_t callback, void *context)
{
    return mpsc_register_producer(self->mpsc, callback, context);
}

void mpsc_consumer_close(mpsc_consumer_t *self)
{
    my_mutex_set_lock_state(&self->mpsc->mutex, true);
    self->mpsc->closed = true;
    my_condition_variable_signal(&self->mpsc->condition_variable);
    for (size_t i = 0; i < self->mpsc->n_producers_waiting; i++)
    {
        size_t index = self->mpsc->producer_waiting_ids_queue[i];
        my_condition_variable_signal(&self->mpsc->producer_condition_variables[index]);
    }
    my_mutex_set_lock_state(&self->mpsc->mutex, false);
}

bool mpsc_producer_ping(mpsc_producer_t *self)
{
    my_mutex_set_lock_state(&self->mpsc->mutex, true);
    bool ok = true;
    if (self->mpsc->closed)
    {
        ok = false;
    }
    my_mutex_set_lock_state(&self->mpsc->mutex, false);
    return ok;
}

bool mpsc_producer_send(mpsc_producer_t *self, void *data, size_t n)
{
    my_mutex_set_lock_state(&self->mpsc->mutex, true);
    if (n > self->mpsc->buffer_size)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] 'n = %zu' is greater than 'buffer_size = %zu'\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, n, self->mpsc->buffer_size);
        abort();
    }
    if (self->mpsc->closed)
    {
        my_mutex_set_lock_state(&self->mpsc->mutex, false);
        return false;
    }
    // NOTE: Checking for these two conditions here is very important, else
    // some races will occur when we have a waiting producer that gets signaled
    // but at the same time a new message is free of sending because `message_pending = false`.
    // What happens is that the "free" sent message gets overriden by the waiting one.
    if (self->mpsc->pending_message || self->mpsc->next_waiting_producer_id != -1)
    {
        size_t id = mpsc_producer_subscribe_to_wait_queue(self);
        pthread_cond_t *condition_variable = &self->mpsc->producer_condition_variables[id];
        while (
            !self->mpsc->closed &&
            self->mpsc->next_waiting_producer_id != (ssize_t)id)
        {
            my_condition_variable_wait(condition_variable, &self->mpsc->mutex);
        }
        if (self->mpsc->closed)
        {
            my_mutex_set_lock_state(&self->mpsc->mutex, false);
            return false;
        }
        //  NOTE: Technically, once closed, shifting this no longer
        //  helps, because the condition does necessarily mean that
        //  `self->mpsc->next_waiting_producer_id != (ssize_t)id`. On
        //  the other hand, if the while loop breaks because id == producer_id,
        //  then we know shifting is for the appropriate waiting producer.
        mpsc_shift_producer_wait_queue(self->mpsc);
    }
    if (n > 0)
    {
        memcpy(self->mpsc->buffer, data, n);
    }
    self->mpsc->n = n;
    self->mpsc->pending_message = true;
    my_condition_variable_signal(&self->mpsc->condition_variable);
    my_mutex_set_lock_state(&self->mpsc->mutex, false);
    return true;
}

bool mpsc_producer_send_empty(mpsc_producer_t *self)
{
    return mpsc_producer_send(self, NULL, 0);
}

static void mpsc_producer_done(mpsc_producer_t *self)
{
    my_mutex_set_lock_state(&self->mpsc->mutex, true);
    if (!self->done)
    {
        self->done = true;
        self->mpsc->n_producers_closed += 1;
        if (
            self->mpsc->n_producers_closed == self->mpsc->producer_count &&
            self->mpsc->joined)
        {
            self->mpsc->closed = true;
            my_condition_variable_signal(&self->mpsc->condition_variable);
        }
    }
    my_mutex_set_lock_state(&self->mpsc->mutex, false);
}

void *mpsc_producer_context(mpsc_producer_t *self)
{
    return self->application_context;
}

mpsc_register_producer_error_t mpsc_producer_register_producer(mpsc_producer_t *self, mpsc_producer_thread_callback_t callback, void *context)
{
    return mpsc_register_producer(self->mpsc, callback, context);
}

static size_t mpsc_producer_subscribe_to_wait_queue(mpsc_producer_t *self)
{
    ssize_t index = -1;
    for (size_t i = 0; i < self->mpsc->n_max_producers; i++)
    {
        if (&self->mpsc->producers[i] == self)
        {
            index = i;
            break;
        }
    }
    if (index == -1)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] producer %p not found in list\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, (void *)self);
        abort();
    }
    size_t id = (size_t)index;
    if (self->mpsc->n_producers_waiting == self->mpsc->n_max_producers)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] producer 'id = %zu' (%p) has already subscribed to waiting list\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, id, (void *)self);
        abort();
    }
    self->mpsc->producer_waiting_ids_queue[self->mpsc->n_producers_waiting] = id;
    self->mpsc->n_producers_waiting += 1;
    return id;
}

static void mpsc_shift_producer_wait_queue(mpsc_t *self)
{
    if (self->n_producers_waiting == 0)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] empty queue\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__);
        abort();
    }
    for (size_t i = 0; i < self->n_producers_waiting - 1; i++)
    {
        self->producer_waiting_ids_queue[i] = self->producer_waiting_ids_queue[i + 1];
    }
    self->n_producers_waiting -= 1;
    self->next_waiting_producer_id = -1;
}

static void mpsc_destroy(mpsc_t *self)
{
    my_mutex_destroy(&self->mutex);
    my_condition_variable_destroy(&self->condition_variable);
    for (size_t i = 0; i < self->n_max_producers; i++)
    {
        my_condition_variable_destroy(&self->producer_condition_variables[i]);
    }
    my_free(self->producer_condition_variables);
    my_free(self->producer_waiting_ids_queue);
    my_free(self->buffer);
    my_free(self->producer_thread_ids);
    my_free(self->producers);
    my_free(self);
}

static void *mpsc_handle_creation_failure(mpsc_t *self, mpsc_handle_creation_failure_type_t type, ssize_t producer_cond_var_index)
{
    // NOTE: The `ssize_t producer_cond_var_index` argument was added later on, along with the
    // new `self->producer_condition_variables` array, to be able to indicate up to which
    // to destroy, if needed. That said, this function could be refactored to take
    // a structure of settings for the second argument, instead of having `producer_cond_var_index`, but
    // it's OK for now I guess. But if other arguments are needed in the future, this should be
    // converted to a structure...
    int custom_errno;
    switch (errno)
    {
    case ENOMEM:
        custom_errno = ENOMEM;
        break;
    case EAGAIN:
        custom_errno = EAGAIN;
        break;
    default:
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] 'errno = %i' no supported by this call\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, errno);
        abort();
    }
    switch (type)
    {
    case MPSC_HANDLE_CREATION_FAILURE_THREAD_CREATE:
        my_mutex_destroy(&self->mutex);
        my_condition_variable_destroy(&self->condition_variable);
        for (size_t i = 0; i < self->n_max_producers; i++)
        {
            my_condition_variable_destroy(&self->producer_condition_variables[i]);
        }
        break;
    case MPSC_HANDLE_CREATION_FAILURE_MUTEX_INIT:
        my_condition_variable_destroy(&self->condition_variable);
        for (size_t i = 0; i < self->n_max_producers; i++)
        {
            my_condition_variable_destroy(&self->producer_condition_variables[i]);
        }
        break;
    case MPSC_HANDLE_CREATION_FAILURE_COND_VAR_INIT:
        if (producer_cond_var_index != -1)
        {
            // This means that the main condition variable has been initialized, and must
            // therefore be destroyed.
            my_condition_variable_destroy(&self->condition_variable);
            // NOTE: If `producer_cond_var_index = 0`, then it means that
            // failure occurred for that condition variable, so we don't
            // destroy it since it was never created.
            for (size_t i = 0; i < (size_t)producer_cond_var_index; i++)
            {
                my_condition_variable_destroy(&self->producer_condition_variables[i]);
            }
        }
        break;
    default:
        break;
    }
    if (self->producer_condition_variables != NULL)
    {
        my_free(self->producer_condition_variables);
    }
    if (self->producer_waiting_ids_queue != NULL)
    {
        my_free(self->producer_waiting_ids_queue);
    }
    if (self->producers != NULL)
    {
        my_free(self->producers);
    }
    if (self->producer_thread_ids != NULL)
    {
        my_free(self->producer_thread_ids);
    }
    if (self->buffer != NULL)
    {
        my_free(self->buffer);
    }
    if (self != NULL)
    {
        my_free(self);
    }
    errno = custom_errno;
    return NULL;
}

static void mpsc_create_params_validate(mpsc_create_params_t *params)
{
    if (params->consumer_callback == NULL)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] invalid 'consumer_callback = NULL'\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__);
        abort();
    }
    if (
        params->error_handling_enabled &&
        params->consumer_error_callback == NULL)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] invalid 'consumer_error_callback = NULL'; must be present when 'error_handling_enabled = true'\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__);
        abort();
    }
    if (params->n_max_producers == 0)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] invalid 'n_max_producers = 0'; requires at least 1\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__);
        abort();
    }
}

static void *my_producer_thread_callback(void *context)
{
    mpsc_producer_t *producer = (mpsc_producer_t *)context;
    (producer->callback)(producer);
    mpsc_producer_done(producer);
    return NULL;
}

static void *my_consumer_thread_callback(void *context)
{
    mpsc_t *mpsc = (mpsc_t *)context;
    pthread_mutex_t *mutex = &mpsc->mutex;
    pthread_cond_t *condition_variable = &mpsc->condition_variable;
    mpsc_consumer_callback_t *callback = mpsc->consumer_callback;
    mpsc_consumer_error_callback_t *error_callback = mpsc->consumer_error_callback;
    bool error_handling_enabled = mpsc->error_handling_enabled;
    while (true)
    {
        my_mutex_set_lock_state(mutex, true);
        while (
            !mpsc->pending_message &&
            !mpsc->closed)
        {
            my_condition_variable_wait(condition_variable, mutex);
        }
        if (
            mpsc->closed &&
            !mpsc->pending_message) // NEW: If we have a pending message, we deliver it first
        {
            my_mutex_set_lock_state(mutex, false);
            break;
        }
        size_t n = mpsc->n;
        void *buffer = NULL;
        if (n > 0)
        {
            buffer = my_malloc(mpsc->n, error_handling_enabled);
            if (buffer == NULL)
            {
                mpsc->n = 0;
                mpsc->pending_message = false;
                my_mutex_set_lock_state(mutex, false);
                // IMPORTANT: don't hold the lock while calling the callback!
                (error_callback)(&mpsc->consumer);
                continue;
            }
            memcpy(buffer, mpsc->buffer, n);
        }
        mpsc->n = 0;
        mpsc->pending_message = false;
        if (mpsc->n_producers_waiting > 0 && !mpsc->closed)
        {
            size_t id = mpsc->producer_waiting_ids_queue[0];
            mpsc->next_waiting_producer_id = id;
            my_condition_variable_signal(&mpsc->producer_condition_variables[id]);
        }
        my_mutex_set_lock_state(mutex, false);
        // IMPORTANT: don't hold the lock while calling the callback!
        (callback)(&mpsc->consumer, buffer, n, false);
    }
    // IMPORTANT: don't hold the lock while calling the callback!
    (callback)(&mpsc->consumer, NULL, 0, true);
    return NULL;
}

static void my_thread_join(pthread_t id)
{
    int reason_code = pthread_join(id, NULL);
    if (reason_code != 0)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] call to pthread_join failed with code = %i\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, reason_code);
        abort();
    }
}

static void my_mutex_set_lock_state(pthread_mutex_t *mutex, bool state)
{
    if (state)
    {
        int reason_code = pthread_mutex_lock(mutex);
        if (reason_code != 0)
        {
            fprintf(
                stderr,
                "%s:%i %s [Fatal Error] call to pthread_mutex_lock failed with code = %i\n",
                MPSC_SRC_FILE_NAME, __LINE__, __func__, reason_code);
            abort();
        }
    }
    else
    {
        int reason_code = pthread_mutex_unlock(mutex);
        if (reason_code != 0)
        {
            fprintf(
                stderr,
                "%s:%i %s [Fatal Error] call to pthread_mutex_unlock failed with code = %i\n",
                MPSC_SRC_FILE_NAME, __LINE__, __func__, reason_code);
            abort();
        }
    }
}

static void my_condition_variable_signal(pthread_cond_t *condition_variable)
{
    int reason_code = pthread_cond_signal(condition_variable);
    if (reason_code != 0)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] call to pthread_cond_signal failed with code = %i\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, reason_code);
        abort();
    }
}

static void my_condition_variable_wait(pthread_cond_t *condition_variable, pthread_mutex_t *mutex)
{
    int reason_code = pthread_cond_wait(condition_variable, mutex);
    if (reason_code != 0)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] call to pthread_cond_wait failed with code = %i\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, reason_code);
        abort();
    }
}

static bool my_mutex_init(pthread_mutex_t *mutex, bool handle_errors)
{
    int reason_code = pthread_mutex_init(mutex, NULL);
    if (reason_code != 0)
    {
        if (
            handle_errors &&
            reason_code == ENOMEM)
        {
            errno = reason_code;
            return false;
        }
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] call to pthread_mutex_init failed with code = %i\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, reason_code);
        abort();
    }
    return true;
}

static void my_mutex_destroy(pthread_mutex_t *mutex)
{
    int reason_code = pthread_mutex_destroy(mutex);
    if (reason_code != 0)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] call to pthread_mutex_destroy failed with code = %i\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, reason_code);
        abort();
    }
}

static bool my_condition_variable_init(pthread_cond_t *condition_variable, bool handle_errors)
{
    int reason_code = pthread_cond_init(condition_variable, NULL);
    if (reason_code != 0)
    {
        if (
            handle_errors &&
            (reason_code == ENOMEM || reason_code == EAGAIN))
        {
            errno = reason_code;
            return false;
        }
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] call to pthread_cond_init failed with code = %i\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, reason_code);
        abort();
    }
    return true;
}

static void my_condition_variable_destroy(pthread_cond_t *condition_variable)
{
    int reason_code = pthread_cond_destroy(condition_variable);
    if (reason_code != 0)
    {
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] call to pthread_cond_destroy failed with code = %i\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, reason_code);
        abort();
    }
}

static bool my_thread_create(pthread_t *id, void *(callback)(void *context), void *context, bool handle_errors)
{
    int reason_code = pthread_create(id, NULL, callback, context);
    if (reason_code != 0)
    {
        if (
            handle_errors &&
            reason_code == EAGAIN)
        {
            errno = reason_code;
            return false;
        }
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] call to pthread_create failed with code = %i\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, reason_code);
        abort();
    }
    return true;
}

static void *my_malloc(size_t n, bool handle_errors)
{
    void *pointer = malloc(n);
    if (pointer == NULL)
    {
        if (
            handle_errors &&
            errno == ENOMEM)
        {
            return NULL;
        }
        fprintf(
            stderr,
            "%s:%i %s [Fatal Error] call to malloc failed with 'strerror = %s'\n",
            MPSC_SRC_FILE_NAME, __LINE__, __func__, strerror(errno));
        abort();
    }
    return pointer;
}

static void my_free(void *pointer)
{
    free(pointer);
}
