"""
Types and classes for the design sketch.

This is some definitions moved out of the sketch file to make that file
easier to read and edit.  This is intended for `import *` use; it is not a
prinicpled abstraction barrier.
"""

from dataclasses import dataclass
from enum import Enum
from typing import Optional, NewType

# To simplify debugging, we use `NewType` to poison erroneous duck typing in
# the static typechecker.  This doesn't work anywhere near 100% of the time,
# but it seems to hint IDE red-squiggle logic pretty well.

ClientId = NewType("ClientId", int)
"""A numerical unique ID for a client.  Values < 2 are reserved."""

MINIMUM_ASSIGNED_CLIENT_ID = ClientId(2)
"""The lowest ID that can be assigned to a client; IDs lower than this are
reserved for future use such as server-directed messages."""

SimTime = NewType("SimTime", int)
"""A number that monotonically increases across all client communications.
No units are implied, although microseconds would be a reasonable choice."""

MINIMUM_SIM_TIME = SimTime(0)
"""The lowest sim time that can ever be mentioned in any context."""

ChannelName = NewType("ChannelName", str)
"""The name of a channel."""

SubsequenceNumber = NewType("SubsequenceNumber", int)
"""A number that increases with each message a client sends within the same
SimTime, in order to guarantee a unique ordering of messages."""

TxSequenceKey = tuple[SimTime, ClientId, SubsequenceNumber]
"""A unique key used to sort message send times."""

RxSequenceKey = tuple[SimTime, SimTime, ClientId, SubsequenceNumber]
"""A unique key used to sort message receive times.
The elements are
 * receipt time
 * send time
 * sender ID
 * sender subsequnece number
"""

SimTimeRange = tuple[Optional[SimTime], Optional[SimTime]]
"""A possibly unbounded range of SimTimes."""

SubscriptionKey = tuple[ClientId, ChannelName]
"""The scope of subscription configuration and delivery."""


@dataclass(# kw_only=True,
           )  # Not frozen, so that it can be fixed up at send time.
class ClientOutgoingMessage:
    """Data and metadata for a message at its point of sending."""
    sender_time: SimTime
    channel: ChannelName
    payload: bytearray

    # These are optional at ctor time and are filled in when sent.
    sender: ClientId = -1
    subsequence_number: SubsequenceNumber = 0

    @property
    def sequence_key(self) -> TxSequenceKey:
        """A unique sort key for messages that determines the order in which
        they are received."""
        return (self.sender_time,
                self.sender,
                self.subsequence_number)


@dataclass(# kw_only=True,
           frozen=True)
class ClientIncomingMessage:
    """Data and metadata for a message at its point of receipt."""
    # Note:  This is has-a rather than is-a ClientOutgoingMessge in order
    # to aid in outgoing message GC while minimizing copies.  Hence the
    # proxy accessors, which serve to hide this fact.
    receiver: ClientId
    receiver_time: SimTime
    message: ClientOutgoingMessage

    # Pass through the sender message data as if it were a subclass:

    @property
    def sender(self): return self.message.sender

    @property
    def sender_time(self): return self.message.sender_time

    @property
    def subsequence_number(self): return self.message.subsequence_number

    @property
    def channel(self): return self.message.channel

    @property
    def payload(self): return self.message.payload

    @property
    def sequence_key(self) -> RxSequenceKey:
        """A unique sort key for messages that determines the order in which
        they are received."""
        return (self.receiver_time,
                self.sender_time,
                self.sender,
                self.subsequence_number)


@dataclass(# kw_only=True,
           frozen=True)
class TransportConfig:
    """Configuration of the server's data management of transmitted messages.
    """

    class BufferPolicy(Enum):
        DISCARD = 1
        "Messages are never queued or delivered; e.g. for server logging."

        MAILBOX = 2
        "Shorthand for QUEUE_NEWEST of size 1"

        UNBOUNDED_QUEUE = 3
        "Messages are retained until fully delivered, at unbounded cost"

        QUEUE_NEWEST = 4
        "Messages are queued; if the queue fills, the oldest is discarded"

        QUEUE_OLDEST = 5
        "Messages are retained only if the queue is not full (LCM behaviour)"

    min_latency: SimTime
    max_latency: SimTime
    sender_loss_fraction: float
    receiver_loss_fraction: float
    buffer_policy: BufferPolicy
    queue_size: Optional[int] = None


DEFAULT_TRANSPORT_CONFIG = TransportConfig(
            min_latency = SimTime(5),
            max_latency = SimTime(10),
            sender_loss_fraction=0.,
            receiver_loss_fraction=0.,
            buffer_policy=TransportConfig.BufferPolicy.UNBOUNDED_QUEUE)
"""The transport config for channels that are not otherwise configured."""
