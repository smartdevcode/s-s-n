# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import bittensor as bt
from abc import ABC, abstractmethod
from taos.common.agents import SimulationAgent
from taos.im.protocol import MarketSimulationStateUpdate, FinanceAgentResponse
from taos.im.protocol.events import *
from taos.im.utils import duration_from_timestamp

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
        self.history = []
        self.accounts = {}
        super().__init__(uid, config)

    def handle(self, state: MarketSimulationStateUpdate) -> FinanceAgentResponse:
        return super().handle(state)

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
        state.config = None
        simulation_ended = False
        update_text = ''
        debug_text = ''
        update_text += "\n" + '-' * 50 + "\n"
        update_text += f'VALIDATOR : {state.dendrite.hotkey} | SIMULATION TIME : {duration_from_timestamp(state.timestamp)} (T={state.timestamp})' + "\n"
        update_text += '-' * 50 + "\n"
        if len(self.events) > 0:
            update_text += 'EVENTS' + "\n"
            update_text += '-' * 50 + "\n"
            for event in self.events:
                match event.type:
                    case"RESET_AGENTS" | "RA":
                        update_text += f"{event}" + "\n"
                    case "EVENT_SIMULATION_START" | "ESS":
                        update_text += f"{event}" + "\n"
                        self.onStart(event)
                    case "EVENT_SIMULATION_END" | "ESE":
                        simulation_ended = True
                    case _:
                        pass
        else:
            update_text += 'NO EVENTS' + "\n"
            update_text += '-' * 50 + "\n"
        for book_id, account in self.accounts.items():
            debug_text += '-' * 50 + "\n"
            debug_text += f"BOOK {book_id}" + "\n"
            debug_text += '-' * 50 + "\n"
            debug_text += f"TOP LEVELS" + "\n"
            debug_text += '-' * 50 + "\n"
            debug_text += ' | '.join([f"{level.quantity:.4f}@{level.price}" for level in reversed(state.books[book_id].bids[:5])]) + '||' + ' | '.join([f"{level.quantity:.4f}@{level.price}" for level in state.books[book_id].asks[:5]]) + "\n"
            debug_text += '-' * 50 + "\n"
            debug_text += 'BALANCES' + "\n"
            debug_text += '-' * 50 + "\n"
            debug_text += f"BASE  : TOTAL={account.base_balance.total:.8f} FREE={account.base_balance.free:.8f} RESERVED={account.base_balance.reserved:.8f}" + "\n"
            debug_text += f"QUOTE : TOTAL={account.quote_balance.total:.8f} FREE={account.quote_balance.free:.8f} RESERVED={account.quote_balance.reserved:.8f}" + "\n"
            debug_text += '-' * 50 + "\n"
            debug_text += 'EVENTS' + "\n"
            debug_text += '-' * 50 + "\n"
            for event in self.events:
                if hasattr(event, 'bookId') and event.bookId == book_id:
                    debug_text += f"{event}" + "\n"
                    update_text += f"BOOK {book_id} : {event}" + "\n"
                    match event.type:
                        case "RESPONSE_DISTRIBUTED_PLACE_ORDER_LIMIT" | "RESPONSE_DISTRIBUTED_PLACE_ORDER_MARKET" | "RDPOL" | "RDPOM":
                            self.onOrderAccepted(event)
                        case "ERROR_RESPONSE_DISTRIBUTED_PLACE_ORDER_LIMIT" | "ERROR_RESPONSE_DISTRIBUTED_PLACE_ORDER_MARKET" | "ERDPOL" | "ERDPOM":
                            self.onOrderRejected(event)
                        case "RESPONSE_DISTRIBUTED_CANCEL_ORDERS" | "RDCO":
                            for cancellation in event.cancellations:
                                self.onOrderCancelled(cancellation)
                        case "ERROR_RESPONSE_DISTRIBUTED_CANCEL_ORDERS" | "ERDCO":
                            for cancellation in event.cancellations:
                                self.onOrderCancellationFailed(cancellation)
                        case "EVENT_TRADE" | "ET":
                            self.onTrade(event)
                        case _:
                            bt.logging.warning(f"Unknown event : {event}")
            if len(account.orders) > 0:
                debug_text += '-' * 50 + "\n"
                debug_text += 'ORDERS' + "\n"
                debug_text += '-' * 50 + "\n"
                for order in sorted(account.orders, key=lambda x: x.timestamp):
                    debug_text += f"#{order.id} : {'BUY ' if order.side == 0 else 'SELL'} {order.quantity}@{order.price} (PLACED AT T={order.timestamp})" + "\n"
            if account.fees:
                debug_text += '-' * 50 + "\n"
                debug_text += f'FEES : TRADED {account.fees.volume_traded} | MAKER {account.fees.maker_fee_rate * 100}% | TAKER {account.fees.taker_fee_rate * 100}%' + "\n"
                debug_text += '-' * 50 + "\n"
                for order in sorted(account.orders, key=lambda x: x.timestamp):
                    debug_text += f"#{order.id} : {'BUY ' if order.side == 0 else 'SELL'} {order.quantity}@{order.price} (PLACED AT T={order.timestamp})" + "\n"
            debug_text += '-' * 50 + "\n"
        if simulation_ended:
            update_text += f"{event}" + "\n"
            update_text += '-' * 50 + "\n"
            self.onEnd(event)
        bt.logging.debug("." + debug_text)        
        bt.logging.info("." + update_text)
    
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
        match event.message:
            case 'EXCEEDING_MAX_ORDERS':
                bt.logging.warning(f"FAILED TO PLACE {'BUY' if event.side == 0 else 'SELL'} ORDER FOR {event.quantity}@{event.price} ON BOOK {event.bookId} : You already have the maximum allowed number of open orders ({self.simulation_config.max_open_orders}) on this book.  You will not be able to place any more orders until you either cancel existing orders, or they are traded.")
            case _:
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
        update_text = '-' * 50 + "\n"
        if len(response.instructions) > 0:
            update_text += 'INSTRUCTIONS' + "\n"
            update_text += '-' * 50 + "\n"
            for instruction in response.instructions:
                update_text += f"{instruction}" + "\n"
        else:
            update_text += 'NO INSTRUCTIONS TO SUBMIT' + "\n"
        update_text += '-' * 50
        bt.logging.info(".\n" + update_text)