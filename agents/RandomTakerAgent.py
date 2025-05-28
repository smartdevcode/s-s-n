from taos.im.agents import FinanceSimulationAgent
from taos.im.protocol import MarketSimulationStateUpdate, FinanceAgentResponse
from taos.im.protocol.models import OrderDirection, STP

import random

"""
A simple example agent which randomly places market orders.
"""
class RandomTakerAgent(FinanceSimulationAgent):
    def initialize(self):
        """
        Initialize properties, variables and quantities that will be used by the agent.
        The fields attached to `self.config` are defined in the launch parameters.
        """
        self.min_quantity = self.config.min_quantity
        self.max_quantity = self.config.max_quantity
        # Initialize a variable which allows to maintain the same direction of trade for a defined period
        self.direction = {}

    def respond(self, state : MarketSimulationStateUpdate) -> FinanceAgentResponse:
        """
        The main logic of the strategy executed when a new state is received from validator.
        Analyses the latest market state data and generates instructions to be submitted.
        """
        # Initialize a response class associated with the current miner
        response = FinanceAgentResponse(agent_id=self.uid)
        # Iterate over all the book realizations in the state message
        for book_id, book in state.books.items():
            # If we have not set a trade direction for this book, or 100 simulation seconds have elapsed
            if not book_id in self.direction or state.timestamp % 100_000_000_000 == 0:
                # Randomly select a new trade direction for the agent on this book
                self.direction[book_id] = random.choice([OrderDirection.BUY,OrderDirection.SELL])
            # Attach a market order instruction in the current trade direction for a random quantity within bounds defined by the parameters
            response.market_order(book_id=book_id, direction=self.direction[book_id], quantity=round(random.uniform(self.min_quantity,self.max_quantity),self.simulation_config.volumeDecimals), stp=STP.DECREASE_CANCEL)
        # Return the response with instructions appended
        # The response will be serialized and sent back to the validator for processing
        return response