/*
 * Copyright (c) 1997 John Birrell <jb@cimlogic.com.au>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by John Birrell.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <signal.h>
#include <sys/param.h>
#include <sys/signalvar.h>
#include <errno.h>
#ifdef _THREAD_SAFE
#include <pthread.h>
#include "pthread_private.h"

int
sigwait(const sigset_t * set, int *sig)
{
	int		ret = 0;
	int		i;
	sigset_t	tempset, waitset;
	struct sigaction act;
	
	_thread_enter_cancellation_point();
	/*
	 * Specify the thread kernel signal handler.
	 */
	act.sa_handler = (void (*) ()) _thread_sig_handler;
	act.sa_flags = SA_RESTART;
	act.sa_mask = *set;

	/* Ensure the scheduling signal is masked: */
	sigaddset(&act.sa_mask, _SCHED_SIGNAL);

	/*
	 * Initialize the set of signals that will be waited on:
	 */
	waitset = *set;

	/* These signals can't be waited on. */
	sigdelset(&waitset, SIGKILL);
	sigdelset(&waitset, SIGSTOP);
	sigdelset(&waitset, _SCHED_SIGNAL);
	sigdelset(&waitset, SIGCHLD);
	sigdelset(&waitset, SIGINFO);

	/* Check to see if a pending signal is in the wait mask. */
	tempset = _thread_run->sigpend;
	SIGSETOR(tempset, _process_sigpending);
	SIGSETAND(tempset, waitset);
	if (SIGNOTEMPTY(tempset)) {
		/* Enter a loop to find a pending signal: */
		for (i = 1; i < NSIG; i++) {
			if (sigismember (&tempset, i))
				break;
		}

		/* Clear the pending signal: */
		if (sigismember(&_thread_run->sigpend,i))
			sigdelset(&_thread_run->sigpend,i);
		else
			sigdelset(&_process_sigpending,i);

		/* Return the signal number to the caller: */
		*sig = i;

		_thread_leave_cancellation_point();
		return (0);
	}

	/*
	 * Enter a loop to find the signals that are SIG_DFL.  For
	 * these signals we must install a dummy signal handler in
	 * order for the kernel to pass them in to us.  POSIX says
	 * that the _application_ must explicitly install a dummy
	 * handler for signals that are SIG_IGN in order to sigwait
	 * on them.  Note that SIG_IGN signals are left in the
	 * mask because a subsequent sigaction could enable an
	 * ignored signal.
	 */
	for (i = 1; i < NSIG; i++) {
		if (sigismember(&waitset, i) &&
		    (_thread_sigact[i - 1].sa_handler == SIG_DFL)) {
			if (_thread_sys_sigaction(i,&act,NULL) != 0)
				ret = -1;
		}
	}
	if (ret == 0) {
		/*
		 * Save the wait signal mask.  The wait signal
		 * mask is independent of the threads signal mask
		 * and requires separate storage.
		 */
		_thread_run->data.sigwait = &waitset;

		/* Wait for a signal: */
		_thread_kern_sched_state(PS_SIGWAIT, __FILE__, __LINE__);

		/* Return the signal number to the caller: */
		*sig = _thread_run->signo;

		/*
		 * Probably unnecessary, but since it's in a union struct
		 * we don't know how it could be used in the future.
		 */
		_thread_run->data.sigwait = NULL;
	}

	/* Restore the sigactions: */
	act.sa_handler = SIG_DFL;
	for (i = 1; i < NSIG; i++) {
		if (sigismember(&waitset, i) &&
		    (_thread_sigact[i - 1].sa_handler == SIG_DFL)) {
			if (_thread_sys_sigaction(i,&act,NULL) != 0)
				ret = -1;
		}
	}

	_thread_leave_cancellation_point();
	/* Return the completion status: */
	return (ret);
}
#endif
