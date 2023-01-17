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

Design notes
------------

### Three layers

The base layer is reliable inorder datagram transport; initially this will
just be TCP with some framing, although really SCTP or ZMQ are both better
choices; I'm just doing the quick and dirty thing for now.

The pub-sub-sequence layer will add a named channel pub-sub architecture with
sequence numbers (WLOG, timestamps) on messages allowing a total order.  This
will introduce a distinction between a server (binary) and client (library).

The time management layer will add client time control to the library, whereby
clients request authorization to use a given sequence number ("advance time")
and the server ensures that all causally prior messages have been delivered
before providing that authorization.

### Thread agnostic

Each element of this project is intended to accomodate any application
threading model; as such no threads are started by this process and obviously
shared data like message buffers are protected by unique reference idioms or
by mutexes.

Blocking work functions are provided so that the caller can spawn trivial
block-loop threads to get a traditional nonblocking API.

### Interface concepts:

* Datagram layer:
  * Transport (bidirectional messaging via buffers)
    * Constructors: From config struct, from move().  Nonblocking.
    * Start:  Blocking (two-phase initialization)
    * Send(TxBuffer):  Nonblocking; copies a buffer into the queue.
    * ReceiveAll -> vector<RxBuffer>:  Nonblocking; drains reecived data.
    * ProcessIO:  Blocking; does all available networking actions.
  * DatagramTransportServer (A factory for Transports)
    * Constructors:  From config struct, from move().  Nonblocking.
    * AwaitIncomingConnection -> DatagramTransport; blocking
    * GetPortNumber -> uint16_t; blocking (forces initialization)
* ...
* Deterministic endpoint:
  * Endpoint (bookkeeping)
    * GetSequence
    * AdvanceSequence
    * Subscribe (factory)
    * Publish
    * HandleIO
    *