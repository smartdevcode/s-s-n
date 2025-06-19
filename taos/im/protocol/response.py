# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import bittensor as bt
from pydantic import Field
from typing import Annotated, Union, List
from annotated_types import Len
from taos.im.protocol.simulator import *
from taos.common.protocol import AgentResponse
from taos.im.protocol.instructions import PlaceMarketOrderInstruction, PlaceLimitOrderInstruction, CancelOrdersInstruction, CancelOrderInstruction, ResetAgentsInstruction
from taos.im.protocol.models import OrderDirection, STP, TimeInForce, OrderCurrency

FinanceInstruction = Annotated[
    Union[PlaceMarketOrderInstruction, PlaceLimitOrderInstruction, CancelOrdersInstruction, ResetAgentsInstruction],
    Field(discriminator="type")
]

class FinanceAgentResponse(AgentResponse):
    """
    Finance agent response class.

    This is the class type which miners are expected to populate and attach to the MarketSimulationState synapse `response` property.

    Attributes:
    - instructions: List of FinanceInstruction objects representing the actions the miner agent wishes to take.
    """
    instructions : Annotated[list[FinanceInstruction],Len(min_length=0, max_length=200000)]  = []
    def market_order(self, book_id : int, direction : OrderDirection, quantity : float, delay : int = 0, clientOrderId : int | None = None, stp : STP = STP.CANCEL_OLDEST, currency : OrderCurrency = OrderCurrency.BASE) -> None:
        """
        Convenience function to attach a market order placement instruction to the response.
        """
        self.add_instruction(PlaceMarketOrderInstruction(agentId=self.agent_id, delay=delay, bookId=book_id, direction=direction, quantity=quantity, clientOrderId=clientOrderId, stp=stp, currency=currency))
        
    def limit_order(self, book_id : int, direction : OrderDirection, quantity : float, price : float, delay : int = 0, clientOrderId : int | None = None, stp : STP = STP.CANCEL_OLDEST, postOnly : bool = False, timeInForce : TimeInForce = TimeInForce.GTC, expiryPeriod : int | None = None) -> None:
        """
        Convenience function to attach a limit order placement instruction to the response.
        """
        if timeInForce == TimeInForce.GTT and not expiryPeriod:
            bt.logging.error(f"Invalid limit order parameters : If using TimeInForce.GTT, expiryPeriod must be specified.")
            return
        if timeInForce in [TimeInForce.IOC, TimeInForce.FOK] and postOnly:
            bt.logging.error(f"Invalid limit order parameters : If using TimeInForce.IOC or FOK, postOnly must be false (IOC and FOK orders cannot open on the book).")
            return
        if timeInForce != TimeInForce.GTT and expiryPeriod:
            bt.logging.warning(f"Limit order parameters : expiryPeriod is set without TimeInForce.GTT - expiry will be ignored.")
        self.add_instruction(PlaceLimitOrderInstruction(agentId=self.agent_id, delay=delay, bookId=book_id, direction=direction, quantity=quantity, price=price, clientOrderId=clientOrderId, stp=stp, postOnly=postOnly, timeInForce=timeInForce, expiryPeriod=expiryPeriod))
        
    def cancel_order(self, book_id : int, order_id : int, quantity : float | None = None, delay : int = 0) -> None:
        """
        Convenience function to attach a cancellation instruction for a single order to the response.
        """
        self.add_instruction(CancelOrdersInstruction(agentId=self.agent_id, delay=delay, bookId=book_id, cancellations=[CancelOrderInstruction(orderId=order_id, volume=quantity)]))
        
    def cancel_orders(self, book_id : int, order_ids : List[int], delay : int = 0) -> None:
        """
        Convenience function to attach a cancellation instruction for multiple orders to the response.
        """
        self.add_instruction(CancelOrdersInstruction(agentId=self.agent_id, delay=delay, bookId=book_id, cancellations=[CancelOrderInstruction(orderId=order_id, volume=None) for order_id in order_ids]))
        
    def reset_agents(self, agent_ids : List[int], delay : int = 0) -> None:
        """
        Convenience function to attach an agent reset instruction to the response (only available to validators).
        """
        self.add_instruction(ResetAgentsInstruction(agentId=self.agent_id, delay=delay, agentIds=agent_ids))