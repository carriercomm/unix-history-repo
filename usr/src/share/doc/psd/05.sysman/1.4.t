.\" Copyright (c) 1983 Regents of the University of California.
.\" All rights reserved.  The Berkeley software License Agreement
.\" specifies the terms and conditions for redistribution.
.\"
.\"	@(#)1.4.t	5.1 (Berkeley) %G%
.\"
.\" 1.4.t 5.1 86/05/08
.sh "Timers
.NH 3
Real time
.PP
The system's notion of the current Greenwich time and the current time
zone is set and returned by the call by the calls:
.DS
#include <sys/time.h>

settimeofday(tvp, tzp);
struct timeval *tp;
struct timezone *tzp;

gettimeofday(tp, tzp);
result struct timeval *tp;
result struct timezone *tzp;
.DE
where the structures are defined in <sys/time.h> as:
.DS
._f
struct timeval {
	long	tv_sec;	/* seconds since Jan 1, 1970 */
	long	tv_usec;	/* and microseconds */
};

struct timezone {
	int	tz_minuteswest;	/* of Greenwich */
	int	tz_dsttime;	/* type of dst correction to apply */
};
.DE
Earlier versions of UNIX contained only a 1-second resolution version
of this call, which remains as a library routine:
.DS
time(tvsec)
result long *tvsec;
.DE
returning only the tv_sec field from the \fIgettimeofday\fP call.
.NH 3
Interval time
.PP
The system provides each process with three interval timers,
defined in <sys/time.h>:
.DS
._d
#define	ITIMER_REAL	0	/* real time intervals */
#define	ITIMER_VIRTUAL	1	/* virtual time intervals */
#define	ITIMER_PROF	2	/* user and system virtual time */
.DE
The ITIMER_REAL timer decrements
in real time.  It could be used by a library routine to
maintain a wakeup service queue.  A SIGALRM signal is delivered
when this timer expires.
.PP
The ITIMER_VIRTUAL timer decrements in process virtual time.
It runs only when the process is executing.  A SIGVTALRM signal
is delivered when it expires.
.PP
The ITIMER_PROF timer decrements both in process virtual time and when
the system is running on behalf of the process.
It is designed to be used by processes to statistically profile
their execution.
A SIGPROF signal is delivered when it expires.
.PP
A timer value is defined by the \fIitimerval\fP structure:
.DS
._f
struct itimerval {
	struct	timeval it_interval;	/* timer interval */
	struct	timeval it_value;	/* current value */
};
.DE
and a timer is set or read by the call:
.DS
getitimer(which, value);
int which; result struct itimerval *value;

setitimer(which, value, ovalue);
int which; struct itimerval *value; result struct itimerval *ovalue;
.DE
The third argument to \fIsetitimer\fP specifies an optional structure
to receive the previous contents of the interval timer.
A timer can be disabled by specifying a timer value of 0.
.PP
The system rounds argument timer intervals to be not less than the
resolution of its clock.  This clock resolution can be determined
by loading a very small value into a timer and reading the timer back to
see what value resulted.
.PP
The \fIalarm\fP system call of earlier versions of UNIX is provided
as a library routine using the ITIMER_REAL timer.  The process
profiling facilities of earlier versions of UNIX
remain because
it is not always possible to guarantee
the automatic restart of system calls after 
receipt of a signal.
.DS
profil(buf, bufsize, offset, scale);
result char *buf; int bufsize, offset, scale;
.DE
