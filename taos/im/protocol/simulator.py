# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
from pydantic import BaseModel
from typing import Any
from copy import deepcopy

"""
The models required to parse messages received from the simulator are defined here.
TODO: Directly generate subnet-format classes in simulator?
"""

class SimulatorOrder(BaseModel):
    """
    Represents an order in the simulator.

    Attributes:
    - orderId: The id of the order as assigned by the simulator.
    - direction: The side of the book on which the order was attempted to be placed (0=BID, 1=ASK).
    - timestamp: The simulation timestamp at which the order was placed.
    - volume: The size of the order in base currency.    
    - price: Optional field for the price of the order (None for market orders).
    - event: String identifier for the event type.
    - agentId: ID of the agent to which the order belongs.
    - clientOrderId: Optional agent-assigned identifier for the order.
    """
    orderId: int
    direction: int
    timestamp: int
    volume: float
    price: float | None
    event: str
    agentId: int
    clientOrderId: int | None

class SimulatorTrade(BaseModel):
    """
    Represents a trade in the simulator.

    Attributes:
    - id: Unique identifier for the trade.
    - direction: Direction of the trade (0 for buy, 1 for sell).
    - timestamp: Timestamp of when the trade occurred.
    - aggressingOrderId: ID of the order that initiated the trade.
    - restingOrderId: ID of the resting order that was matched.
    - volume: Volume of the trade.
    - price: Price at which the trade occurred.
    """
    id: int
    direction: int
    timestamp: int
    aggressingOrderId: int
    restingOrderId: int
    volume: float
    price: float

class SimulatorCancellation(BaseModel):
    """
    Represents a cancellation of an order in the simulator.

    Attributes:
    - orderId: The ID of the order being canceled.
    - volume: The volume of the order to cancel (None if the whole order is cancelled).
    """
    orderId: int
    volume: float | None

class SimulatorTick(BaseModel):
    """
    Represents a tick related to an order in the simulator.

    Attributes:
    - orderId: The ID of the order associated with this tick.
    - timestamp: The timestamp of the tick.
    - volume: The volume associated with this tick.
    - direction: The direction of the tick (0 for buy, 1 for sell).
    """
    orderId: int
    timestamp: int
    volume: float
    direction: int

class SimulatorDeepTick(BaseModel):
    """
    Represents a deeper tick related to an order in the simulator.

    Attributes:
    - orderId: The ID of the order associated with this deep tick.
    - timestamp: The timestamp of the deep tick.
    """
    orderId: int
    timestamp: int

class SimulatorLevel(BaseModel):
    """
    Represents a market level in the order book.

    Attributes:
    - price: The price level in the market.
    - volume: The volume at this price level.
    - orders: List of orders (ticks or deep ticks) at this level.
    """
    price: float
    volume: float
    orders: list[SimulatorTick | SimulatorDeepTick]

class SimulatorBroadLevel(BaseModel):
    """
    Represents a broader market level in the order book.

    Attributes:
    - price: The price level.
    - volume: The volume at this price level.
    """
    price: float
    volume: float

class SimulatorBook(BaseModel):
    """
    Represents an order book in the simulator.

    Attributes:
    - bookId: Unique identifier for the order book.
    - record: Historical record of events.
    - bid: List of bid levels.
    - ask: List of ask levels.
    """
    bookId: int
    record: list[dict] | None
    bid: list[SimulatorLevel | SimulatorBroadLevel] | None
    ask: list[SimulatorLevel | SimulatorBroadLevel] | None

class SimulatorBalance(BaseModel):
    """
    Represents an account balance in the simulator.

    Attributes:
    - free: Amount of free balance available.
    - reserved: Amount of balance reserved for open orders.
    - total: Total balance (free + reserved).
    - symbol: Currency symbol (optional).
    """
    free: float
    reserved: float
    total: float
    symbol: str | None

class SimulatorBalances(BaseModel):
    """
    Represents the balances for a specific account and book in the simulator.

    Attributes:
    - bookId: Unique identifier for the book.
    - base: Base currency balance.
    - quote: Quote currency balance.
    """
    bookId: int
    base: SimulatorBalance
    quote: SimulatorBalance

class SimulatorAccount(BaseModel):
    """
    Represents an account in the simulator.

    Attributes:
    - agentId: Identifier for the agent.
    - balances: List of balances for the account.
    - orders: List of orders associated with the account.
    """
    agentId: int
    balances: list[SimulatorBalances]
    orders: list[list[dict] | None] | None
    
class SimulatorBalances(BaseModel):
    """
    Represents the balances for a specific account and book in the simulator.

    Attributes:
    - bookId: Unique identifier for the book.
    - base: Base currency balance.
    - quote: Quote currency balance.
    """
    bookId: int | None = None
    base: SimulatorBalance
    quote: SimulatorBalance
    
class SimulatorBalancesNew(BaseModel):
    """
    Represents the balances for a specific account and book in the simulator.

    Attributes:
    - bookId: Unique identifier for the book.
    - base: Base currency balance.
    - quote: Quote currency balance.
    """
    holdings : list[SimulatorBalances]
    activeOrders : list[list[dict]]
    
class SimulatorAccountNew(BaseModel):
    """
    Represents an account in the simulator.

    Attributes:
    - agentId: Identifier for the agent.
    - balances: List of balances for the account.
    - orders: List of orders associated with the account.
    """
    agentId: int
    balances: SimulatorBalancesNew
    orders: list[list[dict] | None] | None

class SimulatorAgentMessage(BaseModel):
    """
    Represents a message sent to an agent in the simulator.

    Attributes:
    - timestamp: Timestamp of the message.
    - delay: Delay associated to the message.
    - target: Target recipient of the message.
    - type: Type of the message.
    - payload: Additional data related to the message (optional).
    """
    timestamp: int
    delay: int
    target: str
    type: str
    payload: dict[str, Any] | None = None

class SimulatorMessageBatch(BaseModel):
    """
    Represents a batch of events in the simulator.

    Attributes:
    - messages: List of agent messages.
    """
    messages: list[SimulatorAgentMessage]

class SimulatorStateUpdate(BaseModel):
    """
    Represents a state update in the simulator.

    Attributes:
    - books: List of order books.
    - accounts: Dictionary mapping agent IDs to their accounts.
    - notices: List of events.
    """
    logDir : str | None = None
    books: list[SimulatorBook]
    accounts: dict[int, SimulatorAccountNew]
    notices: list[SimulatorAgentMessage]

    def serialize(self) -> dict:
        """
        Serializes the state update into a dictionary format.

        Returns:
        - A dictionary representation of the state update.
        """
        state_dict = deepcopy(self.__dict__)
        state_dict['books'] = [book.__dict__ for book in state_dict['books']]
        for book in state_dict['books']:
            if book['bid']:
                book['bid'] = [bid.__dict__ for bid in book['bid']]
            else:
                book['bid'] = []
            if book['ask']:
                book['ask'] = [ask.__dict__ for ask in book['ask']]
            else:
                book['ask'] = []
            for level in book['bid']:
                if 'orders' in level:
                    level['orders'] = [order.__dict__ for order in level['orders']]
            for level in book['ask']:
                if 'orders' in level:
                    level['orders'] = [order.__dict__ for order in level['orders']]
        state_dict['accounts'] = {agentId: account.__dict__ for agentId, account in state_dict['accounts'].items()}
        for agentId, account in state_dict['accounts'].items():
            account['balances'] = [balance.__dict__ for balance in account['balances']['holdings']]
            for i, balance in enumerate(account['balances']['holdings']):
                account['balances'][i]['base'] = balance['base'].__dict__
                account['balances'][i]['quote'] = balance['quote'].__dict__
        return state_dict

class SimulatorBookMessage(BaseModel):
    """
    Represents the message published for a state update by the simulator.

    Attributes:
    - timestamp: Timestamp of the message.
    - delay: Delay associated to the message.
    - source: Source of the message.
    - target: Target recipient of the message.
    - type: Type of the message.
    - payload: Payload containing state update data.
    """
    timestamp: int
    delay: int
    source: str
    target: str
    type: str
    payload: SimulatorStateUpdate

class SimulatorAgentResponse(BaseModel):
    """
    Represents a response from an agent.

    Attributes:
    - agentId: Identifier for the agent sending the response.
    - delay: Delay to be applied in processing the response.
    - type: Type of the response.
    - payload: Additional data related to the response.
    """
    agentId: int
    delay: int
    type: str
    payload: dict[str, Any] | None  

    def serialize(self) -> dict:
        """
        Serializes the response into a dictionary format.
        """
        return {
            "agentId": self.agentId,
            "delay": self.delay,
            "type": self.type,
            "payload": self.payload,
        }

class SimulatorResponseBatch(BaseModel):
    """
    Represents a batch of responses from agents.

    Attributes:
    - responses: List of agent responses.
    """
    responses: list[SimulatorAgentResponse]

    def __init__(self, responses: list[SimulatorAgentResponse]):
        """
        Initializes the response batch.

        Args:
        - responses: List of agent responses to be included in the batch.
        """
        instructions = []
        for response in responses:
            if response:
                instructions.extend(response.serialize())
        super().__init__(responses=instructions)

    def serialize(self) -> dict:
        """
        Serializes the batch of responses into a dictionary format.

        Returns:
        - A dictionary representation of the response batch.
        """
        return {
            "responses": [response.serialize() for response in self.responses]
        }