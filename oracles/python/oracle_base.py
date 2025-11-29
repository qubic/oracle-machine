"""
Base Oracle Service - Reusable class for Oracle services

This base class handles:
- RequestResponseHeader parsing/building
- OracleMachineQuery parsing
- OracleMachineReply building
- TCP connection handling

Oracle ervices (Price, Weather, etc.) inherit and implement:
- parse_interface_query() - Parse interface-specific query payload
- build_interface_reply() - Build interface-specific reply payload
- process_query_logic() - Business logic for the service
"""
import socket
import struct
import threading
import time
# for inherit class must implement some inherited functions error before running those functions
from abc import ABC, abstractmethod
from typing import Tuple, Optional

# ============================================================================
# Core Message Constants
# This will need to be update if core change

# RequestResponseHeader
REQUEST_RESPONSE_HEADER_SIZE = 8

# OracleMachineQuery
ORACLE_MACHINE_QUERY_SIZE = 16

# OracleMachineReply
ORACLE_MACHINE_REPLY_SIZE = 16

# ============================================================================
# Base Oracle Service


class BaseOracleService(ABC):
    """
    Abstract base class for Oracle services.

    Handles protocol-level operations (header, query, reply parsing).
    Concrete services implement interface-specific logic.
    """

    def __init__(self,
                 service_name: str,
                 host: str = "0.0.0.0",
                 port: int = 9001,
                 interface_query_size: int = 0,
                 interface_reply_size: int = 0):
        """
        Initialize base service.

        Args:
            service_name: Name of the service (e.g., "Price", "Weather")
            host: Host to bind to
            port: Port to listen on
            interface_query_size: Size of interface-specific query payload
            interface_reply_size: Size of interface-specific reply payload
        """
        self.service_name = service_name
        self.host = host
        self.port = port
        self.interface_query_size = interface_query_size
        self.interface_reply_size = interface_reply_size

        # Calculate packet sizes
        self.query_packet_size = (REQUEST_RESPONSE_HEADER_SIZE +
                                  ORACLE_MACHINE_QUERY_SIZE +
                                  interface_query_size)
        self.reply_packet_size = (REQUEST_RESPONSE_HEADER_SIZE +
                                  ORACLE_MACHINE_REPLY_SIZE +
                                  interface_reply_size)

        # Statistics
        self.stats = {
            "total_queries": 0,
            "successful": 0,
            "failed": 0
        }
        self.stats_lock = threading.Lock()

    def parse_header(self, data: bytes) -> dict:
        """
        Parse RequestResponseHeader (8 bytes).

        Returns:
            dict with keys: raw_bytes, size, type, dejavu
        """
        if len(data) < REQUEST_RESPONSE_HEADER_SIZE:
            raise ValueError(
                f"Header too small: {len(data)} < {REQUEST_RESPONSE_HEADER_SIZE}")

        header_bytes = data[0:REQUEST_RESPONSE_HEADER_SIZE]

        # Parse _size[3] (3 bytes, little-endian)
        msg_size = header_bytes[0] | (
            header_bytes[1] << 8) | (header_bytes[2] << 16)

        # Parse _type (1 byte)
        msg_type = header_bytes[3]

        # Parse _dejavu (4 bytes, little-endian)
        msg_dejavu = struct.unpack('<I', header_bytes[4:8])[0]

        return {
            'raw_bytes': header_bytes,
            'size': msg_size,
            'type': msg_type,
            'dejavu': msg_dejavu
        }

    def parse_oracle_machine_query(self, data: bytes, offset: int = 0) -> dict:
        """
        Parse OracleMachineQuery (16 bytes).

        Returns:
            dict with keys: query_id, interface_index, timeout_sec
        """
        if len(data) < offset + ORACLE_MACHINE_QUERY_SIZE:
            raise ValueError("Data too small for OracleMachineQuery")

        query_id, interface_index, timeout_sec = struct.unpack('<QII',
                                                               data[offset:offset + ORACLE_MACHINE_QUERY_SIZE])

        return {
            'query_id': query_id,
            'interface_index': interface_index,
            'timeout_sec': timeout_sec
        }

    def build_header(self, msg_size: int, msg_type: int, msg_dejavu: int) -> bytes:
        """
        Build RequestResponseHeader (8 bytes).

        Args:
            msg_size: Total message size (including header)
            msg_type: Message type
            msg_dejavu: Dejavu value

        Returns:
            8-byte header
        """
        header = bytearray()

        # _size[3] (3 bytes, little-endian)
        header.append(msg_size & 0xFF)
        header.append((msg_size >> 8) & 0xFF)
        header.append((msg_size >> 16) & 0xFF)

        # _type (1 byte)
        header.append(msg_type)

        # _dejavu (4 bytes, little-endian)
        header += struct.pack('<I', msg_dejavu)

        return bytes(header)

    def build_oracle_machine_reply(self, query_id: int, error_flags: int) -> bytes:
        """
        Build OracleMachineReply (16 bytes).

        Args:
            query_id: Query ID to echo back
            error_flags: Error flags (0 = success)

        Returns:
            16-byte OracleMachineReply
        """
        return struct.pack('<QHHI', query_id, error_flags, 0, 0)

    def build_reply_packet(self,
                           request_header: bytes,
                           query_id: int,
                           error_flags: int,
                           interface_reply_data: bytes) -> bytes:
        """
        Build complete reply packet.

        Args:
            request_header: Original request header (for type/dejavu)
            query_id: Query ID
            error_flags: Error flags
            interface_reply_data: Interface-specific reply payload

        Returns:
            Complete reply packet
        """
        reply = bytearray()

        # Build header with reply size
        msg_type = request_header[3]
        msg_dejavu = struct.unpack('<I', request_header[4:8])[0]
        header = self.build_header(
            self.reply_packet_size, msg_type, msg_dejavu)
        reply += header

        # Add OracleMachineReply
        reply += self.build_oracle_machine_reply(query_id, error_flags)

        # Add interface-specific reply
        reply += interface_reply_data

        return bytes(reply)

# ========== Abstract Methods (Must be implemented by subclasses) ==========
    @abstractmethod
    def parse_interface_query(self, data: bytes, offset: int) -> dict:
        """
        Parse interface-specific query payload.
        
        Args:
            data: Raw packet data
            offset: Offset where interface payload starts
        
        Returns:
            dict with parsed query data (structure depends on interface)
        
        Must be implemented by concrete service classes.
        """
        pass
    
    @abstractmethod
    def build_interface_reply(self, **kwargs) -> bytes:
        """
        Build interface-specific reply payload.
        
        Args:
            **kwargs: Reply data (structure depends on interface)
        
        Returns:
            bytes of interface-specific reply
        
        Must be implemented by concrete service classes.
        """
        pass
    
    @abstractmethod
    def process_query_logic(self, query_data: dict) -> Tuple[int, dict]:
        """
        Process the query and return result.
        
        Args:
            query_data: Parsed query data from parse_interface_query()
        
        Returns:
            Tuple of (error_flags, reply_data_dict)
            - error_flags: 0 for success, non-zero for errors
            - reply_data_dict: Data to pass to build_interface_reply()
        
        Must be implemented by concrete service classes.
        """
        pass

    @abstractmethod
    def get_error_reply_data(self) -> dict:
        """
        Get default error reply data.
        
        Returns:
            dict to pass to build_interface_reply() for error cases
        
        Must be implemented by concrete service classes.
        """
        pass

# ========== Connection related ==========
    def handle_client(self, conn: socket.socket, addr: tuple):
        """Handle a client connection"""
        print(f"\n[Connection] Client connected: {addr}")
        
        try:
            while True:
                # Receive query packet
                data = conn.recv(self.query_packet_size)
                if not data:
                    break
                
                if len(data) < self.query_packet_size:
                    print(f"[Connection] Incomplete packet: {len(data)} bytes")
                    continue
                
                # Process query
                reply = self.process_query_packet(data)
                
                # Send reply
                conn.sendall(reply)
                
        except Exception as e:
            print(f"[Connection] Error: {e}")
        finally:
            conn.close()
            print(f"[Connection] Client disconnected: {addr}")


    def process_query_packet(self, data: bytes) -> bytes:
        """
        Process a complete query packet and return reply.
        
        This is the main processing pipeline that uses the abstract methods.
        """
        with self.stats_lock:
            self.stats["total_queries"] += 1
        
        try:
            offset = 0
            
            # Parse RequestResponseHeader
            header_info = self.parse_header(data)
            print(f"  Header: size={header_info['size']}, type={header_info['type']}, "
                  f"dejavu={header_info['dejavu']}")
            offset += REQUEST_RESPONSE_HEADER_SIZE
            
            # Parse OracleMachineQuery
            om_query = self.parse_oracle_machine_query(data, offset)
            query_id = om_query['query_id']
            print(f"\n[Query #{query_id}] Interface={om_query['interface_index']}, "
                  f"Timeout={om_query['timeout_sec']}s")
            offset += ORACLE_MACHINE_QUERY_SIZE
            
            # Parse interface-specific query
            interface_query = self.parse_interface_query(data, offset)
            
            # Process query (implemented by subclass)
            error_flags, reply_data = self.process_query_logic(interface_query)
            
            # Build interface-specific reply
            interface_reply_bytes = self.build_interface_reply(**reply_data)
            
            # Build complete reply packet
            reply = self.build_reply_packet(
                header_info['raw_bytes'],
                query_id,
                error_flags,
                interface_reply_bytes
            )
            
            # Update statistics
            with self.stats_lock:
                if error_flags == 0:
                    self.stats["successful"] += 1
                    print(f"[Query #{query_id}] Success")
                else:
                    self.stats["failed"] += 1
                    print(f"[Query #{query_id}] Failed with error flags: 0x{error_flags:04x}")
            
            return reply
            
        except Exception as e:
            print(f"[Query] Error: {e}")
            with self.stats_lock:
                self.stats["failed"] += 1
            
            # Try to extract query_id for error response
            try:
                query_id = struct.unpack('<Q', 
                    data[REQUEST_RESPONSE_HEADER_SIZE:REQUEST_RESPONSE_HEADER_SIZE+8])[0]
                header = data[:REQUEST_RESPONSE_HEADER_SIZE]
            except:
                query_id = 0
                header = b'\x00' * REQUEST_RESPONSE_HEADER_SIZE
            
            # Build error reply with zero data
            error_reply = self.build_interface_reply(**self.get_error_reply_data())
            # ORACLE_FLAG_OM_ERROR_FLAGS = 0xff
            return self.build_reply_packet(header, query_id, 0x00ff, error_reply)
    
    def print_stats(self):
        """Print service statistics"""
        with self.stats_lock:
            print(f"\n{'='*60}")
            print(f"{self.service_name} Service Statistics:")
            print(f"  Total Queries:  {self.stats['total_queries']}")
            print(f"  Successful:     {self.stats['successful']}")
            print(f"  Failed:         {self.stats['failed']}")
            if self.stats['total_queries'] > 0:
                success_rate = (self.stats['successful'] / self.stats['total_queries']) * 100
                print(f"  Success Rate:   {success_rate:.1f}%")
            print(f"{'='*60}\n")
    
    def start(self):
        """Start the service"""
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.port))
        server.listen(5)
        
        print(f"\n{'='*60}")
        print(f"{self.service_name} Service Started")
        print(f"{'='*60}")
        print(f"Listening on: {self.host}:{self.port}")
        print(f"Query packet size: {self.query_packet_size} bytes")
        print(f"Reply packet size: {self.reply_packet_size} bytes")
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
                # Handle each client in a separate thread, same fashion as OM
                client_thread = threading.Thread(
                    target=self.handle_client,
                    args=(conn, addr),
                    daemon=True
                )
                client_thread.start()
        
        except KeyboardInterrupt:
            print(f"\n[Service] Shutting down {self.service_name} service...")
            self.print_stats()
        finally:
            server.close()