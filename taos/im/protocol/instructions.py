# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
from pydantic import BaseModel, PositiveFloat, NonNegativeInt, PositiveInt
from typing import Literal
from taos.im.protocol.simulator import *
from taos.common.protocol import AgentInstruction
from taos.im.protocol.models import OrderDirection, STP, TimeInForce, OrderCurrency

"""
Classes representing instructions that may be submitted by miner agents in a intelligent market simulation are defined here.
"""
    
class FinanceAgentInstruction(AgentInstruction):
    """
    Base class representing an instruction submitted by an agent in a intelligent markets simulation.    

    Attributes:
    - agentId: The ID of the agent that submitted the instruction.
    - delay: The processing delay to be assigned to the instruction.  This is set by validators based on the actual response time of the miner, 
             and determines how many simulation steps will elapse after submission before the agent instruction is processed.
    - type : String identifier for the type of the submitted instruction in the simulator.
    """
    agentId : int
    delay: NonNegativeInt = 0
    type : Literal["PLACE_ORDER_MARKET", "PLACE_ORDER_LIMIT", "CANCEL_ORDERS", "RESET_AGENT"]
    
    def serialize(self) -> dict:
        return {
            "agentId": self.agentId,
            "delay": self.delay,
            "type": self.type,
            "payload": self.payload()
        }
    
    def __str__(self):
        return f"{self.type} ON BOOK {self.bookId} : {self.payload()}"
    
class PlaceOrderInstruction(FinanceAgentInstruction):
    """
    Base class representing an instruction by an agent to place an order.

    Attributes:
    - bookId: The ID of the book on which the order is to be placed.
    - direction: Indicates whether the order is to buy or sell.
    - quantity : The size of the order to be placed in base currency.
    """
    bookId: NonNegativeInt
    direction : Literal[OrderDirection.BUY, OrderDirection.SELL]
    quantity : PositiveFloat
    clientOrderId : int | None    
    stp : Literal[STP.CANCEL_OLDEST, STP.CANCEL_NEWEST, STP.CANCEL_BOTH, STP.DECREASE_CANCEL] = STP.CANCEL_OLDEST
    currency : Literal[OrderCurrency.BASE, OrderCurrency.QUOTE] = OrderCurrency.BASE
    
    def __str__(self):
        return f"{'BUY ' if self.direction == OrderDirection.BUY else 'SELL'} {self.quantity} ON BOOK {self.bookId}"
    
class PlaceMarketOrderInstruction(PlaceOrderInstruction):
    """
    Class representing an instruction by an agent to place a market order.
    """
    type : Literal['PLACE_ORDER_MARKET'] = 'PLACE_ORDER_MARKET'
    def payload(self) -> dict:
        return {
            "direction": self.direction,
            "volume": self.quantity,
            "bookId":self.bookId,
            "clientOrderId":self.clientOrderId,
            "stpFlag":self.stp,
            "currency":self.currency
        }
    
    def __str__(self):
        return f"{'BUY ' if self.direction == OrderDirection.BUY else 'SELL'} {self.quantity}{'' if self.currency==OrderCurrency.BASE else 'QUOTE'}@MARKET ON BOOK {self.bookId}"
        
class PlaceLimitOrderInstruction(PlaceOrderInstruction):
    """
    Class representing an instruction by an agent to place a limit order.

    Attributes:
    - price: The price level at which the order is to be placed.    
    """
    type : Literal['PLACE_ORDER_LIMIT'] = 'PLACE_ORDER_LIMIT'
    price : PositiveFloat
    postOnly : bool = False
    timeInForce : Literal[TimeInForce.GTC, TimeInForce.GTT, TimeInForce.IOC, TimeInForce.FOK] = TimeInForce.GTC
    expiryPeriod : PositiveInt | None = None
    def payload(self) -> dict:
        return {
            "direction": self.direction,
            "volume": self.quantity,
            "price": self.price,
            "bookId": self.bookId,
            "clientOrderId":self.clientOrderId,
            "postOnly" : self.postOnly,
            "timeInForce" : self.timeInForce,
            "expiryPeriod" : self.expiryPeriod,
            "stpFlag" : self.stp
        }
    
    def __str__(self):
        return f"{'BUY ' if self.direction == OrderDirection.BUY else 'SELL'} {self.quantity}@{self.price} ON BOOK {self.bookId}"
    
class CancelOrderInstruction(BaseModel):
    """
    Class representing an instruction by an agent to cancel an open limit order.

    Attributes:
    - orderId: The simulator-assigned ID of the order to be cancelled.    
    - volume : The quantity of the order that should be cancelled (`None` to cancel the entire remaining order size).
    """
    orderId : int
    volume : PositiveFloat | None
    def serialize(self) -> dict:
        return {
            "orderId" : self.orderId,
            "volume" : self.volume
        }
    
    def __str__(self):
        return f"CANCEL ORDER #{self.orderId}{' FOR ' + str(self.volume) if self.volume else ''}"
        
class CancelOrdersInstruction(FinanceAgentInstruction):
    """
    Class representing an instruction by an agent to cancel a list of open limit orders.

    Attributes:
    - bookId : The ID of the book on which cancellations are to be performed.
    - cancellations: A list of CancelOrderInstruction objects.    
    """
    bookId: NonNegativeInt
    type : Literal['CANCEL_ORDERS'] = 'CANCEL_ORDERS'
    cancellations : list[CancelOrderInstruction]
    def payload(self) -> dict:
        return {
            "cancellations": [cancellation.serialize() for cancellation in self.cancellations],
            "bookId": self.bookId
        }
    
    def __str__(self):
        return "\n".join([f"{c} ON BOOK {self.bookId}" for c in self.cancellations])
        
class ResetAgentsInstruction(FinanceAgentInstruction):
    """
    Class representing an instruction to reset an agent's accounts.  
    This instruction can only be submitted by validators to handle deregistration of a miner.

    Attributes:
    - agentIds : List of IDs of the agents for which reset should be applied.
    """
    type : Literal['RESET_AGENT'] = 'RESET_AGENT'
    agentIds : list[int]
    def payload(self) -> dict:
        return {
            "agentIds": self.agentIds
        }
    
    def __str__(self):
        return f"RESET AGENTS {','.join(['#' + str(agentId) for agentId in self.agentIds])}"