#pragma once

#include <jsi/jsi.h>

namespace quickjs {
    std::unique_ptr<facebook::jsi::Runtime> __cdecl makeQuickJSRuntime();
}
