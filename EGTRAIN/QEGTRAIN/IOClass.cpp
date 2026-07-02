#include "IOClass.h"
#include <initialParameters.h>
static zmq::context_t ctx;

extern initial_parameters initial_variables;

void read_rttp_train_view(std::string rttp) {
	pugi::xml_document rttp_doc;
	rttp_doc.load_file(rttp.c_str());
	pugi::xml_node trainView = rttp_doc.child("rTTP").child("rTTPTrainView");
	int train_counter = 0;

	for (pugi::xml_node train = trainView.child("rTTPForSingleTrain"); train; train = train.next_sibling("rTTPForSingleTrain")) {
		std::cout << "Train " << train.attribute("trainID").value() << "\n";
		for (pugi::xml_node section = train.child("tDSectionOccupation"); section; section = section.next_sibling("tDSectionOccupation")) {
			std::cout << " in Section " << section.attribute("tDSectionID").value() << "\n";
			std::cout << " trainID " << section.attribute("trainID").value() << "\n";
			std::cout << " occupation starting at " << section.attribute("occupationStart").value() << "\n";
			std::cout << " routeID " << section.attribute("routeId").value() << "\n";
		}
	}
}
void read_rttp_infra_view(std::string rttp) {
	pugi::xml_document rttp_doc;
	rttp_doc.load_file(rttp.c_str());
	pugi::xml_node trainView = rttp_doc.child("rTTP").child("rTTPInfrastructureView");
	int train_counter = 0;

	for (pugi::xml_node train = trainView.child("rTTPForSingleTDSection"); train; train = train.next_sibling("rTTPForSingleTDSection")) {
		std::cout << "tDSectionId " << train.attribute("tDSectionId").value() << "\n";
		for (pugi::xml_node section = train.child("tDSectionOccupation"); section; section = section.next_sibling("tDSectionOccupation")) {
			std::cout << " in Section " << section.attribute("tDSectionID").value() << "\n";
			std::cout << " trainID " << section.attribute("trainID").value() << "\n";
			std::cout << " occupation starting at " << section.attribute("occupationStart").value() << "\n";
			std::cout << " routeID " << section.attribute("routeId").value() << "\n";
		}
	}
}
RailML::RailML(char* filename) {

	RailML::filename = filename;

	// if (!doc.load_file(*filename)) return 0;
	if (!this->railml_doc.load_file(filename)) {
		return;
		std::cout << "\nRailML File not found!\n";
	}

	railml_version = this->railml_doc.child("railml").attribute("version").value();
	if (railml_version == "2.2")
		std::cout << std::endl
				  << "version is 2.2" << std::endl;
	pugi::xml_node tools = this->railml_doc.child("railml").child("infrastructure").child("tracks");

	for (pugi::xml_node_iterator it = tools.begin(); it != tools.end(); ++it) {
		std::cout << "Node:";

		for (pugi::xml_attribute_iterator ait = it->attributes_begin(); ait != it->attributes_end(); ++ait) {
			std::cout << " " << ait->name() << "=" << ait->value();
		}

		std::cout << std::endl;
	}
};
int* RailML::read_ocps() {
	pugi::xml_node tools = this->railml_doc.child("railml").child("infrastructure").child("tracks");
	for (pugi::xml_node_iterator it = tools.begin(); it != tools.end(); ++it) {
		std::cout << "track: \n";

		for (pugi::xml_attribute_iterator ait = it->attributes_begin(); ait != it->attributes_end(); ++ait) {
			// logger.Log(std::string(ait->name()) + "=" + std::string(ait->value()));
			break;
		}

		std::cout << std::endl;
	}
	return nullptr;
};

void RailML::read_attibutes_of_nodes(const char* attribute) {

	pugi::xml_node nodes = this->railml_doc.child("railml");
	for (pugi::xml_node_iterator it = nodes.begin(); it != nodes.end(); ++it) {
		std::cout << "Tool====:" << it->name();
		// char *a = (char*)"infrastructure";

		if (!strcmp(it->name(), attribute)) {
			std::cout << "Toolp999999999999999999ppp:" << it->name();

			for (pugi::xml_attribute_iterator ait = it->attributes_begin(); ait != it->attributes_end(); ++ait) {

				std::cout << " " << ait->name() << "=" << ait->value();
			}
		}

		std::cout << std::endl;
	}
};
void RailML::test() {
	std::cout << "<<<<<<<<<<<<<<<<<<<" << std::endl;
	pugi::xpath_node_set tools = this->railml_doc.select_nodes("/infrastructure/operationControlPoints/ocp[@AllowRemote='true' and @DeriveCaptionFrom='lastparam']");
	std::cout << "Tools:\n";

	for (pugi::xpath_node_set::const_iterator it = tools.begin(); it != tools.end(); ++it) {
		pugi::xpath_node Node = *it;
		std::cout << Node.node().attribute("id").value() << "\n";
	}

	pugi::xpath_node build_tool = this->railml_doc.select_node("//Tool[contains(Description, 'build system')]");

	if (build_tool)
		std::cout << "Build tool: " << build_tool.node().attribute("Filename").value() << "\n";
};

// Prepare TSM file to send
std::string trafficStateMonitoring_xml(nlohmann::json jsmsg) {
	/*
	creates the TSM file used to populate the traffic state to external modules
	*/

	std::stringstream ss;
	// this is the simulation time to use in XML, since it is not the current time equal to simulation time
	// It is equal to 3600*H where H is the start of timetable e.g. 07:00==> 3600*7=25200 seconds
	int startingSimulationTime = 23300; // 25200; 23300=>06:28:20

	// RailML a(filepath);

	// std::cout << a.filename << '\n';
	// std::vector < std::vector<std::vector<double>>> ad=a.read_tracks();
	// a.read_ocps();
	// a.read_attibutes_of_nodes((char*)"timetable");
	time_t now = time(0);
	time_t production_time = now;

	struct tm ltm, prtm, stime;
	localtime_r(&now, &ltm);

	now = now - ((ltm.tm_hour * 3600) + (ltm.tm_min * 60) + ltm.tm_sec) + startingSimulationTime;
	time_t simulation_time = now + static_cast<long>(jsmsg["time"]);
	localtime_r(&now, &ltm); // recalculate on new time
	localtime_r(&simulation_time, &stime);
	localtime_r(&production_time, &prtm); // keep this for the comment of thexml file

	pugi::xml_document doc;
	// Generate XML declaration
	auto declarationNode = doc.append_child(pugi::node_declaration);
	declarationNode.append_attribute("version") = "1.0";
	declarationNode.append_attribute("encoding") = "UTF-8";
	declarationNode.append_attribute("standalone") = "yes";

	pugi::xml_node comment = doc.append_child(pugi::node_comment);
	std::string commentsign;
	char currTime[100];
	strftime(currTime, 50, "%Y-%m-%d %H:%M:%S", &prtm);
	commentsign = "Generated by EGTRAIN on ";
	commentsign += currTime;
	comment.set_value(commentsign.c_str());

	// tag::code[]
	// add Node with some name
	auto root = doc.append_child("trafficState");
	char simTime[100]; // simulation time
	strftime(simTime, 50, "%Y-%m-%d %H:%M:%S.000 CEST", &stime);

	root.append_attribute("currentTime") = simTime; // "2013-02-21 01:02:00.000 CEST";

	// add active trains' traffic state
	for (auto it = jsmsg["trains"].begin(); it != jsmsg["trains"].end(); ++it) {
		if (it.value()["inArea"] == 1) {
			// for (auto it2 = it.value().begin(); it2 != it.value().end(); ++it2) {
			//     if (it2["inArea"] == 1) {
			pugi::xml_node trainStateInArea = root.append_child("trainStateInArea");

			pugi::xml_node trainID = trainStateInArea.append_child("trainID");
			trainID.append_attribute("trainNumber") = it.key().c_str();

			pugi::xml_node trainPosition = trainStateInArea.append_child("trainPosition");
			std::string trackID = it.value()["trackID"];
			trainPosition.append_attribute("trackID") = trackID.c_str();
			if (it.value()["direction"] == false) {
				trainPosition.append_attribute("travelDirection") = "1";
			} else
				trainPosition.append_attribute("travelDirection") = "-1";

			trainPosition.append_attribute("posOnTrack") = to_string(it.value()["km-point"]).c_str();
			std::string BlockOccupied = it.value()["BlockOccupied"];
			trainPosition.append_attribute("currentTrackVacancyDetectionSection") = BlockOccupied.c_str();
			if (it.value()["lastOccTime"] >= 0) {
				double aa = it.value()["lastOccTime"];
				struct tm ltm2;
				time_t temp = now + static_cast<long>(it.value()["lastOccTime"]);

				localtime_r(&temp, &ltm2);
				char eventTime[100]; // event time
				strftime(eventTime, 50, "%Y-%m-%d %H:%M:%S.000 CEST", &ltm2);
				trainPosition.append_attribute("lastOccupationTime") = eventTime;
			}
			pugi::xml_node speed = trainStateInArea.append_child("speed");
			double vitesse = it.value()["speed"];
			speed.append_child(pugi::node_pcdata).set_value(std::to_string(vitesse).c_str());

			//  }
		} else {
			// add non-active trains' traffic state
			pugi::xml_node trainStateOutArea = root.append_child("trainStateOutArea");

			pugi::xml_node trainID = trainStateOutArea.append_child("trainID");

			trainID.append_attribute("trainNumber") = it.key().c_str();
			double depTime = it.value()["depTime"];
			struct tm ltm2;
			time_t temp = now + depTime;
			localtime_r(&temp, &ltm2);
			char eventTime[100]; // event time
			strftime(eventTime, 50, "%Y-%m-%d %H:%M:%S.000 CEST", &ltm2);
			trainID.append_attribute("expectedEntranceTime") = eventTime;
		}
	}

	doc.save(ss, "  ");
	return ss.str();
}

// Prepare route choice file to send
std::string routeChoice_xml(nlohmann::json jsmsg) {
	/*
	creates the TSM file used to populate the traffic state to external modules
	*/

	std::stringstream ss;

	// this is the simulation time to use in XML, since it is not the current time equal to simulation time
	// It is equal to 3600*H where H is the start of timetable e.g. 07:00==> 3600*7=25200 seconds
	int startingSimulationTime = 23300; // 25200; 23300=>06:28:20

	// RailML a(filepath);

	time_t now = time(0);
	time_t production_time = now;

	struct tm ltm, prtm, stime;
	localtime_r(&now, &ltm);

	now = now - ((ltm.tm_hour * 3600) + (ltm.tm_min * 60) + ltm.tm_sec) + initial_variables.startingSimulationTime;
	time_t simulation_time = now + static_cast<long>(jsmsg["time"]);
	localtime_r(&now, &ltm); // recalculate on new time
	localtime_r(&simulation_time, &stime);
	localtime_r(&production_time, &prtm); // keep this for the comment of thexml file

	pugi::xml_document doc;
	// Generate XML declaration
	auto declarationNode = doc.append_child(pugi::node_declaration);
	declarationNode.append_attribute("version") = "1.0";
	declarationNode.append_attribute("encoding") = "UTF-8";
	declarationNode.append_attribute("standalone") = "yes";

	pugi::xml_node comment = doc.append_child(pugi::node_comment);
	std::string commentsign;
	char currTime[100];
	strftime(currTime, 50, "%Y-%m-%d %H:%M:%S", &prtm);
	commentsign = "Generated by EGTRAIN on ";
	commentsign += currTime;
	comment.set_value(commentsign.c_str());

	// tag::code[]
	// add Node with some name
	auto root = doc.append_child("routeChoiceRequest");
	char simTime[100]; // simulation time
	strftime(simTime, 50, "%Y-%m-%d %H:%M:%S.000 CEST", &stime);

	root.append_attribute("currentTime") = simTime; // "2013-02-21 01:02:00.000 CEST";

	// add active trains' traffic state
	for (auto it = jsmsg["passengers"].begin(); it != jsmsg["passengers"].end(); ++it) {
		pugi::xml_node person = root.append_child("person"); //<person>
		std::string person_id = it.key().c_str();
		std::string trip_id = "1";
		person.append_attribute("person_id") = person_id.c_str(); // person_id=
		person.append_attribute("trip_id") = trip_id.c_str();	  // trip_id=

		pugi::xml_node origin = root.append_child("origin");
		std::string origin_station = it.value()["origin"];
		origin.append_child(pugi::node_pcdata).set_value(origin_station.c_str()); //<origin>

		pugi::xml_node destination = root.append_child("destination");
		std::string destination_station = it.value()["destination"];
		destination.append_child(pugi::node_pcdata).set_value(destination_station.c_str()); //<destination>

		pugi::xml_node departure_time = root.append_child("departure_time");
		int departure_time_station = it.value()["departure_time"];
		departure_time.append_child(pugi::node_pcdata).set_value(std::to_string(departure_time_station).c_str()); //<departure_time>
	}

	doc.save(ss, "  ");
	return ss.str();
}

int send_traffic_state(nlohmann::json jsmsg, std::string TSM) {
	zmq::socket_t sock(ctx, zmq::socket_type::req);
	zmq::socket_t sock2(ctx, zmq::socket_type::req);
	// try {
	sock.connect("tcp://127.0.0.1:5555");
	sock2.connect("tcp://127.0.0.1:5556");
	// throw(0);
	// }
	// catch (int error) {
	//     std::cout << "error";
	//    return error;
	// }

	std::string a = TSM; // "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><!--Generated by EGTRAIN on 2022-12-19 16:45:18--><trafficState currentTime=\"2022-12-19 07:00:00.000 CEST\">	<trainStateInArea>		<trainID trainNumber=\"0\" />		<trainPosition trackID=\"1000\" travelDirection=\"1\" posOnTrack=\"1000km\" currentTrackVacancyDetectionSection=\"1000\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"1\" />		<trainPosition trackID=\"1001\" travelDirection=\"1\" posOnTrack=\"1001km\" currentTrackVacancyDetectionSection=\"1001\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"2\" />		<trainPosition trackID=\"1002\" travelDirection=\"1\" posOnTrack=\"1002km\" currentTrackVacancyDetectionSection=\"1002\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"3\" />		<trainPosition trackID=\"1003\" travelDirection=\"1\" posOnTrack=\"1003km\" currentTrackVacancyDetectionSection=\"1003\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"4\" />		<trainPosition trackID=\"1004\" travelDirection=\"1\" posOnTrack=\"1004km\" currentTrackVacancyDetectionSection=\"1004\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"5\" />		<trainPosition trackID=\"1005\" travelDirection=\"1\" posOnTrack=\"1005km\" currentTrackVacancyDetectionSection=\"1005\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"6\" />		<trainPosition trackID=\"1006\" travelDirection=\"1\" posOnTrack=\"1006km\" currentTrackVacancyDetectionSection=\"1006\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"7\" />		<trainPosition trackID=\"1007\" travelDirection=\"1\" posOnTrack=\"1007km\" currentTrackVacancyDetectionSection=\"1007\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"8\" />		<trainPosition trackID=\"1008\" travelDirection=\"1\" posOnTrack=\"1008km\" currentTrackVacancyDetectionSection=\"1008\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"9\" />		<trainPosition trackID=\"1009\" travelDirection=\"1\" posOnTrack=\"1009km\" currentTrackVacancyDetectionSection=\"1009\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateOutArea>		<trainID trainNumber=\"0\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"1\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"2\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"3\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea></trafficState>";

	jsmsg["xml"] = a;
	zmq::message_t z_out(jsmsg.dump()), z_out2(jsmsg.dump());
	std::cout << "\nSending:" << z_out.to_string();
	sock.send(z_out, zmq::send_flags::none);
	sock2.send(z_out2, zmq::send_flags::none);

	zmq::message_t z_in, z_in2;
	(void)sock.recv(z_in);
	(void)sock2.recv(z_in2);
	std::cout << "Received from 5555:" << z_in.to_string();
	std::cout << "Received from 5556:" << z_in2.to_string();
	read_rttp_train_view(z_in2.to_string());

	return 1;
}

int send_traffic_state5555(nlohmann::json jsmsg, std::string TSM) {
	zmq::socket_t sock(ctx, zmq::socket_type::req);

	// try {
	sock.connect("tcp://127.0.0.1:5555");

	// throw(0);
	// }
	// catch (int error) {
	//     std::cout << "error";
	//    return error;
	// }

	std::string a = TSM; // "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><!--Generated by EGTRAIN on 2022-12-19 16:45:18--><trafficState currentTime=\"2022-12-19 07:00:00.000 CEST\">	<trainStateInArea>		<trainID trainNumber=\"0\" />		<trainPosition trackID=\"1000\" travelDirection=\"1\" posOnTrack=\"1000km\" currentTrackVacancyDetectionSection=\"1000\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"1\" />		<trainPosition trackID=\"1001\" travelDirection=\"1\" posOnTrack=\"1001km\" currentTrackVacancyDetectionSection=\"1001\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"2\" />		<trainPosition trackID=\"1002\" travelDirection=\"1\" posOnTrack=\"1002km\" currentTrackVacancyDetectionSection=\"1002\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"3\" />		<trainPosition trackID=\"1003\" travelDirection=\"1\" posOnTrack=\"1003km\" currentTrackVacancyDetectionSection=\"1003\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"4\" />		<trainPosition trackID=\"1004\" travelDirection=\"1\" posOnTrack=\"1004km\" currentTrackVacancyDetectionSection=\"1004\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"5\" />		<trainPosition trackID=\"1005\" travelDirection=\"1\" posOnTrack=\"1005km\" currentTrackVacancyDetectionSection=\"1005\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"6\" />		<trainPosition trackID=\"1006\" travelDirection=\"1\" posOnTrack=\"1006km\" currentTrackVacancyDetectionSection=\"1006\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"7\" />		<trainPosition trackID=\"1007\" travelDirection=\"1\" posOnTrack=\"1007km\" currentTrackVacancyDetectionSection=\"1007\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"8\" />		<trainPosition trackID=\"1008\" travelDirection=\"1\" posOnTrack=\"1008km\" currentTrackVacancyDetectionSection=\"1008\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"9\" />		<trainPosition trackID=\"1009\" travelDirection=\"1\" posOnTrack=\"1009km\" currentTrackVacancyDetectionSection=\"1009\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateOutArea>		<trainID trainNumber=\"0\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"1\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"2\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"3\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea></trafficState>";

	jsmsg["xml"] = a;
	zmq::message_t z_out(jsmsg.dump());
	std::cout << "\nSending:" << z_out.to_string();
	sock.send(z_out, zmq::send_flags::none);

	zmq::message_t z_in;
	(void)sock.recv(z_in);

	std::cout << "Received from 5555:" << z_in.to_string();

	return 1;
}

int send_traffic_state5556(nlohmann::json jsmsg, std::string TSM) {
	zmq::socket_t sock(ctx, zmq::socket_type::req);

	// try {
	sock.connect("tcp://127.0.0.1:5556");

	// throw(0);
	// }
	// catch (int error) {
	//     std::cout << "error";
	//    return error;
	// }

	std::string a = TSM; // "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><!--Generated by EGTRAIN on 2022-12-19 16:45:18--><trafficState currentTime=\"2022-12-19 07:00:00.000 CEST\">	<trainStateInArea>		<trainID trainNumber=\"0\" />		<trainPosition trackID=\"1000\" travelDirection=\"1\" posOnTrack=\"1000km\" currentTrackVacancyDetectionSection=\"1000\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"1\" />		<trainPosition trackID=\"1001\" travelDirection=\"1\" posOnTrack=\"1001km\" currentTrackVacancyDetectionSection=\"1001\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"2\" />		<trainPosition trackID=\"1002\" travelDirection=\"1\" posOnTrack=\"1002km\" currentTrackVacancyDetectionSection=\"1002\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"3\" />		<trainPosition trackID=\"1003\" travelDirection=\"1\" posOnTrack=\"1003km\" currentTrackVacancyDetectionSection=\"1003\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"4\" />		<trainPosition trackID=\"1004\" travelDirection=\"1\" posOnTrack=\"1004km\" currentTrackVacancyDetectionSection=\"1004\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"5\" />		<trainPosition trackID=\"1005\" travelDirection=\"1\" posOnTrack=\"1005km\" currentTrackVacancyDetectionSection=\"1005\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"6\" />		<trainPosition trackID=\"1006\" travelDirection=\"1\" posOnTrack=\"1006km\" currentTrackVacancyDetectionSection=\"1006\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"7\" />		<trainPosition trackID=\"1007\" travelDirection=\"1\" posOnTrack=\"1007km\" currentTrackVacancyDetectionSection=\"1007\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"8\" />		<trainPosition trackID=\"1008\" travelDirection=\"1\" posOnTrack=\"1008km\" currentTrackVacancyDetectionSection=\"1008\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"9\" />		<trainPosition trackID=\"1009\" travelDirection=\"1\" posOnTrack=\"1009km\" currentTrackVacancyDetectionSection=\"1009\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateOutArea>		<trainID trainNumber=\"0\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"1\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"2\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"3\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea></trafficState>";

	jsmsg["xml"] = a;
	zmq::message_t z_out(jsmsg.dump());
	std::cout << "\nSending:" << z_out.to_string();
	sock.send(z_out, zmq::send_flags::none);

	zmq::message_t z_in;
	(void)sock.recv(z_in);

	std::cout << "Received from 5556:" << z_in.to_string();

	return 1;
}
int comm(nlohmann::json jsmsg) {
	zmq::socket_t sock(ctx, zmq::socket_type::req);
	// try {
	sock.connect("tcp://127.0.0.1:5555");
	// throw(0);
	// }
	// catch (int error) {
	//     std::cout << "error";
	//    return error;
	// }

	std::string a = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><!--Generated by EGTRAIN on 2022-12-19 16:45:18--><trafficState currentTime=\"2022-12-19 07:00:00.000 CEST\">	<trainStateInArea>		<trainID trainNumber=\"0\" />		<trainPosition trackID=\"1000\" travelDirection=\"1\" posOnTrack=\"1000km\" currentTrackVacancyDetectionSection=\"1000\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"1\" />		<trainPosition trackID=\"1001\" travelDirection=\"1\" posOnTrack=\"1001km\" currentTrackVacancyDetectionSection=\"1001\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"2\" />		<trainPosition trackID=\"1002\" travelDirection=\"1\" posOnTrack=\"1002km\" currentTrackVacancyDetectionSection=\"1002\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"3\" />		<trainPosition trackID=\"1003\" travelDirection=\"1\" posOnTrack=\"1003km\" currentTrackVacancyDetectionSection=\"1003\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"4\" />		<trainPosition trackID=\"1004\" travelDirection=\"1\" posOnTrack=\"1004km\" currentTrackVacancyDetectionSection=\"1004\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"5\" />		<trainPosition trackID=\"1005\" travelDirection=\"1\" posOnTrack=\"1005km\" currentTrackVacancyDetectionSection=\"1005\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"6\" />		<trainPosition trackID=\"1006\" travelDirection=\"1\" posOnTrack=\"1006km\" currentTrackVacancyDetectionSection=\"1006\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"7\" />		<trainPosition trackID=\"1007\" travelDirection=\"1\" posOnTrack=\"1007km\" currentTrackVacancyDetectionSection=\"1007\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"8\" />		<trainPosition trackID=\"1008\" travelDirection=\"1\" posOnTrack=\"1008km\" currentTrackVacancyDetectionSection=\"1008\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateInArea>		<trainID trainNumber=\"9\" />		<trainPosition trackID=\"1009\" travelDirection=\"1\" posOnTrack=\"1009km\" currentTrackVacancyDetectionSection=\"1009\" lastOccupationTime=\"2010-01-11 06:04:59.0 CET\" />		<speed>True</speed>	</trainStateInArea>	<trainStateOutArea>		<trainID trainNumber=\"0\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"1\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"2\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea>	<trainStateOutArea>		<trainID trainNumber=\"3\" expectedEntranceTime=\"2010-01-11 06:34:30.0 CET\" />	</trainStateOutArea></trafficState>";

	jsmsg["xml"] = a;
	zmq::message_t z_out(jsmsg.dump());
	std::cout << "\nSending:" << z_out.to_string();
	sock.send(z_out, zmq::send_flags::none);

	zmq::message_t z_in;
	(void)sock.recv(z_in);

	// std::cout << "Received:" << z_in.to_string();

	return 1;
}