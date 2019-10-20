#include <catch/catch.hpp>

#include "utils.h"
#include <ibm/IBMEndpoint.h>

#include <faasm/files.h>
#include <scheduler/GlobalMessageBus.h>
#include <rapidjson/pointer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <boost/filesystem.hpp>

using namespace Pistache;
using namespace rapidjson;


namespace tests {
    TEST_CASE("Test ibm initialisation", "[ibm]") {
        cleanSystem();

        // Set up config to something that we know will change
        util::SystemConfig &conf = util::getSystemConfig();
        std::string originalStorage = conf.functionStorage;
        std::string originalCGroup = conf.cgroupMode;
        std::string originalNetns = conf.netNsMode;

        conf.functionStorage = "foobar";
        conf.cgroupMode = "blah";
        conf.netNsMode = "blahblah";

        std::string requestStr;
        ibm::IBMEndpoint endpoint;
        endpoint.testMode = true;
        endpoint.doHandleInit(requestStr);

        REQUIRE(conf.functionStorage == "ibm");
        REQUIRE(conf.cgroupMode == "off");
        REQUIRE(conf.netNsMode == "off");

        conf.functionStorage = originalStorage;
        conf.cgroupMode = originalCGroup;
        conf.netNsMode = originalNetns;
    }

    Document createJsonRequest(const std::string &user, const std::string &function, const std::string &mode) {
        Document d;
        d.SetObject();

        rapidjson::Value valueObject(rapidjson::kObjectType);
        d.AddMember("value", valueObject, d.GetAllocator());

        d["value"].AddMember("user", StringRef(user.c_str()), d.GetAllocator());
        d["value"].AddMember("function", StringRef(function.c_str()), d.GetAllocator());
        d["value"].AddMember("mode", StringRef(mode.c_str()), d.GetAllocator());

        return d;
    }

    void checkIbmResponse(Document &d, const std::string &expectedText, int expectedStatusCode) {
        // Make sure it's async
        d["value"].AddMember("async", true, d.GetAllocator());

        StringBuffer sb;
        Writer<StringBuffer> writer(sb);
        d.Accept(writer);
        std::string jsonStr = sb.GetString();

        ibm::IBMEndpoint endpoint;
        endpoint.testMode = true;
        const std::string actual = endpoint.doHandleCall(jsonStr);

        Document d2;
        d2.Parse(actual.c_str());
        std::string resultMsg = d2.FindMember("result")->value.GetString();
        int actualStatusCode = d2.FindMember("status_code")->value.GetInt();

        REQUIRE(actualStatusCode == expectedStatusCode);

        if (expectedText == "ASYNCID") {
            // In this case we're expecting an async ID message ID so hard to check
        } else {
            REQUIRE(resultMsg == expectedText);
        }
    }

    TEST_CASE("Test invoking", "[ibm]") {
        cleanSystem();

        Document d = createJsonRequest("demo", "echo", "invoke");
        std::string expected = "ASYNCID";

        SECTION("No input") {

        }

        SECTION("With input") {
            d["value"].AddMember("input", StringRef("foobarbaz"), d.GetAllocator());
        }

        message::Message msg = util::messageFactory("demo", "echo");
        checkIbmResponse(d, expected, 200);

        scheduler::Scheduler &sch = scheduler::getScheduler();
        REQUIRE(sch.getFunctionInFlightCount(msg) == 1);
        REQUIRE(sch.getBindQueue()->size() == 1);
    }

    TEST_CASE("Test invalid invocations", "[ibm]") {
        cleanSystem();

        Document d;
        std::string expected;
        message::Message msg;

        SECTION("No user") {
            msg = util::messageFactory("", "echo");
            d = createJsonRequest("", "echo", "invoke");
            expected = "User and function must be present in request";
        }

        SECTION("No function") {
            msg = util::messageFactory("demo", "");
            d = createJsonRequest("demo", "", "invoke");
            expected = "User and function must be present in request";
        }

        SECTION("Invalid mode") {
            msg = util::messageFactory("demo", "echo");
            d = createJsonRequest("demo", "echo", "afafaf");
            expected = "Invalid call mode";
        }

        checkIbmResponse(d, expected, 200);

        scheduler::Scheduler &sch = scheduler::getScheduler();
        REQUIRE(sch.getFunctionInFlightCount(msg) == 0);
        REQUIRE(sch.getBindQueue()->size() == 0);
    }

    TEST_CASE("Test codegen", "[ibm]") {
        std::string expected = "Codegen successful";

        // Make sure obj file doesn't exist
        const message::Message msg = util::messageFactory("demo", "ibm_test");
        const std::string objFilePath = util::getFunctionObjectFile(msg);
        boost::filesystem::remove(objFilePath);

        // Run the codegen
        Document d = createJsonRequest("demo", "ibm_test", "codegen");
        checkIbmResponse(d, expected, 200);

        // Check file is now there
        REQUIRE(boost::filesystem::exists(objFilePath));

        // Check there's something in it
        REQUIRE(faasm::getFileLength(objFilePath.c_str()) > 0);
    }
}