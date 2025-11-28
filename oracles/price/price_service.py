""""
This service receives queries in the format:
  [RequestResponseHeader][OracleMachineQuery][Price::OracleQuery]

And replies in the format:
  [RequestResponseHeader][OracleMachineReply][Price::OracleReply]

The aggregator routes to multiple price providers (coingecko, binance, etc.)
based on the 'oracle' field in Price::OracleQuery.
"""
import socket
import struct
import time
import threading
from typing import Dict, Optional
import requests

# ============================================================================
# Protocol Structures
# ============================================================================

# RequestResponseHeader (FIXED SIZE: 8 bytes)
# struct RequestResponseHeader {
#     unsigned char _size[3];    // 3 bytes
#     unsigned char _type;       // 1 byte
#     unsigned int _dejavu;      // 4 bytes
# }
REQUEST_RESPONSE_HEADER_SIZE = 8  # CONSTANT!

# OracleMachineQuery
# struct OracleMachineQuery {
#     uint64_t oracleQueryId;
#     uint32_t oracleInterfaceIndex;
#     uint32_t timeoutInSeconds;
# }
ORACLE_MACHINE_QUERY_SIZE = 16  # 8 + 4 + 4

# Price::OracleQuery
# struct OracleQuery {
#     id oracle;           // 32 bytes (m256i)
#     DateAndTime timestamp;  // 8 bytes
#     id currency1;        // 32 bytes (m256i)
#     id currency2;        // 32 bytes (m256i)
# }
PRICE_ORACLE_QUERY_SIZE = 104  # 32 + 8 + 32 + 32

# OracleMachineReply
# struct OracleMachineReply {
#     uint64_t oracleQueryId;
#     uint16_t oracleMachineErrorFlags;
#     uint16_t padding0;
#     uint32_t padding1;
# }
ORACLE_MACHINE_REPLY_SIZE = 16  # 8 + 2 + 2 + 4

# Price::OracleReply
# struct OracleReply {
#     int64_t numerator;
#     int64_t denominator;
# }
PRICE_ORACLE_REPLY_SIZE = 16  # 8 + 8

# Total sizes
QUERY_PACKET_SIZE = REQUEST_RESPONSE_HEADER_SIZE + ORACLE_MACHINE_QUERY_SIZE + PRICE_ORACLE_QUERY_SIZE
REPLY_PACKET_SIZE = REQUEST_RESPONSE_HEADER_SIZE + ORACLE_MACHINE_REPLY_SIZE + PRICE_ORACLE_REPLY_SIZE


# ============================================================================
# Price Provider Base Class
PRICE_DELAY_TIME_SECONDS = 60

class PriceProvider:
    """Base class for price providers"""
    
    def __init__(self, name: str):
        self.name = name
        # TODO: clean up old cache entries
        self.cache: Dict[str, tuple] = {}  # (price, timestamp)
        self.cache_ttl = PRICE_DELAY_TIME_SECONDS  # cache time-to-live in seconds
    
    def get_price(self, currency1: str, currency2: str) -> Optional[tuple]:
        """
        Get price for currency1/currency2.
        Returns (numerator, denominator) or None if unavailable.
        """
        # Check cache
        cache_key = f"{currency1}/{currency2}"
        if cache_key in self.cache:
            price, cached_time = self.cache[cache_key]
            # Allow reuse cached price within TTL
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
# helper functions
def float_to_rational_fixed(price: float, decimals: int = 10) -> tuple:
    """
    Convert float to rational with fixed decimal places.
    
    Args:
        price: Float price
        decimals: Number of decimal places to preserve (default: 8)
    
    Returns:
        (numerator, denominator) as integers
    """
    multiplier = 10 ** decimals
    numerator = int(round(price * multiplier))
    denominator = multiplier

    return (numerator, denominator)

# ============================================================================
# CoinGecko
class CoinGeckoProvider(PriceProvider):
    """CoinGecko API provider"""

    API_TYPE = "free"  # Demo, pro ...
    # Rate limiting for CoinGecko Free API
    # Public API: 5-15 calls/min
    # Demo account: 30 calls/min
    RATE_LIMIT_DELAY = 2.0  # 2 seconds between calls
    last_request_time = 0
    rate_limit_lock = threading.Lock()
    
    def __init__(self, api_key=None):
        super().__init__("CoinGecko")
        self.api_url = "https://api.coingecko.com/api/v3/simple/price"
        self.api_key = api_key
        # Map currency codes to CoinGecko IDs
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
        
        headers = {}
        if self.api_key:
            if COINGECKO_API_API_TYPE == "demo":
                headers["x-cg-demo-api-key"] = self.api_key  # For Demo
            elif COINGECKO_API_API_TYPE == "pro":
                headers["x-cg-pro-api-key"] = self.api_key   # For Paid
            else:
                # empty, just follow free tier
                pass
            
        try:
            # Rate limiting: Wait if needed
            with CoinGeckoProvider.rate_limit_lock:
                elapsed = time.time() - CoinGeckoProvider.last_request_time
                if elapsed < self.RATE_LIMIT_DELAY:
                    sleep_time = self.RATE_LIMIT_DELAY - elapsed
                    print(f"  [Rate limit] Sleeping {sleep_time:.1f}s before CoinGecko request")
                    time.sleep(sleep_time)
                CoinGeckoProvider.last_request_time = time.time()

            coin_id = self.coin_map.get(currency1.upper())
            if not coin_id:
                print(f"[{self.name}] Unknown currency: {currency1}")
                return None
            
            # Temporary hack
            # TODO: improve this, we can request and convert the pair of currency
            vs_currency = currency2.lower()
            if vs_currency in ['usdt', 'usdc']:
                vs_currency = 'usd'  # CoinGecko expects 'usd' not 'usdt'

            params = {
                "ids": coin_id,
                "vs_currencies": vs_currency
            }

            response = requests.get(self.api_url, params=params, headers=headers, timeout=5)
            if response.status_code == 429:
                print(f"[{self.name}] Rate limit hit! Waiting 60 seconds...")
                time.sleep(60)
                response = requests.get(self.api_url, params=params, headers=headers, timeout=5)

            if response.status_code == 200:
                data = response.json()
                price = data.get(coin_id, {}).get(vs_currency)
                if price:
                    # Convert to numerator/denominator
                    numerator, denominator = float_to_rational_fixed(price)
                    print(f"[{self.name}] {currency1}/{currency2} = {numerator}/{denominator}")
                    return (numerator, denominator)
            
            print(f"[{self.name}] API error: {response.status_code}")
            return None
            
        except Exception as e:
            print(f"[{self.name}] Error: {e}")
            return None

# Dummy Provider for Testing
class MockProvider(PriceProvider):
    """Mock provider for testing"""
    
    def __init__(self):
        super().__init__("Mock")
    
    def fetch_price(self, currency1: str, currency2: str) -> Optional[tuple]:
        # Return mock data
        mock_prices = {
            "BTC/USD": (100000, 1),
            "ETH/USD": (4000, 1),
        }
        
        pair = f"{currency1}/{currency2}".upper()
        price = mock_prices.get(pair, (1000, 1))
        print(f"[{self.name}] {pair} = {price[0]}/{price[1]}")
        return price

# ============================================================================
# Price Service
# TODO: make a base service class for other oracles to inherit from
class PriceService:
    """
    Price service that receives queries and routes to providers.
    
    Protocol:
    - Receives: [RequestResponseHeader][OracleMachineQuery][Price::OracleQuery]
    - Sends:    [RequestResponseHeader][OracleMachineReply][Price::OracleReply]
    """
    
    def __init__(self, host: str = "0.0.0.0", port: int = 9001):
        self.host = host
        self.port = port
        self.providers: Dict[str, PriceProvider] = {}
        self.stats = {
            "total_queries": 0,
            "successful": 0,
            "failed": 0,
        }
        
        # Register providers
        self.register_provider("coingecko", CoinGeckoProvider())
        self.register_provider("mock", MockProvider())

    def register_provider(self, oracle_id: str, provider: PriceProvider):
        """Register a price provider"""
        self.providers[oracle_id] = provider
        print(f"Registered provider: {oracle_id} ({provider.name})")

    def parse_query_packet(self, data: bytes) -> tuple:
        """
        Parse incoming query packet.
        Returns: (header, oracle_query, price_query)
        """
        if len(data) < QUERY_PACKET_SIZE:
            raise ValueError(f"Packet too small: {len(data)} < {QUERY_PACKET_SIZE}")
        
        offset = 0
        
        # Parse RequestResponseHeader (8 bytes)
        # struct RequestResponseHeader {
        #     unsigned char _size[3];    // 3 bytes
        #     unsigned char _type;       // 1 byte
        #     unsigned int _dejavu;      // 4 bytes
        # }
        header = data[offset:offset + REQUEST_RESPONSE_HEADER_SIZE]
        
        # Extract fields for validation
        msg_size = header[0] | (header[1] << 8) | (header[2] << 16)
        msg_type = header[3]
        msg_dejavu = struct.unpack('<I', header[4:8])[0]
        
        print(f"  Header: size={msg_size}, type={msg_type}, dejavu={msg_dejavu}")
        
        offset += REQUEST_RESPONSE_HEADER_SIZE
        
        # Parse OracleMachineQuery
        query_id, interface_index, timeout_sec = struct.unpack('<QII', 
            data[offset:offset + ORACLE_MACHINE_QUERY_SIZE])
        offset += ORACLE_MACHINE_QUERY_SIZE
        
        # Parse Price::OracleQuery
        oracle_bytes = data[offset:offset + 32]
        offset += 32
        timestamp = struct.unpack('<Q', data[offset:offset + 8])[0]
        offset += 8
        currency1_bytes = data[offset:offset + 32]
        offset += 32
        currency2_bytes = data[offset:offset + 32]
        
        # Convert bytes to strings (strip null bytes)
        oracle_id = oracle_bytes.decode('utf-8', errors='ignore').rstrip('\x00')
        currency1 = currency1_bytes.decode('utf-8', errors='ignore').rstrip('\x00')
        currency2 = currency2_bytes.decode('utf-8', errors='ignore').rstrip('\x00')
        print(f"  ID: id={oracle_id}, currency1={currency1}, currency2={currency2}")
        
        return (
            header,
            (query_id, interface_index, timeout_sec),
            (oracle_id, timestamp, currency1, currency2))

    def build_reply_packet(self, header: bytes, query_id: int, error_flags: int,
                          numerator: int, denominator: int) -> bytes:
        """
        Build reply packet.
        Format: [RequestResponseHeader][OracleMachineReply][Price::OracleReply]
        """
        reply = bytearray()
        
        # RequestResponseHeader (8 bytes)
        # Update the size field in the header to match reply packet size
        reply_size = REPLY_PACKET_SIZE
        
        # Encode _size[3] (3 bytes)
        reply.append(reply_size & 0xFF)
        reply.append((reply_size >> 8) & 0xFF)
        reply.append((reply_size >> 16) & 0xFF)
        
        # Keep _type and _dejavu from request header
        reply.append(header[3])  # _type
        reply += header[4:8]     # _dejavu (4 bytes)
        
        # OracleMachineReply
        reply += struct.pack('<QHHI', query_id, error_flags, 0, 0)
        
        # Price::OracleReply
        reply += struct.pack('<qq', numerator, denominator)
        
        return bytes(reply)
    
    def process_query(self, data: bytes) -> bytes:
        """Process a price query and return reply"""
        self.stats["total_queries"] += 1
        
        try:
            # Parse query
            header, oracle_query, price_query = self.parse_query_packet(data)
            query_id, interface_index, timeout_sec = oracle_query
            oracle_id, timestamp, currency1, currency2 = price_query
            
            print(f"\n[Query #{query_id}] Interface={interface_index}, Oracle={oracle_id}, "
                  f"{currency1}/{currency2}, Timeout={timeout_sec}s")
            
            # Find provider
            provider = self.providers.get(oracle_id.lower())
            if not provider:
                print(f"[Query #{query_id}] Unknown oracle: {oracle_id}")
                self.stats["failed"] += 1
                return self.build_reply_packet(header, query_id, 0x0001, 0, 0)  # Error flag
            
            # Get price
            result = provider.get_price(currency1, currency2)
            if result:
                numerator, denominator = result
                self.stats["successful"] += 1
                print(f"[Query #{query_id}] Success: {numerator}/{denominator}")
                return self.build_reply_packet(header, query_id, 0, numerator, denominator)
            else:
                print(f"[Query #{query_id}] Provider failed")
                self.stats["failed"] += 1
                return self.build_reply_packet(header, query_id, 0x0002, 0, 0)  # Provider error
        except Exception as e:
            print(f"[Query] Error: {e}")
            self.stats["failed"] += 1
            # Try to extract query_id if possible
            try:
                query_id = struct.unpack('<Q', data[REQUEST_RESPONSE_HEADER_SIZE:REQUEST_RESPONSE_HEADER_SIZE+8])[0]
                header = data[:REQUEST_RESPONSE_HEADER_SIZE]
            except:
                query_id = 0
                header = b'\x00' * REQUEST_RESPONSE_HEADER_SIZE
            return self.build_reply_packet(header, query_id, 0x0004, 0, 0)  # Parse error
        
    def handle_client(self, conn: socket.socket, addr: tuple):
        """Handle a client connection"""
        print(f"\n[Connection] Client connected: {addr}")
        
        try:
            while True:
                # Receive query packet
                data = conn.recv(QUERY_PACKET_SIZE)
                if not data:
                    break
                
                if len(data) < QUERY_PACKET_SIZE:
                    print(f"[Connection] Incomplete packet: {len(data)} bytes")
                    continue
                
                # Process query
                reply = self.process_query(data)
                
                # Send reply
                conn.sendall(reply)
                
        except Exception as e:
            print(f"[Connection] Error: {e}")
        finally:
            conn.close()
            print(f"[Connection] Client disconnected: {addr}")

    def print_stats(self):
        """Print service statistics"""
        print(f"\n{'='*60}")
        print(f"Service Statistics:")
        print(f"  Total Queries:  {self.stats['total_queries']}")
        print(f"  Successful:     {self.stats['successful']}")
        print(f"  Failed:         {self.stats['failed']}")
        if self.stats['total_queries'] > 0:
            success_rate = (self.stats['successful'] / self.stats['total_queries']) * 100
            print(f"  Success Rate:   {success_rate:.1f}%")
        print(f"{'='*60}\n")

    def start(self):
        """Start the service"""
        # TODO: filter by allowed clients
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.port))
        server.listen(5)
        
        print(f"\n{'='*60}")
        print(f"Price Service Started")
        print(f"{'='*60}")
        print(f"Listening on: {self.host}:{self.port}")
        print(f"Registered providers: {', '.join(self.providers.keys())}")
        print(f"{'='*60}\n")
        
        # Statistics thread
        def stats_thread():
            while True:
                time.sleep(60)  # Print stats every minute
                self.print_stats()
        
        threading.Thread(target=stats_thread, daemon=True).start()
        
        try:
            while True:
                conn, addr = server.accept()
                # Handle each client in a separate thread
                client_thread = threading.Thread(
                    target=self.handle_client,
                    args=(conn, addr),
                    daemon=True
                )
                client_thread.start()
        
        except KeyboardInterrupt:
            print("\n[Service] Shutting down...")
            self.print_stats()
        finally:
            server.close()

if __name__ == "__main__":
    service = PriceService(host="0.0.0.0", port=9001)
    service.start()