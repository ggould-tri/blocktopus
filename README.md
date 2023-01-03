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

Design notes
------------

The plan is three layers.

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

Additional notes will follow as the implementation progresses.
