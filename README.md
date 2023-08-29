Blocktopus
==========

An experimental repository for deterministic multiprocess datagram networking
using the lock-time algorithm.

This repository is expected to be moved or deleted; do not link to it in its
current form.

Operation
---------

Build and test the project with:

```
bazel test //...
```

This project is intended to work on both MacOS 13 (Ventura) and Ubuntu 22.04
(Jammy).

Motivation and Design
---------------------

Robots are very small distributed systems, with multiple components
(hardware actuators and sensors, drivers, processing units, etc.) producing
and consuming data and performing actions and observations separately but
connected via a network substrate.

Publish/subscribe of datagrams is the standard model for communication in
robotics; it is a versatile model and good fit for the various different
communication links used (e.g. ethernet, ethercat, canbus, vendor-specific).
Real-world pub/sub datagram systems are nondeterministic, both because of
packet loss and reordering in the network physical layer and because of the
messy timing relationships between the individual components of the system
which may be located on the same or different hardware and contend for
resources (bandwidth, compute, GPU RAM, etc.) both with one another and with
other elements of the robotic system.

Simulations of robotic systems, if they are to provide sufficiently high
fidelity to capture startup and lifecycle concerns and to simulate off-nominal
conditions like plesiochrony, component failure, and estops, must retain
the distributed system characteristics of the real robot.  However a headline
feature of many simulation environments is determinism, i.e., that the outcome
of a simulation is uniquely determined by its input.  This can be particularly
important in reinforcement learning, where an RL engine seeks to control the
randomization of an ensemble of simulations:  Covariance-based RL methods
assume that relevant random variables of the simulation are accessible to the
learning process.

This library provides a datagram pub/sub system consisting of two primary
parts:
 * A client library that components can use to subscribe to, publish, and
   receive datagrams, and
 * A server binary that interleaves and orders those subscriptions,
   publications, and receptions into a unique and fully deterministic order.
   * The server binary is a thin wrapper around a library, as many consumers
     are likely to want to locate monitoring, telemetry, and debugging there,
     or to introduce simulated network properties (delay/drop/reorder) there.

For deterministic communication to work, clients must sign over control of
"time" to the communications substrate.  "Time" is in quotation marks here as
every client can potentially have its own notion not only of wall-clock time
but of simulation time, and disagree with each other.  In the code instead of
"time", we use the term "sequence number" to denote the property of messages
that increases monotonically from the point of view of every client.

As an illustrative example, a client that wishes to receive incoming messages
at sequence number T<sub>1</sub> must first wait for all other clients that
might have sent messages at sequence number T<sub>0</sub> < T<sub>1</sub>.  As
such, clients receiving messages must block, and clients sending messages must
declare how those clients block (that is, the sequence number at which the
message is to be received).  Related requirements apply to subscribing and
unsubscribing, and to a client initially connecting to the network.

One important note:  If every element of a system works only by reacting to
incoming messages, and if messages have zero latency, then at all times every
client but one is blocking:  If client A is processing an incoming message at
sequence number T<sub>1</sub>, no other client B can know if A will publish a
message at any sepcific sequence number T<sub>2</sub> until that message is
in fact published.

As such, a distributed system simply becomes a single-threaded system with
annoyingly complicated dispatch.

To avoid this, we must break the assumptions.  This is done via:
 * Sending messages with latency, that is, a receive sequence number strictly
   greater than their sending sequence number.
 * Clients declaring themselves ready to advance to a subsequent sequence
   number (for instance because they are performing a computation of a
   particular known real-world latency, or because they send only at
   particular publication event boundaries).
Both of these approaches involve declaring a particular relationship between
sequence numbers and time, and are the reason for using simulation time as
the sequence number.

Formal Properties
-----------------

To understand the exact operation and correctness guarantees of the system,
we introduce the idea of _sequence points_.  A sequence point is an operation
at which a client obtains a complete and causal view of its past and may then
carry out computation and emit messages causally subsequent to that past.

The fundamental client operation is to receive a batch of messages prior to a
given sequence number T, and to emit any number of messages with sender
sequence number prior to T, which introduces a sequence point at T.  We also
require that a client at this time declare an earliest arrival sequence number
for all future messages that it will send.  This computation can usually be
automatic, by adding a fixed minimum latency to the latest sequence number
mentioned in the operation.

With this operation in hand, the server need only assure that a client
asking to induce a sequence point at T is blocked from doing so until no
messages will be sent with an earlier arrival sequence.

Having done this, and assuming deterministic behaviour by all clients, any
relative timings by different clients should still yield the same sequence
points and thus the same list of messages.

Test Suite
----------

To test the operation of the algorithm, we have a set of clients with
different message sending and receiving properties, with an overall
pattern of sends and receives guaranteed to complete after a certain
number of operations.  We then generate permutations of the client
IDs and advance the clients as possible in that sequence.  If the
algorithm is correct, every permutation should generate the same sequence.

