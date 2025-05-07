# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import bittensor as bt
from abc import ABC, abstractmethod
from taos.common.agents import SimulationAgent
from taos.im.protocol import MarketSimulationStateUpdate, FinanceAgentResponse
from taos.im.protocol.events import *

# Base class for agents operating in intelligent market simulations
class FinanceSimulationAgent(SimulationAgent):    
    def __init__(self, uid : int, config : object) -> None:
        """
        Initializer method that sets up the agent's unique ID and configuration, and initializes common objects for storing agent data.
        
        Args:
            uid (int): The UID of the agent in the subnet.
            config (obj): Config object for the agent.

        Returns:
            None
        """
        self.uid = uid
        self.config = config

        self.history = []
        self.accounts = {}
        self.initialize()

    def update(self, state : MarketSimulationStateUpdate) -> None:
        """
        Method to update the stored agent data, print relevant state information and trigger handlers for reported events.
        
        Args:
            state (taos.im.protocol.MarketSimulationStateUpdate): The UID of the agent in the subnet.

        Returns:
            None
        """
        self.history.append(state)
        self.history = self.history[-10:]
        self.accounts = state.accounts[self.uid]
        self.events = state.notices[self.uid]
        self.simulation_config = state.config
        update_text = ''
        update_text += "\n" + '-' * 50 + "\n"
        update_text += f'VALIDATOR : {state.dendrite.hotkey} | TIMESTAMP : {state.timestamp}' + "\n"
        for event in self.events:
            match event.type:
                case"RESET_AGENTS":
                    bt.logging.warning(f"Agent Balances Reset! {event}")
                case "EVENT_SIMULATION_START":
                    update_text += f"{event}" + "\n"
                    self.onStart(event)
                case "EVENT_SIMULATION_STOP":
                    update_text += f"{event}" + "\n"
                    self.onEnd(event)
                case _:
                    pass
        for book_id, account in self.accounts.items():
            update_text += '-' * 50 + "\n"
            update_text += f"BOOK {book_id}" + "\n"
            update_text += '-' * 50 + "\n"
            update_text += f"TOP LEVELS" + "\n"
            update_text += '-' * 50 + "\n"
            update_text += ' | '.join([f"{level.quantity:.4f}@{level.price}" for level in reversed(state.books[book_id].bids[:5])]) + '||' + ' | '.join([f"{level.quantity:.4f}@{level.price}" for level in state.books[book_id].asks[:5]]) + "\n"
            update_text += '-' * 50 + "\n"
            update_text += 'BALANCES' + "\n"
            update_text += '-' * 50 + "\n"
            update_text += f"BASE  : TOTAL={account.base_balance.total:.8f} FREE={account.base_balance.free:.8f} RESERVED={account.base_balance.reserved:.8f}" + "\n"
            update_text += f"QUOTE : TOTAL={account.quote_balance.total:.8f} FREE={account.quote_balance.free:.8f} RESERVED={account.quote_balance.reserved:.8f}" + "\n"
            update_text += '-' * 50 + "\n"
            update_text += 'EVENTS' + "\n"
            update_text += '-' * 50 + "\n"
            for event in self.events:    
                if hasattr(event, 'bookId') and event.bookId == book_id:
                    update_text += f"{event}" + "\n"
                    match event.type:
                        case "RESPONSE_DISTRIBUTED_PLACE_ORDER_LIMIT" | "RESPONSE_DISTRIBUTED_PLACE_ORDER_MARKET":
                            self.onOrderAccepted(event)
                        case "ERROR_RESPONSE_DISTRIBUTED_PLACE_ORDER_LIMIT" | "ERROR_RESPONSE_DISTRIBUTED_PLACE_ORDER_MARKET":
                            self.onOrderRejected(event)
                        case "RESPONSE_DISTRIBUTED_CANCEL_ORDERS":
                            for cancellation in event.cancellations:
                                self.onOrderCancelled(cancellation)
                        case "ERROR_RESPONSE_DISTRIBUTED_CANCEL_ORDERS":
                            for cancellation in event.cancellations:
                                self.onOrderCancellationFailed(cancellation)
                        case "EVENT_TRADE":
                            self.onTrade(event)
                        case _:
                            bt.logging.warning(f"Unknown event : {event}")
            if len(account.orders) > 0:
                update_text += '-' * 50 + "\n"
                update_text += 'ORDERS' + "\n"
                update_text += '-' * 50 + "\n"
                for order in sorted(account.orders, key=lambda x: x.timestamp):
                    update_text += f"#{order.id} : {'BUY ' if order.side == 0 else 'SELL'} {order.quantity}@{order.price} (PLACED AT T={order.timestamp})" + "\n"
            update_text += '-' * 50 + "\n"
        bt.logging.info("\n" + update_text)
    
    # Handler functions for various simulation events, to be overridden in agent implementations.
    def onStart(self, event : SimulationStartEvent) -> None:
        """
        Handler for simulation start event.  To be implemented by subclasses.
        
        Args:
            event (taos.im.protocol.events.SimulationStartEvent): The event class representing start of simulation.

        Returns:
            None
        """
        pass      

    def onOrderAccepted(self, event : OrderPlacementEvent) -> None:
        """
        Handler for event where order is accepted to the book by simulator.  To be implemented by subclasses.
        
        Args:
            event (taos.im.protocol.events.OrderPlacementEvent): The event class representing order placement.

        Returns:
            None
        """
        pass

    def onOrderRejected(self, event : OrderPlacementEvent) -> None:
        """
        Handler for event where order is rejected for placement by simulator.  To be implemented by subclasses.
        
        Args:
            event (taos.im.protocol.events.OrderPlacementEvent): The event class representing order rejection.

        Returns:
            None
        """
        pass

    def onOrderCancelled(self, event : OrderCancellationEvent) -> None:
        """
        Handler for event where order is cancelled in the simulator.  To be implemented by subclasses.
        
        Args:
            event (taos.im.protocol.events.OrderCancellationEvent): The event class representing order cancellation.

        Returns:
            None
        """
        pass        

    def onOrderCancellationFailed(self, event : OrderCancellationEvent) -> None:
        """
        Handler for event where order cancellation request is rejected by the simulator.  To be implemented by subclasses.
        
        Args:
            event (taos.im.protocol.events.OrderCancellationEvent): The event class representing order cancellation.

        Returns:
            None
        """
        pass

    def onTrade(self, event : TradeEvent) -> None:
        """
        Handler for event where an order is traded in the simulator.  To be implemented by subclasses.
        
        Args:
            event (taos.im.protocol.events.TradeEvent): The event class representing a trade.

        Returns:
            None
        """
        pass
        
    def onEnd(self, event : SimulationEndEvent) -> None:
        """
        Handler for simulation end event.  To be implemented by subclasses.
        
        Args:
            event (taos.im.protocol.events.SimulationEndEvent): The event class representing end of simulation.

        Returns:
            None
        """
        pass 

    def respond(self, state : MarketSimulationStateUpdate) -> FinanceAgentResponse:
        """
        Abstract method for handling generation of response to new state update.  To be implemented by subclasses.
        
        Args:
            state (taos.im.protocol.MarketSimulationStateUpdate): The class representing the latest state of the simulation.

        Returns:
            taos.im.protocol.FinanceAgentResponse : The response which will be attached to the synapse for return to the querying validator.
        """
        ...

    def report(self, state : MarketSimulationStateUpdate, response : FinanceAgentResponse) -> None:
        """
        Method for reporting the latest simulation state and the response generated by the agent.
        
        Args:
            state (taos.im.protocol.MarketSimulationStateUpdate): The class representing the latest state of the simulation.
            response (taos.im.protocol.FinanceAgentResponse): The class representing the response of the agent.

        Returns:
            None
        """
        update_text = 'INSTRUCTIONS' + "\n"
        update_text += '-' * 50 + "\n"
        for instruction in response.instructions:
            update_text += f"{instruction}" + "\n"
        update_text += '-' * 50
        bt.logging.info("\n" + update_text)