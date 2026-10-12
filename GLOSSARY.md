# Glossary

Domain terms used in the code, commits and design discussions.

## Radio

**Radio job**
One transmission handed to the radio task: an encoded first frame plus a number of repeat frames. A job is either *our own* (a command or sensor frame from one of our remotes, with outcome events) or a *repeater frame* (a frame received from someone else's remote and sent again). A repeater frame reports no events, and since it carries none of our rolling codes it never holds back a stop.

**RadioScheduler**
The module that owns the radio jobs waiting for, or on, the air. It decides their order, when a delayed job is due, which jobs a flush or suspend drops, and the outcome of every job. Producers hand it jobs and get a ticket back. The radio task asks it for the next job and reports back how the job went. It knows nothing about FreeRTOS, the CC1101 or shades.

**Ticket**
What submitting a radio job returns: the job's sequence number, 0 when the radio refused it. Every event about the job carries it. A command that went out over GPIO, not the radio, has ticket 0.

**Back job / front job**
A job normally joins the back of the queue. A *priority* job (a stop) goes to the front, unless an earlier job from the same remote is still waiting. Overtaking that job would put the remote's rolling codes out of order, and the motor would reject the older frame.

**Delayed job**
A job that must not go on air before a given time (for example the long press that stores a favorite). It holds up the jobs behind it, but front jobs may overtake it while it waits.

**Flush**
Drops every waiting job (each gets a *dropped* event) and stops the job on air after its current frame (it gets *aborted*).

**Suspend**
Stops the radio taking jobs for a reason (frequency scan, OTA). Suspending flushes; while suspended, new jobs are refused.

**Job outcome**
Every job we submit gets exactly one *started* event, unless it is dropped before going on air. It then ends with exactly one of *finished* (every frame went on air), *aborted* (cut short) or *dropped* (never went on air).

**Rolling code**
The per-remote counter in every Somfy frame. A motor ignores any frame whose code is not newer than the last one it accepted, so a remote's frames must go on air in the order their codes were assigned.
