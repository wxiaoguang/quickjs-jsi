#include <string>
#include <unordered_set>

#include "quickjs/quickjs.h"
#include "QuickJSRuntime.h"

namespace quickjs {
    using namespace facebook;

    namespace {
        std::once_flag g_hostObjectClassOnceFlag;
        JSClassID g_hostObjectClassId{};
        JSClassExoticMethods g_hostObjectExoticMethods;
        JSClassDef g_hostObjectClassDef;

        std::once_flag g_hostFunctionClassOnceFlag;
        JSClassID g_hostFunctionClassId{};
        JSClassDef g_hostFunctionClassDef;
    } // namespace

    static constexpr size_t MaxCallArgCount = 32;

    class QuickJSRuntime : public jsi::Runtime {
    private:
        JSRuntime *jsRuntime;
        JSContext *jsContext;

        JSAtom atomToString, atomLength, atomName;

        class QuickJSPointerValue final : public jsi::Runtime::PointerValue {
        public:
            void invalidate() override {
                if (!jsContext) return;
                JS_FreeValue(jsContext, jsValue);
                jsValue = JS_UNDEFINED;
                jsContext = nullptr;
            }

            ~QuickJSPointerValue() override {
                invalidate();
            }

            static PointerValue *clonePointerValue(const PointerValue *pv) {
                auto p = (QuickJSPointerValue *)(pv);
                return takeJSValue(p->jsContext, JS_DupValue(p->jsContext, p->jsValue));
            }

            static PointerValue *takeJSValue(JSContext *ctx, JSValue val) {
                return new QuickJSPointerValue(ctx, val);
            }

        protected:
            JSValue jsValue;
            JSContext *jsContext;

            friend class QuickJSRuntime;
        private:
            QuickJSPointerValue(JSContext *ctx, JSValue val) : jsContext{ctx}, jsValue{val} {}
        };

        struct QuickJSAtomPointerValue final : public jsi::Runtime::PointerValue {
        public:
            void invalidate() override {
                if (!jsContext) return;
                JS_FreeAtom(jsContext, jsAtom);
                jsAtom = 0;
                jsContext = nullptr;
            }

            ~QuickJSAtomPointerValue() override {
                invalidate();
            }

            static PointerValue *clonePointerValue(const PointerValue *pv) {
                auto p = (QuickJSAtomPointerValue *)(pv);
                return dupJSAtom(p->jsContext, p->jsAtom);
            }

            static PointerValue *dupJSAtom(JSContext *ctx, JSAtom atom) {
                return new QuickJSAtomPointerValue(ctx, JS_DupAtom(ctx, atom));
            }

            static PointerValue *takeJSAtom(JSContext *ctx, JSAtom atom) {
                return new QuickJSAtomPointerValue(ctx, atom);
            }
        protected:
            JSAtom jsAtom;
            JSContext *jsContext;

            friend class QuickJSRuntime;
        private:
            QuickJSAtomPointerValue(JSContext *ctx, JSAtom atom) : jsContext{ctx}, jsAtom{atom} {}
        };

        static const QuickJSPointerValue *pointerValue(const jsi::Pointer& pointer) noexcept {
            return (QuickJSPointerValue *)facebook::jsi::Runtime::getPointerValue(pointer);
        }

        static const QuickJSAtomPointerValue *pointerValue(const jsi::PropNameID& pointer) noexcept {
            return (QuickJSAtomPointerValue *)facebook::jsi::Runtime::getPointerValue(pointer);
        }

        static const JSValue pointerJSValue(const jsi::Pointer &pointer) noexcept {
            return pointerValue(pointer)->jsValue;
        }

        static const JSAtom pointerAtomValue(const jsi::PropNameID &pointer) noexcept {
            return pointerValue(pointer)->jsAtom;
        }

        /*
        template<typename T>
        T createPointerValue(qjs::Value &&val) {
            return make<T>(new QuickJSPointerValue(std::move(val)));
        }

        jsi::PropNameID createPropNameID(JSAtom &&atom) {
            return make<jsi::PropNameID>(new QuickJSAtomPointerValue{Atom{jsContext, std::move(atom)}});
        }

        jsi::String throwException(qjs::Value &&val) {
            // TODO:
            return make<jsi::String>(new QuickJSPointerValue(_context.getException()));
        }
*/

        static JSValue dupJSValueFromJSI(JSContext *ctx, const jsi::Value &value) {
            auto v = pickJSValueFromJSI(ctx, value);
            return JS_VALUE_HAS_REF_COUNT(v) ? JS_DupValue(ctx, v) : v;
        }

        static JSValue pickJSValueFromJSI(JSContext *ctx, const jsi::Value &value) {
            if (value.isUndefined()) {
                return JS_UNDEFINED;
            } else if (value.isNull()) {
                return JS_NULL;
            } else if (value.isBool()) {
                return JS_NewBool(ctx, value.getBool());
            } else if (value.isNumber()) {
                return JS_NewFloat64(ctx, value.getNumber()); // FIXME: need to handle int?
            } else if (value.isSymbol()) {
                return ((QuickJSPointerValue *) getPointerValue(value))->jsValue;
            } else if (value.isString()) {
                return ((QuickJSPointerValue *) getPointerValue(value))->jsValue;
            } else if (value.isObject()) {
                return((QuickJSPointerValue *) getPointerValue(value))->jsValue;
            } else {
                std::abort(); // FXIME: error check
            }
        }

        static jsi::Value takeToJsiValue(QuickJSRuntime *runtime, JSValue jsValue) {
            if (JS_IsException(jsValue)) runtime->ThrowJSError(); // TODO: right or not?

            switch (JS_VALUE_GET_TAG(jsValue)) {
                case JS_TAG_UNDEFINED:
                case JS_TAG_UNINITIALIZED: // TODO: undefined or null?
                    return {};
                case JS_TAG_INT:
                    return {JS_VALUE_GET_INT(jsValue)};
                case JS_TAG_FLOAT64:
                    return {JS_VALUE_GET_FLOAT64(jsValue)};
                case JS_TAG_BOOL:
                    return {(bool)JS_VALUE_GET_BOOL(jsValue)};
                case JS_TAG_NULL:
                    return {nullptr};
                case JS_TAG_STRING:
                    return jsi::Value(*runtime, make<jsi::String>(QuickJSPointerValue::takeJSValue(runtime->jsContext, jsValue)));
                case JS_TAG_OBJECT:
                    return jsi::Value(*runtime, make<jsi::Object>(QuickJSPointerValue::takeJSValue(runtime->jsContext, jsValue)));
                case JS_TAG_SYMBOL:
                    return jsi::Value(*runtime, make<jsi::Symbol>(QuickJSPointerValue::takeJSValue(runtime->jsContext, jsValue)));

                // TODO: rest of types
                case JS_TAG_EXCEPTION:
                case JS_TAG_BIG_DECIMAL:
                case JS_TAG_BIG_INT:
                case JS_TAG_BIG_FLOAT:
                case JS_TAG_CATCH_OFFSET:
                default:
                    return {}; // FIXME: rest JSValue types
            }
        }


/*
        static JSAtom AsJSAtomConst(const jsi::PropNameID &propertyId) noexcept {
            return QuickJSAtomPointerValue::GetJSAtom(getPointerValue(propertyId));
        }

        template<typename T>
        static JSValue AsJSValue(const T &obj) noexcept {
            return QuickJSPointerValue::GetJSValue(getPointerValue(obj));
        }

        JSValue AsJSValue(const jsi::Value &value) noexcept {
            return fromJSIValue(value).v;
        }

        JSValueConst AsJSValueConst(const jsi::Value &value) const noexcept {
            if (value.isUndefined()) {
                return JS_UNDEFINED;
            } else if (value.isNull()) {
                return JS_NULL;
            } else if (value.isBool()) {
                return value.getBool() ? JS_TRUE : JS_FALSE;
            } else if (value.isNumber()) {
                return JS_NewFloat64(jsContext, value.getNumber());
            } else if (value.isSymbol()) {
                return QuickJSPointerValue::GetJSValue(getPointerValue(value));
            } else if (value.isString()) {
                return QuickJSPointerValue::GetJSValue(getPointerValue(value));
            } else if (value.isObject()) {
                return QuickJSPointerValue::GetJSValue(getPointerValue(value));
            } else {
                QJS_VERIFY_ELSE_CRASH_MSG(false, "Unknown JSI Value kind");
            }
        }

        JSValueConst AsJSValueConst(const jsi::Pointer &ptr) const noexcept {
            return QuickJSPointerValue::GetJSValue(getPointerValue(ptr));
        }

        template<typename T>
        static qjs::Value AsValue(const T &obj) noexcept {
            return QuickJSPointerValue::GetValue(getPointerValue(obj));
        }

        JSValue CloneJSValue(const jsi::Value &value) noexcept {
            return JS_DupValue(jsContext, AsJSValueConst(value));
        }

        std::string getExceptionDetails() {
            auto exc = _context.getException();

            std::stringstream strstream;
            strstream << (std::string) exc << std::endl;

            if (JS_HasProperty(jsContext, exc.v, Atom{jsContext, "stack"}.a)) {
                strstream << (std::string) exc["stack"] << std::endl;
            }

            return strstream.str();
        }
*/
        [[noreturn]]
        void ThrowJSError() {
            auto self = const_cast<QuickJSRuntime *>(this);
            auto exc = takeToJsiValue(this, JS_GetException(self->jsContext));
            auto obj = exc.asObject(*this);

            std::string message, stack;

            auto propMessage = createPropNameIDFromCString(jsContext, "message");
            auto propStack = createPropNameIDFromCString(jsContext, "stack");
            if (obj.hasProperty(*this, propMessage)) {
                message = obj.getProperty(*this, propMessage).asString(*this).utf8(*this);
            }
            if (obj.hasProperty(*this, propStack)) {
                message = obj.getProperty(*this, propStack).asString(*this).utf8(*this);
            }
            throw jsi::JSError(*self, std::move(message), std::move(stack));
        }

        int CheckBool(int value) {
            if (value < 0) {
                ThrowJSError();
            }

            return value;
        }

        JSValue CheckJSValue(JSValue &&value) {
            if (JS_IsException(value)) {
                ThrowJSError();
            }

            return std::move(value);
        }

        bool _dontExecutePending{false};
        struct PendingExecutionScope {
            PendingExecutionScope(QuickJSRuntime &rt)
                    : _pushedScope{std::exchange(rt._dontExecutePending, true)}, _rt(rt) {
            }

            ~PendingExecutionScope() {
                _rt._dontExecutePending = _pushedScope;
                // Do not run if there is a new exception in the scope
                if (_uncaughtExceptions == std::uncaught_exceptions()) {
                    ExecutePendingJobs();
                }
            }

        private:
            void ExecutePendingJobs() {
                if (_rt._dontExecutePending)
                    return;

                JSContext *ctx1{nullptr};
                int err{1};
                while (err > 0) {
                    err = JS_ExecutePendingJob(_rt.jsRuntime, &ctx1);
                    if (err < 0) {
                        // TODO: throw exception details from ctx1
                        _rt.ThrowJSError();
                    }
                }

                // JS_RunGC(_rt.jsRuntime);
            }

            bool _pushedScope;
            QuickJSRuntime &_rt;
            int _uncaughtExceptions{std::uncaught_exceptions()};
        };

        static QuickJSRuntime *FromContext(JSContext *ctx) {
            return static_cast<QuickJSRuntime *>(JS_GetContextOpaque(ctx));
        }


        static int SetException(JSContext *ctx, const char *message, const char *stack) {
            JSValue errorObj = JS_NewError(ctx);
            auto atomMessage = JS_NewAtom(ctx, "message");
            auto atomStack = JS_NewAtom(ctx, "stack");
            if (!message) {
                message = "Unknown error";
            }
            JS_DefinePropertyValue(ctx, errorObj, atomMessage, JS_NewString(ctx, message), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
            if (stack) {
                JS_DefinePropertyValue(ctx, errorObj, atomStack, JS_NewString(ctx, stack), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
            }
            JS_FreeAtom(ctx, atomMessage);
            JS_FreeAtom(ctx, atomStack);
            JS_Throw(ctx, errorObj);
            return -1;
        }

    public:
        QuickJSRuntime(QuickJSRuntimeArgs &&args) {
            jsRuntime = JS_NewRuntime();
            jsContext = JS_NewContext(jsRuntime);
            JS_SetContextOpaque(jsContext, this);

            atomToString = JS_NewAtom(jsContext, "toString");
            atomLength = JS_NewAtom(jsContext, "length");
            atomName = JS_NewAtom(jsContext, "name");
        }

        ~QuickJSRuntime() override {
        }

        virtual jsi::Value evaluateJavaScript(const std::shared_ptr<const jsi::Buffer> &buffer, const std::string &sourceURL) override {
            PendingExecutionScope scope(*this);
            JSValue v = JS_Eval(jsContext, (const char *)buffer->data(), buffer->size(), sourceURL.c_str(), JS_EVAL_TYPE_GLOBAL);
            return takeToJsiValue(this, v);
        }

        virtual std::shared_ptr<const jsi::PreparedJavaScript>
        prepareJavaScript(const std::shared_ptr<const jsi::Buffer> &buffer, std::string sourceURL) override {
            // TODO: NYI
            //std::abort();
            return nullptr;
        }

        virtual jsi::Value
        evaluatePreparedJavaScript(const std::shared_ptr<const jsi::PreparedJavaScript> &js) override {
            // TODO: NYI
            //std::abort();
            return jsi::Value();
        }

        virtual jsi::Object global() override {
            return make<jsi::Object>(QuickJSPointerValue::takeJSValue(jsContext, JS_GetGlobalObject(jsContext)));
        }

        virtual std::string description() override {
            return "QuickJS";
        }

        virtual bool isInspectable() override {
            return false;
        }

        virtual PointerValue *cloneSymbol(const Runtime::PointerValue *pv) override {
            return QuickJSPointerValue::clonePointerValue(pv);
        }

        virtual PointerValue *cloneString(const Runtime::PointerValue *pv) override {
            return QuickJSPointerValue::clonePointerValue(pv);
        }

        virtual PointerValue *cloneObject(const Runtime::PointerValue *pv) override {
            return QuickJSPointerValue::clonePointerValue(pv);
        }

        virtual PointerValue *clonePropNameID(const Runtime::PointerValue *pv) override {
            return QuickJSAtomPointerValue::clonePointerValue(pv);
        }


        static jsi::PropNameID dupToPropNameID(JSContext *ctx, JSAtom atom) {
            return make<jsi::PropNameID>( QuickJSAtomPointerValue::dupJSAtom(ctx, atom));
        }

        static jsi::PropNameID takeToPropNameID(JSContext *ctx, JSAtom atom) {
            return make<jsi::PropNameID>( QuickJSAtomPointerValue::takeJSAtom(ctx, atom));
        }

        static jsi::PropNameID createPropNameIDFromCString(JSContext *ctx, const char *str) {
            return takeToPropNameID(ctx, JS_NewAtomLen(ctx, str, strlen(str)));
        }

        virtual jsi::PropNameID createPropNameIDFromAscii(const char *str, size_t length) override {
            return takeToPropNameID(jsContext, JS_NewAtomLen(jsContext, str, length));
        }

        virtual jsi::PropNameID createPropNameIDFromUtf8(const uint8_t *utf8, size_t length) override {
            return takeToPropNameID(jsContext, JS_NewAtomLen(jsContext, (const char *) utf8, length));
        }

        virtual jsi::PropNameID createPropNameIDFromString(const jsi::String &str) override {
            auto pv = pointerValue(str);
            return takeToPropNameID(jsContext, JS_ValueToAtom(pv->jsContext, pv->jsValue));
        }

        virtual std::string utf8(const jsi::PropNameID &sym) override {
            auto pv = pointerValue(sym);
            const char *str = JS_AtomToCString(jsContext, pv->jsAtom);
            if (!str) {
                ThrowJSError();
            }
            std::string result{str};
            JS_FreeCString(jsContext, str);
            return result;
        }

        virtual bool compare(const jsi::PropNameID &left, const jsi::PropNameID &right) override {
            auto leftPv = pointerValue(left);
            auto rightPv = pointerValue(right);
            return leftPv->jsAtom == rightPv->jsAtom;
        }

        virtual std::string symbolToString(const jsi::Symbol &sym) override {
            auto pv = pointerValue(sym);
            auto jsvToString = JS_GetProperty(pv->jsContext, pv->jsValue, atomToString);
            auto jsvResult = JS_Call(jsContext, jsvToString, pv->jsValue, 0, nullptr);
            JS_FreeValue(pv->jsContext,jsvToString);

            auto strResult = JS_ToCString(pv->jsContext, jsvResult);
            auto string = std::string(strResult);
            JS_FreeCString(pv->jsContext, strResult);
            return string;
        }

        virtual jsi::String createStringFromAscii(const char *str, size_t length) override {
            return make<jsi::String>(QuickJSPointerValue::takeJSValue(jsContext, JS_NewStringLen(jsContext, str, length)));
        }

        virtual jsi::String createStringFromUtf8(const uint8_t *utf8, size_t length) override {
            return make<jsi::String>(QuickJSPointerValue::takeJSValue(jsContext, JS_NewStringLen(jsContext, (const char *)utf8, length)));
        }

        virtual std::string utf8(const jsi::String &str) override {
            auto pv = pointerValue(str);
            auto strResult = JS_ToCString(pv->jsContext, pv->jsValue);
            auto string = std::string(strResult);
            JS_FreeCString(pv->jsContext, strResult);
            return string;
        }

        virtual jsi::Object createObject() override {
            return make<jsi::Object>(QuickJSPointerValue::takeJSValue(jsContext, JS_NewObject(jsContext)));
        }

        struct HostObjectProxyBase {
            HostObjectProxyBase(std::shared_ptr<jsi::HostObject> &&hostObject) noexcept
                    : _hostObject{std::move(hostObject)} {
            }

            std::shared_ptr<jsi::HostObject> _hostObject;
        };

        virtual jsi::Object createObject(std::shared_ptr<jsi::HostObject> hostObject) override {
            struct HostObjectProxy : HostObjectProxyBase {
                HostObjectProxy(std::shared_ptr<jsi::HostObject> &&hostObject) noexcept
                        : HostObjectProxyBase{std::move(hostObject)} {
                }

                static JSValue GetProperty(JSContext *ctx, JSValueConst obj, JSAtom prop, JSValueConst /*receiver*/) noexcept try {
                    QuickJSRuntime *runtime = QuickJSRuntime::FromContext(ctx);
                    auto proxy = GetProxy(ctx, obj);
                    jsi::Value result = proxy->_hostObject->get(*runtime, dupToPropNameID(ctx, prop));
                    return dupJSValueFromJSI(ctx, result);
                }
                catch (const jsi::JSError &jsError) {
                    QuickJSRuntime::SetException(ctx, jsError.getMessage().c_str(), jsError.getStack().c_str());
                    return JS_EXCEPTION;
                }
                catch (const std::exception &ex) {
                    QuickJSRuntime::SetException(ctx, ex.what(), nullptr);
                    return JS_EXCEPTION;
                }
                catch (...) {
                    QuickJSRuntime::SetException(ctx, "Unexpected error", nullptr);
                    return JS_EXCEPTION;
                }

                static int GetOwnPropertyNames(JSContext *ctx, JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj) noexcept try {
                    *ptab = nullptr;
                    *plen = 0;
                    QuickJSRuntime *runtime = QuickJSRuntime::FromContext(ctx);
                    auto proxy = GetProxy(ctx, obj);
                    std::vector<jsi::PropNameID> propNames = proxy->_hostObject->getPropertyNames(*runtime);
                    if (!propNames.empty()) {
                        std::unordered_set<JSAtom> uniqueConstAtoms;
                        uniqueConstAtoms.reserve(propNames.size());
                        for (size_t i = 0; i < propNames.size(); ++i) {
                            auto pv = pointerValue(propNames[i]);
                            uniqueConstAtoms.insert(pv->jsAtom);
                        }

                        *ptab = (JSPropertyEnum *) js_malloc(ctx, uniqueConstAtoms.size() * sizeof(JSPropertyEnum));
                        *plen = uniqueConstAtoms.size();
                        size_t index = 0;
                        for (const auto atom: uniqueConstAtoms) {
                            (*ptab + index)->atom = JS_DupAtom(ctx, atom);
                            (*ptab + index)->is_enumerable = 1;
                            ++index;
                        }
                    }

                    return 0; // Must return 0 on success
                }
                catch (const jsi::JSError &jsError) {
                    return QuickJSRuntime::SetException(ctx, jsError.getMessage().c_str(), jsError.getStack().c_str());
                }
                catch (const std::exception &ex) {
                    return QuickJSRuntime::SetException(ctx, ex.what(), nullptr);
                }
                catch (...) {
                    return QuickJSRuntime::SetException(ctx, "Unexpected error", nullptr);
                }

                static int SetProperty(JSContext *ctx, JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst receiver, int flags) noexcept try {
                    QuickJSRuntime *runtime = QuickJSRuntime::FromContext(ctx);
                    auto proxy = GetProxy(ctx, obj);
                    proxy->_hostObject->set(*runtime, dupToPropNameID(ctx, JS_DupAtom(ctx, prop)), takeToJsiValue(runtime, JS_DupValue(ctx, value)));
                    return 1;
                }
                catch (const jsi::JSError &jsError) {
                    return QuickJSRuntime::SetException(ctx, jsError.getMessage().c_str(), jsError.getStack().c_str());
                }
                catch (const std::exception &ex) {
                    return QuickJSRuntime::SetException(ctx, ex.what(), nullptr);
                }
                catch (...) {
                    return QuickJSRuntime::SetException(ctx, "Unexpected error", nullptr);
                }

                static void Finalize(JSRuntime *rt, JSValue val) noexcept {
                    // Take ownership of proxy object to delete it
                    std::unique_ptr<HostObjectProxy> proxy{GetProxy(val)};
                }

                static HostObjectProxy *GetProxy(JSValue obj) {
                    return static_cast<HostObjectProxy *>(JS_GetOpaque(obj, g_hostObjectClassId));
                }

                static HostObjectProxy *GetProxy(JSContext *ctx, JSValue obj) {
                    return static_cast<HostObjectProxy *>(JS_GetOpaque2(ctx, obj, g_hostObjectClassId));
                }
            };

            // Register custom ClassDef for HostObject only once.
            // We use it to associate the HostObject with JSValue with help of opaque value
            // and to implement the HostObject proxy.
            std::call_once(g_hostObjectClassOnceFlag, [this]() {
                g_hostObjectExoticMethods = {};
                g_hostObjectExoticMethods.get_property = HostObjectProxy::GetProperty;
                g_hostObjectExoticMethods.get_own_property_names = HostObjectProxy::GetOwnPropertyNames;
                g_hostObjectExoticMethods.set_property = HostObjectProxy::SetProperty;

                g_hostObjectClassDef = {};
                g_hostObjectClassDef.class_name = "HostObject";
                g_hostObjectClassDef.finalizer = HostObjectProxy::Finalize;
                g_hostObjectClassDef.exotic = &g_hostObjectExoticMethods;

                g_hostObjectClassId = JS_NewClassID(&g_hostObjectClassId);
            });

            if (!JS_IsRegisteredClass(jsRuntime, g_hostObjectClassId)) {
                CheckBool(JS_NewClass(jsRuntime, g_hostObjectClassId, &g_hostObjectClassDef));
            }

            JSValue obj = CheckJSValue(JS_NewObjectClass(jsContext, g_hostObjectClassId));
            JS_SetOpaque(obj, new HostObjectProxy{std::move(hostObject)});
            return make<jsi::Object>(QuickJSPointerValue::takeJSValue(jsContext, obj));
        }

        virtual std::shared_ptr<jsi::HostObject> getHostObject(const jsi::Object &obj) override {
            return static_cast<HostObjectProxyBase *>(JS_GetOpaque2(jsContext, pointerJSValue(obj),g_hostObjectClassId))->_hostObject;
        }

        virtual jsi::HostFunctionType &getHostFunction(const jsi::Function &func) override {
            return static_cast<HostFunctionProxyBase *>(JS_GetOpaque2(jsContext, pointerJSValue(func),g_hostFunctionClassId))->_hostFunction;
        }

        virtual jsi::Value getProperty(const jsi::Object &obj, const jsi::PropNameID &name) override {
            return takeToJsiValue(this, JS_GetProperty(jsContext, pointerJSValue(obj), pointerAtomValue(name)));
        }

        virtual jsi::Value getProperty(const jsi::Object &obj, const jsi::String &name) override {
            auto v = pointerJSValue(name);
            auto a = JS_ValueToAtom(jsContext, v);
            auto ret = takeToJsiValue(this, JS_GetProperty(jsContext, pointerJSValue(obj), a));
            JS_FreeAtom(jsContext, a);
            return ret;
        }

        virtual bool hasProperty(const jsi::Object &obj, const jsi::PropNameID &name) override {
            return CheckBool(JS_HasProperty(jsContext, pointerJSValue(obj), pointerAtomValue(name)));
        }

        virtual bool hasProperty(const jsi::Object &obj, const jsi::String &name) override {
            auto prop = takeToPropNameID(jsContext, JS_ValueToAtom(jsContext, pointerJSValue(name)));
            return hasProperty(obj, prop);
        }

        virtual void setPropertyValue(jsi::Object &obj, const jsi::PropNameID &name, const jsi::Value &value) override {
            auto v = pointerJSValue(obj);
            auto prop = pointerAtomValue(name);
            JS_SetProperty(jsContext, v, prop, pickJSValueFromJSI(jsContext, value));
        }

        virtual void setPropertyValue(jsi::Object &obj, const jsi::String &name, const jsi::Value &value) override {
            auto prop = takeToPropNameID(jsContext, JS_ValueToAtom(jsContext, pointerJSValue(name)));
            return setPropertyValue(obj, prop, value);
        }

        virtual bool isArray(const jsi::Object &obj) const override {
            return JS_IsArray(jsContext, pointerJSValue(obj));
        }

        virtual bool isArrayBuffer(const jsi::Object &) const override {
            // return JS_(jsContext, pointerJSValue(obj));
            return false; // FIXME: array buffer support?
        }

        virtual bool isFunction(const jsi::Object &obj) const override {
            return JS_IsFunction(jsContext, pointerJSValue(obj));
        }

        virtual bool isHostObject(const jsi::Object &obj) const override {
            return JS_GetOpaque2(jsContext, pointerJSValue(obj), g_hostObjectClassId) != nullptr;
        }

        virtual bool isHostFunction(const jsi::Function &func) const override {
            return JS_GetOpaque2(jsContext, pointerJSValue(func), g_hostFunctionClassId) != nullptr;
        }

        virtual jsi::Array getPropertyNames(const jsi::Object &obj) override {
            // Handle to the Object constructor.
            auto objectConstructor = global().getProperty(*this, "Object");

            // Handle to the Object.prototype Object.
            auto objectPrototype = objectConstructor.asObject(*this).getProperty(*this, "prototype");

            // We now traverse the object's property chain and collect all enumerable
            // property names.
            std::vector<JSValue> enumerablePropNames{};
            auto currentObjectOnPrototypeChain = pointerJSValue(obj);

            // We have a small optimization here where we stop traversing the prototype
            // chain as soon as we hit Object.prototype. However, we still need to check
            // for null here, as one can create an Object with no prototype through
            // Object.create(null).
            while (JS_VALUE_GET_PTR(currentObjectOnPrototypeChain) != JS_VALUE_GET_PTR(pickJSValueFromJSI(jsContext, objectPrototype)) &&
                   !JS_IsNull(currentObjectOnPrototypeChain)) {
                JSPropertyEnum *propNamesEnum;
                uint32_t propNamesSize;
                //TODO: check error
                JS_GetOwnPropertyNames(jsContext, &propNamesEnum, &propNamesSize, currentObjectOnPrototypeChain,JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);

                for (uint32_t i = 0; i < propNamesSize; ++i) {
                    JSPropertyEnum *propName = propNamesEnum + i;
                    if (propName->is_enumerable) {
                        enumerablePropNames.emplace_back(JS_AtomToValue(jsContext, propName->atom));
                    }
                    JS_FreeAtom(jsContext, propName->atom);
                }
                js_free(jsContext, propNamesEnum);

                currentObjectOnPrototypeChain = JS_GetPrototype(jsContext, currentObjectOnPrototypeChain);
            }

            size_t enumerablePropNamesSize = enumerablePropNames.size();
            facebook::jsi::Array result = createArray(enumerablePropNamesSize);
            for (size_t i = 0; i < enumerablePropNamesSize; ++i) {
                result.setValueAtIndex(*this, i, takeToJsiValue(this, enumerablePropNames[i]));
            }

            return result;
        }

        virtual jsi::WeakObject createWeakObject(const jsi::Object &) override {
            // TODO: NYI
            std::abort();
        }

        virtual jsi::Value lockWeakObject(const jsi::WeakObject &) override {
            // TODO: NYI
            std::abort();
        }

        virtual jsi::Array createArray(size_t length) override {
            // Note that in ECMAScript Array doesn't take length as a constructor argument (although many other engines do)
            auto arr = make<jsi::Object>(QuickJSPointerValue::takeJSValue(jsContext, JS_NewArray(jsContext)));
            arr.setProperty(*this, "length", (int)length);
            return arr.getArray(*this);
        }

        virtual size_t size(const jsi::Array &arr) override {
            return (size_t)arr.getProperty(*this, "length").asNumber();
        }

        virtual size_t size(const jsi::ArrayBuffer &) override {
            // TODO: NYI
            std::abort();
        }

        virtual uint8_t *data(const jsi::ArrayBuffer &) override {
            // TODO: NYI
            std::abort();
        }

        virtual jsi::Value getValueAtIndex(const jsi::Array &arr, size_t i) override {
            return takeToJsiValue(this, JS_GetPropertyUint32(jsContext, pointerJSValue(arr), static_cast<uint32_t>(i)));
        }

        virtual void setValueAtIndexImpl(jsi::Array &arr, size_t i, const jsi::Value &value) override {
            auto jsValue = pickJSValueFromJSI(jsContext, value);
            JS_DupValue(jsContext, jsValue);
            CheckBool(JS_SetPropertyUint32(jsContext, pointerJSValue(arr), static_cast<uint32_t>(i), jsValue));
        }

        struct HostFunctionProxyBase {
            HostFunctionProxyBase(jsi::HostFunctionType &&hostFunction)
                    : _hostFunction{std::move(hostFunction)} {
            }

            jsi::HostFunctionType _hostFunction;
        };

        virtual jsi::Function createFunctionFromHostFunction(const jsi::PropNameID &name, unsigned int paramCount,
                                                             jsi::HostFunctionType func) override {
            struct HostFunctionProxy : HostFunctionProxyBase {
                HostFunctionProxy(jsi::HostFunctionType &&hostFunction)
                        : HostFunctionProxyBase{std::move(hostFunction)} {
                }

                static JSValue Call(JSContext *ctx, JSValueConst func_obj,JSValueConst this_val, int argc, JSValueConst *argv, int flags) noexcept try {
                    if (argc > MaxCallArgCount) throw jsi::JSINativeException("Argument count must not exceed MaxCallArgCount");

                    QuickJSRuntime *runtime = QuickJSRuntime::FromContext(ctx);
                    auto proxy = GetProxy(ctx, func_obj);

                    //TODO: avoid extra memory allocation here when creating jsi::Value
                    jsi::Value thisArg = takeToJsiValue(runtime, JS_DupValue(ctx, this_val));
                    std::array<jsi::Value, MaxCallArgCount> args;
                    for (int i = 0; i < argc; ++i) {
                        args[i] = takeToJsiValue(runtime, JS_DupValue(ctx, argv[i]));
                    }

                    jsi::Value result = proxy->_hostFunction(*runtime, thisArg, args.data(), argc);
                    return dupJSValueFromJSI(ctx, result);
                }
                catch (const jsi::JSError &jsError) {
                    QuickJSRuntime::SetException(ctx, jsError.getMessage().c_str(), jsError.getStack().c_str());
                    return JS_EXCEPTION;
                }
                catch (const std::exception &ex) {
                    QuickJSRuntime::SetException(ctx, (std::string("Exception in HostFunction: ") + ex.what()).c_str(), nullptr);
                    return JS_EXCEPTION;
                }
                catch (...) {
                    QuickJSRuntime::SetException(ctx, "Exception in HostFunction: <unknown>", nullptr);
                    return JS_EXCEPTION;
                }

                static void Finalize(JSRuntime *rt, JSValue val) noexcept {
                    // Take ownership of proxy object to delete it
                    std::unique_ptr<HostFunctionProxy> proxy{GetProxy(val)};
                }

                static HostFunctionProxy *GetProxy(JSValue obj) {
                    return static_cast<HostFunctionProxy *>(JS_GetOpaque(obj, g_hostFunctionClassId));
                }

                static HostFunctionProxy *GetProxy(JSContext *ctx, JSValue obj) {
                    return static_cast<HostFunctionProxy *>(JS_GetOpaque2(ctx, obj, g_hostFunctionClassId));
                }
            };

            // Register custom ClassDef for HostFunction only once.
            // We use it to associate the HostFunction with JSValue with help of opaque value
            // and to implement the HostFunction proxy.
            std::call_once(g_hostFunctionClassOnceFlag, [this]() {
                g_hostFunctionClassDef = {};
                g_hostFunctionClassDef.class_name = "HostFunction";
                g_hostFunctionClassDef.call = HostFunctionProxy::Call;
                g_hostFunctionClassDef.finalizer = HostFunctionProxy::Finalize;

                g_hostFunctionClassId = JS_NewClassID(&g_hostFunctionClassId);
            });

            if (!JS_IsRegisteredClass(jsRuntime, g_hostFunctionClassId)) {
                CheckBool(JS_NewClass(jsRuntime, g_hostFunctionClassId, &g_hostFunctionClassDef));
            }

            auto funcCtor = global().getProperty(*this, "Function");
            auto funcCtorVal = pickJSValueFromJSI(jsContext, funcCtor);
            auto funcObj = CheckJSValue(JS_NewObjectProtoClass(jsContext, JS_GetPrototype(jsContext,funcCtorVal) /* FIXME: free? */, g_hostFunctionClassId));

            JS_SetOpaque(funcObj, new HostFunctionProxy{std::move(func)});

            JS_DefineProperty(jsContext, funcObj, atomLength,JS_NewUint32(jsContext, paramCount),JS_UNDEFINED, JS_UNDEFINED, JS_PROP_HAS_VALUE | JS_PROP_HAS_CONFIGURABLE);

            JSAtom funcNameAtom = pointerAtomValue(name);
            auto funcNameValue = JS_AtomToValue(jsContext, funcNameAtom);
            JS_DefineProperty(jsContext, funcObj, atomName, funcNameValue, JS_UNDEFINED, JS_UNDEFINED, JS_PROP_HAS_VALUE);
            JS_FreeValue(jsContext, funcNameValue);
            return make<jsi::Object>(QuickJSPointerValue::takeJSValue(jsContext, funcObj)).getFunction(*this);
        }

        virtual jsi::Value call(const jsi::Function &func, const jsi::Value &jsThis, const jsi::Value *args, size_t count) override {
            if (count > MaxCallArgCount) throw jsi::JSINativeException("Argument count must not exceed the supported max arg count.");

            std::array<JSValue, MaxCallArgCount> jsArgsConst;

            for (size_t i = 0; i < count; ++i) {
                // Increment the args ref count in case if the call causes the GC run.
                jsArgsConst[i] = pickJSValueFromJSI(jsContext, *(args + i));
            }

            auto funcValConst = pointerJSValue(func);
            auto thisValConst = pickJSValueFromJSI(jsContext, jsThis);

            PendingExecutionScope scope(*this);
            JSValue jsResult = JS_Call(jsContext, funcValConst, thisValConst, static_cast<int>(count), jsArgsConst.data());
            return takeToJsiValue(this, jsResult);
        }

        virtual jsi::Value
        callAsConstructor(const jsi::Function &func, const jsi::Value *args, size_t count) override {
            if (count > MaxCallArgCount) throw jsi::JSINativeException("Argument count must not exceed the supported max arg count.");
            std::array<JSValue, MaxCallArgCount> jsArgsConst;
            for (size_t i = 0; i < count; ++i) {
                jsArgsConst[i] = pickJSValueFromJSI(jsContext, *(args + i));
            }

            auto funcValConst = pointerJSValue(func);

            PendingExecutionScope scope(*this);
            JSValue jsResult = JS_CallConstructor(jsContext, funcValConst, static_cast<int>(count), jsArgsConst.data());
            return takeToJsiValue(this, jsResult);
        }

        virtual bool strictEquals(const jsi::Symbol &a, const jsi::Symbol &b) const override {
            return JS_VALUE_GET_OBJ(pointerJSValue(a)) == JS_VALUE_GET_OBJ(pointerJSValue(b));
        }

        virtual bool strictEquals(const jsi::String &a, const jsi::String &b) const override {
            auto pv1 = pointerValue(a);
            auto s1 = JS_ToCString(pv1->jsContext, pv1->jsValue);
            auto pv2 = pointerValue(b);
            auto s2 = JS_ToCString(pv2->jsContext, pv2->jsValue);
            auto ret = strcmp(s1, s2) == 0;
            JS_FreeCString(pv1->jsContext, s1);
            JS_FreeCString(pv2->jsContext, s2);
            return ret;
        }

        virtual bool strictEquals(const jsi::Object &a, const jsi::Object &b) const override {
            return JS_VALUE_GET_OBJ(pointerJSValue(a)) == JS_VALUE_GET_OBJ(pointerJSValue(b));
        }

        virtual bool instanceOf(const jsi::Object &o, const jsi::Function &f) override {
            return CheckBool(JS_IsInstanceOf(jsContext, pointerJSValue(o), pointerJSValue(f)));
        }
    };

    std::unique_ptr<jsi::Runtime> __cdecl makeQuickJSRuntime(QuickJSRuntimeArgs &&args) {
        return std::make_unique<QuickJSRuntime>(std::move(args));
    }
}
