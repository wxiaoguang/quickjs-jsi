#pragma once

#include <jsi/jsi.h>

struct JSContext;

namespace quickjs {
    std::unique_ptr<facebook::jsi::Runtime> __cdecl makeQuickJSRuntime(JSContext *ctx = nullptr);
}
