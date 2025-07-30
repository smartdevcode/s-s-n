# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import bittensor as bt
from taos.common.agents import launch
from taos.im.agents import FinanceSimulationAgent
from taos.im.protocol.models import *
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
        self.min_leverage = self.config.min_leverage if hasattr(self.config, 'min_leverage') else 0.0
        self.max_leverage = self.config.max_leverage if hasattr(self.config, 'max_leverage') else 0.0
        self.expiry_period = self.config.expiry_period
        self.open_order = {}

    def quantity(self):
        """
        Obtains a random quantity for order placement within the bounds defined by the agent strategy parameters.
        """
        return round(random.uniform(self.min_quantity,self.max_quantity),self.simulation_config.volumeDecimals)

    def leverage(self):
        """
        Obtains a random quantity for order placement within the bounds defined by the agent strategy parameters.
        """
        return round(random.uniform(self.min_leverage,self.max_leverage),2)

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
                # Obtain a random leverage value
                leverage = self.leverage()
                bt.logging.info(f"BOOK {book_id} | QUOTE : {self.accounts[book_id].quote_balance.total} [LOAN {self.accounts[book_id].quote_loan} | COLLAT {self.accounts[book_id].quote_collateral}]")
                bt.logging.info(f"BOOK {book_id} | BASE : {self.accounts[book_id].base_balance.total} [LOAN {self.accounts[book_id].base_loan} | COLLAT {self.accounts[book_id].base_collateral}]")
                # If the agent can afford to place the buy order
                if self.accounts[book_id].quote_balance.free >= quantity * bidprice:
                    # Attach a buy limit order placement instruction to the response
                    # On the BUY side, we place leveraged orders according to the config
                    response.limit_order(
                        book_id=book_id, 
                        direction=OrderDirection.BUY, 
                        quantity=quantity, price=bidprice, 
                        stp=STP.CANCEL_BOTH, 
                        timeInForce=TimeInForce.GTT, expiryPeriod=self.expiry_period,
                        leverage=leverage)
                else:
                    print(f"Cannot place BUY order for {quantity}@{bidprice} : Insufficient quote balance!")
                # If the agent can afford to place the sell order
                if self.accounts[book_id].base_balance.free >= quantity:
                    # Attach a sell limit order placement instruction to the response
                    # In the SELL case, the order quantity is adjusted to approximate that of the leveraged buy order
                    # By setting LoanSettlementOption.FIFO, these orders will repay the loans taken in executing the BUY orders.
                    response.limit_order(
                        book_id=book_id, 
                        direction=OrderDirection.SELL, 
                        quantity=round(quantity * 1 + leverage, self.simulation_config.volumeDecimals), price=askprice, 
                        stp=STP.CANCEL_NEWEST, 
                        timeInForce=TimeInForce.GTT, expiryPeriod=self.expiry_period,
                        settlement_option=LoanSettlementOption.FIFO
                    )
                else:
                    print(f"Cannot place SELL order for {quantity}@{askprice} : Insufficient base balance!")
        # Return the response with instructions appended
        # The response will be serialized and sent back to the validator for processing
        return response

if __name__ == "__main__":
    """
    Example command for local standalone testing execution using Proxy:
    python RandomMakerAgent.py --port 8888 --agent_id 0 --params min_quantity=0.1 max_quantity=1.0 min_leverage=0.0 max_leverage=1.0 expiry_period=200000000000
    """
    launch(RandomMakerAgent)