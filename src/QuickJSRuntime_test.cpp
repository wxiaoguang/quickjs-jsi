#include "QuickJSRuntime.h"
#include "gtest/gtest.h"

TEST(QuickJSRuntimeTest, SimpleTest)
{
    auto runtime = quickjs::makeQuickJSRuntime();

    runtime->evaluateJavaScript(std::make_unique<facebook::jsi::StringBuffer>(
        "let x = 2;" "\n"
        "var result = `result is ${x + x}`;" "\n"
        ), "<test_code>");

    auto result = runtime->global().getProperty(*runtime, "result");

    EXPECT_EQ(result.getString(*runtime).utf8(*runtime), "result is 4");
}
