from taos.im.agents import FinanceSimulationAgent
from taos.im.protocol.models import OrderDirection, STP
from taos.im.protocol.instructions import *
from taos.im.protocol import MarketSimulationStateUpdate, FinanceAgentResponse

import random

"""
A simple example agent which randomly places limit orders between the best levels of the book.
"""
class RandomMakerAgent(FinanceSimulationAgent):
    def initialize(self):
        """
        Initializes properties, variables and quantities that will be used by the agent.
        The fields attached to `self.config` are defined in the launch parameters.
        """
        self.min_quantity = self.config.min_quantity
        self.max_quantity = self.config.max_quantity
        self.expiry_period = self.config.expiry_period

    def quantity(self):
        """
        Obtains a random quantity for order placement within the bounds defined by the agent strategy parameters.
        """
        return round(random.uniform(self.min_quantity,self.max_quantity),self.simulation_config.volumeDecimals)

    def respond(self, state : MarketSimulationStateUpdate) -> FinanceAgentResponse:
        """
        The main logic of the strategy executed when a new state is received from validator.
        Analyses the latest market state data and generates instructions to be submitted.
        """
        # Initialize a response class associated with the current miner
        response = FinanceAgentResponse(agent_id=self.uid)
        # Iterate over all the book realizations in the state message
        for book_id, book in state.books.items():
            # Iterate over all open orders belonging to the current agent in the current book
            for order in self.accounts[book_id].orders:
                # If the simulation timestamp of the latest state is after the time at which the agent wants to expire the order
                if state.timestamp > order.timestamp + self.expiry_period:
                    # Add a cancellation instruction for the order to response
                    response.cancel_order(book_id=book_id, order_id=order.id)
            # If the book is populated (it of course always should be)
            if len(book.bids) > 0 and len(book.asks) > 0:
                # Calculate placement prices for new orders to be a random distance between the current best bid and best ask
                bidprice = round(random.uniform(book.bids[0].price,book.asks[0].price),self.simulation_config.priceDecimals)
                askprice = round(random.uniform(bidprice,book.asks[0].price),self.simulation_config.priceDecimals)
            else:
                # Otherwise, place orders within 0.05 of the 100.0 price level
                bidprice = round(random.uniform(99.95,100.05),self.simulation_config.priceDecimals)
                askprice = round(random.uniform(bidprice,100.05),self.simulation_config.priceDecimals)
            # If the bid and ask prices are different i.e the spread is not too small to place both orders at different prices
            if bidprice != askprice:
                # Obtain a random quantity
                quantity = self.quantity()
                # If the agent can afford to place the buy order
                if self.accounts[book_id].quote_balance.free >= quantity * bidprice:
                    # Attach a buy limit order placement instruction to the response
                    response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=quantity, price=bidprice, stp=STP.CANCEL_BOTH)
                else:
                    print(f"Cannot place BUY order for {quantity}@{bidprice} : Insufficient quote balance!")
                # If the agent can afford to place the sell order
                if self.accounts[book_id].base_balance.free >= quantity:
                    # Attach a sell limit order placement instruction to the response
                    response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=askprice, stp=STP.CANCEL_NEWEST)
                else:
                    print(f"Cannot place SELL order for {quantity}@{askprice} : Insufficient base balance!")
        # Return the response with instructions appended
        # The response will be serialized and sent back to the validator for processing
        return response