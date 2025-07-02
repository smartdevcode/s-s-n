# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT

from xml.etree.ElementTree import Element
from pydantic import Field
from enum import IntEnum
from taos.common.protocol import BaseModel
from taos.im.protocol.simulator import *

"""
Classes representing models of objects occurring within intelligent market simulations are defined here.
"""

class FeeTier(BaseModel):
    volume_required : float
    maker_fee : float
    taker_fee : float

class FeePolicy(BaseModel):
    fee_type : str
    params : dict
    tiers : list[FeeTier]
    
    @classmethod
    def from_xml(cls, xml : Element):
        """
        Constructs an instance of the class from the XML simulation configuration element.
        """
        if xml:
            fee_policy = FeePolicy(fee_type=xml.attrib['type'], params={k : v for k, v in xml.attrib.items() if k != 'type'}, tiers=[FeeTier(volume_required=0, maker_fee=0.0, taker_fee=0.0 )])
            match fee_policy.fee_type:
                case 'static':
                    fee_policy.tiers = [FeeTier(volume_required=0, maker_fee=xml.attrib['makerFee'], taker_fee=xml.attrib['takerFee'] )]
                case 'tiered':                    
                    fee_policy.tiers = [FeeTier(volume_required=tier.attrib['volumeRequired'], maker_fee=tier.attrib['makerFee'], taker_fee=tier.attrib['takerFee']) for tier in xml.findall("Tier")]
        else:
            fee_policy = FeePolicy(fee_type=xml.attrib['type'], params={k : v for k, v in xml.attrib if k != 'type'})
            fee_policy.tiers = [FeeTier(volume_required=0, maker_fee=0.0, taker_fee=0.0 )]
        return fee_policy
    
    def to_prom_info(self) -> dict:
        """
        Creates a dictionary containing the details of the fee policy specification in format suitable for publishing via Prometheus Info metric
        """
        prometheus_info = {}
        prometheus_info['simulation_fee_policy_type'] = self.fee_type
        for name, value in self.params.items():
            prometheus_info[f'simulation_fee_policy_{name}'] = str(value)
        for i, tier in enumerate(self.tiers):
            prometheus_info[f'simulation_fee_policy_tier_{i}_volume_required'] = f"{tier.volume_required:.2f}"
            prometheus_info[f'simulation_fee_policy_tier_{i}_maker_rate'] = f"{tier.maker_fee * 100:.4f}"
            prometheus_info[f'simulation_fee_policy_tier_{i}_taker_rate'] = f"{tier.taker_fee * 100:.4f}"
        return prometheus_info

class MarketSimulationConfig(BaseModel):
    """
    Class to represent the configuration of a intelligent markets simulation.
    """
    logDir : str | None = None
    
    block_count : int

    time_unit : str = 'ns'
    duration : int
    grace_period : int
    publish_interval : int
    log_window : int | None = None

    books_per_block : int
    book_count : int
    book_levels : int

    baseDecimals : int
    quoteDecimals : int
    priceDecimals : int
    volumeDecimals : int
    
    fee_policy : FeePolicy | None = None
    
    max_open_orders : int | None = None

    max_leverage : float
    max_loan : float
    maintenance_margin : float

    miner_capital_type : str
    miner_base_balance : float | None
    miner_quote_balance : float | None
    miner_wealth : float

    init_price : float

    fp_update_period : int | None = None
    fp_seed_interval : int | None = None
    fp_mu : float | None = None
    fp_sigma : float | None = None
    fp_lambda : float | None = None
    fp_mu_jump : float | None = None
    fp_sigma_jump : float | None = None

    init_agent_count : int
    init_agent_capital_type : str
    init_agent_base_balance : float | None
    init_agent_quote_balance : float | None
    init_agent_wealth : float

    init_agent_tau : int

    hft_agent_count : int
    hft_agent_capital_type : str
    hft_agent_base_balance : float | None
    hft_agent_quote_balance : float | None
    hft_agent_wealth : float

    hft_agent_feed_latency_min : int
    hft_agent_order_latency_min : int
    hft_agent_order_latency_max : int
    hft_agent_order_latency_scale : float

    hft_agent_tau : int
    hft_agent_delta : int
    hft_agent_psi : float
    hft_agent_gHFT : float
    hft_agent_kappa : float
    hft_agent_spread : float
    hft_agent_order_size_mean : float
    hft_agent_price_noise : float
    hft_agent_price_shift : float

    sta_agent_count : int
    sta_agent_capital_type : str
    sta_agent_base_balance : float | None
    sta_agent_quote_balance : float | None
    sta_agent_wealth : float

    sta_agent_feed_latency_min : int = 0
    sta_agent_feed_latency_mean : int
    sta_agent_feed_latency_std : int
    sta_agent_order_latency_min : int
    sta_agent_order_latency_max : int
    sta_agent_order_latency_scale : float
    sta_agent_decision_latency_mean : int
    sta_agent_decision_latency_std : int
    sta_agent_selection_scale : float

    sta_agent_noise_weight : float
    sta_agent_chartist_weight : float
    sta_agent_fundamentalist_weight : float

    sta_agent_tau : int
    sta_agent_tauHist : int
    sta_agent_tauF : int
    sta_agent_sigmaEps : float
    sta_agent_r_aversion : float
    
    futures_agent_count : int | None = None
    futures_agent_capital_type : str | None = None
    futures_agent_base_balance : float | None = None
    futures_agent_quote_balance : float | None = None
    futures_agent_wealth : float | None = None

    futures_agent_volume : float | None = None
    futures_agent_sigmaEps : float | None = None
    futures_agent_lambda : float | None = None
    futures_agent_feed_latency_mean : int | None = None
    futures_agent_feed_latency_std : int | None = None
    futures_agent_order_latency_min : int | None = None
    futures_agent_order_latency_max : int | None = None
    futures_agent_selection_scale : float | None = None

    @classmethod
    def from_xml(cls, xml : Element):
        """
        Constructs an instance of the class from the XML simulation configuration.
        """
        MBE_config = xml.find('Agents').find('MultiBookExchangeAgent')
        books_config = MBE_config.find('Books')
        processes_config = books_config.find("Processes")
        FP_config = processes_config.find("FundamentalPrice")
        balances_config = MBE_config.find('Balances')
        fees_config = MBE_config.find("FeePolicy")
        
        init_config = xml.find('Agents').find('InitializationAgent')
        init_balances_config = init_config.find("Balances") if init_config.find("Balances") else balances_config
        STA_config = xml.find('Agents').find('StylizedTraderAgent')
        STA_balances_config = STA_config.find("Balances") if STA_config.find("Balances") else balances_config
        HFT_config = xml.find('Agents').find('HighFrequencyTraderAgent')
        HFT_balances_config = HFT_config.find("Balances") if HFT_config.find("Balances") else balances_config
        Futures_config = xml.find('Agents').find('FuturesTraderAgent')
        Futures_balances_config = Futures_config.find("Balances") if Futures_config.find("Balances") else balances_config
        return MarketSimulationConfig(
            block_count=int(xml.attrib['blockCount']),
            
            time_unit = str(xml.attrib['timescale']),
            duration = int(xml.attrib['duration']),
            grace_period = int(MBE_config.attrib['gracePeriod']),
            publish_interval = int(xml.attrib['step']),
            log_window = int(xml.attrib['logWindow']),

            books_per_block = int(books_config.attrib['instanceCount']),
            book_count = int(xml.attrib['blockCount']) * int(books_config.attrib['instanceCount']),
            book_levels = int(books_config.attrib['maxDepth']),

            baseDecimals = int(MBE_config.attrib['baseDecimals']),
            quoteDecimals = int(MBE_config.attrib['quoteDecimals']),
            priceDecimals = int(MBE_config.attrib['priceDecimals']),
            volumeDecimals = int(MBE_config.attrib['volumeDecimals']),
            
            fee_policy=FeePolicy.from_xml(fees_config),
            
            max_open_orders=int(MBE_config.attrib['maxOpenOrders']),

            max_leverage = float(MBE_config.attrib['maxLeverage']),
            max_loan = float(MBE_config.attrib['maxLoan']),
            maintenance_margin = float(MBE_config.attrib['maintenanceMargin']),

            miner_capital_type="static" if balances_config.find("Base") != None else balances_config.attrib['type'],
            miner_base_balance = float(balances_config.find('Base').attrib['total']) if balances_config.find("Base") != None else None,
            miner_quote_balance = float(balances_config.find('Quote').attrib['total']) if balances_config.find("Quote") != None else None,
            miner_wealth = round(float(balances_config.find('Quote').attrib['total']) + float(balances_config.find('Base').attrib['total']) * float(MBE_config.attrib['initialPrice']), int(MBE_config.attrib['quoteDecimals'])) if balances_config.find("Base") != None else float(balances_config.attrib['wealth']),

            init_price = float(MBE_config.attrib['initialPrice']),

            fp_update_period = int(processes_config.attrib['updatePeriod']) + 1,
            fp_seed_interval = int(FP_config.attrib['seedInterval']),
            fp_mu = float(FP_config.attrib['mu']),
            fp_sigma = float(FP_config.attrib['sigma']),
            fp_lambda = float(FP_config.attrib['lambda']),
            fp_mu_jump = float(FP_config.attrib['muJump']),
            fp_sigma_jump = float(FP_config.attrib['sigmaJump']),

            init_agent_count = int(init_config.attrib['instanceCount']),
            init_agent_capital_type = "static" if init_balances_config.find("Base") != None else init_balances_config.attrib['type'],
            init_agent_base_balance = float(init_balances_config.find('Base').attrib['total']) if init_balances_config.find("Base") != None else None,
            init_agent_quote_balance = float(init_balances_config.find('Quote').attrib['total']) if init_balances_config.find("Quote") != None else None,
            init_agent_wealth = round(float(init_balances_config.find('Quote').attrib['total']) + float(init_balances_config.find('Base').attrib['total']) * float(MBE_config.attrib['initialPrice']), int(MBE_config.attrib['quoteDecimals'])) if init_balances_config.find("Base") != None else float(init_balances_config.attrib['wealth']),

            init_agent_tau = int(init_config.attrib['tau']),

            hft_agent_count = int(HFT_config.attrib['instanceCount']),
            hft_agent_capital_type = "static" if HFT_balances_config.find("Base") != None else HFT_balances_config.attrib['type'],
            hft_agent_base_balance = float(HFT_balances_config.find('Base').attrib['total']) if HFT_balances_config.find("Base") != None else None,
            hft_agent_quote_balance = float(HFT_balances_config.find('Quote').attrib['total']) if HFT_balances_config.find("Quote") != None else None,
            hft_agent_wealth = round(float(HFT_balances_config.find('Quote').attrib['total']) + float(HFT_balances_config.find('Base').attrib['total']) * float(MBE_config.attrib['initialPrice']), int(MBE_config.attrib['quoteDecimals'])) if HFT_balances_config.find("Base") != None else float(HFT_balances_config.attrib['wealth']),

            hft_agent_feed_latency_min = int(HFT_config.attrib['minMFLatency']),
            hft_agent_order_latency_min = int(HFT_config.attrib['minOPLatency']),
            hft_agent_order_latency_max = int(HFT_config.attrib['maxOPLatency']),
            hft_agent_order_latency_scale = float(HFT_config.attrib['opLatencyScaleRay']),

            hft_agent_tau = int(HFT_config.attrib['tau']),
            hft_agent_delta = int(HFT_config.attrib['delta']),
            hft_agent_psi = float(HFT_config.attrib['psiHFT_constant']),
            hft_agent_gHFT = float(HFT_config.attrib['gHFT']),
            hft_agent_kappa = float(HFT_config.attrib['kappa']),
            hft_agent_spread = float(HFT_config.attrib['spread']),
            hft_agent_order_size_mean = float(HFT_config.attrib['orderMean']),
            hft_agent_price_noise = float(HFT_config.attrib['noiseRay']),
            hft_agent_price_shift = float(HFT_config.attrib['shiftPercentage']),

            sta_agent_count = int(STA_config.attrib['instanceCount']),
            sta_agent_capital_type = "static" if STA_balances_config.find("Base") != None else STA_balances_config.attrib['type'],
            sta_agent_base_balance = float(STA_balances_config.find('Base').attrib['total']) if STA_balances_config.find("Base") != None else None,
            sta_agent_quote_balance = float(STA_balances_config.find('Quote').attrib['total']) if STA_balances_config.find("Quote") != None else None,
            sta_agent_wealth = round(float(STA_balances_config.find('Quote').attrib['total']) + float(STA_balances_config.find('Base').attrib['total']) * float(MBE_config.attrib['initialPrice']), int(MBE_config.attrib['quoteDecimals'])) if STA_balances_config.find("Base") != None else float(STA_balances_config.attrib['wealth']),

            sta_agent_feed_latency_mean = int(STA_config.attrib['MFLmean']),
            sta_agent_feed_latency_std = int(STA_config.attrib['MFLstd']),
            sta_agent_order_latency_min = int(STA_config.attrib['minOPLatency']),
            sta_agent_order_latency_max = int(STA_config.attrib['maxOPLatency']),
            sta_agent_order_latency_scale = float(STA_config.attrib['opLatencyScaleRay']),
            sta_agent_decision_latency_mean = int(STA_config.attrib['delayMean']),
            sta_agent_decision_latency_std = int(STA_config.attrib['delaySTD']),
            sta_agent_selection_scale = float(STA_config.attrib['scaleR']),

            sta_agent_noise_weight = float(STA_config.attrib['sigmaN']),
            sta_agent_chartist_weight = float(STA_config.attrib['sigmaC']),
            sta_agent_fundamentalist_weight = float(STA_config.attrib['sigmaF']),

            sta_agent_tau = int(STA_config.attrib['tau']),
            sta_agent_tauHist = int(STA_config.attrib['tauHist']),
            sta_agent_tauF = int(STA_config.attrib['tau']),
            sta_agent_sigmaEps = float(STA_config.attrib['sigmaEps']),
            sta_agent_r_aversion = float(STA_config.attrib['r_aversion']),
            
            futures_agent_count = int(Futures_config.attrib['instanceCount']),
            futures_agent_capital_type = "static" if Futures_balances_config.find("Base") != None else Futures_balances_config.attrib['type'],
            futures_agent_base_balance = float(Futures_balances_config.find('Base').attrib['total']) if Futures_balances_config.find("Base") != None else None,
            futures_agent_quote_balance = float(Futures_balances_config.find('Quote').attrib['total']) if Futures_balances_config.find("Quote") != None else None,
            futures_agent_wealth = round(float(Futures_balances_config.find('Quote').attrib['total']) + float(Futures_balances_config.find('Base').attrib['total']) * float(MBE_config.attrib['initialPrice']), int(MBE_config.attrib['quoteDecimals'])) if Futures_balances_config.find("Base") != None else float(Futures_balances_config.attrib['wealth']),
        
            futures_agent_volume = float(Futures_config.attrib['volume']),
            futures_agent_sigmaEps = float(Futures_config.attrib['sigmaEps']),
            futures_agent_lambda = float(Futures_config.attrib['lambda']),
            futures_agent_feed_latency_mean = int(Futures_config.attrib['MFLmean']),
            futures_agent_feed_latency_std = int(Futures_config.attrib['MFLstd']),
            futures_agent_order_latency_min = int(Futures_config.attrib['minOPLatency']),
            futures_agent_order_latency_max = int(Futures_config.attrib['maxOPLatency']),
            futures_agent_selection_scale = float(Futures_config.attrib['scaleR']),
        )

    def label(self) -> str:
        """
        Function to generate a unique label based on the config parameters for a simulation.
        This is used to ensure that simulation-specific data is reset when a new simulation config is deployed.
        """
        return f"du{self.duration}{self.time_unit}_gr{self.grace_period}-bo{self.book_count}-{self.miner_capital_type}_{self.miner_wealth}-" + \
            f"pd{self.priceDecimals}_vd{self.volumeDecimals}_bd{self.baseDecimals}_qd{self.quoteDecimals}-" + \
            f"ip{self.init_price}-" + \
            f"ina_{self.init_agent_count}_{self.init_agent_capital_type}_{self.init_agent_wealth}_" + \
            f"sta_{self.sta_agent_count}_{self.sta_agent_capital_type}_{self.sta_agent_wealth}_" + \
            f"wn{self.sta_agent_noise_weight}_wc{self.sta_agent_chartist_weight}_wf{self.sta_agent_fundamentalist_weight}_" + \
            f"hft_{self.hft_agent_count}_{self.hft_agent_capital_type}_{self.hft_agent_wealth}_" + \
            f"ta{self.hft_agent_tau}_de{self.hft_agent_delta}_ps{self.hft_agent_psi}"

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
    i : int = Field(alias='id')
    c : int | None = Field(alias='client_id', default=None)
    t : int = Field(alias='timestamp')
    q : float = Field(alias='quantity')
    s : int = Field(alias='side')
    p : float | None = Field(alias='price')
    
    @property
    def id(self) -> int:
        return self.i
    
    @property
    def client_id(self) -> int | None:
        return self.c
    
    @property
    def timestamp(self) -> int:
        return self.t
    
    @property
    def quantity(self) -> float:
        return self.q
    
    @property
    def side(self) -> int:
        return self.s
    
    @property
    def price(self) -> float | None:
        return self.p
    
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
    p : float = Field(alias='price')
    q : float = Field(alias='quantity')
    o: list[Order] | None = Field(alias='orders')
    
    @property
    def price(self) -> float:
        return self.p
    
    @property
    def quantity(self) -> float:
        return self.q
    
    @property
    def orders(self) -> list[Order]:
        return self.o

    @classmethod
    def from_json(self, json : dict):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        if not 'orders' in json:
            orders = None
        else:
            orders = [Order(id=order['orderId'], timestamp=order['timestamp'],quantity=order['volume'],side=order['direction'],order_type="limit",price=json['price']) for order in json['orders']]
        return LevelInfo(price = json['price'], quantity=json['volume'], orders=orders)

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
    i : int = Field(alias='id')
    s : int = Field(alias='side')
    t : int = Field(alias='timestamp')
    q : float = Field(alias='quantity')
    p : float = Field(alias='price')
    Ti : int = Field(alias='taker_id')
    Ta : int = Field(alias='taker_agent_id')
    Tf : float | None = Field(alias='taker_fee', default=None)
    Mi : int = Field(alias='maker_id')
    Ma : int = Field(alias='maker_agent_id')
    Mf : float | None = Field(alias='maker_fee', default=None)
    
    @property
    def id(self) -> int:
        return self.i
    
    @property
    def side(self) -> int:
        return self.s
    
    @property
    def timestamp(self) -> int:
        return self.t
    
    @property
    def quantity(self) -> float:
        return self.q
    
    @property
    def price(self) -> float:
        return self.p
    
    @property
    def taker_id(self) -> int:
        return self.Ti
    
    @property
    def taker_agent_id(self) -> int:
        return self.Ta
    
    @property
    def taker_fee(self) -> float | None:
        return self.Tf
    
    @property
    def maker_id(self) -> int:
        return self.Mi
    
    @property
    def maker_agent_id(self) -> int:
        return self.Ma
    
    @property
    def maker_fee(self) -> float | None:
        return self.Mf
    
    @classmethod
    def from_event(self, event : dict):
        """
        Method to extract model data from simulation event in the format required by the MarketSimulationStateUpdate synapse.
        """
        return TradeInfo(id=event['tradeId'],timestamp=event['timestamp'],quantity=event['volume'],side=event['direction'],price=event['price'],
                         taker_agent_id=event['aggressingAgentId'], taker_id=event['aggressingOrderId'], maker_agent_id=event['restingAgentId'], maker_id=event['restingOrderId'],
                         maker_fee=event['fees']['maker'], taker_fee=event['fees']['taker'])

class Cancellation(BaseModel):
    """
    Represents an order cancellation.

    Attributes:
    - orderId: ID of the cancelled order.
    - quantity: Quantity which was cancelled (None if the entire order was cancelled).
    """
    i: int = Field(alias="orderId")
    t: int | None = Field(alias='timestamp', default=None)
    p: float | None = Field(alias="price", default=None)
    q: float | None = Field(alias="quantity")
    
    @property
    def orderId(self) -> int:
        return self.i
    
    @property
    def timestamp(self) -> int:
        return self.t
    
    @property
    def price(self) -> float:
        return self.p
    
    @property
    def quantity(self) -> float | None:
        return self.q

    @classmethod
    def from_event(self, event : dict):
        """
        Method to extract model data from simulation event in the format required by the MarketSimulationStateUpdate synapse.
        """
        return Cancellation(orderId=event['orderId'], timestamp=event['timestamp'], price=event['price'], quantity=event['volume'])
    
class L2Snapshot(BaseModel):
    timestamp : int
    bids : dict[float, LevelInfo]
    asks : dict[float, LevelInfo]
    
    def best_bid(self):
        return max(self.bids.keys())
    
    def best_ask(self):
        return min(self.asks.keys())
    
    def bid_level(self, index):
        return self.bids[list(sorted(self.bids.values(), reverse=True))[index]]
    
    def ask_level(self, index):
        return self.asks[list(sorted(self.asks.values()))[index]]
    
    def compare(self, target):
        discrepancies = []
        for price, bid in self.bids.items():
            if price in target.bids:
                if bid.quantity != target.bids[price].quantity:
                    discrepancies.append(f"BID : RECON {bid.quantity}@{price} vs. TARGET {target.bids[price].quantity}@{price}")
            else:
                discrepancies.append(f"BID : RECON {bid.quantity}@{price} vs. TARGET 0.0@{price}")
        for price, ask in self.asks.items():
            if price in target.asks:
                if ask.quantity != target.asks[price].quantity:
                    discrepancies.append(f"ASK : RECON {ask.quantity}@{price} vs. TARGET {target.asks[price].quantity}@{price}")
            else:
                discrepancies.append(f"ASK : RECON {ask.quantity}@{price} vs. TARGET 0.0@{price}")
        return len(discrepancies) == 0, discrepancies

class Book(BaseModel):
    """
    Represents an orderbook.

    Attributes:
    - id: ID of the orderbook in the simulation.
    - bids: List of LevelInfo objects representing the BID side of the book.
    - asks: List of LevelInfo objects representing the ASK side of the book.
    - events: List of models representing the events having occurred on the book since the last state update.
    """
    i : int = Field(alias="id")
    b : list[LevelInfo] = Field(alias="bids")
    a : list[LevelInfo] = Field(alias="asks")
    e : list[Order | TradeInfo | Cancellation] | None = Field(alias="events")
    
    @property
    def id(self) -> int:
        return self.i
    
    @property
    def bids(self) -> list[LevelInfo]:
        return self.b
    
    @property
    def asks(self) -> list[LevelInfo]:
        return self.a
    
    @property
    def events(self) -> list[Order | TradeInfo | Cancellation] | None:
        return self.e

    @classmethod
    def from_json(self, json : dict):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        id = json['bookId']
        bids = []
        asks = []
        if json['bid']:
            bids = [LevelInfo.from_json(bid) for bid in json['bid']][:21]
        if json['ask']:
            asks = [LevelInfo.from_json(ask) for ask in json['ask']][:21]
        events = []
        if json['record']:
            events = [Order.from_event(event) if event['event'] == 'place' else
                    (TradeInfo.from_event(event)) if event['event'] == 'trade' else
                        (Cancellation.from_event(event) if event['event'] == 'cancel' else
                            None) for event in json['record']]
        return Book(id=id,bids=bids,asks=asks,events=events)
    
    def snapshot(self, timestamp):
        return L2Snapshot(
            timestamp=timestamp,
            bids={l.price : l for l in self.bids},
            asks={l.price : l for l in self.asks}
        )
    
    def l2_history(self, snapshot : L2Snapshot, config : MarketSimulationConfig):
        history = {snapshot.timestamp : snapshot}
        for event in sorted(self.events, key=lambda x: x.timestamp):
            match event:
                case o if isinstance(event, Order):
                    if o.side == OrderDirection.BUY:
                        if not o.price in snapshot.bids:
                            snapshot.bids[o.price] = LevelInfo(price=o.price, quantity=0.0, orders=None)
                        snapshot.bids[o.price].q = round(snapshot.bids[o.price].q + o.quantity, config.volumeDecimals)
                    else:
                        if not o.price in snapshot.asks:
                            snapshot.asks[o.price] = LevelInfo(price=o.price, quantity=0.0, orders=None)
                        snapshot.asks[o.price].q = round(snapshot.asks[o.price].q + o.quantity, config.volumeDecimals)
                case t if isinstance(event, TradeInfo):
                    if t.side == OrderDirection.BUY:
                        if t.price in snapshot.asks:
                            snapshot.asks[t.price].q = round(snapshot.asks[t.price].q - t.quantity, config.volumeDecimals)
                            if snapshot.asks[t.price].quantity == 0.0:
                                del snapshot.asks[t.price]
                    else:
                        if t.price in snapshot.bids:
                            snapshot.bids[t.price].q = round(snapshot.bids[t.price].q - t.quantity, config.volumeDecimals)
                            if snapshot.bids[t.price].quantity == 0.0:
                                del snapshot.bids[t.price]
                case c if isinstance(event, Cancellation):
                    if c.price >= snapshot.best_ask():
                        if c.price in snapshot.asks:
                            snapshot.asks[c.price].q = round(snapshot.asks[c.price].q - c.quantity, config.volumeDecimals)
                            if snapshot.asks[c.price].quantity == 0.0:
                                del snapshot.asks[c.price]
                    else:
                        if c.price in snapshot.bids:
                            snapshot.bids[c.price].q = round(snapshot.bids[c.price].q - c.quantity, config.volumeDecimals)
                            if snapshot.bids[c.price].quantity == 0.0:
                                del snapshot.bids[c.price]
            history[event.timestamp] = snapshot
        snapshot.bids = dict(list(sorted(snapshot.bids.items(), reverse=True))[:len(target_snapshot.bids)])
        snapshot.asks = dict(list(sorted(snapshot.asks.items()))[:len(target_snapshot.asks)])
        target_snapshot = self.snapshot(snapshot.timestamp + config.publish_interval)
        matched, discrepancies = snapshot.compare(target_snapshot)
        return history, matched, discrepancies
                        
                    

class Balance(BaseModel):
    """
    Represents an account balance for a specific currency.

    Attributes:
    - currency: String identifier for the currency.
    - total: Total currency balance of account.
    - free: Free curreny balance of account (this amount is available for order placement).
    - reserved: Reserved currency balance; this represents the amount tied up in resting orders.
    """
    c : str = Field(alias="currency")
    t : float = Field(alias="total")
    f : float = Field(alias="free")
    r : float = Field(alias="reserved")
    
    @property
    def currency(self) -> str:
        return self.c
    
    @property
    def total(self) -> float:
        return self.t
    
    @property
    def free(self) -> float:
        return self.f
    
    @property
    def reserved(self) -> float:
        return self.r

    @classmethod
    def from_json(self, currency : str, json : dict):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        return Balance(currency=currency,total=json['total'],free=json['free'],reserved=json['reserved'])
    
class Fees(BaseModel):
    """
    Represents account fees for a specific agent and book.

    Attributes:
    - volume_traded: Total volume traded in the aggregation period for tiered fees assignment.
    - maker_fee_rate: The current maker fee rate for the agent.
    - maker_fee_rate: The current taker fee rate for the agent.
    """
    v : float = Field(alias="volume_traded")
    m : float = Field(alias="maker_fee_rate")
    t : float = Field(alias="taker_fee_rate")
    
    @property
    def volume_traded(self) -> str:
        return self.v
    
    @property
    def maker_fee_rate(self) -> float:
        return self.m
    
    @property
    def taker_fee_rate(self) -> float:
        return self.t

    @classmethod
    def from_json(self, json : dict):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        return Fees(volume_traded=json['volume'],maker_fee_rate=json['makerFeeRate'],taker_fee_rate=json['takerFeeRate'])

class Account(BaseModel):
    """
    Represents an agent's trading account.

    Attributes:
    - agent_id: The agent ID which owns the account.
    - book_id: ID of the book on which the account is able to trade.
    - base_balance: Balance object for the base currency.
    - quote_balance: Balance object for the quote currency.
    - orders: List of the current open orders associated to the agent.
    - fees: The current fee structure for the account.
    """
    i : int = Field(alias="agent_id")
    b : int = Field(alias="book_id")
    bb : Balance = Field(alias="base_balance")
    qb : Balance = Field(alias="quote_balance")
    o : list[Order] = Field(alias="orders", default=[])
    f : Fees | None = Field(alias="fees")
    
    @property
    def agent_id(self) -> int:
        return self.i
    
    @property
    def book_id(self) -> int:
        return self.b
    
    @property
    def base_balance(self) -> Balance:
        return self.bb
    
    @property
    def quote_balance(self) -> Balance:
        return self.qb
    
    @property
    def orders(self) -> list[Order]:
        return self.o
    
    @property
    def fees(self) -> Fees | None:
        return self.f

class OrderDirection(IntEnum):
    """
    Enum to represent order direction.
    """
    BUY=0
    SELL=1

class STP(IntEnum):
    """
    Enum to represent order direction.
    """
    NO_STP=0
    CANCEL_OLDEST=1
    CANCEL_NEWEST=2
    CANCEL_BOTH=3
    DECREASE_CANCEL=4

class TimeInForce(IntEnum):
    """
    Enum to represent order time-in-force options.
    """
    GTC=0
    GTT=1
    IOC=2
    FOK=3

class OrderCurrency(IntEnum):
    """
    Enum to represent order direction.
    """
    BASE=0
    QUOTE=1