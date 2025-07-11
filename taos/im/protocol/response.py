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

    This class is used by miner agents to populate and attach responses to the
    `MarketSimulationState.response` property in the market simulation. It encapsulates
    a list of financial instructions representing the agent's intended actions.

    Attributes:
        instructions (list[FinanceInstruction]): 
            A list of instructions that the miner agent wishes to execute. 
            These can include market orders, limit orders or cancellations.
    """
    
    instructions: Annotated[
        list[FinanceInstruction],
        Len(min_length=0, max_length=200_000)
    ] = []

    def market_order(
        self, 
        book_id: int, 
        direction: OrderDirection, 
        quantity: float, 
        delay: int = 0, 
        clientOrderId: int | None = None, 
        stp: STP = STP.CANCEL_OLDEST, 
        currency: OrderCurrency = OrderCurrency.BASE
    ) -> None:
        """
        Add a market order instruction to the agent response.

        Args:
            book_id (int): The ID of the order book to place the market order in.
            direction (OrderDirection): Direction of the order (OrderDirection.BUY or OrderDirection.SELL).
            quantity (float): Size of the order in `currency`.
            delay (int, optional): Delay in simulation nanoseconds which must elapse before the instruction is processed at the exchange. 
                                This delay will be added to the delay calculated based on your response time to the validator.
                                Defaults to 0.
            clientOrderId (int | None, optional): Optional client-specified order ID for tracking.
            stp (STP, optional): Self-trade prevention strategy (`STP.NO_STP`, `STP.CANCEL_OLDEST`, `STP.CANCEL_NEWEST`, `STP.CANCEL_BOTH` or `STP.DECREASE_CANCEL`). 
                                Defaults to STP.CANCEL_OLDEST.
            currency (OrderCurrency, optional): Currency to use for the order quantity (OrderCurrency.BASE or OrderCurrency.QUOTE). 
                                If set to `OrderCurrency.QUOTE`, the `quantity` will be interpreted as the amount of QUOTE currency that the agent wishes to exchange.
                                The matching engine at the simulator will determine the corresponding BASE amount to assign based on the asset price at the time of execution.
                                Defaults to BASE.

        Returns:
            None
        """
        self.add_instruction(
            PlaceMarketOrderInstruction(
                agentId=self.agent_id, 
                delay=delay, 
                bookId=book_id, 
                direction=direction, 
                quantity=quantity, 
                clientOrderId=clientOrderId, 
                stp=stp, 
                currency=currency
            )
        )

    def limit_order(
        self, 
        book_id: int, 
        direction: OrderDirection, 
        quantity: float, 
        price: float, 
        delay: int = 0, 
        clientOrderId: int | None = None, 
        stp: STP = STP.CANCEL_OLDEST, 
        postOnly: bool = False, 
        timeInForce: TimeInForce = TimeInForce.GTC, 
        expiryPeriod: int | None = None
    ) -> None:
        """
        Add a limit order instruction to the agent response.

        Args:
            book_id (int): The ID of the order book to place the limit order in.
            direction (OrderDirection): Direction of the order (BUY or SELL).
            quantity (float): Quantity of the asset to trade.
            price (float): Price at which to place the limit order.
            delay (int, optional): Delay in simulation nanoseconds which must elapse before the instruction is processed at the exchange. 
                                This delay will be added to the delay calculated based on your response time to the validator.
                                Defaults to 0.
            clientOrderId (int | None, optional): Optional client-specified order ID for tracking.
            stp (STP, optional): Self-trade prevention strategy (`STP.NO_STP`, `STP.CANCEL_OLDEST`, `STP.CANCEL_NEWEST`, `STP.CANCEL_BOTH` or `STP.DECREASE_CANCEL`). 
                                Defaults to STP.CANCEL_OLDEST.
            postOnly (bool, optional): If True, prevents the order from matching immediately.  
                                If the limit order would match with any existing levels on the book at the time of processing, 
                                the instruction is rejected and no trade or order placement will take place.
                                Defaults to False.
            timeInForce (TimeInForce, optional): Time-in-force option to be applied for the order (`TimeInForce.GTC`, `TimeInForce.GTT`, `TimeInForce.IOC`, `TimeInForce.FOK`).
                                Good Till Cancelled : Order remains on the book until cancelled by the agent, or executed in a trade.
                                Good Till Time : Order remains on the book for `expiryPeriod` simulation nanoseconds unless traded or cancelled before expiry.
                                Immediate Or Cancel : Any part of the order which is not immediately traded will be cancelled.
                                Fill Or Kill : If the order will not be executed in its entirety immediately upon receipt by the simulator, the order will be rejected.
                                Defaults to GTC.
            expiryPeriod (int | None, optional): Expiry period for GTT (Good Till Time) orders, in simulation nanoseconds.

        Returns:
            None

        Notes:
            - If `timeInForce` is GTT, `expiryPeriod` must be specified.
            - If `timeInForce` is IOC (Immediate or Cancel) or FOK (Fill or Kill), `postOnly` must be False.
            - If `expiryPeriod` is specified but `timeInForce` is not GTT, expiry is ignored.
        """
        if timeInForce == TimeInForce.GTT and not expiryPeriod:
            bt.logging.error(
                "Invalid limit order parameters: If using TimeInForce.GTT, expiryPeriod must be specified."
            )
            return
        if timeInForce in [TimeInForce.IOC, TimeInForce.FOK] and postOnly:
            bt.logging.error(
                "Invalid limit order parameters: IOC/FOK orders cannot be postOnly."
            )
            return
        if timeInForce != TimeInForce.GTT and expiryPeriod:
            bt.logging.warning(
                "Limit order parameters: expiryPeriod is set without TimeInForce.GTT - expiry will be ignored."
            )

        self.add_instruction(
            PlaceLimitOrderInstruction(
                agentId=self.agent_id, 
                delay=delay, 
                bookId=book_id, 
                direction=direction, 
                quantity=quantity, 
                price=price, 
                clientOrderId=clientOrderId, 
                stp=stp, 
                postOnly=postOnly, 
                timeInForce=timeInForce, 
                expiryPeriod=expiryPeriod
            )
        )

    def cancel_order(
        self, 
        book_id: int, 
        order_id: int, 
        quantity: float | None = None, 
        delay: int = 0
    ) -> None:
        """
        Add a cancellation instruction for a single order.

        Args:
            book_id (int): The ID of the order book where the order exists.
            order_id (int): The ID of the order to cancel.
            quantity (float | None, optional): Quantity (in BASE) to cancel (if None, cancels the entire order).
            delay (int, optional): Delay in simulation nanoseconds which must elapse before the instruction is processed at the exchange. 
                                This delay will be added to the delay calculated based on your response time to the validator.
                                Defaults to 0.

        Returns:
            None
        """
        self.add_instruction(
            CancelOrdersInstruction(
                agentId=self.agent_id, 
                delay=delay, 
                bookId=book_id, 
                cancellations=[CancelOrderInstruction(orderId=order_id, volume=quantity)]
            )
        )

    def cancel_orders(
        self, 
        book_id: int, 
        order_ids: list[int], 
        delay: int = 0
    ) -> None:
        """
        Add a cancellation instruction for multiple orders.

        Args:
            book_id (int): The ID of the order book where the orders exist.
            order_ids (list[int]): A list of order IDs to cancel.
            delay (int, optional): Delay in simulation nanoseconds which must elapse before the instruction is processed at the exchange. 
                                This delay will be added to the delay calculated based on your response time to the validator.
                                Defaults to 0.

        Returns:
            None
        """
        self.add_instruction(
            CancelOrdersInstruction(
                agentId=self.agent_id, 
                delay=delay, 
                bookId=book_id, 
                cancellations=[
                    CancelOrderInstruction(orderId=order_id, volume=None) 
                    for order_id in order_ids
                ]
            )
        )

    def reset_agents(
        self, 
        agent_ids: list[int], 
        delay: int = 0
    ) -> None:
        """
        Add a reset instruction for one or more agents.

        Args:
            agent_ids (list[int]): List of agent IDs to reset.
            delay (int, optional): Delay in milliseconds before executing the reset. Defaults to 0.

        Returns:
            None

        Notes:
            This function is only available to validator agents for handling miner deregistrations.
        """
        self.add_instruction(
            ResetAgentsInstruction(
                agentId=self.agent_id, 
                delay=delay, 
                agentIds=agent_ids
            )
        )
