# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
from pydantic import BaseModel
from enum import IntEnum
from taos.im.protocol.simulator import *

"""
Classes representing models of objects occurring within intelligent market simulations are defined here.
"""

class MarketSimulationConfig(BaseModel):
    """
    Class to represent the configuration of a intelligent markets simulation.
    """
    logDir : str | None = None
    
    time_unit : str = 'ns'
    duration : int
    grace_period : int
    publish_interval : int
    
    book_count : int
    book_levels : int
    
    start_base_balance : float
    start_quote_balance : float    
    baseDecimals : int
    quoteDecimals : int
    priceDecimals : int
    volumeDecimals : int

    init_agent_count : int    
    init_price : int

    fp_GBM_mu : float
    fp_GBM_sigma : float
    fp_GBM_lambda_jump : float
    fp_GBM_mu_jump : float
    fp_GBM_sigma_jump : float
    fp_GBM_flag_jump : int
    fp_GBM_seed : int

    sta_agent_count : int
    sta_noise_agent_weight : float
    sta_chartist_agent_weight : float
    sta_fundamentalist_agent_weight : float
    sta_tau : int
    sta_sigmaEps : float
    sta_r_aversion : float

    hft_agent_count : int
    hft_tau : int | None
    hft_delta : int | None
    hft_psiHFT : float | None
    hft_gHFT : float | None


    def label(self) -> str:
        """
        Function to generate a unique label based on the config parameters for a simulation.
        This is used to ensure that validator state files for a particular config are not loaded when a new simulation config is deployed.
        """
        return f"du{self.duration}{self.time_unit}_gr{self.grace_period}-bo{self.book_count}-ba{self.start_base_balance}_qu{self.start_quote_balance}-" + \
            f"dp{self.priceDecimals}_vp{self.volumeDecimals}_bp{self.baseDecimals}_qp{self.quoteDecimals}-" + \
            f"ic{self.init_agent_count}_ip{self.init_price}-sta_{self.sta_agent_count}_n{self.sta_noise_agent_weight}_c{self.sta_chartist_agent_weight}_f{self.sta_fundamentalist_agent_weight}-" + \
            f"ta{self.sta_tau}_se{self.sta_sigmaEps}_ra{self.sta_r_aversion}_gm{self.fp_GBM_mu}_gs{self.fp_GBM_sigma}-" + \
            f"hf{self.hft_agent_count}"

class Order(BaseModel):
    """
    Represents an order.    

    Attributes:
    - id: The id of the order as assigned by the simulator.
    - client_id: Optional agent-assigned identifier for the order.
    - timestamp: The simulation timestamp at which the order was placed.
    - quantity: The size of the order in base currency.    
    - side: The side of the book on which the order was attempted to be placed (0=BID, 1=ASK).
    - order_type: String identifier for the type of the order (limit or market).
    - price: Optional field for the price of the order (None for market orders).
    """
    id : int
    client_id : int | None = None
    timestamp : int
    quantity : float
    side : int
    order_type : str
    price : float | None

    @classmethod    
    def from_simulator(self, sim_order : SimulatorOrder):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        return Order(id=sim_order.orderId,client_id=sim_order.clientOrderId,timestamp=sim_order.timestamp,quantity=sim_order.volume,side=sim_order.direction,order_type="limit" if sim_order.price else "market",price=sim_order.price)

    @classmethod    
    def from_event(self, event : dict):
        """
        Method to extract model data from simulation event in the format required by the MarketSimulationStateUpdate synapse.
        """
        return Order(id=event['orderId'],client_id=event['clientOrderId'], timestamp=event['timestamp'],quantity=event['volume'],side=event['direction'],order_type="limit" if event['price'] else 'market',price=event['price'])
    
    @classmethod    
    def from_account(self, acc_order : dict):
        """
        Method to extract model data from simulation account representation in the format required by the MarketSimulationStateUpdate synapse.
        """
        return Order(id=acc_order['orderId'],client_id=acc_order['clientOrderId'], timestamp=acc_order['timestamp'],quantity=acc_order['volume'],side=acc_order['direction'],order_type="limit",price=acc_order['price'])
    
class LevelInfo(BaseModel):
    """
    Represents an orderbook level.    

    Attributes:
    - price: Price level in the orderbook at which the level exists.
    - quantity: Total quantity in base currency which exists at this price level in the book.
    - orders: List of the individual orders composing the orderbook level.
    """
    price : float
    quantity : float
    orders: list[Order] | None
    
    @classmethod    
    def from_simulator(self, sim_level : SimulatorLevel | SimulatorBroadLevel):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        if isinstance(sim_level, SimulatorBroadLevel):
            orders = None
        else:
            orders = [Order(id=order.orderId, timestamp=order.timestamp,quantity=order.volume,side=order.direction,order_type="limit",price=sim_level.price) for order in sim_level.orders]
        return LevelInfo(price = sim_level.price, quantity=sim_level.volume, orders=orders)

class TradeInfo(BaseModel):
    """
    Represents a trade.    

    Attributes:
    - id: Simulator-assigned ID of the trade.
    - side: Direction in which trade was initiated (0=BUY initiated, 1=SELL initiated).
    - timestamp: Simulation timestamp at which the trade occurred.
    - taker_id: ID of the aggressing order.
    - taker_agent_id: ID of the agent which placed the aggressing order.
    - maker_id: ID of the resting order.
    - maker_agent_id: ID of the agent which placed the resting order.
    - quantity: Quantity in base currency which was traded.
    - price: The price at which the trade occurred.
    """
    id : int
    side : int
    timestamp : int
    taker_id : int
    taker_agent_id : int
    maker_id : int
    maker_agent_id : int
    quantity : float
    price : float

    @classmethod    
    def from_simulator(self, sim_trade : SimulatorTrade):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        return TradeInfo(id=sim_trade.id,timestamp=sim_trade.timestamp,taker_id=sim_trade.aggressingOrderId,maker_id=sim_trade.restingOrderId,side=sim_trade.direction,quantity=sim_trade.volume,price=sim_trade.price)

    @classmethod    
    def from_event(self, event : dict):
        """
        Method to extract model data from simulation event in the format required by the MarketSimulationStateUpdate synapse.
        """
        return TradeInfo(id=event['tradeId'],timestamp=event['timestamp'],quantity=event['volume'],side=event['direction'],price=event['price'],
                         taker_agent_id=event['aggressingAgentId'], taker_id=event['aggressingOrderId'], maker_agent_id=event['restingAgentId'], maker_id=event['restingOrderId'])
    
class Cancellation(BaseModel):
    """
    Represents an order cancellation.    

    Attributes:
    - orderId: ID of the cancelled order.
    - quantity: Quantity which was cancelled (None if the entire order was cancelled).
    """
    orderId: int
    quantity: float | None

    @classmethod    
    def from_simulator(self, sim_canc : SimulatorCancellation):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        return Cancellation(orderId=sim_canc.orderId,quantity=sim_canc.volume)

    @classmethod    
    def from_event(self, event : dict):
        """
        Method to extract model data from simulation event in the format required by the MarketSimulationStateUpdate synapse.
        """
        return Cancellation(orderId=event['orderId'],quantity=event['volume'])

class Book(BaseModel):
    """
    Represents an orderbook.    

    Attributes:
    - id: ID of the orderbook in the simulation.
    - bids: List of LevelInfo objects representing the BID side of the book.
    - asks: List of LevelInfo objects representing the ASK side of the book.
    - events: List of models representing the events having occurred on the book since the last state update.
    """
    id : int
    bids : list[LevelInfo]
    asks : list[LevelInfo]
    events : list[Order | TradeInfo | Cancellation] | None

    @classmethod    
    def from_simulator(self, sim_book : SimulatorBook):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        id = sim_book.bookId
        bids = []
        asks = []
        if sim_book.bid:            
            bids = [LevelInfo.from_simulator(bid) for bid in sim_book.bid][:21]        
        if sim_book.ask:         
            asks = [LevelInfo.from_simulator(ask) for ask in sim_book.ask][:21]
        events = []
        if sim_book.record:
            events = [Order.from_event(event) if event['event'] == 'place' else 
                    (TradeInfo.from_event(event)) if event['event'] == 'trade' else 
                        (Cancellation.from_event(event) if event['event'] == 'cancel' else
                            None) for event in sim_book.record]
        return Book(id=id,bids=bids,asks=asks,events=events)

class Balance(BaseModel):
    """
    Represents an account balance for a specific currency.    

    Attributes:
    - currency: String identifier for the currency.
    - total: Total currency balance of account.
    - free: Free curreny balance of account (this amount is available for order placement).
    - reserved: Reserved currency balance; this represents the amount tied up in resting orders.
    """
    currency : str
    total : float
    free : float
    reserved : float

    @classmethod    
    def from_simulator(self, sim_balance : SimulatorBalance):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        return Balance(currency=sim_balance.symbol,total=sim_balance.total,free=sim_balance.free,reserved=sim_balance.reserved)

class Account(BaseModel):
    """
    Represents an agent's trading account.    

    Attributes:
    - agent_id: The agent ID which owns the account.
    - book_id: ID of the book on which the account is able to trade.
    - base_balance: Balance object for the base currency.
    - quote_balance: Balance object for the quote currency.
    - orders: List of the current open orders associated to the agent.
    """
    agent_id : int
    book_id : int
    base_balance : Balance
    quote_balance : Balance
    orders : list[Order] = []
    
class OrderDirection(IntEnum):
    """
    Enum to represent order direction.
    """
    BUY=0
    SELL=1