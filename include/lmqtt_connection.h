#pragma once

#include "lmqtt_common.h"
#include "lmqtt_packet.h"
#include "lmqtt_reason_codes.h"

namespace lmqtt {

class connection : public std::enable_shared_from_this<connection> {
	 // enable_shared_from_this will allow us to create a shared pointer "this" internally
	 // in other word, a shared this pointer other than a raw pointer
public:

	connection(
		asio::io_context& context,
		asio::ip::tcp::socket socket
	) :
		_context(context), _socket(std::move(socket)) {}

	virtual ~connection() {}

public:
	[[nodiscard]] uint32_t get_id() const noexcept {
		return _id;
	}

	void connect_to_client(uint32_t id = 0) noexcept {
		if (_socket.is_open()) {
			//_id = id;

			// read availabe messages
			read_fixed_header();
		}
	}

	void disconnect() {
		if (is_connected()) {
			//the context can close the socket whenever it is available
			asio::post(
				_context,
				[this]() {
					_socket.close();
				}
			);
		}
	}

	bool is_connected() const noexcept {
		return _socket.is_open();
	};

private:
	// async method: prime the context ready to read a packet header
	void read_fixed_header() {
		asio::async_read(
			_socket,
			asio::buffer(
				&_tempPacket._header._controlField,
				sizeof(uint8_t)
			),
			[this](std::error_code ec, size_t length) {
				if (!ec) {

					// we identify the packet type
					const reason_code rcode = _tempPacket.create_fixed_header();

					if (rcode == reason_code::MALFORMED_PACKET
						|| rcode == reason_code::PROTOCOL_ERROR) {
						_socket.close();
					}

					// on first connection, only accept CONNECT packets
					if (_isFirstPacket) {
						if (_tempPacket._type != packet_type::CONNECT) {
							_socket.close();
							return;
						}
						_isFirstPacket = false;
					}

					// read packet length
					uint8_t mul = 1;
					uint32_t packetLenth = 0;

					// the following section is used to decode the packet length, where we use the asio buffer to read
					// one byte at a time. So if the MSB bit = 1, we read the next byte and so and fourth. If we read 
					// more than 4 (mul is multiplied 4 times) it means that the packet is malformed.
					reason_code rCode = reason_code::SUCCESS;
					for (uint32_t offset = 0; offset < 4; ++offset) {
						uint8_t nextByte = read_byte();
						_tempPacket._header._packetLen += (nextByte & 0x7f) * mul;
						if (mul > 0x200000) { // 128 * 128 * 128
							rCode = reason_code::MALFORMED_PACKET;
						}
						mul *= 0x80; // prepare for next byte
						if (!(nextByte & 0x80) || (rCode == reason_code::MALFORMED_PACKET)) break;
					}

					if (rCode == reason_code::MALFORMED_PACKET) {
						_socket.close();
						return;
					}

					// only allow packets with a certain size
					if (_tempPacket._header._packetLen > PACKET_SIZE_LIMIT) {
						std::cout << "[" << _id << "] Closed connection. Reason: Packet size limit exceeded\n";
						_socket.close();
						return;
					}

					// resize packet body to hold the rest of the data
					_tempPacket._body.resize(_tempPacket._header._packetLen);
					
					read_packet_body();

				} else {
					std::cout << "[" << _id << "] Reading Header Failed: " << ec.message() << "\n";
					_socket.close();
				}
			}
		);
	}

	[[nodiscard]] uint8_t read_byte() {
		uint8_t byte;
		asio::async_read(
			_socket,
			asio::buffer(
				&byte,
				sizeof(uint8_t)
			),
			[this](std::error_code ec, size_t length) {
				if (ec) {
					std::cout << "[" << _id << "] Reading Byte Failed: " << ec.message() << "\n";
					_socket.close();
				}
			}
		);
		return byte;
	}

	void read_packet_body() {
		asio::async_read(
			_socket,
			asio::buffer(
				_tempPacket._body.data(),
				_tempPacket._body.size()
			),
			[this](std::error_code ec, size_t length) {
				if (!ec) {

					reason_code rcode;
					switch (_tempPacket._type) {
					case packet_type::CONNECT:
					{
						rcode = _tempPacket.decode_connect_packet_body();
						break;
					}
					}

					read_packet();

					/*payload::payload_proxy* data = _tempPacket._payloads[0].get();
					payload::payload<std::string_view>* realData =
						static_cast<payload::payload<std::string_view>*>(data);

					std::cout << "[" << realData->get_data() << "] Connection Approved\n";*/

					_socket.close();
					return;


					/*if (_tempPacket.decode_packet_body() != reason_code::SUCCESS) {
						// According to packet type, we create our ACK packet to be sent to the client

						_socket.close();
					}*/
					
					//read_fixed_header();

				} else {
					std::cout << "[" << _id << "] Reading pakcet body Failed: " << ec.message() << "\n";
					_socket.close();
				}
			}
		);
	}

	void read_packet() {

		switch (_tempPacket._type) {
		case packet_type::CONNECT:
		{
			configure_client();
			break;
		}


		}

		read_fixed_header();
	}

	void configure_client() {
		for (uint8_t i = 0; i < _tempPacket._payloads.size(); ++i) {

			auto ptype = _tempPacket._payloads[i]->get_payload_type();



		}
	}


public:

	std::string get_remote_endpoint() const {
		return _socket.remote_endpoint().address().to_string();
	}

	void set_client_id(const std::string_view& clientId) {
		_clientId = clientId;
	}

protected:
	// each connection has a unique socket
	asio::ip::tcp::socket _socket;

	// context
	asio::io_context& _context;
	
	// connection ID
	uint32_t _id = 0;

	// on connect, we expect a connect packet
	bool _isFirstPacket = true;

	lmqtt_packet _tempPacket;

	std::string_view _clientId;
};

} // namespace lmqtt
