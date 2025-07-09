# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
from taos.common.agents import launch
from taos.im.agents import FinanceSimulationAgent
from taos.im.protocol.models import *
from taos.im.protocol.instructions import *
from taos.im.protocol import MarketSimulationStateUpdate, FinanceAgentResponse

import random

"""
A simple example agent to demonstrate usage of advanced order options.
"""
class OrderOptionAgent(FinanceSimulationAgent):
    def initialize(self):
        """
        Initializes properties, variables and quantities that will be used by the agent.
        The fields attached to `self.config` are defined in the launch parameters.
        """
        self.min_quantity = self.config.min_quantity
        self.max_quantity = self.config.max_quantity
        # Process config flags indicating which tests are to be run
        self.tests = {
            'PO' : bool(self.config.PO) if hasattr(self.config, 'PO') else False,
            'GTT' : bool(self.config.GTT) if hasattr(self.config, 'GTT') else False,
            'IOC' : bool(self.config.IOC) if hasattr(self.config, 'IOC') else False,
            'FOK' : bool(self.config.FOK) if hasattr(self.config, 'FOK') else False,
            'QUOTE' : bool(self.config.QUOTE) if hasattr(self.config, 'QUOTE') else False
        }
        # If no tests explicitly specified in launch parameters, assume all tests should be run
        if not any(self.tests.values()):
            self.tests = {k : True for k in self.tests}

    def quantity(self):
        """
        Obtains a random quantity for order placement within the bounds defined by the agent strategy parameters.
        """
        return round(random.uniform(self.min_quantity,self.max_quantity),self.simulation_config.volumeDecimals)

    def respond(self, state : MarketSimulationStateUpdate) -> FinanceAgentResponse:
        """
        The main logic of the strategy executed when a new state is received from validator.
        Analyses the latest market state data and generates instructions to be submitted.

        Args:
            state (MarketSimulationStateUpdate): The current market state data 
                provided by the simulation validator.

        Returns:
            FinanceAgentResponse: A response object containing the list of 
                instructions (e.g., limit orders) to submit to the market.
        """
        # Initialize a response class associated with the current miner
        response = FinanceAgentResponse(agent_id=self.uid)
        # Iterate over all the book realizations in the state message
        for book_id, book in state.books.items():
            bid = book.bids[0].price
            ask = book.asks[0].price
            bidvol = book.bids[0].quantity
            askvol = book.asks[0].quantity
            # Obtain a random quantity
            quantity = self.quantity()
            
            if self.tests['QUOTE']:                
                response.market_order(book_id=book_id, direction=OrderDirection.BUY, quantity=round(ask * (askvol / 2),self.simulation_config.quoteDecimals), currency=OrderCurrency.QUOTE)
                response.market_order(book_id=book_id, direction=OrderDirection.BUY, quantity=round(ask * askvol,self.simulation_config.quoteDecimals), currency=OrderCurrency.QUOTE)
                response.market_order(book_id=book_id, direction=OrderDirection.SELL, quantity=round(bid * (bidvol / 2),self.simulation_config.quoteDecimals), currency=OrderCurrency.QUOTE)
                response.market_order(book_id=book_id, direction=OrderDirection.SELL, quantity=round(bid * bidvol,self.simulation_config.quoteDecimals), currency=OrderCurrency.QUOTE)
            
            if self.tests['PO']:
                # Populate prices which are expected to trigger key scenarios in Post-Only handling
                bidpricePO = bid
                bidpricePOFail = ask
                askpricePO = ask
                askpricePOFail = bid
                # Place a buy order which is expected to be opened on the book
                response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=quantity, price=bidpricePO, postOnly=True)
                # Place a buy order which is expected to be rejected due to post-only limitation
                response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=quantity, price=bidpricePOFail, postOnly=True)
                # Place a sell order which is expected to be opened on the book
                response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=askpricePO, postOnly=True)
                # Place a sell order which is expected to be rejected due to post-only limitation
                response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=askpricePOFail, postOnly=True)
            
            if self.tests['GTT']:
                # Place a buy order with expiry in 10 seconds
                response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=quantity, price=bid, timeInForce=TimeInForce.GTT, expiryPeriod=10_000_000_000)
                # Place a sell order with expiry in 10 seconds
                response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=ask, timeInForce=TimeInForce.GTT, expiryPeriod=10_000_000_000)
                
                # Place a sell order with TimeInForce.GTT and no expiry (INVALID)
                response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=ask, timeInForce=TimeInForce.GTT)
                # Place a sell order without TimeInForce.GTT and expiry given (WARNING)
                response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=ask, timeInForce=TimeInForce.GTC, expiryPeriod=10000000000)
            
            if self.tests['IOC']:
                # Populate prices which are expected to trigger key scenarios in Immediate-or-cancel order handling
                bidpriceIOCFull = ask + 10
                bidpriceIOCPartial = ask
                bidqtyIOCPartial = askvol * 2
                bidpriceIOCCancel = bid
                askpriceIOCFull = bid - 10
                askpriceIOCPartial = bid
                askqtyIOCPartial = bidvol * 2
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
                
                # Place an IOC order with postOnly=True (INVALID)
                response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=askpricePOFail, postOnly=True, timeInForce=TimeInForce.IOC)
            
            if self.tests['FOK']:
                # Populate prices and quantities which are expected to trigger key scenarios in Fill-or-kill order handling
                bidpriceFOKFull = ask + 10
                bidpriceFOKPartial = ask
                bidqtyFOKPartial = book.asks[0].quantity * 2
                bidpriceFOKCancel = bid
                askpriceFOKFull = bid - 10
                askpriceFOKPartial = bid
                askqtyFOKPartial = book.bids[0].quantity * 2
                askpriceFOKCancel = ask            
                # Place a buy FOK order which is expected to be traded in full when processed by the simulator
                response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=quantity, price=bidpriceFOKFull, timeInForce=TimeInForce.FOK)
                # Place a buy FOK order which is expected to attempt partial trade when processed by the simulator (therefore rejected in full due to FOK flag)
                response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=bidqtyFOKPartial, price=bidpriceFOKPartial, timeInForce=TimeInForce.FOK)
                # Place a buy FOK order which is expected not to be matched (therefore rejected in full due to FOK flag)
                response.limit_order(book_id=book_id, direction=OrderDirection.BUY, quantity=quantity, price=bidpriceFOKCancel, timeInForce=TimeInForce.FOK)
                # Place a sell FOK order which is expected to be traded in full when processed by the simulator
                response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=askpriceFOKFull, timeInForce=TimeInForce.FOK)
                # Place a sell FOK order which is expected to attempt partial trade when processed by the simulator (therefore rejected in full due to FOK flag)
                response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=askqtyFOKPartial, price=askpriceFOKPartial, timeInForce=TimeInForce.FOK)
                # Place a sell FOK order which is expected not to be matched (therefore rejected in full due to FOK flag)
                response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=askpriceFOKCancel, timeInForce=TimeInForce.FOK)
                
                # Place an FOK order with postOnly=True (INVALID)
                response.limit_order(book_id=book_id, direction=OrderDirection.SELL, quantity=quantity, price=askpricePOFail, postOnly=True, timeInForce=TimeInForce.FOK)
            
            
        # Return the response with instructions appended
        # The response will be serialized and sent back to the validator for processing
        return response

if __name__ == "__main__":
    """
    Example command for local standalone testing execution using Proxy:
    python OrderOptionAgent.py --port 8888 --agent_id 0 --params min_quantity=0.1 max_quantity=1.0 expiry_period=200 PO=1 GTT=1 IOC=1 FOK=1 QUOTE=1"
    """
    launch(OrderOptionAgent)