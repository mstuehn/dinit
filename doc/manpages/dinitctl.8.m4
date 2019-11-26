changequote(`@@@',`$$$')dnl
@@@.TH DINITCTL "8" "$$$MONTH YEAR@@@" "Dinit $$$VERSION@@@" "Dinit \- service management system"
.SH NAME
dinitctl \- control services supervised by Dinit
.\"
.SH SYNOPSIS
.\"
.B dinitctl
[\fIoptions\fR] \fBstart\fR [\fB\-\-no\-wait\fR] [\fB\-\-pin\fR] \fIservice-name\fR
.br
.B dinitctl
[\fIoptions\fR] \fBstop\fR [\fB\-\-no\-wait\fR] [\fB\-\-pin\fR] \fIservice-name\fR
.br
.B dinitctl
[\fIoptions\fR] \fBrestart\fR [\fB\-\-no\-wait\fR] \fIservice-name\fR
.br
.B dinitctl
[\fIoptions\fR] \fBwake\fR [\fB\-\-no\-wait\fR] \fIservice-name\fR
.br
.B dinitctl
[\fIoptions\fR] \fBrelease\fR \fIservice-name\fR
.br
.B dinitctl
[\fIoptions\fR] \fBunpin\fR \fIservice-name\fR
.br
.B dinitctl
[\fIoptions\fR] \fBunload\fR \fIservice-name\fR
.br
.B dinitctl
[\fIoptions\fR] \fBlist\fR
.br
.B dinitctl
[\fIoptions\fR] \fBshutdown\fR
.br
.B dinitctl
[\fIoptions\fR] \fBadd-dep\fR \fIdependency-type\fR \fIfrom-service\fR \fIto-service\fR
.br
.B dinitctl
[\fIoptions\fR] \fBrm-dep\fR \fIdependency-type\fR \fIfrom-service\fR \fIto-service\fR
.br
.B dinitctl
[\fIoptions\fR] \fBenable\fR [\fB\-\-from\fR \fIfrom-service\fR] \fIto-service\fR
.br
.B dinitctl
[\fIoptions\fR] \fBdisable\fR [\fB\-\-from\fR \fIfrom-service\fR] \fIto-service\fR
.\"
.SH DESCRIPTION
.\"
\fBdinitctl\fR is a utility to control services being managed by the
\fBdinit\fR daemon. It allows starting and stopping services, and listing
service status, amongst other actions. It functions by issuing commands to the daemon
via a control socket.
.\"
.SH GENERAL OPTIONS
.TP
\fB\-\-help\fR
Display brief help text and then exit.
.TP
\fB\-s\fR, \fB\-\-system\fR
Control the system init process (this is the default unless run as a non-root user). This option
determines the default path to the control socket used to communicate with the \fBdinit\fR daemon
process (it does not override the \fB\-s\fR option).
.TP
\fB\-u\fR, \fB\-\-user\fR
Control the user init process (this is the default when not run as root). This option determines
the default path to the control socket used to communicate with the \fBdinit\fR daemon process
(it does not override the \fB\-s\fR option).
.TP
\fB\-\-socket\-path\fR \fIsocket-path\fR, \fB\-p\fR \fIsocket-path\fR
Specify the path to the socket used for communicating with the service manager daemon.
.TP
\fB\-\-quiet\fR
Suppress status output, except for errors. 
.\"
.SH COMMAND OPTIONS
.TP
\fB\-\-no\-wait\fR
Do not wait for issued command to complete; exit immediately.
.TP
\fB\-\-pin\fR
Pin the service in the requested state. The service will not leave the state until it is unpinned, although
start/stop commands will be "remembered" while the service is pinned.
.TP
\fB\-\-force\fR
Stop the service even if it will require stopping other services which depend on the specified service.
.TP
\fIservice-name\fR
Specifies the name of the service to which the command applies.
.TP
\fBstart\fR
Start the specified service. The service is marked as explicitly activated and will not be stopped
automatically if its dependents stop. If the service is currently stopping it will generally continue
to stop before it is then restarted.
.TP
\fBstop\fR
Stop the specified service, and remove explicit activation. If the service has (non-soft) dependents, an
error will be displayed unless the \fB\-\-force\fR option is used.

A service with any dependents via soft dependencies will have these dependency links broken when it stops.

The \fBrestart\fR option applied to the stopped service will not by itself cause the service to restart
when it is stopped via this command. However, a dependent which is configured to restart may
cause the service itself to restart as a result.

Any pending \fBstart\fR orders are cancelled,
though a service which is starting will continue its startup before then stopping (unless the service is
configured to have an interruptible startup or is otherwise at a stage of startup which can be safely
interrupted).
.TP
\fBrestart\fR
Restart the specified service. The service will be stopped and then restarted, without affecting explicit
activation status or dependency links from dependents.
.TP
\fBwake\fR
Start the specified service after reattaching dependency links from all active dependents of the specified
service. The service will not be marked explicitly activated, and so will stop if the dependents stop.
.TP
\fBrelease\fR
Clear the explicit activation mark from a service (the service will then stop if it has no active dependents).
.TP
\fBunpin\fR
Remove start- and stop- pins from a service. If a started service is not explicitly activated and
has no active dependents, it will stop. If a started service has a dependency service which is stopping,
it will stop. If a stopped service has a dependent service which is starting, it will start. Otherwise,
any pending start/stop commands will be carried out.
.TP
\fBunload\fR
Completely unload a service. This can only be done if the service is stopped and has no loaded dependents
(i.e. dependents must be unloaded before their dependencies).
.TP
\fBlist\fR
List loaded services and their state. Before each service, one of the following state indicators is
displayed:

.RS
.nf
\f[C]\m[blue][{+}\ \ \ \ \ ]\m[]\fR \[em] service has started.
\f[C]\m[blue][{\ }<<\ \ \ ]\m[]\fR \[em] service is starting.
\f[C]\m[blue][\ \ \ <<{\ }]\m[]\fR \[em] service is starting, will stop once started.
\f[C]\m[blue][{\ }>>\ \ \ ]\m[]\fR \[em] service is stopping, will start once stopped.
\f[C]\m[blue][\ \ \ >>{\ }]\m[]\fR \[em] service is stopping.
\f[C]\m[blue][\ \ \ \ \ {-}]\m[]\fR \[em] service has stopped.
.fi

The << and >> symbols represent a transition state (starting and stopping respectively); curly braces
indicate the desired state (left: started, right: stopped). An 's' in place of '+' means that service
startup was skipped (possible only if the service is configured as skippable). An 'X' in place of '-'
means that the service failed to start, or that the service process unexpectedly terminated with an
error status or signal while running.

Additional information, if available, will be printed after the service name: whether the service owns,
or is waiting to acquire, the console; the process ID; the exit status or signal that caused termination.
.RE
.TP
\fBshutdown\fR
Stop all services (without restart) and terminate Dinit. If issued to the system instance of Dinit,
this will also shut down the system.
.TP
\fBadd-dep\fR
Add a dependency between two services. The \fIdependency-type\fR must be one of \fBregular\fR,
\fBmilestone\fR or \fBwaits-for\fR. Note that adding a regular dependency requires that the service
states are consistent with the dependency (i.e. if the "from" service is started, the "to" service
must also be started). Circular dependency chains may not be created.
.TP
\fBrm-dep\fR
Remove a dependency between two services. The \fIdependency-type\fR must be one of \fBregular\fR,
\fBmilestone\fR or \fBwaits-for\fR. If the "to" service is not otherwise active it may be stopped
as a result of removing the dependency.  
.TP
\fBenable\fR
Permanently enable a \fBwaits-for\fR dependency between two services. This is much like \fBadd-dep\fR
but it also starts the dependency if the dependent is started (without explicit activation, so the
dependency will stop if the dependent stops), and it creates a symbolic link in the directory
specified via the \fBwaits-for.d\fR directive in the service description (there must be only one such
directive). The dependency should therefore be persistent.

If the \fB--from\fR option is not used to specify the dependent, the dependency is created from the
\fBboot\fR service by default.
.TP
\fBdisable\fR
Permanently disable a \fBwaits-for\fR dependency between two services. This is the complement of the
\fBenable\fR command; see the description above for more information.
.\"
.SH SERVICE OPERATION
.\"
Normally, services are only started if they have been explicitly activated (\fBstart\fR command) or if
a started service depends on them. Therefore, starting a service also starts all services that the first
depends on; stopping the same service then also stops the dependency services, unless they are also
required by another explicitly activated service.
.LP
A service can be pinned in either the started or stopped state. This is mainly intended to be used to
prevent automated stop or start of a service, including via a dependency or dependent service, during
a manual administrative procedure.
.LP
Stopping a service does not in general prevent it from restarting. A service configured to restart
automatically, or with a dependent service configured to do so, will restart immediately after stopping
unless pinned.
.\"
.SH SEE ALSO
\fBdinit\fR(8).
.\"
.SH AUTHOR
Dinit, and this manual, were written by Davin McCall.
$$$dnl
