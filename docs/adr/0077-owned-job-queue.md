# ADR 0077: Bounded jobs with owner fences

Status: Accepted for the 07.09 foundation. Owner: GPT.

Workers perform independent CPU work on owned inputs. Any thread may submit a
callback with an Owner token to a Queue configured with explicit worker and
pending counts. Zero limits are refused. FIFO dequeue does not promise completion
order. Gameplay must consume results in its own stable order through commands;
workers may not write ECS or physics. Physics, tick scheduling and command dispatch
remain on the main thread. This is not a simulation scheduler replacement.

Owner destruction invalidates tokens, cancels work that has not started and waits
for running callbacks, including capture cleanup. Callers destroy the Owner before
any data callbacks borrow. A token may safely outlive its Owner. Queued captures
are reclaimed when dequeued or at queue shutdown, not necessarily at owner teardown.
Do not destroy an Owner from its callback, or shut down/destroy a Queue from its
worker. Callbacks must terminate and must not wait for work that requires their
own exhausted pool. These lifetime/deadlock obligations are caller contracts.

Queue shutdown rejects further submissions, drains eligible pending work, counts
cancelled work and joins all threads. No detached threads exist, including on
partial worker creation failure. Callback exceptions are contained and counted;
completed, failed and cancelled counters provide a non-allocating failure report.
Submit distinguishes expired owner, full queue, stopped queue and empty callback.
Allocation and OS thread creation errors propagate; successful submissions own
captures until execution or cancellation. Thread/capture memory is bounded by the
explicit limits, with payload sizes controlled by the caller. No guessed hardware
budget is introduced: RTX 4070 resource measurements remain Joshua runs this under
01.11. No new dependency or service is required.
