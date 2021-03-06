#include <aws/aws.h>
#include <aws/lambda-runtime/runtime.h>

#include <util/logging.h>
#include <util/config.h>
#include <util/json.h>
#include <util/func.h>
#include <scheduler/GlobalMessageBus.h>
#include <scheduler/Scheduler.h>

#include <lambda/backend.h>
#include <aws/LambdaWrapper.h>


using namespace aws::lambda_runtime;


int main() {
    util::initLogging();

    awswrapper::initSDK();
    const std::shared_ptr<spdlog::logger> &logger = util::getLogger();

    util::SystemConfig &config = util::getSystemConfig();
    config.print();

    scheduler::GlobalMessageBus &globalBus = scheduler::getGlobalMessageBus();

    auto handler_fn = [&logger, &globalBus, &config](aws::lambda_runtime::invocation_request const &req) {
        // Get the function
        message::Message msg = util::jsonToMessage(req.payload);

        const std::string funcStr = util::funcToString(msg, true);
        logger->info("Queueing request to {}", funcStr);
        util::setMessageId(msg);

        // Dispatch function
        globalBus.enqueueMessage(msg);

        // Handle sync/ async Faasm requests accordingly
        std::string resultData = "Function dispatched";
        if (msg.isasync()) {
            logger->info("Async request {}", funcStr);
            resultData = util::buildAsyncResponse(msg);
        } else {
            logger->info("Sync request {}", funcStr);
            message::Message result = globalBus.getFunctionResult(msg.id(), config.globalMessageTimeout);

            logger->info("Finished request {}", funcStr);

            resultData = result.outputdata();
        }

        return invocation_response::success(
                resultData,
                "text/plain"
        );
    };

    logger->info("Listening for function dispatch calls");
    run_handler(handler_fn);

    // Tidy up
    logger->info("Shutting down");
    awswrapper::cleanUpSDK();

    return 0;
}

