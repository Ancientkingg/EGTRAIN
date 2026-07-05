#ifndef IOClass_hpp
#define IOClass_hpp

#include <iostream>
#include <zmq.hpp>
#include <zmq_addon.hpp>
#include <nlohmann/json.hpp>
#include <assert.h>

#include "io/third_party/pugixml.hpp"

// --- RailML: RailML XML file parser ---
class RailML {
public:
	char a[10] = "sdfsd";

	const char* nodess[8] =
		{
			"null", "document", "element", "pcdata", "cdata", "comment", "pi", "declaration"};

	std::string railml_version;
	char* filename;
	pugi::xml_document railml_doc;

	RailML(char* filename);

	int* read_ocps();
	void read_attibutes_of_nodes(const char* attribute);

	void test();
};

int comm(nlohmann::json jsmsg);
int send_traffic_state(nlohmann::json jsmsg, std::string TSM);
int send_traffic_state5555(nlohmann::json jsmsg, std::string TSM);
int send_traffic_state5556(nlohmann::json jsmsg, std::string TSM);
std::string trafficStateMonitoring_xml(nlohmann::json jsmsg);
std::string routeChoice_xml(nlohmann::json jsmsg);

// --- ZMQ_channel: ZeroMQ communication channel ---
class ZMQ_channel {
public:
	zmq::context_t ctx;

	static void* context;

	void* requester;

	ZMQ_channel() {
		context = zmq_ctx_new();
		context = new zmq::context_t;

		requester = zmq_socket(context, ZMQ_REQ);
		int rc = zmq_connect(requester, "tcp://*:5555");
	}
	~ZMQ_channel() {
	}

	void zmq_communication(std::string the_text) {
		pugi::xml_document doc;

		pugi::xml_node node = doc.append_child("html");

		pugi::xml_node head = node.append_child("head");
		pugi::xml_node script = head.append_child("script");
		script.append_attribute("src") = "https://cdn.plot.ly/plotly-2.6.3.min.js";
		script.append_child(pugi::node_pcdata).set_value("");
		pugi::xml_node body = node.append_child("body");
		pugi::xml_node div = body.append_child("div");
		div.append_attribute("id") = "myDiv";

		pugi::xml_node script1 = body.append_child("script");

		std::string stree = "var trace1 = {\
        x: [10000, 11000, 12000,14000, 15000,16000,17000,18000] ,\
        y : ['asdf', 'ddfdf', null, 'asdfasdddddf', 'sdfsdg', 'sdfsdgh', 'sdfgghhjt', 'sdfhhhtew'] ,\
        mode : 'lines+markers+text',\
        connectgaps : true,\
		line:{\n\
			color : 'rgb(255, 0, 0)',\n\
			 width : 2\n\
			},\n\
		marker:{ size:5}\
		};\n\
		\
		var trace2 = {\
		  x: [11000, 12000, 15000,16000, 17000,19000,21000,28000] ,\
		  y : ['asdf', 'ddfdf', null, 'asdfasdddddf', 'sdfsdg', 'sdfsdgh', 'sdfgghhjt', 'sdfhhhtew'] ,\
		  mode : 'lines',\
		  connectgaps : true\
		};\
		\
		var data = [trace1,trace2];\n\
		\
		var layout = {\
		  title: 'Connect the Gaps Between Data',\n\
		  showlegend : false,\
			autosize:true,\
			margin:{autoexpand:true},\n\
			yaxis:{tickmode:\"array\"},\n\
		xaxis:{\
			type:'date',\
			tickformat:\"%H:%M:%S\",\
			title:'Hours',\
    showticklabels:true\
    }\
		};\
		\
		Plotly.newPlot('myDiv', data, layout);\
		\
		";
		script1.append_child(pugi::node_pcdata).set_value(stree.c_str());

		body.set_value("sdfsdf");
		std::stringstream aaa;
		doc.save(aaa);
		std::string ss = aaa.str();

		const char* cstr = ss.c_str();

		int counter = 0;

		std::cout << counter << std::endl;
		counter = counter + 1;
		int zipcode, temperature, relhumidity;
		char s[3000];

		char a[] = "===> ";

		snprintf(s, sizeof(s), "%s %d %s", a, counter, cstr);

		zipcode = 10001;
		temperature = 6655 + counter;
		relhumidity = 556;
		char update[3500];

		snprintf(update, sizeof(update), "%05d %d %d %s", zipcode, temperature, relhumidity, s);
		std::cout << "ohoho" << update;
	}
};

#endif
