# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
from abc import ABC, abstractmethod  # Importing the ABC class and abstractmethod decorator for creating abstract base classes
from taos.common.protocol import SimulationStateUpdate, AgentResponse, EventNotification  # Importing required classes for simulation state and agent responses

# Defining an abstract base class for simulation agents
class SimulationAgent(ABC):
    def __init__(self, uid, config):
        """
        Initializer method that sets up the agent's unique ID and configuration.
        """
        self.uid = uid
        self.config = config 
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