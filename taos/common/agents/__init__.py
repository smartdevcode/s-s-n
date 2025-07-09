# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import os
from abc import ABC, abstractmethod  # Importing the ABC class and abstractmethod decorator for creating abstract base classes
from fastapi import APIRouter
from taos.common.protocol import SimulationStateUpdate, AgentResponse, EventNotification  # Importing required classes for simulation state and agent responses

# Defining an abstract base class for simulation agents
class SimulationAgent(ABC):
    def __init__(self, uid, config, log_dir):
        """
        Initializer method that sets up the agent's unique ID and configuration.
        """
        self.uid = uid
        self.config = config
        self.log_dir = log_dir
        self.state_file = os.path.join(log_dir, 'state.mp')
        self.router = APIRouter()
        self.router.add_api_route("/handle", self.handle, methods=["POST"])
        self.initialize()  # Calling the abstract method to perform any agent-specific setup

    def handle(self, state: SimulationStateUpdate) -> AgentResponse:
        """
        Method to handle a new simulation state update.
        """
        self.update(state)  # Update the agent's state based on the new simulation state
        response = self.respond(state)  # Generate a response based on the current state
        self.report(state, response)  # Report the state and response (for logging or other purposes)
        return response  # Return the generated response

    def process(self, notification: EventNotification) -> EventNotification:
        """
        Method to handle a new event notification.
        """
        notification.acknowledged = True
        return notification

    @abstractmethod
    def initialize(self):
        """
        Abstract method for initialization logic, to be implemented by subclasses.
        """
        ...

    @abstractmethod
    def update(self, state: SimulationStateUpdate) -> None:
        """
        Abstract method to update the agent's state, to be implemented by subclasses.
        
        Args:
            state (taos.common.protocol.SimulationStateUpdate): The synapse object containing the latest simulation state update.

        Returns:
            None
        """
        ...

    @abstractmethod
    def respond(self, state: SimulationStateUpdate) -> AgentResponse:
        """
        Abstract method to create a response based on the current state, to be implemented by subclasses.
        
        Args:
            state (taos.common.protocol.SimulationStateUpdate): The synapse object containing the latest simulation state update.

        Returns:
            taos.common.protocol.AgentResponse: AgentResponse object which will be attached to the synapse for return to querying validator.
        """
        ...

    @abstractmethod
    def report(self, state: SimulationStateUpdate, response: AgentResponse) -> None:
        """
        Abstract method for reporting the state and response, to be implemented by subclasses.
        
        Args:
            state (taos.common.protocol.SimulationStateUpdate): The synapse object containing the latest simulation state update.
            response (taos.common.protocol.AgentResponse): AgentResponse object which will be attached to the synapse for return to querying validator.

        Returns:
        """
        ...

def launch(agent_class):
    import argparse
    import uvicorn
    from taos.common.config import ParseKwargs
    from fastapi import FastAPI
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument("--agent_id", type=int, required=True)
    parser.add_argument("--params",nargs='*',action=ParseKwargs)
    args = parser.parse_args()
    app = FastAPI()
    agent = agent_class(args.agent_id, args.params)
    app.include_router(agent.router)
    uvicorn.run(app, port=args.port)