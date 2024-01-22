"""
Example of clients operating at different realtime latencies.  For any given
configuration, the output should be identical for all random seeds.
"""

from abc import ABC, abstractmethod
from math import ceil, floor
from typing import Optional, NewType, List
from random import Random

from sketch_types import *


class Server:
    """The core logic of the pub/sub system."""

    def __init__(self, randomness: Random):
        self._randomness = randomness
        self._assigned_ids = set()
        self._client_names: dict[ClientId, str] = {}
        self._client_randomness: dict[ClientId, Random] = {}
        self._channel_configs: dict[ChannelName, TransportConfig] = {}
        self._default_channel_config: TransportConfig = \
            DEFAULT_TRANSPORT_CONFIG
        self._subscriptions: dict[SubscriptionKey, SimTimeRange] = {}
        self._client_channel_queues: \
            dict[SubscriptionKey, list[ClientIncomingMessage]] = {}
        self._client_send_waterlines: dict[ClientId, TxSequenceKey] = {}
        self._client_receive_waterlines: dict[ClientId, RxSequenceKey] = {}

    def set_channel_config(self,
                           channel: ChannelName,
                           config: TransportConfig):
        """Set a channel to a non-default configuration.  This is intended
        to advise the server that it need not store or block on every message,
        improving performance.
        """
        self._channel_configs[channel] = config

    def _channel_config(self, channel: ChannelName) -> TransportConfig:
        return self._channel_configs.get

    def _make_client_id(self, force_id: ClientId=None) -> ClientId:
        result = force_id
        if force_id is not None:
            if force_id < MINIMUM_ASSIGNED_CLIENT_ID:
                raise ValueError(f"ID {force_id} less than minimum"
                                 f"({MINIMUM_ASSIGNED_CLIENT_ID})")
            if force_id in self._assigned_ids:
                raise ValueError(
                    f"Illegal forced ID {force_id} "
                    f"already assigned to '{self._client_names[force_id]}'")
            self._next_client_id = force_id
        if result is None:
            if not self._assigned_ids:
                result = MINIMUM_ASSIGNED_CLIENT_ID
            else:
                result = max(self._assigned_ids) + 1
        self._assigned_ids.add(result)
        return result

    def _clients(self) -> ClientId: return self._client_names.keys

    def _client_may_send_on_channel(self,
                                    sender: ClientId,
                                    channel: ChannelName) -> bool:
        return True  # TODO declared publish channels not implemented yet.

    def _client_earliest_next_send(self,
                                   sender: ClientId,
                                   _channel: ChannelName) -> SimTime:
        return self._client_send_waterlines[sender][0]

    def _latest_message_receipt_time(self) -> SimTime:
        return max(self._client_receive_waterlines.values(),
                   default=MINIMUM_SIM_TIME)

    def _latest_time_not_fully_received(self, receiver):
        # For every channel that the receiver subscribes to, for every
        # publisher on any of those channels, what is the minimum time that
        # a message from that publisher could arrive?
        result = SimTime(float('inf'))
        for (client, channel), _interval in self._subscriptions:
            if client == receiver:
                for publisher in self._assigned_ids:
                    if self._client_may_send_on_channel(publisher, channel):
                        min_arrival = self._client_earliest_next_send(
                            publisher, channel)
                        result = min(result, min_arrival)
        return result

    def accept_connection(self,
                          client_name: str,
                          client_id: Optional[ClientId]=None
                          ) -> tuple[SimTime, ClientId]:
        """Requests that the server recognize a new client.  The client
        may request an ID (and receive an error if that ID is not available)
        or else it will receive a random ID."""
        id = self._make_client_id(client_id)
        self._client_names[id] = client_name
        self._client_randomness[id] = Random(self._randomness.random())
        client_start_time = self._latest_message_receipt_time()
        self._client_send_waterlines[id] = client_start_time
        self._client_receive_waterlines[id] = client_start_time
        return (client_start_time, id)

    def queue_message(self, message: ClientOutgoingMessage):
        """Requests that the server queue a sent message for receipt by any
        subscribers.  The message is assumed to be causally valid."""
        # TODO:  Consider relocating validation into this class via a helper
        # method to this method?
        relevant_subscriptions = []
        sender = message.sender
        self._client_send_waterlines[sender] = (
            message.sender_time, sender, message.subsequence_number)
        config = self._channel_config(message.channel)
        if self._client_randomness[sender]() < config.sender_loss_fraction:
            return  # Packet loss by sender.
        for receiver in self._clients:
            self._queue_message_to_receiver(message, config, receiver)

    def _queue_message_to_receiver(self,
                                   message: ClientOutgoingMessage,
                                   config: TransportConfig,
                                   receiver: ClientId):
        subscription_key = (receiver, message.channel)
        if subscription_key in self._subscriptions:
            randomness = self._client_randomness[receiver]
            if (randomness.random() < config.receiver_loss_fraction):
                return  # Packet loss by this receiver.
            latency = randomness.randrange(config.min_latency,
                                            config.max_latency)
            rx_time = message.sender_time + latency
            sub_begin, sub_end = self._subscriptions[subscription_key]
            if sub_begin is not None and rx_time < sub_begin:
                return  # This subscription was not active at this time.
            if sub_end is not None and rx_time > sub_end:
                return  # This subscription was not active at this time.
            key = receiver, message.channel
            if key not in self._client_channel_queues:
                self._client_channel_queues[key] = []
            queue = self._client_channel_queues[key]
            queue.append(ClientIncomingMessage(receiver=receiver,
                                               receiver_time=rx_time,
                                               message=message))
            queue.sort(key=ClientIncomingMessage.sequence_key)

    def dequeue_messages(self, receiver: ClientId, until: SimTime
                         ) -> tuple[SimTime, list[ClientIncomingMessage]]:
        """Extract all receivable messages before `until`.  A message
        is receivable if the receiver cannot receive any other message
        not yet queued before that message."""
        until = min([self._latest_time_not_fully_received(receiver), until])
        result = []
        queues = [
            q for (client, channel), q in self._client_channel_queues.items()
            if client == receiver]
        for q in queues:
            while q and q[0].receiver_time <= until:
                result.append(q.pop(0))
        result.sort(key=ClientIncomingMessage.sequence_key)
        if result:
            self._client_receive_waterlines[receiver] = max(
                self._client_receive_waterlines[receiver],
                result[-1].sequence_key())
        return until, result


class ServerConnection:
    """Per-client logic of the server, responsible for validation and
    configuration and providing the client API."""

    def __init__(self,
                 name: str,
                 server: Server,
                 client_id: Optional[ClientId]=None):
        self._name: str = name
        self._server: Server = server
        starting_time, self._id = server.accept_connection(client_id)
        self._sends_committed_time = starting_time
        self._receives_complete_time = starting_time
        self._subsequence_number: SubsequenceNumber = SubsequenceNumber(0)

    def commit_sends_until(self,
                           new_time: SimTime,
                           messages: list[ClientOutgoingMessage]=None):
        """A client commits that `messages` are all of the messages it will
        send up through and including `new_time`.  The client does not
        request messages through that time, so its time does not advance.

        The sender and subsequence number of each message will be replaced
        with the client ID and subsequence number of this connection.

        Following this call the client may no longer send a message with
        a lower time than the highest time `messages`."""
        assert new_time > self._sends_committed_time
        for message in messages or []:
            message.sender = self._id
            message.subsequence_number = self._subsequence_number
            if message.sender_time == self._sends_committed_time:
                # Duplicate times; apply a subsequence number.
                self._subsequence_number += 1
                message.subsequence_number = self._subsequence_number
            else:
                assert message.sender_time > self._sends_committed_time
            self._server.queue_message(self._id, message)
            self._sends_committed_time = message.sender_time
        assert new_time >= self._sends_committed_time
        self._sends_committed_time = new_time

    def try_receive_until(self, new_time: SimTime
                          ) -> tuple[SimTime, list[ClientIncomingMessage]]:
        """A client asks to advance its time to `new_time`.  It must
        previously have committed its sends until at least that time.
        A new time <= `new_time` is returned, along with all incoming messages
        arriving in that interval in order of receipt."""
        return self._server.dequeue_messages(self._id, new_time)

    def send(self, message: ClientOutgoingMessage):
        """Sugar for `commit_sends_until` that sends a single message."""
        self.commit_sends_until(message.sender_time, [message])

    def advance(self, desired_time=None
                ) -> tuple[SimTime, list[ClientIncomingMessage]]:
        """Sugar for a `commit_sends_until` / `receive_until` pair."""
        self.commit_sends_until(desired_time)
        return self.try_receive_until(desired_time)


class Client(ABC):
    _server: ServerConnection = None
    _current_time: SimTime = MINIMUM_SIM_TIME

    @abstractmethod
    def act(self):
        raise NotImplementedError  # Abstract.


class SequentialNumbers(Client):
    """Example / debugging client that sends sequential integers at a fixed
    interval forever."""
    @dataclass(# kw_only=True,
               frozen=True)
    class Config:
        name: str
        output_channel: str

    def __init__(self, config: Config, server_conn: ServerConnection):
        self._config : self.Config = config
        self._server_conn : Server = server_conn
        self._current_time : SimTime = SimTime(0)
        self._sequence_number : int = 0

    def act(self):
        new_time, incoming_messages = self._server_conn.advance(
            desired_time=self._current_time + 1)
        assert not incoming_messages  # No subscriptions.
        time_cursor = self._current_time
        while time_cursor <= floor(new_time):
            payload = self._sequence_number.to_bytes(4, byteorder='big')
            self._server_conn.send(
                ClientOutgoingMessage(
                    channel=self._config.output_channel,
                    sender_time=time_cursor,
                    payload=payload))
            time_cursor += 1
        self._current_time = new_time


import unittest

class TestSketch(unittest.TestCase):
    MAX_ITERS = 100
    SEED = 42

    def test_sequential(self):
        server = Server(randomness=Random(self.SEED))
        client = SequentialNumbers(
            SequentialNumbers.Config(name="seqnum_sender",
                                     output_channel="seqnum"),
            server_conn=ServerConnection("seqnum_sender", server))
        for _ in range(self.MAX_ITERS):
            client.act()


if __name__ == "__main__":
    unittest.main()
