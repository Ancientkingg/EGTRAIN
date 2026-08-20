#include "io/RailMLParser.h"
#include "simulation/InitialParameters.h"

#include <chrono>
#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <zmq.hpp>

InitialParameters initial_variables(0);

void read_rttp_train_view(std::string rttp);

int main() {
	std::ostringstream output;
	std::ostringstream errors;
	auto* oldOutput = std::cout.rdbuf(output.rdbuf());
	auto* oldErrors = std::cerr.rdbuf(errors.rdbuf());

	read_rttp_train_view(
		"<rTTP><rTTPTrainView><rTTPForSingleTrain trainID=\"T42\">"
		"<tDSectionOccupation tDSectionID=\"S7\" trainID=\"T42\" occupationStart=\"10\" routeId=\"R1\"/>"
		"</rTTPForSingleTrain></rTTPTrainView></rTTP>");
	const bool parsedPayload = output.str().find("Train T42") != std::string::npos && output.str().find("in Section S7") != std::string::npos;

	errors.str("");
	errors.clear();
	read_rttp_train_view("<rTTP><unclosed>");
	const auto sendStart = std::chrono::steady_clock::now();
	const bool sentWithoutListener = send_external_state(
		{{"time", 0}}, "<trafficState/>", "inproc://egtrain-no-listener");
	const auto sendElapsed = std::chrono::steady_clock::now() - sendStart;

	zmq::context_t replyContext;
	std::promise<std::string> endpointPromise;
	auto endpointFuture = endpointPromise.get_future();
	std::promise<void> clientDone;
	auto clientDoneFuture = clientDone.get_future();
	bool receivedEnvelope = false;
	std::thread replyServer([&] {
		zmq::socket_t socket(replyContext, zmq::socket_type::rep);
		socket.set(zmq::sockopt::rcvtimeo, 2000);
		socket.bind("tcp://127.0.0.1:*");
		endpointPromise.set_value(socket.get(zmq::sockopt::last_endpoint));
		zmq::message_t request;
		if (socket.recv(request, zmq::recv_flags::none)) {
			const nlohmann::json envelope = nlohmann::json::parse(request.to_string());
			receivedEnvelope = envelope["time"] == 7 && envelope["xml"] == "<routeChoiceRequest/>";
			socket.send(zmq::buffer("ok"), zmq::send_flags::none);
			clientDoneFuture.wait();
		}
	});
	const std::string endpoint = endpointFuture.get();
	bool sentToListener = false;
	for (int attempt = 0; attempt < 20 && !sentToListener; ++attempt) {
		sentToListener = send_external_state(
			{{"time", 7}}, "<routeChoiceRequest/>", endpoint);
		if (!sentToListener)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	clientDone.set_value();
	replyServer.join();
	std::cout.rdbuf(oldOutput);
	std::cerr.rdbuf(oldErrors);
	const bool parseErrorReported = errors.str().find("RTTP XML parse error") != std::string::npos;
	if (!parsedPayload || !parseErrorReported || sentWithoutListener
			|| sendElapsed >= std::chrono::seconds(1) || !sentToListener || !receivedEnvelope) {
		std::cerr << "railmlparser test failed: parsed=" << parsedPayload
				  << " parse_error=" << parseErrorReported
				  << " absent_peer_sent=" << sentWithoutListener
				  << " absent_peer_ms="
				  << std::chrono::duration_cast<std::chrono::milliseconds>(sendElapsed).count()
				  << " listener_sent=" << sentToListener
				  << " envelope_received=" << receivedEnvelope << "\n";
		return 1;
	}

	return 0;
}
