/**
 * @file
 *
 * @brief Gets Scheduling Policy and Parameters of Individual Threads
 * @ingroup POSIXAPI
 */

/*
 *  13.5.2 Dynamic Thread Scheduling Parameters Access,
 *         P1003.1c/Draft 10, p. 124
 *
 *  COPYRIGHT (c) 1989-2014.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include <errno.h>

#include <rtems/posix/pthreadimpl.h>
#include <rtems/posix/pthreadattrimpl.h>
#include <rtems/posix/priorityimpl.h>
#include <rtems/score/schedulerimpl.h>
#include <rtems/score/threadimpl.h>

int pthread_getschedparam(
  pthread_t           thread,
  int                *policy,
  struct sched_param *param
)
{
  Thread_Control          *the_thread;
  Thread_queue_Context     queue_context;
  POSIX_API_Control       *api;
  const Scheduler_Control *scheduler;
  Priority_Control         priority;

  if ( policy == NULL || param == NULL ) {
    return EINVAL;
  }

  _Thread_queue_Context_initialize( &queue_context );
  the_thread = _Thread_Get( thread, &queue_context.Lock_context.Lock_context );

  if ( the_thread == NULL ) {
    return ESRCH;
  }

  api = the_thread->API_Extensions[ THREAD_API_POSIX ];

  _Thread_Wait_acquire_critical( the_thread, &queue_context );

  *policy = api->schedpolicy;
  scheduler = _Thread_Scheduler_get_home( the_thread );
  _POSIX_Threads_Get_sched_param_sporadic( the_thread, api, scheduler, param );
  priority = the_thread->Real_priority.priority;

  _Thread_Wait_release( the_thread, &queue_context );

  param->sched_priority = _POSIX_Priority_From_core( scheduler, priority );
  return 0;
}
