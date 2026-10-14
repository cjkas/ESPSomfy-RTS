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
A job that must wait a given time after it was queued and after the transmission ahead of it ended, so the motor has settled (for example the long press that stores a favorite). It holds up the jobs behind it, but front jobs may overtake it while it waits.

**Flush**
Drops every waiting job (each gets a *dropped* event) and stops the job on air after its current frame (it gets *aborted*).

**Suspend**
Stops the radio taking jobs for a reason (frequency scan, OTA). Suspending flushes; while suspended, new jobs are refused.

**Job outcome**
Every job we submit gets exactly one *started* event, unless it is dropped before going on air. It then ends with exactly one of *finished* (every frame went on air), *aborted* (cut short) or *dropped* (never went on air).

**Rolling code**
The per-remote counter in every Somfy frame. A motor ignores any frame whose code is not newer than the last one it accepted, so a remote's frames must go on air in the order their codes were assigned.

**Pulse decoder**
Turns the times between edges of a received transmission into a frame's bits: it finds the hardware and software sync, which also tell a 56-bit frame from an 80-bit one, then reads the Manchester-coded bits.

## Shades

**Favorite**
The position a motor stores and goes to on My. A shade may have none.
_Avoid_: My position

**Step**
A short move in one direction, sized by the shade's step time and the number of steps the frame asks for. An integrated tilt turns its slats to the end first, then moves the lift.

**Sensor frame**
A frame from a sun and wind sensor reporting whether it is sunny and whether it is windy. It carries no command; the shade decides what to do.

**Sun flag**
The per-shade switch that lets the sun move a shade. While it is on, lasting sun extends the shade and lasting cloud retracts it; wind retracts the shade whether it is on or not.

**Extend / retract**
Where the weather sends a shade: extend to its favorite, or fully when it has none; retract fully. A tilt-only shade extends and retracts its tilt, every other shade its lift.
