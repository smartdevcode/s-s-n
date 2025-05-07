# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
from pydantic import BaseModel, Field
from typing import Annotated, Union
from annotated_types import Len
from taos.im.protocol.simulator import *
from taos.common.protocol import AgentResponse
from taos.im.protocol.instructions import PlaceMarketOrderInstruction, PlaceLimitOrderInstruction, CancelOrdersInstruction, CancelOrderInstruction, ResetAgentsInstruction

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
    def market_order(self, book_id, direction, quantity, delay=0, clientOrderId=None) -> None:
        """
        Convenience function to attach a market order placement instruction to the response.
        """
        self.add_instruction(PlaceMarketOrderInstruction(agentId=self.agent_id, delay=delay, bookId=book_id, direction=direction, quantity=quantity, clientOrderId=clientOrderId))
    def limit_order(self, book_id, direction, quantity, price, delay=0, clientOrderId=None) -> None:
        """
        Convenience function to attach a limit order placement instruction to the response.
        """
        self.add_instruction(PlaceLimitOrderInstruction(agentId=self.agent_id, delay=delay, bookId=book_id, direction=direction, quantity=quantity, price=price, clientOrderId=clientOrderId))
    def cancel_order(self, book_id, order_id, quantity=None, delay=0) -> None:
        """
        Convenience function to attach a cancellation instruction for a single order to the response.
        """
        self.add_instruction(CancelOrdersInstruction(agentId=self.agent_id, delay=delay, bookId=book_id, cancellations=[CancelOrderInstruction(orderId=order_id, volume=quantity)]))
    def cancel_orders(self, book_id, order_ids, delay=0) -> None:
        """
        Convenience function to attach a cancellation instruction for multiple orders to the response.
        """
        self.add_instruction(CancelOrdersInstruction(agentId=self.agent_id, delay=delay, bookId=book_id, cancellations=[CancelOrderInstruction(orderId=order_id, volume=None) for order_id in order_ids]))
    def reset_agents(self, agent_ids, delay=0) -> None:
        """
        Convenience function to attach an agent reset instruction to the response (only available to validators).
        """
        self.add_instruction(ResetAgentsInstruction(agentId=self.agent_id, delay=delay, agentIds=agent_ids))