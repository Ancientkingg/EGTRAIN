#include "io/ExternalState.h"
#include "util/Log.h"
#include <map>
#include <memory>
#include <zmq.hpp>

namespace {
constexpr int TransportTimeoutMs = 100;

// Made by the first send, so a process that never shares its state holds no ZeroMQ context.
// Members are destroyed in reverse order: the sockets go before the context, whose destructor
// waits for every socket to be closed.
struct ExternalTransport {
	zmq::context_t context;
	std::map<std::string, std::unique_ptr<zmq::socket_t>> sockets;
};

ExternalTransport& externalTransport() {
	static ExternalTransport transport;
	return transport;
}
}

bool send_external_state(nlohmann::json jsmsg, const std::string& xml, const std::string& endpoint) {
	ExternalTransport& transport = externalTransport();
	auto& socket = transport.sockets[endpoint];
	try {
		if (!socket) {
			socket = std::make_unique<zmq::socket_t>(transport.context, zmq::socket_type::req);
			socket->set(zmq::sockopt::linger, 0);
			socket->set(zmq::sockopt::immediate, 1);
			socket->set(zmq::sockopt::sndtimeo, TransportTimeoutMs);
			socket->set(zmq::sockopt::rcvtimeo, TransportTimeoutMs);
			socket->connect(endpoint);
		}

		jsmsg["xml"] = xml;
		zmq::message_t request(jsmsg.dump());
		if (!socket->send(request, zmq::send_flags::dontwait))
			return false;

		zmq::message_t reply;
		if (!socket->recv(reply, zmq::recv_flags::none)) {
			socket.reset();
			return false;
		}
		EG_LOG(IoZmq, Debug) << "Received from " << endpoint << ":" << reply.to_string();
		return true;
	} catch (const std::exception& error) {
		socket.reset();
		EG_LOG(IoZmq, Warning) << "External state send failed for " << endpoint << ": " << error.what() << "\n";
	}
	return false;
}
