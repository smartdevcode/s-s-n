from taos.common.agents import launch
from taos.im.agents import FinanceSimulationAgent
from taos.im.protocol.models import OrderDirection, STP
from taos.im.protocol.instructions import *
from taos.im.protocol import MarketSimulationStateUpdate, FinanceAgentResponse

import random

"""
A simple example agent which randomly places limit orders between the best levels of the book.
"""
class OrderOptionAgent(FinanceSimulationAgent):
    def initialize(self):
        """
        Initializes properties, variables and quantities that will be used by the agent.
        The fields attached to `self.config` are defined in the launch parameters.
        """
        self.min_quantity = self.config.min_quantity
        self.max_quantity = self.config.max_quantity

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
            bid = book.bids[0].price
            ask = book.asks[0].price
            # Populate prices which are expected to trigger key scenarios in Post-Only handling
            bidpricePO = bid
            bidpricePOFail = ask
            askpricePO = ask
            askpricePOFail = bid
            # Obtain a random quantity
            quantity = self.quantity()
            # Place a buy order which is expected to be opened on the book
            response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=quantity, price=bidpricePO, postOnly=True)
            # Place a buy order which is expected to be rejected due to post-only limitation
            response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=quantity, price=bidpricePOFail, postOnly=True)
            # Place a sell order which is expected to be opened on the book
            response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=askpricePO, postOnly=True)
            # Place a sell order which is expected to be rejected due to post-only limitation
            response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=askpricePOFail, postOnly=True)
            
            # # Populate prices which are expected to trigger key scenarios in Immediate-or-cancel order handling
            bidpriceIOCFull = ask + 10
            bidpriceIOCPartial = ask
            bidqtyIOCPartial = book.asks[0].quantity * 2
            bidpriceIOCCancel = bid
            askpriceIOCFull = bid - 10
            askpriceIOCPartial = bid
            askqtyIOCPartial = book.bids[0].quantity * 2
            askpriceIOCCancel = ask            
            # Place a buy IOC order which is expected to be traded in full when processed by the simulator
            response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=quantity, price=bidpriceIOCFull, timeInForce=TimeInForce.IOC)
            # Place a buy IOC order which is expected to be partially traded when processed by the simulator
            response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=bidqtyIOCPartial, price=bidpriceIOCPartial, timeInForce=TimeInForce.IOC)
            # Place a buy IOC order which is expected not to be matched (therefore rejected in full due to IOC flag)
            response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=quantity, price=bidpriceIOCCancel, timeInForce=TimeInForce.IOC)
            # Place a sell IOC order which is expected to be traded in full when processed by the simulator
            response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=askpriceIOCFull, timeInForce=TimeInForce.IOC)
            # Place a sell IOC order which is expected to be partially traded when processed by the simulator
            response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=askqtyIOCPartial, price=askpriceIOCPartial, timeInForce=TimeInForce.IOC)
            # Place a sell IOC order which is expected not to be matched (therefore rejected in full due to IOC flag)
            response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=askpriceIOCCancel, timeInForce=TimeInForce.IOC)
            
        # Return the response with instructions appended
        # The response will be serialized and sent back to the validator for processing
        return response

if __name__ == "__main__":
    launch(OrderOptionAgent)