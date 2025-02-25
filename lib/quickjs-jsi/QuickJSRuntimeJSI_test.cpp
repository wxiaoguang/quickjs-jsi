#include "gtest/gtest.h"
#include "QuickJSRuntime.h"
#include "jsi/test/testlib.h"

namespace facebook::jsi {

// Required by JSI testlib.cpp and by our tests below
std::vector<RuntimeFactory> runtimeGenerators()
{
  return { RuntimeFactory([]() -> std::unique_ptr<Runtime>
  {
    return quickjs::makeQuickJSRuntime();
  }) };
}

} // namespace facebook::jsi

using namespace facebook::jsi;

// Simple JSI tests that write in addition to the testlib.cpp JSI tests
class QuickJSRuntimeJSITest : public JSITestBase
{
};

TEST_P(QuickJSRuntimeJSITest, MultipleEval)
{
    eval("x = 1");
    eval("y = 2");
    eval("z = 3");
    EXPECT_EQ(rt.global().getProperty(rt, "x").getNumber(), 1);
    EXPECT_EQ(rt.global().getProperty(rt, "y").getNumber(), 2);
    EXPECT_EQ(rt.global().getProperty(rt, "z").getNumber(), 3);
}

INSTANTIATE_TEST_SUITE_P(Runtimes, QuickJSRuntimeJSITest, ::testing::ValuesIn(runtimeGenerators()));
