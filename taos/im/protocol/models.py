# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import numpy as np
from collections.abc import Mapping, Sequence
from xml.etree.ElementTree import Element
from pydantic import Field
from ypyjson import YpyObject
from enum import IntEnum
from itertools import accumulate
from typing import Literal, Any
from taos.common.protocol import BaseModel
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
    Class to represent the configuration of an intelligent markets simulation.

    Attributes:
        simulation_id (str | None): Unique identifier for the simulation instance.
        logDir (str | None): Directory where simulation logs are saved.

        block_count (int): Number of parallel "blocks" of simulation runs (related to parallelization implementation).

        time_unit (str): Unit of time used in the simulation (e.g., 'ns' for nanoseconds). Default is 'ns'.
        duration (int): Total simulation time in the given time_unit.
        grace_period (int): Time period at start of simulation which must elapse before miner agents are able to submit instructions.
        publish_interval (int): Interval at which the simulation state is published.
        log_window (int | None): Size of the time window for logs.

        books_per_block (int): Number of order books simulated in each parallelization block.
        book_count (int): Total number of order books in the simulation.
        book_levels (int): Number of levels included for each book in the state update (containing price and volume data).
        detailed_book_levels (int): Number of levels for which full book level data is included (level composition in terms of orders).

        baseDecimals (int): Decimal precision for base currency values.
        quoteDecimals (int): Decimal precision for quote currency values.
        priceDecimals (int): Decimal precision for price values.
        volumeDecimals (int): Decimal precision for order volumes.

        fee_policy (FeePolicy | None): The fee policy applied to trades.

        max_open_orders (int | None): Maximum number of open orders per agent.

        max_leverage (float): Maximum leverage allowed for agents.
        max_loan (float): Maximum loan amount agents can take.
        maintenance_margin (float): Maintenance margin ratio required for agents to avoid liquidation.

        miner_capital_type (str): Capital allocation strategy for miners ('static' or 'pareto').
        miner_base_balance (float | None): Initial base currency balance for miners.
        miner_quote_balance (float | None): Initial quote currency balance for miners.
        miner_wealth (float): Total wealth allocated to miners (QUOTE value of initial BASE balance at initial price + initial QUOTE balance).

        init_price (float): Initial market price for the simulation.

        # Fundamental Price (FP) parameters
        fp_update_period (int | None): Period for updating the fundamental price.
        fp_seed_interval (int | None): Interval for reseeding the fundamental process.
        fp_mu (float | None): Drift term in the fundamental price process.
        fp_sigma (float | None): Volatility in the fundamental price process.
        fp_lambda (float | None): Intensity of price jumps.
        fp_mu_jump (float | None): Mean size of price jumps.
        fp_sigma_jump (float | None): Volatility of price jumps.

        # Initialization Agent Configuration
        # Initialization Agents are triggered only once at the start of the simulation to provide an initial random state of the orderbook
        # After some time, when the other background agents have had time to create a sensible orderbook structure, their orders are cancelled.
        init_agent_count (int): Number of initialization agents.
        init_agent_capital_type (str): Capital allocation strategy for initialization agents.
        init_agent_base_balance (float | None): Base currency balance for initialization agents.
        init_agent_quote_balance (float | None): Quote currency balance for initialization agents.
        init_agent_wealth (float): Total wealth allocated to initialization agents.
        init_agent_tau (int): Time period after which orders placed by initialization agents are cancelled.

        # High-Frequency Trader (HFT) agents
        # HFT Agents function somewhat like market makers in real markets.
        # https://papers.ssrn.com/sol3/papers.cfm?abstract_id=2336772
        hft_agent_count (int): Number of HFT agents.
        hft_agent_capital_type (str): Capital allocation strategy for HFT agents.
        hft_agent_base_balance (float | None): Base currency balance for HFT agents.
        hft_agent_quote_balance (float | None): Quote currency balance for HFT agents.
        hft_agent_wealth (float): Total wealth allocated to HFT agents.

        hft_agent_feed_latency_min (int): Minimum market data feed latency for HFT agents.
        hft_agent_order_latency_min (int): Minimum order placement latency for HFT agents.
        hft_agent_order_latency_max (int): Maximum order placement latency for HFT agents.
        hft_agent_order_latency_scale (float): Scaling factor for HFT order latencies.

        hft_agent_tau (int): Latency parameter for HFT agents.
        hft_agent_delta (int): Sensitivity parameter for HFT agents.
        hft_agent_psi (float): Probability weighting factor for HFT decisions.
        hft_agent_gHFT (float): Aggressiveness factor in HFT strategies.
        hft_agent_kappa (float): Inventory control parameter for HFT agents.
        hft_agent_spread (float): Target bid-ask spread for HFT agents.
        hft_agent_order_size_mean (float): Mean size of orders placed by HFT agents.
        hft_agent_price_noise (float): Noise applied to HFT agent pricing decisions.
        hft_agent_price_shift (float): Systematic price shift applied by HFT agents.

        # Stylized Trader Agent (STA) configuration
        # STA agents aim to approximate the behaviour of several interacting classes of traders.
        # https://arxiv.org/abs/0711.3581
        sta_agent_count (int): Number of STA agents.
        sta_agent_capital_type (str): Capital allocation strategy for STA agents.
        sta_agent_base_balance (float | None): Base currency balance for STA agents.
        sta_agent_quote_balance (float | None): Quote currency balance for STA agents.
        sta_agent_wealth (float): Total wealth allocated to STA agents.

        sta_agent_feed_latency_min (int): Minimum market data feed latency for STA agents. Default is 0.
        sta_agent_feed_latency_mean (int): Mean feed latency for STA agents.
        sta_agent_feed_latency_std (int): Standard deviation of feed latency for STA agents.
        sta_agent_order_latency_min (int): Minimum order placement latency for STA agents.
        sta_agent_order_latency_max (int): Maximum order placement latency for STA agents.
        sta_agent_order_latency_scale (float): Scaling factor for STA order latencies.
        sta_agent_decision_latency_mean (int): Mean decision-making latency for STA agents.
        sta_agent_decision_latency_std (int): Standard deviation of decision latency for STA agents.
        sta_agent_selection_scale (float): Scale factor influencing STA selection preferences.

        sta_agent_noise_weight (float): Weight for noise component in STA decision making.
        sta_agent_chartist_weight (float): Weight for chartist component in STA agents.
        sta_agent_fundamentalist_weight (float): Weight for fundamentalist component in STA agents.

        sta_agent_tau (int): Decision interval for STA agents.
        sta_agent_tauHist (int): Historical observation window size for STA agents.
        sta_agent_tauF (int): Forecast horizon for STA agents.
        sta_agent_sigmaEps (float): Volatility parameter in STA forecasting.
        sta_agent_r_aversion (float): Risk aversion parameter for STA agents.

        # Futures Agent configuration
        # The Futures Agent aims to bring real-world connection into the simulation dynamics
        # These agents make trading decisions based on external signals obtained from live futures markets
        futures_agent_count (int | None): Number of futures agents.
        futures_agent_capital_type (str | None): Capital allocation strategy for futures agents.
        futures_agent_base_balance (float | None): Base currency balance for futures agents.
        futures_agent_quote_balance (float | None): Quote currency balance for futures agents.
        futures_agent_wealth (float | None): Total wealth allocated to futures agents.

        futures_agent_volume (float | None): Typical trade volume for futures agents.
        futures_agent_sigmaEps (float | None): Noise level in futures agent decisions.
        futures_agent_lambda (float | None): Order arrival intensity for futures agents.
        futures_agent_feed_latency_mean (int | None): Mean market data latency for futures agents.
        futures_agent_feed_latency_std (int | None): Standard deviation of feed latency for futures agents.
        futures_agent_order_latency_min (int | None): Minimum order latency for futures agents.
        futures_agent_order_latency_max (int | None): Maximum order latency for futures agents.
        futures_agent_selection_scale (float | None): Scale factor for futures agent selection.
    """
    simulation_id : str | None = None
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
    detailed_book_levels : int = 5

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
        init_balances_config = init_config.find("Balances") if init_config.find("Balances") != None else balances_config
        STA_config = xml.find('Agents').find('StylizedTraderAgent')
        STA_balances_config = STA_config.find("Balances") if STA_config.find("Balances") != None else balances_config
        HFT_config = xml.find('Agents').find('HighFrequencyTraderAgent')
        HFT_balances_config = HFT_config.find("Balances") if HFT_config.find("Balances") != None else balances_config
        Futures_config = xml.find('Agents').find('FuturesTraderAgent')
        Futures_balances_config = Futures_config.find("Balances") if Futures_config.find("Balances") != None else balances_config
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
            detailed_book_levels = int(books_config.attrib['detailedDepth']),

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

            fp_update_period = int(FP_config.attrib['updatePeriod']) + 1,
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
        type (str): The type of the instruction; fixed to `"o"` (used for parallelized history reconstruction).
        id (int): The ID of the order as assigned by the simulator.
        client_id (int | None): Optional agent-assigned identifier for the order.
        timestamp (int): Simulation timestamp at which the order was placed.
        quantity (float): The size of the order in base currency.
        side (int): The side of the book on which the order was attempted to be placed (`0=BID`, `1=ASK`).
        price (float | None): Price of the order (`None` for market orders).
        leverage (float): Leverage ratio applied to the order. Defaults to 0.0 (unleveraged).
    """
    y : str = "o"
    i : int = Field(alias='id')
    c : int | None = Field(alias='client_id', default=None)
    t : int = Field(alias='timestamp')
    q : float = Field(alias='quantity')
    s : int = Field(alias='side')
    p : float | None = Field(alias='price')
    l : float = Field(alias="leverage", default=0.0)

    @property
    def type(self) -> str:
        return self.y

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
    
    @property
    def leverage(self) -> float:
        return self.l

    @classmethod
    def from_event(self, event : dict):
        """
        Method to extract model data from simulation event in the format required by the MarketSimulationStateUpdate synapse.
        """
        return Order(order_type="limit" if event['price'] else 'market', id=event['orderId'],client_id=event['clientOrderId'], timestamp=event['timestamp'],
                     quantity=event['volume'], side=event['direction'], price=event['price'], 
                     leverage=event['leverage'])

    @classmethod
    def from_json(self, json : dict):
        """
        Method to extract model data from simulation account representation in the format required by the MarketSimulationStateUpdate synapse.
        """
        return Order.model_construct(order_type="limit", id=json['i'], client_id=json['c'], timestamp=json['t'],
                     quantity=json['q'], side=json['s'], price=json['p'], 
                     leverage=json['l'])

class LevelInfo(BaseModel):
    """
    Represents a level in the order book.

    Attributes:
        price (float): The price level in the order book.
        quantity (float): Total quantity in base currency at this price level.
        orders (list[Order] | None): List of individual orders at this level (if available).
    """
    
    p : float = Field(alias='price')
    q : float = Field(alias='quantity')
    o: list[Order] | None = Field(alias='orders', default=None)

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
        if not 'o' in json:
            orders = None
        else:
            orders = [Order.model_construct(id=order['i'], timestamp=order['t'],quantity=order['q'],side=order['s'],order_type="limit",price=json['p'],leverage=json['l'] if 'l' in json else 0.0) for order in json['o']]
        return LevelInfo.model_construct(price = json['p'], quantity=json['q'], orders=orders)

class TradeInfo(BaseModel):
    """
    Represents a trade.

    Attributes:
        type (str): The type of instruction; fixed to `t` (used for parallelized history reconstruction).
        id (int): Simulator-assigned ID of the trade.
        side (int): Direction in which the trade was initiated (0 = BUY, 1 = SELL).
        timestamp (int): Simulation timestamp at which the trade occurred.
        quantity (float): Quantity in base currency that was traded.
        price (float): Price at which the trade occurred.
        taker_id (int): ID of the aggressing order.
        taker_agent_id (int): ID of the agent placing the aggressing order.
        taker_fee (float | None): Transaction fee paid by the taker agent.
        maker_id (int): ID of the resting order.
        maker_agent_id (int): ID of the agent placing the resting order.
        maker_fee (float | None): Transaction fee paid by the maker agent.
    """
    y : str = "t"
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
    def type(self) -> str:
        return self.y

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

    @classmethod
    def from_json(self, json : dict):
        """
        Method to extract model data from simulation event in the format required by the MarketSimulationStateUpdate synapse.
        """
        return TradeInfo.model_construct(id=json['i'],timestamp=json['t'],quantity=json['q'],side=json['s'],price=json['p'],
                         taker_agent_id=json['Ta'], taker_id=json['Ti'], maker_agent_id=json['Ma'], maker_id=json['Mi'],
                         maker_fee=json['Mf'], taker_fee=json['Tf'])

class Cancellation(BaseModel):
    """
    Represents an order cancellation.

    Attributes:
        type (str): The type of instruction; fixed to `c` (used for parallelized history reconstruction).
        orderId (int): ID of the cancelled order.
        timestamp (int | None): Simulation timestamp at which the cancellation occurred.
        price (float | None): Price of the order that was cancelled.
        quantity (float | None): Quantity cancelled (None if the entire order was cancelled).
    """
    y : str = "c"
    i: int = Field(alias="orderId")
    t: int | None = Field(alias='timestamp', default=None)
    p: float | None = Field(alias="price", default=None)
    q: float | None = Field(alias="quantity")

    @property
    def type(self) -> str:
        return self.y

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

    @classmethod
    def from_json(self, json : dict):
        """
        Method to extract model data from simulation event in the format required by the MarketSimulationStateUpdate synapse.
        """
        return Cancellation.model_construct(orderId=json['i'], timestamp=json['t'], price=json['p'], quantity=json['q'])

class L2Snapshot(BaseModel):
    """
    Represents a level-2 order book snapshot at a specific timestamp.

    Attributes:
        timestamp (int): Simulation timestamp of the snapshot in nanoseconds.
        bids (dict[float, LevelInfo]): Bid side of the order book (price → LevelInfo).
        asks (dict[float, LevelInfo]): Ask side of the order book (price → LevelInfo).
    """

    timestamp: int
    bids: dict[float, LevelInfo]
    asks: dict[float, LevelInfo]

    def best_bid(self) -> float:
        """
        Get the highest bid price in the snapshot.

        Returns:
            float: The best (highest) bid price.
        """
        return max(self.bids.keys())

    def best_ask(self) -> float:
        """
        Get the lowest ask price in the snapshot.

        Returns:
            float: The best (lowest) ask price.
        """
        return min(self.asks.keys())

    def bid_level(self, index: int) -> LevelInfo:
        """
        Get a specific bid level sorted by price descending.

        Args:
            index (int): The index of the bid level to retrieve.

        Returns:
            LevelInfo: The bid level at the specified index.
        """
        return self.bids[list(sorted(self.bids.values(), reverse=True))[index]]

    def ask_level(self, index: int) -> LevelInfo:
        """
        Get a specific ask level sorted by price ascending.

        Args:
            index (int): The index of the ask level to retrieve.

        Returns:
            LevelInfo: The ask level at the specified index.
        """
        return self.asks[list(sorted(self.asks.values()))[index]]

    def imbalance(self, depth: int | None = None) -> float:
        """
        Calculate the order book imbalance at a given depth.

        Imbalance formula:
            (total_bid_volume - total_ask_volume) / (total_bid_volume + total_ask_volume)

        Args:
            depth (int | None): Optional number of levels to include in the calculation. If None, uses all levels.

        Returns:
            float: The imbalance ratio.
        """
        total_bid_vol = sum(
            [bid.quantity for bid in list(self.bids.values())[:(depth if depth else len(self.bids))]]
        )
        total_ask_vol = sum(
            [ask.quantity for ask in list(self.asks.values())[:(depth if depth else len(self.asks))]]
        )
        return (total_bid_vol - total_ask_vol) / (total_bid_vol + total_ask_vol)

    def compare(self, target: 'L2Snapshot', config: MarketSimulationConfig) -> tuple[bool, list[str], dict[str, dict[float, float]]]:
        """
        Compare this snapshot to a target snapshot, and return a list of discrepancies as well as a dictionary mapping price level to the volume determined to already exist at that level
        prior to the original snapshot being constructed.  This is necessary as some new price levels may enter the top levels due to cancellations and trades.

        Args:
            target (L2Snapshot): The snapshot to compare against.
            config (MarketSimulationConfig): Simulation configuration with rounding and volume precision.

        Returns:
            tuple:
                - bool: True if snapshots match (no discrepancies), False otherwise.
                - list[str]: List of textual discrepancy descriptions.
                - dict: Dictionary of existing volumes needed to reconcile (bids and asks).
        """
        discrepancies = []
        existing_volumes = {'bid': {}, 'ask': {}}

        # Compare bids
        for price, bid in self.bids.items():
            if price in target.bids:
                if bid.quantity != target.bids[price].quantity:
                    discrepancies.append(f"BID : RECON {bid.quantity}@{price} vs. TARGET {target.bids[price].quantity}@{price}")
                if bid.quantity < target.bids[price].quantity:
                    existing_volumes['bid'][price] = round(target.bids[price].quantity - bid.quantity, config.volumeDecimals)
            else:
                discrepancies.append(f"BID : RECON {bid.quantity}@{price} vs. TARGET 0.0@{price}")
                if bid.quantity < 0:
                    existing_volumes['bid'][price] = round(-bid.quantity, config.volumeDecimals)

        # Add missing bids from target
        for price, bid in target.bids.items():
            if price not in self.bids:
                discrepancies.append(f"BID : RECON 0.0@{price} vs. TARGET {bid.quantity}@{price}")
                existing_volumes['bid'][price] = bid.quantity

        # Compare asks
        for price, ask in self.asks.items():
            if price in target.asks:
                if ask.quantity != target.asks[price].quantity:
                    discrepancies.append(f"ASK : RECON {ask.quantity}@{price} vs. TARGET {target.asks[price].quantity}@{price}")
                if ask.quantity < target.asks[price].quantity:
                    existing_volumes['ask'][price] = round(target.asks[price].quantity - ask.quantity, config.volumeDecimals)
            else:
                discrepancies.append(f"ASK : RECON {ask.quantity}@{price} vs. TARGET 0.0@{price}")
                if ask.quantity < 0:
                    existing_volumes['ask'][price] = round(-ask.quantity, config.volumeDecimals)

        # Add missing asks from target
        for price, ask in target.asks.items():
            if price not in self.asks:
                discrepancies.append(f"ASK : RECON 0.0@{price} vs. TARGET {ask.quantity}@{price}")
                existing_volumes['ask'][price] = ask.quantity

        return len(discrepancies) == 0, discrepancies, existing_volumes

    def sort(self, depth: int | None = None, in_place : bool = True) -> 'L2Snapshot':
        """
        Sort bids descending and asks ascending, and truncates levels to the specified depth.

        Args:
            depth (int | None): Optional number of levels to keep after sorting.
        """
        if in_place:
            self.bids = dict(list(sorted(self.bids.items(), reverse=True))[:(depth if depth else len(self.bids))])
            self.asks = dict(list(sorted(self.asks.items()))[:(depth if depth else len(self.asks))])
            return self
        else:
            return self.model_copy(update = {
                "bids" : dict(list(sorted(self.bids.items(), reverse=True))[:(depth if depth else len(self.bids))]),
                "asks" : dict(list(sorted(self.asks.items()))[:(depth if depth else len(self.asks))])
            })

    def reconcile(self, existing_volumes: dict[str, dict[float, float]], config: MarketSimulationConfig, depth: int) -> 'L2Snapshot':
        """
        Reconcile snapshot levels with specified volume adjustments.

        Args:
            existing_volumes (dict): Volumes to adjust for bids and asks.
            config (MarketSimulationConfig): Simulation configuration, for rounding.
            depth (int): Depth of levels to retain.

        Returns:
            L2Snapshot: The updated snapshot after reconciliation.
        """
        if len(existing_volumes['bid']) > 0 or len(existing_volumes['ask']) > 0:
            # Adjust bid levels
            for price, volume in existing_volumes['bid'].items():
                if price in self.bids:
                    self.bids[price].q = round(self.bids[price].q + volume, config.volumeDecimals)
                    if self.bids[price].q == 0:
                        del self.bids[price]
                else:
                    self.bids[price] = LevelInfo(price=price, quantity=volume, orders=None)
            # Adjust ask levels
            for price, volume in existing_volumes['ask'].items():
                if price in self.asks:
                    self.asks[price].q = round(self.asks[price].q + volume, config.volumeDecimals)
                    if self.asks[price].q == 0:
                        del self.asks[price]
                else:
                    self.asks[price] = LevelInfo(price=price, quantity=volume, orders=None)
        self.sort(depth)
        return self
    
class History:
    start : int
    end : int
    retention_mins : int | None

    def is_full(self) -> bool:
        """
        Check whether the history covers the full retention window.

        Returns:
            bool: True if the history is full (matches retention window), False otherwise.
        """
        if self.retention_mins:
            return self.start == self.end - self.retention_mins * 60_000_000_000
        return False
    
    def bucket(self, series: dict[int, Any], interval: float) -> dict[int, list[Any]]:
        """
        Buckets a time series into intervals based on timestamp.

        Args:
            series (dict[int, Any]): Time series mapping timestamps to values.
            interval (float): Bucket size in seconds.

        Returns:
            dict[int, list[Any]]: Buckets indexed by upper-bound timestamps.
        """
        interval_ns = int(interval * 1_000_000_000)
        bucketed: dict[int, list[Any]] = {}

        for timestamp, value in series.items():
            if timestamp < self.start or timestamp > self.end:
                continue  # ignore out-of-range timestamps

            # Compute upper bound of bucket interval
            bucket_index = ((timestamp - self.start) // interval_ns) + 1
            bucket_ts = self.start + bucket_index * interval_ns

            if bucket_ts not in bucketed:
                bucketed[bucket_ts] = []

            bucketed[bucket_ts].append(value)

        return bucketed

    def sample(
        self,
        series: dict[int, float],
        interval: float,
        method: Literal['open', 'high', 'low', 'close', 'ohlc'] = 'close'
    ) -> dict[int, Any]:
        """
        Sample a time series at regular intervals.

        Args:
            series (dict[int, float]): Original time series (timestamp → value).
            interval (float): Interval between samples in seconds.
            method (str): Sampling method; one of 'open', 'high', 'low', 'close', 'ohlc'.

        Returns:
            dict[int, float | dict]: Sampled series with requested method.
        """
        buckets = self.bucket(series, interval)
        sampled: dict[int, Any] = {}
        last_val = None

        if method == 'ohlc':
            for ts, bucket in buckets.items():
                if bucket:
                    open_ = last_val if last_val is not None else bucket[0]
                    high = max(bucket + [open_])
                    low = min(bucket + [open_])
                    close = bucket[-1]
                    sampled[ts] = {'open': open_, 'high': high, 'low': low, 'close': close}
                    last_val = close
                elif last_val is not None:
                    sampled[ts] = {'open': last_val, 'high': last_val, 'low': last_val, 'close': last_val}
                else:
                    sampled[ts] = None
        else:
            pick_fn = {
                'open': lambda b: b[0],
                'high': max,
                'low': min,
                'close': lambda b: b[-1]
            }[method]

            for ts, bucket in buckets.items():
                if bucket:
                    sampled[ts] = pick_fn(bucket)
                    last_val = sampled[ts]
                elif last_val is not None:
                    sampled[ts] = last_val
                else:
                    sampled[ts] = None

        return sampled
                
from typing import Union, Optional
from itertools import accumulate
import numpy as np

class EventHistory(History):
    """
    EventHistory is a specialized history tracker for market events, including:
    - Trades
    - Orders
    - Cancellations

    It allows filtering and analysis of these events for use in modeling,
    feature extraction, and simulation.
    """

    events: dict[int, Union[Order, TradeInfo, Cancellation]]

    def __init__(
        self,
        start: int,
        end: int,
        events: list[Union[Order, TradeInfo, Cancellation]],
        publish_interval: int,
        retention_mins: Optional[int] = None
    ):
        """
        Initializes the EventHistory object.

        Args:
            start (int): Start timestamp in nanoseconds.
            end (int): End timestamp in nanoseconds.
            events (list[Order | TradeInfo | Cancellation]): Initial market events.
            publish_interval (int): Interval at which states are published.
            retention_mins (int | None): Optional retention window in minutes.
        """
        self.events = {e.timestamp: e for e in events}
        self.start = start
        self.end = end
        self.retention_mins = retention_mins
        self.publish_interval = publish_interval

    @property
    def trades(self) -> dict[int, TradeInfo]:
        """Returns all trades indexed by timestamp."""
        return {ts: t for ts, t in self.events.items() if t.type == 't'}

    @property
    def orders(self) -> dict[int, Order]:
        """Returns all orders indexed by timestamp."""
        return {ts: o for ts, o in self.events.items() if o.type == 'o'}

    @property
    def cancellations(self) -> dict[int, Cancellation]:
        """Returns all cancellations indexed by timestamp."""
        return {ts: c for ts, c in self.events.items() if c.type == 'c'}

    @property
    def last_trade(self) -> TradeInfo:
        """Returns the most recent trade."""
        return self.trades[max(self.trades)]

    @property
    def trade_prices(self) -> dict[int, float]:
        """Returns trade prices indexed by timestamp."""
        return {ts: t.price for ts, t in self.trades.items()}

    @property
    def OHLC(self) -> Optional[dict[str, float]]:
        """
        Computes OHLC (Open, High, Low, Close) prices from trade data.

        Returns:
            dict[str, float] | None: OHLC structure or None if no trades.
        """
        trade_prices = self.trade_prices
        if trade_prices:
            values = list(trade_prices.values())
            return {
                "open": values[0],
                "high": max(values),
                "low": min(values),
                "close": values[-1],
            }
        return None

    @property
    def traded_volume(self) -> float:
        """
        Computes the total traded volume (price * quantity).

        Returns:
            float: Total traded value.
        """
        return sum(t.quantity * t.price for t in self.trades.values())

    @property
    def traded_volumes(self) -> dict[int, float]:
        """Returns traded volume per timestamp."""
        return {ts: t.quantity * t.price for ts, t in self.trades.items()}

    @property
    def trade_imbalance(self) -> float:
        """
        Computes net trade imbalance (BUY - SELL quantity).

        Returns:
            float: Net trade imbalance.
        """
        return (
            sum(t.quantity for t in self.trades.values() if t.side == OrderDirection.BUY)
            - sum(t.quantity for t in self.trades.values() if t.side == OrderDirection.SELL)
        )

    @property
    def trade_imbalances(self) -> dict[int, float]:
        """
        Returns cumulative trade imbalance over time.

        Returns:
            dict[int, float]: Time-indexed cumulative trade imbalance.
        """
        return dict(zip(
            self.trades.keys(),
            accumulate(
                t.quantity if t.side == OrderDirection.BUY else -t.quantity
                for t in self.trades.values()
            )
        ))

    @property
    def order_volume(self) -> float:
        """
        Computes total order volume.

        Returns:
            float: Total order volume.
        """
        return sum(o.quantity for o in self.orders.values())

    @property
    def order_volumes(self) -> dict[int, float]:
        """Returns order volume per timestamp."""
        return {ts: o.quantity for ts, o in self.orders.items()}

    @property
    def order_imbalance(self) -> float:
        """
        Computes net order imbalance (BUY - SELL quantity).

        Returns:
            float: Net order imbalance.
        """
        return (
            sum(o.quantity for o in self.orders.values() if o.side == OrderDirection.BUY)
            - sum(o.quantity for o in self.orders.values() if o.side == OrderDirection.SELL)
        )

    @property
    def order_imbalances(self) -> dict[int, float]:
        """
        Returns cumulative order imbalance over time.

        Returns:
            dict[int, float]: Time-indexed cumulative order imbalance.
        """
        return dict(zip(
            self.orders.keys(),
            accumulate(
                o.quantity if o.side == OrderDirection.BUY else -o.quantity
                for o in self.orders.values()
            )
        ))

    def append(self, new_history: 'EventHistory') -> 'EventHistory':
        """
        Efficiently appends a new EventHistory to this instance and applies retention logic.

        Args:
            new_history (EventHistory): New event history to append.

        Returns:
            EventHistory: Self, with updated events and time range.
        """
        # Fast in-place update of event dict (assumes timestamps are unique)
        self.events.update(new_history.events)
        self.end = new_history.end

        # Apply retention window if specified
        if self.retention_mins is not None:
            retention_threshold = self.end - self.retention_mins * 60_000_000_000
            self.events = {ts: event for ts, event in self.events.items() if ts >= retention_threshold}
            self.start = max(self.start, retention_threshold)

        return self

    def trade_price(self, sampling_secs: Optional[float] = None) -> dict[int, float]:
        """
        Returns sampled or raw trade price series.

        Args:
            sampling_secs (float | None): Optional sampling interval in seconds.

        Returns:
            dict[int, float]: Time-series of trade prices.
        """
        trades = {time: trade.price for time, trade in self.trades.items()}
        return self.sample(trades, sampling_secs) if sampling_secs else trades

    def ohlc(self, interval: float) -> dict[int, dict[str, float]]:
        """
        Computes OHLC over sampled intervals.

        Args:
            interval (float): Sampling interval in seconds.

        Returns:
            dict[int, dict[str, float]]: OHLC per bucket.
        """
        return self.sample(self.trade_price(), interval, 'ohlc')

    def mean_trade_price(self, interval: float) -> dict[int, Optional[float]]:
        """
        Computes mean trade price per time bucket.

        Args:
            interval (float): Sampling interval in seconds.

        Returns:
            dict[int, float | None]: Time-indexed average trade price.
        """
        sampled: dict[int, Optional[float]] = {}
        last_val: Optional[float] = None

        for ts, prices in self.bucket(self.trade_price(), interval).items():
            if prices:
                sampled[ts] = float(np.mean(prices))
                last_val = prices[-1]
            elif last_val is not None:
                sampled[ts] = last_val
            else:
                sampled[ts] = None

        return sampled

class L2History(History):
    """
    Represents the historical record of L2Snapshots and trades over time.

    Attributes:
        snapshots (dict[int, L2Snapshot]): Mapping of timestamps to L2Snapshot instances.
        trades (dict[int, TradeInfo]): Mapping of timestamps to TradeInfo instances.
        start (int): The earliest timestamp in the history.
        end (int): The latest timestamp in the history.
        retention_mins (int | None): Optional retention window in minutes. If set, older data will be purged.
    """

    snapshots: dict[int, L2Snapshot]
    trades: dict[int, TradeInfo]
    start : int
    end : int
    retention_mins : int | None

    def __init__(
        self,
        snapshots: dict[int, L2Snapshot],
        trades: dict[int, TradeInfo],
        publish_interval: int,
        retention_mins: int | None = None
    ):
        """
        Initialize an L2History object.

        Args:
            snapshots (dict[int, L2Snapshot]): Initial snapshots to populate history.
            trades (dict[int, TradeInfo]): Initial trades to populate history.
            retention_mins (int | None): Optional retention window in minutes.
        """
        self.snapshots = snapshots
        self.trades = trades
        self.start = list(snapshots.keys())[0] - publish_interval
        self.end = list(snapshots.keys())[-1]
        self.retention_mins = retention_mins

    def append(self, new_history: 'L2History') -> 'L2History':
        """
        Append another L2History instance to this history.

        Merges snapshots and trades, then applies retention logic if enabled.

        Args:
            new_history (L2History): The history instance to append.

        Returns:
            L2History: Updated history instance with merged data.
        """
        # Merge and sort snapshots and trades
        self.snapshots = dict(list(sorted((self.snapshots | new_history.snapshots).items())))
        self.trades = dict(list(sorted((self.trades | new_history.trades).items())))
        self.end = list(self.snapshots.keys())[-1]

        # Apply retention if configured
        if self.retention_mins:
            min_time = self.end - self.retention_mins * 60_000_000_000  # nanoseconds
            # Remove old snapshots
            for t in list(self.snapshots):
                if t < min_time:
                    del self.snapshots[t]
                else:
                    break
            # Remove old trades
            for t in list(self.trades):
                if t < min_time:
                    del self.trades[t]
                else:
                    break
        self.start = list(self.snapshots.keys())[0]
        return self

    def insert(self, snapshot : L2Snapshot):
        """
        Insert a snapshot to the history, sorting and updating start/end times.
        
        Args:
            snapshot (L2Snapshot): The snapshot to insert.
        """
        self.snapshots[snapshot.timestamp] = snapshot
        self.snapshots = dict(list(sorted((self.snapshots).items())))
        self.end = list(self.snapshots.keys())[-1]
        self.start = list(self.snapshots.keys())[0]

    def reconcile(self, existing_volumes: dict[str, dict[float, float]], config: MarketSimulationConfig, depth: int) -> None:
        """
        Reconcile all snapshots in history with specified volume adjustments.

        Args:
            existing_volumes (dict): Dictionary of volume adjustments (bids and asks).
            config (MarketSimulationConfig): Simulation configuration.
            depth (int): Depth of order book to retain.
        """
        for time in self.snapshots:
            self.snapshots[time] = self.snapshots[time].reconcile(existing_volumes, config, depth)
    
    def ohlc(self, interval: float):
        return self.sample(self.trade(), interval, 'ohlc')

    def midquote(self, sampling_secs: float | None = None) -> dict[int, float]:
        """
        Compute the midquote (average of best bid and ask) over time.

        Args:
            sampling_secs (float | None): Optional sampling interval in seconds.

        Returns:
            dict[int, float]: Time series of midquotes.
        """
        midquotes = {
            time: (snapshot.best_bid() + snapshot.best_ask()) / 2
            for time, snapshot in self.snapshots.items()
        }
        return self.sample(midquotes, sampling_secs) if sampling_secs else midquotes

    def bid(self, sampling_secs: float | None = None) -> dict[int, float]:
        """
        Get the best bid prices over time.

        Args:
            sampling_secs (float | None): Optional sampling interval in seconds.

        Returns:
            dict[int, float]: Time series of best bid prices.
        """
        bids = {time: snapshot.best_bid() for time, snapshot in self.snapshots.items()}
        return self.sample(bids, sampling_secs) if sampling_secs else bids

    def ask(self, sampling_secs: float | None = None) -> dict[int, float]:
        """
        Get the best ask prices over time.

        Args:
            sampling_secs (float | None): Optional sampling interval in seconds.

        Returns:
            dict[int, float]: Time series of best ask prices.
        """
        asks = {time: snapshot.best_ask() for time, snapshot in self.snapshots.items()}
        return self.sample(asks, sampling_secs) if sampling_secs else asks

    def trade(self, sampling_secs: float | None = None) -> dict[int, float]:
        """
        Get the trade prices over time.

        Args:
            sampling_secs (float | None): Optional sampling interval in seconds.

        Returns:
            dict[int, float]: Time series of trade prices.
        """
        trades = {time: trade.price for time, trade in self.trades.items()}
        return self.sample(trades, sampling_secs) if sampling_secs else trades

    def imbalance(self, depth: int | None = None, sampling_secs: float | None = None) -> dict[int, float]:
        """
        Get the order book imbalance over time.

        Args:
            depth (int | None): Depth of order book to consider.
            sampling_secs (float | None): Optional sampling interval in seconds.

        Returns:
            dict[int, float]: Time series of imbalance values.
        """
        imbalance = {time: snapshot.imbalance(depth) for time, snapshot in self.snapshots.items()}
        return self.sample(imbalance, sampling_secs) if sampling_secs else imbalance

    def mean_imbalance(self, depth: int | None = None) -> float:
        """
        Compute the mean order book imbalance over the history.

        Args:
            depth (int | None): Depth of order book to consider.

        Returns:
            float: Mean imbalance value.
        """
        imbalance_history = self.imbalance(depth)
        return sum(imbalance_history.values()) / len(imbalance_history)


class Book(BaseModel):
    """
    Represents an order book at a specific point in time, including events
    (orders, trades, cancellations) that have occurred since the last update.

    Attributes:
        id (int): Internal book identifier.
        bids (list[LevelInfo]): List of LevelInfo objects representing bid levels.
        asks (list[LevelInfo]): List of LevelInfo objects representing ask levels.
        events (list[Order | TradeInfo | Cancellation] | None): List of events applied to the book 
            since the last snapshot.
    """

    i: int = Field(alias="id")
    b: list[LevelInfo] = Field(alias="bids")
    a: list[LevelInfo] = Field(alias="asks")
    e: list[Order | TradeInfo | Cancellation] | None = Field(alias="events")

    @property
    def id(self) -> int:
        """
        Get the ID of the order book.

        Returns:
            int: The book's unique identifier.
        """
        return self.i

    @property
    def bids(self) -> list[LevelInfo]:
        """
        Get the list of bid levels.

        Returns:
            list[LevelInfo]: Bid levels in descending price order.
        """
        return self.b

    @property
    def asks(self) -> list[LevelInfo]:
        """
        Get the list of ask levels.

        Returns:
            list[LevelInfo]: Ask levels in ascending price order.
        """
        return self.a

    @property
    def events(self) -> list[Order | TradeInfo | Cancellation] | None:
        """
        Get the list of recent events applied to the book.

        Returns:
            list[Order | TradeInfo | Cancellation] | None: List of events or None.
        """
        return self.e
    
    @property
    def trades(self) -> dict[int, TradeInfo]:
        return {t.timestamp : t for t in self.events if t.type == 't'}
    
    @property
    def orders(self) -> dict[int, Order]:
        return {o.timestamp : o for o in self.events if o.type == 'o'}
    
    @property
    def cancellations(self) -> dict[int, Cancellation]:
        return {c.timestamp : c for c in self.events if c.type == 'c'}
    
    @property
    def trade_prices(self) -> dict[int, float]:
        return {ts : t.price for ts, t in self.trades.items()}
    
    @property
    def last_trade(self) -> TradeInfo:
        return self.trades[max(self.trades)]
    
    @property
    def OHLC(self) -> dict:       
        trade_prices = self.trade_prices 
        if len(trade_prices) > 0:
            return {
                "open" : list(trade_prices.values())[0],
                "high" : max(trade_prices.values()),
                "low" : min(trade_prices.values()),
                "close" : list(trade_prices.values())[-1],
            }
        else:
            return None
        
    @property
    def traded_volume(self) -> float:       
        return sum([t.quantity * t.price for t in self.trades.values()])
    
    @property
    def traded_volumes(self) -> dict:
        return {ts: t.quantity * t.price for ts,t in self.trades.items()}
        
    @property
    def trade_imbalance(self) -> float:       
        return sum([t.quantity for t in self.trades.values() if t.side == OrderDirection.BUY]) - sum([t.quantity for t in self.trades.values() if t.side == OrderDirection.SELL])
    
    @property 
    def trade_imbalances(self) -> dict[int,float]:        
        return dict(zip(
            self.trades.keys(),
            accumulate(t.quantity if t.side == OrderDirection.BUY else -t.quantity for t in self.trades.values())
        ))
    
    @property
    def order_volume(self) -> float:       
        return sum([o.quantity for o in self.orders.values()])

    @property 
    def order_volumes(self) -> dict[int,float]:
        return {ts : o.quantity for ts, o in self.orders.items()}
    
    @property
    def order_imbalance(self) -> float:       
        return sum([o.quantity for o in self.orders.values() if o.side == OrderDirection.BUY]) - sum([o.quantity for o in self.orders.values() if o.side == OrderDirection.SELL])
   
    # THIS IS NOT NEEDED MOST LIKELY 
    @property 
    def order_imbalances(self) -> dict[int,float]:
        return dict(zip(
            self.orders.keys(),
            accumulate(o.quantity if o.side == OrderDirection.BUY else -o.quantity for o in self.orders.values())
        ))
        
    @classmethod
    def from_json(cls, json: dict, depth : int = 21) -> 'Book':
        """
        Convert a JSON object from the simulator format into a Book instance.

        Args:
            json (dict): JSON dictionary with book details.
            depth (int): Number of book levels to retain in the bids and asks arrays.

        Returns:
            Book: A new Book instance populated with bids, asks, and events.
        """
        id = json['i']
        bids = []
        asks = []
        if json['b']:
            bids = [LevelInfo.from_json(bid) for bid in json['b']][:depth]
        if json['a']:
            asks = [LevelInfo.from_json(ask) for ask in json['a']][:depth]

        events = []
        if json['e']:
            # Parse events: orders, trades, cancellations
            events = [
                Order.from_json(event) if event['y'] == 'o' else
                TradeInfo.from_json(event) if event['y'] == 't' else
                Cancellation.from_json(event) if event['y'] == 'c' else
                None
                for event in json['e']
            ]

        return cls.model_construct(id=id, bids=bids, asks=asks, events=events)
    
    @classmethod
    def from_ypy(cls, json: YpyObject, depth : int = 21) -> 'Book':
        book_id = json['bookId']
        bids = []
        for i, lvl in enumerate(json['bid']):
            if i >= 21:
                break
            bids.append(LevelInfo.model_construct(
                p=lvl['price'],
                q=lvl['volume'],
                o=[Order.model_construct(
                        id=o['orderId'],
                        timestamp=o['timestamp'],
                        quantity=o['volume'],
                        side=o['direction'],
                        order_type="limit",
                        price=lvl['price']
                    ) for o in lvl['orders']] if i < 5 else None
            ))
        asks = []
        for i, lvl in enumerate(json['ask']):
            if i >= 21:
                break
            asks.append(LevelInfo.model_construct(
                p=lvl['price'],
                q=lvl['volume'],
                o=[Order.model_construct(
                        id=o['orderId'],
                        timestamp=o['timestamp'],
                        quantity=o['volume'],
                        side=o['direction'],
                        order_type="limit",
                        price=lvl['price']
                    ) for o in lvl['orders']] if i < 5 else None
            ))
        events = []
        for ev in json['record']:
            ev_type = ev['event']
            if ev_type == 'place':
                events.append(Order.from_event(ev))
            elif ev_type == 'trade':
                events.append(TradeInfo.from_event(ev))
            elif ev_type == 'cancel':
                events.append(Cancellation.from_event(ev))

        return Book.model_construct(
            i=book_id,
            b=bids,
            a=asks,
            e=events if events else None
        )

    def snapshot(self, timestamp: int) -> L2Snapshot:
        """
        Generate an L2Snapshot of the current book state.

        Args:
            timestamp (int): Timestamp to assign to the snapshot.

        Returns:
            L2Snapshot: Snapshot representing current bids and asks.
        """
        return L2Snapshot(
            timestamp=timestamp,
            bids={l.price: LevelInfo.model_construct(price=l.price, quantity=l.quantity, orders=l.orders) for l in self.bids},
            asks={l.price: LevelInfo.model_construct(price=l.price, quantity=l.quantity, orders=l.orders) for l in self.asks}
        )
        
    def process_history(
        self, 
        history: dict[int, L2Snapshot], 
        trades: dict[int, TradeInfo], 
        timestamp: int, 
        config: MarketSimulationConfig, 
        retention_mins: int, 
        depth: int | None = None
    ) -> tuple[L2History, bool, list[str]]:
        """
        Processes an existing L2 history with the current book state.

        Args:
            history (dict[int, L2Snapshot]): Dictionary of previous snapshots indexed by timestamp.
            trades (dict[int, TradeInfo]): Dictionary of trades indexed by timestamp.
            timestamp (int): Current timestamp for the new snapshot.
            config (MarketSimulationConfig): Configuration settings for volume precision and publish intervals.
            retention_mins (int): Retention period for keeping history (in minutes).
            depth (int | None): Optional depth to limit order book levels.

        Returns:
            tuple:
                - L2History: The updated history object including the new snapshot.
                - bool: True if the reconstructed snapshot matches the target snapshot.
                - list[str]: List of discrepancies detected between reconstructed and target snapshot.
        """
        # Generate a snapshot of the current book state at the given timestamp
        target_snapshot: L2Snapshot = self.snapshot(timestamp)

        # Compare the last snapshot in history with the target snapshot to check for discrepancies
        pre_matched, pre_discrepancies, pre_existing_volumes = (
            list(history.values())[-1].compare(target_snapshot, config)
        )

        # Build a new history object from the provided snapshots and trades
        history_obj: L2History = L2History(
            snapshots=history, 
            trades=trades, 
            retention_mins=retention_mins,
            publish_interval=config.publish_interval
        )

        # Attempt to reconcile discrepancies by applying existing volume corrections
        history_obj.reconcile(pre_existing_volumes, config, depth)

        # After reconciliation, compare again to detect any remaining mismatches
        matched, discrepancies, existing_volumes = (
            list(history_obj.snapshots.values())[-1].compare(target_snapshot, config)
        )

        # Insert the target snapshot into the history
        history_obj.insert(target_snapshot)

        return history_obj, matched, discrepancies

    def history(
        self,
        snapshot: L2Snapshot,
        config: MarketSimulationConfig,
        retention_mins: int | None = None,
        depth: int | None = None
    ) -> tuple[L2History, bool, list[str]]:
        """
        Build an L2History from the current book and apply all events.

        Args:
            snapshot (L2Snapshot): The initial snapshot to start from.
            config (MarketSimulationConfig): Simulation configuration.
            retention_mins (int | None): Optional retention window in minutes.
            depth (int | None): Optional depth to limit order book levels.

        Returns:
            tuple:
                - L2History: The resulting history after applying events.
                - bool: True if the resulting snapshot matches the target.
                - list[str]: List of discrepancies found during reconciliation.
        """
        if not depth:
            depth = len(snapshot.bids)
        # Create history dictionary and add the starting snapshot
        history = {snapshot.timestamp: snapshot.model_copy(deep=True)}
        trades = {}

        # Generate target snapshot for comparison
        target_snapshot = self.snapshot(snapshot.timestamp + config.publish_interval)
        # Apply events in chronological order
        for event in sorted(self.events, key=lambda x: x.timestamp):
            match event:
                case o if isinstance(event, Order):
                    # Place new order
                    if o.side == OrderDirection.BUY:
                        if o.price not in snapshot.bids:
                            snapshot.bids[o.price] = LevelInfo(price=o.price, quantity=0.0, orders=None)
                        snapshot.bids[o.price].q = round(
                            snapshot.bids[o.price].q + o.quantity,
                            config.volumeDecimals
                        )
                    else:
                        if o.price not in snapshot.asks:
                            snapshot.asks[o.price] = LevelInfo(price=o.price, quantity=0.0, orders=None)
                        snapshot.asks[o.price].q = round(
                            snapshot.asks[o.price].q + o.quantity,
                            config.volumeDecimals
                        )

                case t if isinstance(event, TradeInfo):
                    # Record trade
                    trades[t.timestamp] = t
                    if t.side == OrderDirection.BUY:
                        if t.price in snapshot.asks:
                            snapshot.asks[t.price].q = round(
                                snapshot.asks[t.price].q - t.quantity,
                                config.volumeDecimals
                            )
                            if snapshot.asks[t.price].quantity == 0.0:
                                del snapshot.asks[t.price]
                    else:
                        if t.price in snapshot.bids:
                            snapshot.bids[t.price].q = round(
                                snapshot.bids[t.price].q - t.quantity,
                                config.volumeDecimals
                            )
                            if snapshot.bids[t.price].quantity == 0.0:
                                del snapshot.bids[t.price]

                case c if isinstance(event, Cancellation):
                    # Cancel existing order
                    if c.price >= snapshot.best_ask():
                        if c.price in snapshot.asks:
                            snapshot.asks[c.price].q = round(
                                snapshot.asks[c.price].q - c.quantity,
                                config.volumeDecimals
                            )
                            if snapshot.asks[c.price].quantity == 0.0:
                                del snapshot.asks[c.price]
                    else:
                        if c.price in snapshot.bids:
                            snapshot.bids[c.price].q = round(
                                snapshot.bids[c.price].q - c.quantity,
                                config.volumeDecimals
                            )
                            if snapshot.bids[c.price].quantity == 0.0:
                                del snapshot.bids[c.price]

            # Add snapshot to history after each update
            history[event.timestamp] = snapshot.model_copy(deep=True)
        # Compare resulting snapshot to target
        pre_matched, pre_discrepancies, pre_existing_volumes = snapshot.compare(target_snapshot, config)
        history_obj = L2History(snapshots=history, trades=trades, retention_mins=retention_mins, publish_interval=config.publish_interval)
        # Apply determined existing volumes to attempt to reconcile any discrepancies
        history_obj.reconcile(pre_existing_volumes, config, depth)
        # Check if any remaining discrepancies after reconciliation
        matched, discrepancies, existing_volumes = list(history_obj.snapshots.values())[-1].compare(target_snapshot, config)
        # Add the target snapshot to the history
        history_obj.insert(target_snapshot)

        return history_obj, matched, discrepancies

    def append_to_history(
        self,
        history: L2History,
        config: MarketSimulationConfig,
        depth: int | None = None
    ) -> tuple[L2History, bool, list[str]]:
        """
        Append the book's events to an existing L2History.

        Args:
            history (L2History): Existing history to append to.
            config (MarketSimulationConfig): Simulation configuration.
            depth (int | None): Optional depth to limit levels.

        Returns:
            tuple:
                - L2History: Updated history including new events.
                - bool: True if final snapshot matches target.
                - list[str]: List of discrepancies found.
        """
        new_history, matched, discrepancies = self.history(
            snapshot=list(history.snapshots.values())[-1],
            config=config,
            retention_mins=history.retention_mins,
            depth=depth
        )
        return history.append(new_history=new_history), matched, discrepancies
    
    def event_history(
        self,
        timestamp : int,
        config: MarketSimulationConfig,
        retention_mins: int | None = None
    ) -> EventHistory:
        """
        Build an EventHistory from the current book and apply all events.

        Args:
            timestamp (int): Timestamp of the state update associated with the book instance.
            config (MarketSimulationConfig): Simulation configuration.
            retention_mins (int | None): Optional retention window in minutes.

        Returns:
            tuple:
                - EventHistory: The resulting history after applying events.
                - bool: True if the resulting snapshot matches the target.
                - list[str]: List of discrepancies found during reconciliation.
        """
        return EventHistory(timestamp - config.publish_interval, timestamp, self.events, config.publish_interval, retention_mins)
    
    def append_to_event_history(
        self,
        timestamp : int,
        history: EventHistory,
        config: MarketSimulationConfig
    ) -> tuple[EventHistory, bool, list[str]]:
        """
        Append the book's events to an existing L2History.

        Args:
            history (EventHistory): Existing history to append to.
            config (MarketSimulationConfig): Simulation configuration.
            depth (int | None): Optional depth to limit levels.

        Returns:
            tuple:
                - EventHistory: Updated history including new events.
                - bool: True if final snapshot matches target.
                - list[str]: List of discrepancies found.
        """
        new_history = self.event_history(
            timestamp=timestamp,
            config=config,
            retention_mins=history.retention_mins
        )
        return history.append(new_history=new_history)

class Balance(BaseModel):
    """
    Represents an account balance for a specific currency.

    Attributes:
        currency (str): String identifier for the currency (e.g., "USD", "BTC").
        total (float): Total currency balance in the account.
        free (float): Free currency balance available for order placement.
        reserved (float): Reserved currency balance tied up in resting orders.
        initial (float | None): Initial balance for the currency at the start of the simulation or session.
    """
    c : str = Field(alias="currency")
    t : float = Field(alias="total")
    f : float = Field(alias="free")
    r : float = Field(alias="reserved")
    i : float = Field(alias="initial", default=None)

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

    @property
    def initial(self) -> float:
        return self.i

    @classmethod
    def from_json(self, currency : str, json : dict):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        return Balance.model_construct(currency=currency,total=json['t'],free=json['f'],reserved=json['r'], initial=json['i'])

class Fees(BaseModel):
    """
    Represents account fees for a specific agent and book.

    Attributes:
        volume_traded (float): Total volume traded in the aggregation period for tiered fee assignment.
        maker_fee_rate (float): The current maker fee rate for the agent.
        taker_fee_rate (float): The current taker fee rate for the agent.
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
        return Fees.model_construct(volume_traded=json['v'],maker_fee_rate=json['m'],taker_fee_rate=json['t'])
    
class OrderCurrency(IntEnum):
    """
    Enum to represent the currency in which the quantity of an order is specified.

    Attributes:
        BASE (int): Quantity is specified in BASE currency.
        BASE (int): Quantity is specified in QUOTE currency.
    """
    BASE=0
    QUOTE=1
    
class Loan(BaseModel):
    """
    Represents a loan associated with an open position for the agent.

    Attributes:
        order_id (int): ID of the order associated with the loan.
        amount (float): Total loan amount.
        currency (OrderCurrency): Currency in which the loan is denominated.
        base_collateral (float): Amount of base currency collateral posted for the loan.
        quote_collateral (float): Amount of quote currency collateral posted for the loan.
    """
    i : int = Field(alias="order_id")
    a : float = Field(alias="amount")
    c : OrderCurrency = Field(alias="currency")    
    bc : float = Field(alias="base_collateral")    
    qc : float = Field(alias="quote_collateral")

    @property
    def order_id(self) -> int:
        return self.i

    @property
    def amount(self) -> float:
        return self.a

    @property
    def currency(self) -> OrderCurrency:
        return self.c

    @property
    def base_collateral(self) -> float:
        return self.bc

    @property
    def quote_collateral(self) -> float:
        return self.qc

    @classmethod
    def from_json(self, json : dict):
        """
        Method to transform simulator format model to the format required by the MarketSimulationStateUpdate synapse.
        """
        return Loan.model_construct(order_id=json['i'],amount=json['a'],currency=OrderCurrency(json['c']),base_collateral=json['bc'],quote_collateral=json['qc'])
    
    def __str__(self):
        return f"{self.amount} {self.currency.name} [COLLAT : {self.base_collateral} BASE | {self.quote_collateral} QUOTE]"

class Account(BaseModel):
    """
    Represents an agent's trading account.

    Attributes:
        agent_id (int): The agent ID which owns the account.
        book_id (int): ID of the book on which the account is able to trade.
        base_balance (Balance): Balance object for the base currency.
        quote_balance (Balance): Balance object for the quote currency.
        base_loan (float): Amount of base currency currently borrowed.
        quote_loan (float): Amount of quote currency currently borrowed.
        base_collateral (float): Amount of base currency posted as collateral.
        quote_collateral (float): Amount of quote currency posted as collateral.
        orders (list[Order]): List of the current open orders associated to the agent.
        loans (dict[int, Loan]): Mapping from order ID to Loan objects representing open loans.
        fees (Fees | None): The current fee structure for the account.
        traded_volume (float | None): Total volume traded by the account. Defaults to None.
    """
    i : int = Field(alias="agent_id")
    b : int = Field(alias="book_id")
    bb : Balance = Field(alias="base_balance")
    qb : Balance = Field(alias="quote_balance")
    bl : float = Field(alias="base_loan", default=0.0)
    ql : float = Field(alias="quote_loan", default=0.0)
    bc : float = Field(alias="base_collateral", default=0.0)
    qc : float = Field(alias="quote_collateral", default=0.0)    
    o : list[Order] = Field(alias="orders", default=[])
    l : dict[int, Loan] = Field(alias="loans", default={})
    f : Fees | None = Field(alias="fees")
    v : float | None = Field(alias="traded_volume", default=None)

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
    def base_loan(self) -> float:
        return self.bl

    @property
    def quote_loan(self) -> float:
        return self.ql

    @property
    def base_collateral(self) -> float:
        return self.bc

    @property
    def quote_collateral(self) -> float:
        return self.qc

    @property
    def orders(self) -> list[Order]:
        return self.o

    @property
    def loans(self) -> dict[int, Loan]:
        return self.l

    @property
    def fees(self) -> Fees | None:
        return self.f

    @property
    def traded_volume(self) -> float | None:
        return self.v
    
    @property
    def own_quote(self) -> float:
        return self.quote_balance.total - self.quote_loan + self.quote_collateral
    
    @property
    def own_base(self) -> float:
        return self.base_balance.total - self.base_loan + self.base_collateral
    
    @classmethod
    def from_json(cls, json: dict) -> "Account":
        """
        Construct an Account from simulator JSON into an Account model,
        using model_construct and manually populating nested classes.
        """
        return cls.model_construct(
            i=json["i"],
            b=json["b"],
            bb=Balance.model_construct(**json["bb"]),
            qb=Balance.model_construct(**json["qb"]),
            bl=json.get("bl", 0.0),
            ql=json.get("ql", 0.0),
            bc=json.get("bc", 0.0),
            qc=json.get("qc", 0.0),
            o=[Order.from_json(o) for o in json.get("o", [])],
            l={int(k): Loan.from_json(v) for k, v in json.get("l", {}).items()},
            f=Fees.model_construct(**json["f"]) if json.get("f") else None,
            v=json.get("v"),
        )

class OrderDirection(IntEnum):
    """
    Enum to represent order direction.

    Attributes:
        BUY (int): Associated with an order placed in the BUY direction.
        SELL (int): Associated with an order placed in the SELL direction.
    """
    BUY=0
    SELL=1

class STP(IntEnum):
    """
    Enum to represent self-trade prevention options.

    Attributes:
        NO_STP (int): No self-trade prevention.
        CANCEL_OLDEST (int): If self-trade would occur when placing an order, cancel the resting order.
        CANCEL_NEWEST (int): If self-trade would occur when placing an order, cancel the aggressive order.
        CANCEL_BOTH (int): If self-trade would occur when placing an order, cancel both orders.
        DECREASE_CANCEL (int): If self-trade would occur when placing an order, cancel the quantity of the smaller order from the larger.
    """
    NO_STP=0
    CANCEL_OLDEST=1
    CANCEL_NEWEST=2
    CANCEL_BOTH=3
    DECREASE_CANCEL=4

class TimeInForce(IntEnum):
    """
    Enum to represent order time-in-force options.

    Attributes:
        GTC (int): Order remains on the book until cancelled by the agent, or executed in a trade.
        GTT (int): Order remains on the book until specified expiry period elapses, unless traded or cancelled before expiry.
        IOC (int): Any part of the order which is not immediately traded will be cancelled.
        FOK (int): If the order will not be executed in its entirety immediately upon receipt by the simulator, the order will be rejected.
    """
    GTC=0
    GTT=1
    IOC=2
    FOK=3
    
class LoanSettlementOption(IntEnum):
    """
    Enum to represent options for repayment of margin loans when submitting an order.

    Attributes:
        NONE (int): Do not settle outstanding margin loans with proceeds from this order.
        FIFO (int): Settle outstanding margin loans in a FIFO (First-In-First-Out) manner
                    using proceeds from this order.
    """
    NONE = -2
    FIFO = -1
    
    @classmethod
    def from_string(cls, name):
        match name:
            case 'NONE':
                return LoanSettlementOption.NONE
            case 'FIFO':
                return LoanSettlementOption.FIFO
            case _:
                try:
                    order_id = int(name)
                    return order_id
                except:
                    return None

class LazyLevel(Sequence):
    """
    Lazily-parsed order book level.

    This class defers construction of the `LevelInfo` and `Order` objects until their data is accessed.

    Attributes:
        _raw (dict): Raw data for the level.
        _parsed (LevelInfo | None): Parsed LevelInfo object once loaded.
    """
    __slots__ = ("_raw", "_parsed")

    def __init__(self, raw_level):
        self._raw = raw_level
        self._parsed = None

    def _load(self):
        if self._parsed is None:
            orders = [Order.model_construct(**o) for o in self._raw.get("o", [])] if self._raw.get("o") else []
            self._parsed = LevelInfo.model_construct(
                p=self._raw.get("p"),
                q=self._raw.get("q"),
                o=orders
            )
            self._raw = None

    def __getattr__(self, name):
        self._load()
        return getattr(self._parsed, name)

    def __getitem__(self, index):
        self._load()
        return self._parsed[index]

    def __len__(self):
        self._load()
        return len(self._parsed)

    def parse(self) -> LevelInfo:
        """Return fully parsed LevelInfo object."""
        self._load()
        return self._parsed


class LazyLevels(Sequence):
    """
    Collection of lazily-parsed order book levels.

    Attributes:
        _raw_levels (list[dict]): Raw level data.
        _parsed (dict[int, LazyLevel]): Cache of parsed LazyLevel objects.
    """
    def __init__(self, raw_levels):
        self._raw_levels = raw_levels
        self._parsed = {}

    def __getitem__(self, i):
        if i not in self._parsed:
            self._parsed[i] = LazyLevel(self._raw_levels[i])
        return self._parsed[i]

    def __iter__(self):
        for i in range(len(self._raw_levels)):
            yield self[i]

    def __len__(self):
        return len(self._raw_levels)

    def parse(self) -> list[LevelInfo]:
        """Parse all levels and return list of LevelInfo objects."""
        return [lvl.parse() for lvl in self]


class LazyBook(Book):
    """
    Lazily-parsed order book.

    Attributes:
        _raw (dict): Raw order book data.
        _bids (LazyLevels | None): Lazily-parsed bid levels.
        _asks (LazyLevels | None): Lazily-parsed ask levels.
        _events (list | None): Parsed events (Orders, Trades, Cancellations).
    """
    def __init__(self, raw_book):
        self._raw = raw_book
        self._bids = None
        self._asks = None
        self._events = None

    @property
    def id(self) -> int:
        return self._raw.get("i")

    @property
    def bids(self):
        if self._bids is None:
            self._bids = LazyLevels(self._raw.get("b", []))
        return self._bids

    @property
    def asks(self):
        if self._asks is None:
            self._asks = LazyLevels(self._raw.get("a", []))
        return self._asks

    @property
    def events(self):
        if self._events is None:
            raw_events = self._raw.get("e", [])
            parsed_events = []
            for e in raw_events:
                ty = e.get("y")
                if ty == "o":
                    parsed_events.append(Order.model_construct(**e))
                elif ty == "t":
                    parsed_events.append(TradeInfo.model_construct(**e))
                elif ty == "c":
                    parsed_events.append(Cancellation.model_construct(**e))
                else:
                    parsed_events.append(e)
            self._events = parsed_events
        return self._events

    def parse(self) -> Book:
        """Return fully parsed Book object."""
        return Book.model_construct(
            i=self._raw.get("i"),
            b=self.bids.parse(),
            a=self.asks.parse(),
            e=self.events
        )


class LazyBooks(Mapping):
    """
    Lazily-parsed collection of order books.

    Attributes:
        _raw_books (dict[int, dict]): Raw book data keyed by book_id.
        _parsed_books (dict[int, LazyBook]): Cache of parsed LazyBook objects.
    """
    def __init__(self, raw_books: dict):
        self._raw_books = {int(k): v for k, v in raw_books.items()}
        self._parsed_books = {}

    def __getitem__(self, book_id: int):
        if book_id not in self._parsed_books:
            self._parsed_books[book_id] = LazyBook(self._raw_books[book_id])
        return self._parsed_books[book_id]

    def __iter__(self):
        return iter(self._raw_books)

    def __len__(self):
        return len(self._raw_books)

    def items(self):
        for k in self._raw_books:
            yield k, self[k]

    def values(self):
        for k in self._raw_books:
            yield self[k]

    def parse(self) -> dict[int, Book]:
        """Return dict of fully parsed Book objects keyed by book_id."""
        return {book_id: lb.parse() for book_id, lb in self.items()}


class LazyAccount:
    """
    Lazily-parsed trading account.

    Attributes:
        _raw (dict): Raw account data.
        _parsed (Account | None): Parsed Account object.
    """
    def __init__(self, raw_acc):
        self._raw = raw_acc
        self._parsed = None

    @property
    def data(self):
        if self._parsed is None:
            bb = Balance.model_construct(**self._raw.get("bb", {}))
            qb = Balance.model_construct(**self._raw.get("qb", {}))
            orders = [Order.model_construct(**o) for o in self._raw.get("o", [])]

            loans = {}
            for k, v in self._raw.get("l", {}).items():
                loan = Loan.model_construct(**v)
                loan.c = OrderCurrency(loan.c)
                loans[int(k)] = loan

            fees = Fees.model_construct(**self._raw["f"]) if self._raw.get("f") else None

            self._parsed = Account.model_construct(
                i=self._raw.get("i"),
                b=self._raw.get("b"),
                bb=bb,
                qb=qb,
                bl=self._raw.get("bl", 0.0),
                ql=self._raw.get("ql", 0.0),
                bc=self._raw.get("bc", 0.0),
                qc=self._raw.get("qc", 0.0),
                o=orders,
                l=loans,
                f=fees,
                v=self._raw.get("v")
            )
            self._raw = None
        return self._parsed

    def __getattr__(self, name):
        return getattr(self.data, name)

    def parse(self) -> Account:
        """Return fully parsed Account object."""
        return self.data


class LazyAccounts(Mapping):
    """
    Lazily-parsed collection of agent accounts.

    Attributes:
        _raw_accounts (dict[int, dict[int, dict]]): Outer dict keyed by agent ID (uid), inner dict keyed by book_id.
        _parsed_accounts (dict[int, dict[int, LazyAccount]]): Cache of parsed LazyAccount objects.
    """
    def __init__(self, raw_accounts: dict):
        self._raw_accounts = {
            int(uid): {int(book_id): account for book_id, account in uid_accounts.items()}
            for uid, uid_accounts in raw_accounts.items()
        }
        self._parsed_accounts = {}

    def __getitem__(self, uid: int):
        if uid not in self._parsed_accounts:
            self._parsed_accounts[uid] = {
                book_id: LazyAccount(raw_acc)
                for book_id, raw_acc in self._raw_accounts[uid].items()
            }
        return self._parsed_accounts[uid]

    def __iter__(self):
        return iter(self._raw_accounts)

    def __len__(self):
        return len(self._raw_accounts)

    def items(self):
        for k in self._raw_accounts:
            yield k, self[k]

    def values(self):
        for k in self._raw_accounts:
            yield self[k]

    def parse(self) -> dict[int, dict[int, Account]]:
        """Return dict of fully parsed Account objects keyed by uid and book_id."""
        return {
            uid: {book_id: la.parse() for book_id, la in books.items()}
            for uid, books in self.items()
        }