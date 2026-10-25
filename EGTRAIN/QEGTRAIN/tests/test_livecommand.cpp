// Tests of the checks that the command socket makes on a request (io/LiveCommand.h) and of the event
// frame that reports a command (io/LiveFrame.h). No socket, thread or run.
//
//   test_livecommand

#include "io/LiveCommand.h"
#include "io/LiveFrame.h"

#include <QByteArray>
#include <QObject>
#include <QTest>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

namespace {
using nlohmann::json;

const char* const kToken = "0123456789abcdef0123456789abcdef";

// A request that passes every check, with the members of `changes` replacing or adding to it. A null
// member in `changes` removes the member.
std::string request(const json& changes = json::object()) {
	json base = {{"v", 1}, {"id", "r1"}, {"token", kToken}, {"cmd", "pause"}};
	for (auto member = changes.begin(); member != changes.end(); ++member) {
		if (member.value().is_null())
			base.erase(member.key());
		else
			base[member.key()] = member.value();
	}
	return base.dump();
}

std::string reasonOf(const std::string& text) {
	const LiveRequest result = parseLiveRequest(text, kToken);
	return result.reason ? result.reason : "";
}

// A row of a table of requests and the reason that each is rejected with, empty when it is accepted.
void addReason(const char* what, const std::string& text, const char* reason) {
	QTest::newRow(what) << QByteArray(text.data(), static_cast<qsizetype>(text.size())) << QByteArray(reason);
}

void addReasonColumns() {
	QTest::addColumn<QByteArray>("text");
	QTest::addColumn<QByteArray>("reason");
}
} // namespace

class TestLiveCommand : public QObject {
	Q_OBJECT

private slots:
	void acceptsCommands_data();
	void acceptsCommands();
	void acceptsDescribe();
	void acceptsDelays_data();
	void acceptsDelays();
	void namesDelayCommand();
	void ignoresUnknownMembers();
	void rejections_data();
	void rejections();
	void orderOfChecks_data();
	void orderOfChecks();
	void acceptsRequestAtTheLimit();
	void idInTheReply();
	void commandEventFrames();
};

void TestLiveCommand::acceptsCommands_data() {
	QTest::addColumn<QByteArray>("name");
	QTest::addColumn<LiveCommandKind>("kind");
	QTest::newRow("pause") << QByteArray("pause") << LiveCommandKind::Pause;
	QTest::newRow("resume") << QByteArray("resume") << LiveCommandKind::Resume;
	QTest::newRow("stop") << QByteArray("stop") << LiveCommandKind::Stop;
}

void TestLiveCommand::acceptsCommands() {
	QFETCH(QByteArray, name);
	QFETCH(LiveCommandKind, kind);
	const LiveRequest result = parseLiveRequest(request({{"cmd", name.toStdString()}}), kToken);
	// The command is accepted.
	QVERIFY(!result.reason);
	QVERIFY(!result.describe);
	QCOMPARE(result.command.kind, kind);
	QCOMPARE(result.command.id, std::string("r1"));
	QVERIFY(result.id == std::string("r1"));
	// The name of the command.
	QCOMPARE(std::string(liveCommandName(kind)), name.toStdString());
}

void TestLiveCommand::acceptsDescribe() {
	const LiveRequest describe = parseLiveRequest(request({{"cmd", "describe"}}), kToken);
	// Describe is accepted and is no command.
	QVERIFY(!describe.reason);
	QVERIFY(describe.describe);
	QVERIFY(describe.id == std::string("r1"));
}

void TestLiveCommand::acceptsDelays_data() {
	QTest::addColumn<int>("delay");
	for (const int delay : {0, 1, 250, kLiveMaxDelayMs})
		QTest::addRow("set_delay_ms %d", delay) << delay;
}

void TestLiveCommand::acceptsDelays() {
	QFETCH(int, delay);
	const LiveRequest result = parseLiveRequest(request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", delay}}}}), kToken);
	// set_delay_ms with the delay.
	QVERIFY(!result.reason);
	QCOMPARE(result.command.kind, LiveCommandKind::SetDelayMs);
	QCOMPARE(result.command.delayMs, delay);
	QCOMPARE(result.command.id, std::string("r1"));
}

void TestLiveCommand::namesDelayCommand() {
	// The name of set_delay_ms.
	QCOMPARE(std::string(liveCommandName(LiveCommandKind::SetDelayMs)), std::string("set_delay_ms"));
}

void TestLiveCommand::ignoresUnknownMembers() {
	const LiveRequest extra = parseLiveRequest(request({{"cmd", "stop"}, {"args", {{"delay_ms", 5}}}, {"note", "x"}}), kToken);
	// Members the contract does not know are ignored.
	QVERIFY(!extra.reason);
	QCOMPARE(extra.command.kind, LiveCommandKind::Stop);
	QCOMPARE(extra.command.delayMs, 0);
}

void TestLiveCommand::rejections_data() {
	addReasonColumns();
	addReason("an empty request", "", "bad_json");
	addReason("text that is no JSON", "pause", "bad_json");
	addReason("JSON that ends early", "{\"v\":1", "bad_json");
	addReason("JSON that is no object", "[1,2]", "bad_json");
	addReason("a number", "3", "bad_json");
	addReason("bytes after the object", std::string("{\"v\":1,\"id\":\"r\",\"token\":\"") + kToken + "\",\"cmd\":\"pause\"}\xff", "bad_json");

	addReason("no version", request({{"v", nullptr}}), "bad_version");
	addReason("another version", request({{"v", 2}}), "bad_version");
	addReason("version 0", request({{"v", 0}}), "bad_version");
	addReason("a version that is text", request({{"v", "1"}}), "bad_version");
	addReason("a version that is no integer", request({{"v", 1.5}}), "bad_version");

	addReason("no id", request({{"id", nullptr}}), "bad_argument");
	addReason("an id that is a number", request({{"id", 7}}), "bad_argument");
	addReason("an id of 65 bytes", request({{"id", std::string(kLiveMaxRequestIdBytes + 1, 'x')}}), "bad_argument");
	addReason("an id of 64 bytes", request({{"id", std::string(kLiveMaxRequestIdBytes, 'x')}}), "");
	addReason("an empty id", request({{"id", ""}}), "");

	addReason("no token", request({{"token", nullptr}}), "bad_token");
	addReason("a token that is a number", request({{"token", 5}}), "bad_token");
	addReason("an empty token", request({{"token", ""}}), "bad_token");
	addReason("a token that is too long", request({{"token", std::string(kToken) + "0"}}), "bad_token");
	addReason("a token that is too short", request({{"token", std::string(kToken).substr(1)}}), "bad_token");
	addReason("a token that differs in the first digit", request({{"token", "1123456789abcdef0123456789abcdef"}}), "bad_token");
	addReason("a token that differs in the last digit", request({{"token", "0123456789abcdef0123456789abcdee"}}), "bad_token");

	addReason("no command", request({{"cmd", nullptr}}), "unknown_command");
	addReason("a command that is a number", request({{"cmd", 3}}), "unknown_command");
	addReason("an unknown command", request({{"cmd", "launch"}}), "unknown_command");
	addReason("a command in capitals", request({{"cmd", "Pause"}}), "unknown_command");
	addReason("a command that would change the railway", request({{"cmd", "set_speed"}}), "unknown_command");

	const json delayCommand = {{"cmd", "set_delay_ms"}};
	addReason("set_delay_ms without args", request(delayCommand), "bad_argument");
	addReason("args that is no object", request({{"cmd", "set_delay_ms"}, {"args", 5}}), "bad_argument");
	addReason("args without delay_ms", request({{"cmd", "set_delay_ms"}, {"args", json::object()}}), "bad_argument");
	addReason("a negative delay", request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", -1}}}}), "bad_argument");
	addReason("a delay above the limit", request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", kLiveMaxDelayMs + 1}}}}), "bad_argument");
	addReason("a delay that is no integer", request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", 1.5}}}}), "bad_argument");
	addReason("a delay that is text", request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", "10"}}}}), "bad_argument");
	addReason("a delay that is null", request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", nullptr}}}}), "bad_argument");
}

void TestLiveCommand::rejections() {
	QFETCH(QByteArray, text);
	QFETCH(QByteArray, reason);
	QCOMPARE(reasonOf(text.toStdString()), reason.toStdString());
}

// The first check that fails gives the reason, so a reply never says more than the first fault.
void TestLiveCommand::orderOfChecks_data() {
	addReasonColumns();
	addReason("version before id, token and command", request({{"v", 2}, {"id", 7}, {"token", "x"}, {"cmd", "launch"}}), "bad_version");
	addReason("id before token and command", request({{"id", 7}, {"token", "x"}, {"cmd", "launch"}}), "bad_argument");
	addReason("token before command", request({{"token", "x"}, {"cmd", "launch"}}), "bad_token");
	addReason("token before the arguments", request({{"token", "x"}, {"cmd", "set_delay_ms"}}), "bad_token");
	addReason("command before the arguments", request({{"cmd", "launch"}, {"args", 5}}), "unknown_command");

	// A request that is too large is refused before it is parsed: it does not have to be JSON.
	addReason("text over the limit", std::string(kLiveMaxRequestBytes + 1, 'x'), "too_large");
	addReason("a valid request over the limit", request({{"padding", std::string(kLiveMaxRequestBytes, 'x')}}), "too_large");
}

void TestLiveCommand::orderOfChecks() {
	QFETCH(QByteArray, text);
	QFETCH(QByteArray, reason);
	QCOMPARE(reasonOf(text.toStdString()), reason.toStdString());
}

void TestLiveCommand::acceptsRequestAtTheLimit() {
	std::string exact = request();
	const std::size_t padding = kLiveMaxRequestBytes - exact.size() - std::string(",\"padding\":\"\"").size();
	exact = request({{"padding", std::string(padding, 'x')}});
	// The request at the limit has the size of the limit.
	QCOMPARE(exact.size(), kLiveMaxRequestBytes);
	// A request of exactly the limit.
	QCOMPARE(reasonOf(exact), std::string());
}

// The reply repeats the id of a request once it is valid, whatever fails later.
void TestLiveCommand::idInTheReply() {
	QVERIFY2(!parseLiveRequest(request({{"v", 2}}), kToken).id, "no id comes back from a request with another version");
	QVERIFY2(!parseLiveRequest(request({{"id", 7}}), kToken).id, "no id comes back from an invalid id");
	QVERIFY2(!parseLiveRequest("not json", kToken).id, "no id comes back from text that is no JSON");
	QVERIFY2(parseLiveRequest(request({{"token", "x"}}), kToken).id == std::string("r1"), "the id comes back with a wrong token");
	QVERIFY2(parseLiveRequest(request({{"cmd", "launch"}}), kToken).id == std::string("r1"), "the id comes back with an unknown command");
}

void TestLiveCommand::commandEventFrames() {
	LiveRunInfo run;
	run.runId = "run-1";
	LiveCommand command;
	command.kind = LiveCommandKind::SetDelayMs;
	command.id = "client-7";
	command.delayMs = 40;

	const json applied = makeCommandEventFrame(run, 12, command, 300);
	QVERIFY2(applied == json({{"v", kLiveContractVersion}, {"run_id", "run-1"}, {"seq", 12}, {"kind", "applied"}, {"step", 300}, {"command_id", "client-7"}, {"cmd", "set_delay_ms"}}),
		("applied frame: " + applied.dump()).c_str());

	command.kind = LiveCommandKind::Stop;
	const json rejected = makeCommandEventFrame(run, 13, command, 1500, "run_ended");
	QVERIFY2(rejected == json({{"v", kLiveContractVersion}, {"run_id", "run-1"}, {"seq", 13}, {"kind", "rejected"}, {"step", 1500}, {"command_id", "client-7"}, {"cmd", "stop"}, {"reason", "run_ended"}}),
		("rejected frame: " + rejected.dump()).c_str());
}

QTEST_APPLESS_MAIN(TestLiveCommand)
#include "test_livecommand.moc"
