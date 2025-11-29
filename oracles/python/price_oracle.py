"""
Price Oracle - Concrete implementation of BaseOracleService

Handles price queries for crypto/fiat pairs.
Routes to multiple providers (CoinGecko, Binance, Mock).

API Key Configuration:
    Set via environment variables:
        export COINGECKO_API_KEY="your-key-here"
        export COINGECKO_API_TYPE="demo"  # or "pro" or "free"
"""

import struct
import time
import threading
import os
from typing import Dict, Optional, Tuple
from fractions import Fraction
import requests
from oracle_base import BaseOracleService

# ============================================================================
# Protocol Constants - Price Interface

# Price::OracleQuery (104 bytes)
PRICE_ORACLE_QUERY_SIZE = 104

# Price::OracleReply (16 bytes)
PRICE_ORACLE_REPLY_SIZE = 16


class PriceProvider:
    """Base class for price providers"""

    def __init__(self, name: str):
        self.name = name
        self.cache: Dict[str, tuple] = {}  # (price, timestamp)
        self.cache_ttl = 60  # seconds

    def get_price(self, currency1: str, currency2: str) -> Optional[tuple]:
        """
        Get price for currency1/currency2.
        Returns (numerator, denominator) or None if unavailable.
        """
        # Check cache
        cache_key = f"{currency1}/{currency2}"
        if cache_key in self.cache:
            price, cached_time = self.cache[cache_key]
            if time.time() - cached_time < self.cache_ttl:
                print(f"[{self.name}] Cache hit: {cache_key} = {price}")
                return price

        # Fetch fresh data
        price = self.fetch_price(currency1, currency2)
        if price:
            self.cache[cache_key] = (price, time.time())
        return price

    def fetch_price(self, currency1: str, currency2: str) -> Optional[tuple]:
        """Override this method in subclasses"""
        raise NotImplementedError

# ============================================================================
# Price Provider Implementations
# ============================================================================


class CoinGeckoProvider(PriceProvider):
    """CoinGecko API provider with rate limit handling"""

    RATE_LIMIT_DELAY = 2.0
    last_request_time = 0
    rate_limit_lock = threading.Lock()

    def __init__(self, api_key=None, api_type=None):
        super().__init__("CoinGecko")
        self.api_url = "https://api.coingecko.com/api/v3/simple/price"

        # Get API key and type from environment if not provided
        self.api_key = api_key or os.getenv('COINGECKO_API_KEY')
        self.api_type = api_type or os.getenv('COINGECKO_API_TYPE', 'free')

        # Log configuration (without exposing full key)
        if self.api_key:
            print(
                f"[{self.name}] Configured with provide API key(type: {self.api_type})")
        else:
            print(f"[{self.name}] Using free tier (no API key)")

        # Currency mappings
        self.coin_map = {
            "BTC": "bitcoin",
            "ETH": "ethereum",
            "USDT": "tether",
            "BNB": "binancecoin",
            "USDC": "usd-coin",
            "XRP": "ripple",
            "ADA": "cardano",
            "SOL": "solana",
            "DOGE": "dogecoin",
        }

    def fetch_price(self, currency1: str, currency2: str) -> Optional[tuple]:
        try:
            # Rate limiting
            with CoinGeckoProvider.rate_limit_lock:
                elapsed = time.time() - CoinGeckoProvider.last_request_time
                if elapsed < self.RATE_LIMIT_DELAY:
                    sleep_time = self.RATE_LIMIT_DELAY - elapsed
                    print(
                        f"  [Rate limit] Sleeping {sleep_time:.1f}s before CoinGecko request")
                    time.sleep(sleep_time)
                CoinGeckoProvider.last_request_time = time.time()

            coin_id = self.coin_map.get(currency1.upper())
            if not coin_id:
                print(f"[{self.name}] Unknown currency: {currency1}")
                return None

            # Convert stablecoins to USD, or use crypto directly
            vs_currency = currency2.lower()
            if vs_currency in ['usdt', 'usdc']:
                vs_currency = 'usd'

            params = {
                "ids": coin_id,
                "vs_currencies": vs_currency
            }

            # Add API key if provided
            headers = {}
            if self.api_key:
                if self.api_type == "demo":
                    headers["x-cg-demo-api-key"] = self.api_key
                elif self.api_type == "pro":
                    headers["x-cg-pro-api-key"] = self.api_key

            response = requests.get(
                self.api_url, params=params, headers=headers, timeout=5)

            # Handle rate limit
            if response.status_code == 429:
                print(f"[{self.name}] Rate limit hit! Waiting 60 seconds...")
                time.sleep(60)
                response = requests.get(
                    self.api_url, params=params, headers=headers, timeout=5)

            if response.status_code == 200:
                data = response.json()
                price = data.get(coin_id, {}).get(vs_currency)
                if price:
                    # High precision conversion
                    frac = Fraction(price).limit_denominator(10**9)
                    numerator = int(frac.numerator)
                    denominator = int(frac.denominator)
                    print(
                        f"[{self.name}] {currency1}/{currency2} = {numerator}/{denominator}")
                    return (numerator, denominator)

            print(f"[{self.name}] API error: {response.status_code}")
            return None

        except Exception as e:
            print(f"[{self.name}] Error: {e}")
            return None


class BinanceProvider(PriceProvider):
    """Binance API provider"""

    def __init__(self):
        super().__init__("Binance")
        self.api_url = "https://api.binance.com/api/v3/ticker/price"

    def fetch_price(self, currency1: str, currency2: str) -> Optional[tuple]:
        try:
            # Convert USD to USDT for Binance
            currency2_converted = currency2.upper()
            if currency2_converted == 'USD':
                currency2_converted = 'USDT'

            symbol = f"{currency1}{currency2_converted}".upper()
            params = {"symbol": symbol}

            response = requests.get(self.api_url, params=params, timeout=5)
            if response.status_code == 200:
                data = response.json()
                price = float(data.get("price", 0))
                if price > 0:
                    # High precision conversion
                    frac = Fraction(price).limit_denominator(10**9)
                    numerator = int(frac.numerator)
                    denominator = int(frac.denominator)
                    print(
                        f"[{self.name}] {currency1}/{currency2} = {numerator}/{denominator}")
                    return (numerator, denominator)

            print(f"[{self.name}] API error: {response.status_code}")
            return None

        except Exception as e:
            print(f"[{self.name}] Error: {e}")
            return None


class MockProvider(PriceProvider):
    """Mock provider for testing"""

    def __init__(self):
        super().__init__("Mock")

    def fetch_price(self, currency1: str, currency2: str) -> Optional[tuple]:
        # Return mock data
        mock_prices = {
            "BTC/USD": (45000, 1),
            "BTC/USDT": (45000, 1),
            "ETH/USD": (3000, 1),
            "ETH/BTC": (15, 500),  # 0.03 BTC
            "BTC/ETH": (30, 1),    # 30 ETH
        }

        pair = f"{currency1}/{currency2}".upper()
        price = mock_prices.get(pair, (1000, 1))
        print(f"[{self.name}] {pair} = {price[0]}/{price[1]}")
        return price

# ============================================================================
# Price Service Implementation


class PriceService(BaseOracleService):
    """
    Price Oracle Service.

    Handles price queries and routes to providers (CoinGecko, Binance, Mock).
    """

    def __init__(self, host: str = "0.0.0.0", port: int = 9001):
        # Initialize base service
        super().__init__(
            service_name="Price",
            host=host,
            port=port,
            interface_query_size=PRICE_ORACLE_QUERY_SIZE,
            interface_reply_size=PRICE_ORACLE_REPLY_SIZE
        )

        # Price providers
        self.providers: Dict[str, PriceProvider] = {}

        # Register default providers
        # TODO: make this easier to config ?
        self.register_provider("coingecko", CoinGeckoProvider())
        self.register_provider("binance", BinanceProvider())
        self.register_provider("mock", MockProvider())

    def register_provider(self, oracle_id: str, provider: PriceProvider):
        """Register a price provider"""
        self.providers[oracle_id.lower()] = provider
        print(f"Registered provider: {oracle_id} ({provider.name})")

    # ========== Abstract Methods of BaseOracleService ==========

    def parse_interface_query(self, data: bytes, offset: int) -> dict:
        """
        Parse Price::OracleQuery (104 bytes).

        Structure:
            oracle (32 bytes) - Oracle provider ID
            timestamp (8 bytes) - Query timestamp
            currency1 (32 bytes) - Base currency
            currency2 (32 bytes) - Quote currency

        Returns:
            dict with keys: oracle_id, timestamp, currency1, currency2
        """
        if len(data) < offset + PRICE_ORACLE_QUERY_SIZE:
            raise ValueError(f"Data too small for Price::OracleQuery")

        # Parse oracle ID (32 bytes)
        oracle_bytes = data[offset:offset + 32]
        offset += 32

        # Parse timestamp (8 bytes)
        timestamp = struct.unpack('<Q', data[offset:offset + 8])[0]
        offset += 8

        # Parse currency1 (32 bytes)
        currency1_bytes = data[offset:offset + 32]
        offset += 32

        # Parse currency2 (32 bytes)
        currency2_bytes = data[offset:offset + 32]

        # Convert bytes to strings (strip null bytes)
        oracle_id = oracle_bytes.decode(
            'utf-8', errors='ignore').rstrip('\x00')
        currency1 = currency1_bytes.decode(
            'utf-8', errors='ignore').rstrip('\x00')
        currency2 = currency2_bytes.decode(
            'utf-8', errors='ignore').rstrip('\x00')

        print(
            f"  Query: oracle={oracle_id}, {currency1}/{currency2}, timestamp={timestamp}")

        return {
            'oracle_id': oracle_id,
            'timestamp': timestamp,
            'currency1': currency1,
            'currency2': currency2
        }

    def build_interface_reply(self, numerator: int = 0, denominator: int = 0) -> bytes:
        """
        Build Price::OracleReply (16 bytes).

        Structure:
            numerator (8 bytes, signed) - Price numerator
            denominator (8 bytes, signed) - Price denominator

        Args:
            numerator: Price numerator
            denominator: Price denominator

        Returns:
            16-byte reply
        """
        return struct.pack('<qq', numerator, denominator)

    def process_query_logic(self, query_data: dict) -> Tuple[int, dict]:
        """
        Process price query.

        Args:
            query_data: Parsed query from parse_interface_query()

        Returns:
            Tuple of (error_flags, reply_data_dict)
        """
        oracle_id = query_data['oracle_id']
        currency1 = query_data['currency1']
        currency2 = query_data['currency2']

        # Find provider
        provider = self.providers.get(oracle_id.lower())
        if not provider:
            print(f"  Unknown oracle: {oracle_id}")
            # ORACLE_FLAG_INVALID_ORACLE
            # Error: unknown oracle
            return (0x0001, {'numerator': 0, 'denominator': 0})

        # Get price
        result = provider.get_price(currency1, currency2)
        if result:
            numerator, denominator = result
            # Success
            return (0, {'numerator': numerator, 'denominator': denominator})
        else:
            print(f"  Provider {oracle_id} failed to get price")
            # ORACLE_FLAG_ORACLE_UNAVAIL
            # Error: provider failed
            return (0x0002, {'numerator': 0, 'denominator': 0})

    def get_error_reply_data(self) -> dict:
        """Get default error reply data"""
        return {'numerator': 0, 'denominator': 0}

# ============================================================================
# Main
# ============================================================================


if __name__ == "__main__":

    # Create service with configuration
    service = PriceService(
        host='0.0.0.0',
        port=9001
    )

    service.start()
