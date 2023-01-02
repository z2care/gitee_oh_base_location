/*
 * Copyright (C) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "location_napi_event.h"

#include "callback_manager.h"
#include "common_utils.h"
#include "location_log.h"
#include "location_napi_errcode.h"
#include "country_code_callback_host.h"
#include "locator.h"
#include "napi_util.h"

namespace OHOS {
namespace Location {
CallbackManager<LocationSwitchCallbackHost> g_switchCallbacks;
CallbackManager<LocatorCallbackHost> g_locationCallbacks;
CallbackManager<GnssStatusCallbackHost> g_gnssStatusInfoCallbacks;
CallbackManager<NmeaMessageCallbackHost> g_nmeaCallbacks;
CallbackManager<CachedLocationsCallbackHost> g_cachedLocationCallbacks;
CallbackManager<CountryCodeCallbackHost> g_countryCodeCallbacks;
std::vector<GeoFenceState*> mFences;
auto g_locatorProxy = Locator::GetInstance();

std::map<std::string, bool(*)(const napi_env &)> g_offAllFuncMap;
std::map<std::string, bool(*)(const napi_env &, const napi_value &)> g_offFuncMap;
std::map<std::string, bool(*)(const napi_env &, const size_t, const napi_value *)> g_onFuncMap;

void InitOnFuncMap()
{
    if (g_onFuncMap.size() != 0) {
        return;
    }
#ifdef ENABLE_NAPI_MANAGER
    g_onFuncMap.insert(std::make_pair("locationEnabledChange", &OnLocationServiceStateCallback));
    g_onFuncMap.insert(std::make_pair("cachedGnssLocationsChange", &OnCachedGnssLocationsReportingCallback));
    g_onFuncMap.insert(std::make_pair("satelliteStatusChange", &OnGnssStatusChangeCallback));
    g_onFuncMap.insert(std::make_pair("gnssFenceStatusChange", &OnFenceStatusChangeCallback));
    g_onFuncMap.insert(std::make_pair("nmeaMessage", &OnNmeaMessageChangeCallback));
#else
    g_onFuncMap.insert(std::make_pair("locationServiceState", &OnLocationServiceStateCallback));
    g_onFuncMap.insert(std::make_pair("cachedGnssLocationsReporting", &OnCachedGnssLocationsReportingCallback));
    g_onFuncMap.insert(std::make_pair("gnssStatusChange", &OnGnssStatusChangeCallback));
    g_onFuncMap.insert(std::make_pair("fenceStatusChange", &OnFenceStatusChangeCallback));
    g_onFuncMap.insert(std::make_pair("nmeaMessageChange", &OnNmeaMessageChangeCallback));
#endif
    g_onFuncMap.insert(std::make_pair("locationChange", &OnLocationChangeCallback));
    g_onFuncMap.insert(std::make_pair("countryCodeChange", &OnCountryCodeChangeCallback));
}

void InitOffFuncMap()
{
    if (g_offAllFuncMap.size() != 0 || g_offFuncMap.size() != 0) {
        return;
    }
#ifdef ENABLE_NAPI_MANAGER
    g_offAllFuncMap.insert(std::make_pair("locationEnabledChange", &OffAllLocationServiceStateCallback));
    g_offAllFuncMap.insert(std::make_pair("cachedGnssLocationsChange", &OffAllCachedGnssLocationsReportingCallback));
    g_offAllFuncMap.insert(std::make_pair("satelliteStatusChange", &OffAllGnssStatusChangeCallback));
    g_offAllFuncMap.insert(std::make_pair("nmeaMessage", &OffAllNmeaMessageChangeCallback));
#else
    g_offAllFuncMap.insert(std::make_pair("locationServiceState", &OffAllLocationServiceStateCallback));
    g_offAllFuncMap.insert(std::make_pair("cachedGnssLocationsReporting", &OffAllCachedGnssLocationsReportingCallback));
    g_offAllFuncMap.insert(std::make_pair("gnssStatusChange", &OffAllGnssStatusChangeCallback));
    g_offAllFuncMap.insert(std::make_pair("nmeaMessageChange", &OffAllNmeaMessageChangeCallback));
#endif
    g_offAllFuncMap.insert(std::make_pair("locationChange", &OffAllLocationChangeCallback));
    g_offAllFuncMap.insert(std::make_pair("countryCodeChange", &OffAllCountryCodeChangeCallback));

#ifdef ENABLE_NAPI_MANAGER
    g_offFuncMap.insert(std::make_pair("locationEnabledChange", &OffLocationServiceStateCallback));
    g_offFuncMap.insert(std::make_pair("cachedGnssLocationsChange", &OffCachedGnssLocationsReportingCallback));
    g_offFuncMap.insert(std::make_pair("satelliteStatusChange", &OffGnssStatusChangeCallback));
    g_offFuncMap.insert(std::make_pair("nmeaMessage", &OffNmeaMessageChangeCallback));
#else
    g_offFuncMap.insert(std::make_pair("locationServiceState", &OffLocationServiceStateCallback));
    g_offFuncMap.insert(std::make_pair("cachedGnssLocationsReporting", &OffCachedGnssLocationsReportingCallback));
    g_offFuncMap.insert(std::make_pair("gnssStatusChange", &OffGnssStatusChangeCallback));
    g_offFuncMap.insert(std::make_pair("nmeaMessageChange", &OffNmeaMessageChangeCallback));
#endif
    g_offFuncMap.insert(std::make_pair("locationChange", &OffLocationChangeCallback));
    g_offFuncMap.insert(std::make_pair("countryCodeChange", &OffCountryCodeChangeCallback));
}

LocationErrCode SubscribeLocationServiceState(const napi_env& env,
    const napi_ref& handlerRef, sptr<LocationSwitchCallbackHost>& switchCallbackHost)
{
    switchCallbackHost->SetEnv(env);
    switchCallbackHost->SetHandleCb(handlerRef);
    return g_locatorProxy->RegisterSwitchCallback(switchCallbackHost->AsObject(), DEFAULT_UID);
}

LocationErrCode SubscribeGnssStatus(const napi_env& env, const napi_ref& handlerRef,
    sptr<GnssStatusCallbackHost>& gnssStatusCallbackHost)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
        return errorCode;
    }
    gnssStatusCallbackHost->SetEnv(env);
    gnssStatusCallbackHost->SetHandleCb(handlerRef);
    return g_locatorProxy->RegisterGnssStatusCallback(gnssStatusCallbackHost->AsObject(), DEFAULT_UID);
}

LocationErrCode SubscribeNmeaMessage(const napi_env& env, const napi_ref& handlerRef,
    sptr<NmeaMessageCallbackHost>& nmeaMessageCallbackHost)
{
    nmeaMessageCallbackHost->SetEnv(env);
    nmeaMessageCallbackHost->SetHandleCb(handlerRef);
    return g_locatorProxy->RegisterNmeaMessageCallback(nmeaMessageCallbackHost->AsObject(), DEFAULT_UID);
}

LocationErrCode SubscribeNmeaMessageV9(const napi_env& env, const napi_ref& handlerRef,
    sptr<NmeaMessageCallbackHost>& nmeaMessageCallbackHost)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
        return errorCode;
    }
    nmeaMessageCallbackHost->SetEnv(env);
    nmeaMessageCallbackHost->SetHandleCb(handlerRef);
    return g_locatorProxy->RegisterNmeaMessageCallbackV9(nmeaMessageCallbackHost->AsObject());
}

LocationErrCode UnSubscribeLocationServiceState(sptr<LocationSwitchCallbackHost>& switchCallbackHost)
{
    LBSLOGI(LOCATION_NAPI, "UnSubscribeLocationServiceState");
    return g_locatorProxy->UnregisterSwitchCallback(switchCallbackHost->AsObject());
}

LocationErrCode UnSubscribeGnssStatus(sptr<GnssStatusCallbackHost>& gnssStatusCallbackHost)
{
    LBSLOGI(LOCATION_NAPI, "UnSubscribeGnssStatus");
    return g_locatorProxy->UnregisterGnssStatusCallback(gnssStatusCallbackHost->AsObject());
}

LocationErrCode UnSubscribeNmeaMessage(sptr<NmeaMessageCallbackHost>& nmeaMessageCallbackHost)
{
    LBSLOGI(LOCATION_NAPI, "UnSubscribeNmeaMessage");
    return g_locatorProxy->UnregisterNmeaMessageCallback(nmeaMessageCallbackHost->AsObject());
}

LocationErrCode UnSubscribeNmeaMessageV9(sptr<NmeaMessageCallbackHost>& nmeaMessageCallbackHost)
{
    LBSLOGI(LOCATION_NAPI, "UnSubscribeNmeaMessageV9");
    return g_locatorProxy->UnregisterNmeaMessageCallbackV9(nmeaMessageCallbackHost->AsObject());
}

LocationErrCode SubscribeLocationChange(const napi_env& env, const napi_value& object,
    const napi_ref& handlerRef, sptr<LocatorCallbackHost>& locatorCallbackHost)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
        return errorCode;
    }
    auto locatorCallback = sptr<ILocatorCallback>(locatorCallbackHost);
    locatorCallbackHost->SetFixNumber(0);
    locatorCallbackHost->SetEnv(env);
    locatorCallbackHost->SetHandleCb(handlerRef);
    auto requestConfig = std::make_unique<RequestConfig>();
    JsObjToLocationRequest(env, object, requestConfig);
    return g_locatorProxy->StartLocating(requestConfig, locatorCallback);
}

LocationErrCode SubscribeCountryCodeChange(const napi_env& env,
    const napi_ref& handlerRef, sptr<CountryCodeCallbackHost>& callbackHost)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
        return errorCode;
    }
    auto callbackPtr = sptr<ICountryCodeCallback>(callbackHost);
    callbackHost->SetEnv(env);
    callbackHost->SetCallback(handlerRef);
    return g_locatorProxy->RegisterCountryCodeCallback(callbackPtr->AsObject(), DEFAULT_UID);
}

LocationErrCode UnsubscribeCountryCodeChange(sptr<CountryCodeCallbackHost>& callbackHost)
{
    LBSLOGI(LOCATION_NAPI, "UnsubscribeCountryCodeChange");
    return g_locatorProxy->UnregisterCountryCodeCallback(callbackHost->AsObject());
}

LocationErrCode SubscribeCacheLocationChange(const napi_env& env, const napi_value& object,
    const napi_ref& handlerRef, sptr<CachedLocationsCallbackHost>& cachedCallbackHost)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
        return errorCode;
    }
    auto cachedCallback = sptr<ICachedLocationsCallback>(cachedCallbackHost);
    cachedCallbackHost->SetEnv(env);
    cachedCallbackHost->SetHandleCb(handlerRef);
    auto request = std::make_unique<CachedGnssLocationsRequest>();
    JsObjToCachedLocationRequest(env, object, request);
    return g_locatorProxy->RegisterCachedLocationCallback(request, cachedCallback);
}

LocationErrCode SubscribeFenceStatusChange(const napi_env& env, const napi_value& object, const napi_value& handler)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
        return errorCode;
    }
    auto wantAgent = AbilityRuntime::WantAgent::WantAgent();
    NAPI_CALL_BASE(env, napi_unwrap(env, handler, (void **)&wantAgent), ERRCODE_GEOFENCE_FAIL);
    auto request = std::make_unique<GeofenceRequest>();
    JsObjToGeoFenceRequest(env, object, request);
    auto state = new (std::nothrow) GeoFenceState(request->geofence, wantAgent);
    if (state != nullptr) {
        mFences.push_back(state);
        return g_locatorProxy->AddFence(request);
    }
    return ERRCODE_GEOFENCE_FAIL;
}

LocationErrCode UnSubscribeFenceStatusChange(const napi_env& env, const napi_value& object, const napi_value& handler)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
        return errorCode;
    }
    auto wantAgent = AbilityRuntime::WantAgent::WantAgent();
    NAPI_CALL_BASE(env, napi_unwrap(env, handler, (void **)&wantAgent), ERRCODE_GEOFENCE_FAIL);
    auto request = std::make_unique<GeofenceRequest>();
    JsObjToGeoFenceRequest(env, object, request);
    if (mFences.size() > 0) {
        mFences.erase(mFences.begin());
        return g_locatorProxy->RemoveFence(request);
    }
    return ERRCODE_GEOFENCE_FAIL;
}

SingleLocationAsyncContext* CreateSingleLocationAsyncContext(const napi_env& env,
    std::unique_ptr<RequestConfig>& config, sptr<LocatorCallbackHost> callback)
{
    auto asyncContext = new (std::nothrow) SingleLocationAsyncContext(env);
    NAPI_ASSERT(env, asyncContext != nullptr, "asyncContext is null.");
    NAPI_CALL(env, napi_create_string_latin1(env, "GetCurrentLocation",
        NAPI_AUTO_LENGTH, &asyncContext->resourceName));
    asyncContext->timeout_ = config->GetTimeOut();
    asyncContext->callbackHost_ = callback;
    asyncContext->executeFunc = [&](void* data) -> void {
        if (data == nullptr) {
            LBSLOGE(LOCATOR_STANDARD, "data is nullptr!");
            return;
        }
        auto context = static_cast<SingleLocationAsyncContext*>(data);
        auto callbackHost = context->callbackHost_;
        int state = DISABLED;
        LocationErrCode errorCode = g_locatorProxy->IsLocationEnabled(state);
        if (errorCode != ERRCODE_SUCCESS) {
            context->errCode = errorCode;
            return;
        }
        if (state == DISABLED) {
            context->errCode = ERRCODE_SWITCH_OFF;
            return;
        }
        if (callbackHost != nullptr) {
            callbackHost->Wait(context->timeout_);
            auto callbackPtr = sptr<ILocatorCallback>(callbackHost);
            errorCode = g_locatorProxy->StopLocating(callbackPtr);
            if (errorCode != ERRCODE_SUCCESS) {
                context->errCode = errorCode;
                callbackHost->SetCount(1);
                return;
            }
            if (callbackHost->GetCount() != 0) {
                context->errCode = ERRCODE_LOCATING_FAIL;
            }
            callbackHost->SetCount(1);
        }
    };
    asyncContext->completeFunc = [&](void* data) -> void {
        if (data == nullptr) {
            LBSLOGE(LOCATOR_STANDARD, "data is nullptr!");
            return;
        }
        auto context = static_cast<SingleLocationAsyncContext*>(data);
        NAPI_CALL_RETURN_VOID(context->env, napi_create_object(context->env, &context->result[PARAM1]));
        auto callbackHost = context->callbackHost_;
        if (callbackHost != nullptr && callbackHost->GetSingleLocation() != nullptr) {
            std::unique_ptr<Location> location = std::make_unique<Location>(*callbackHost->GetSingleLocation());
            LocationToJs(context->env, location, context->result[PARAM1]);
        } else {
            LBSLOGE(LOCATOR_STANDARD, "m_singleLocation is nullptr!");
        }
        if (context->callbackHost_) {
            context->callbackHost_ = nullptr;
        }
        LBSLOGI(LOCATOR_STANDARD, "Push single location to client");
    };
    return asyncContext;
}

int GetObjectArgsNum(const napi_env& env, const size_t argc, const napi_value* argv)
{
    napi_valuetype valueType = napi_undefined;
    int objectArgsNum = PARAM0;
    if (argc == PARAM0) {
        objectArgsNum = PARAM0;
    } else if (argc == PARAM1) {
        NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM0], &valueType), objectArgsNum);
        if (valueType == napi_object) {
            objectArgsNum = PARAM1;
        } else if (valueType == napi_function) {
            objectArgsNum = PARAM0;
        }
    } else if (argc == PARAM2) {
        objectArgsNum = PARAM1;
    } else {
        LBSLOGI(LOCATION_NAPI, "argc of GetCurrentLocation is wrong.");
    }
    return objectArgsNum;
}

std::unique_ptr<RequestConfig> CreateRequestConfig(const napi_env& env,
    const napi_value* argv, const size_t& objectArgsNum)
{
    auto requestConfig = std::make_unique<RequestConfig>();
    if (objectArgsNum > 0) {
        JsObjToCurrentLocationRequest(env, argv[objectArgsNum - 1], requestConfig);
    } else {
        requestConfig->SetPriority(PRIORITY_FAST_FIRST_FIX);
    }
    requestConfig->SetFixNumber(1);
    return requestConfig;
}

sptr<LocatorCallbackHost> CreateSingleLocationCallbackHost()
{
    auto callbackHost =
        sptr<LocatorCallbackHost>(new (std::nothrow) LocatorCallbackHost());
    if (callbackHost) {
        callbackHost->SetFixNumber(1);
    }
    return callbackHost;
}

napi_value RequestLocationOnce(const napi_env& env, const size_t argc, const napi_value* argv)
{
    size_t objectArgsNum = 0;

    objectArgsNum = static_cast<size_t>(GetObjectArgsNum(env, argc, argv));
    auto requestConfig = CreateRequestConfig(env, argv, objectArgsNum);
    NAPI_ASSERT(env, requestConfig != nullptr, "requestConfig is null.");
    auto singleLocatorCallbackHost = CreateSingleLocationCallbackHost();
#ifdef ENABLE_NAPI_MANAGER
    if (singleLocatorCallbackHost == nullptr) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return UndefinedNapiValue(env);
    }
#else
    NAPI_ASSERT(env, singleLocatorCallbackHost != nullptr, "callbackHost is null.");
#endif
    int state = DISABLED;
    LocationErrCode errorCode = g_locatorProxy->IsLocationEnabled(state);
#ifdef ENABLE_NAPI_MANAGER
    if (errorCode != ERRCODE_SUCCESS) {
        HandleSyncErrCode(env, errorCode);
        return UndefinedNapiValue(env);
    }
    if (state == DISABLED) {
        HandleSyncErrCode(env, ERRCODE_SWITCH_OFF);
        return UndefinedNapiValue(env);
    }
#endif
    if (state == ENABLED) {
        auto callbackPtr = sptr<ILocatorCallback>(singleLocatorCallbackHost);
        errorCode = g_locatorProxy->StartLocating(requestConfig, callbackPtr);
#ifdef ENABLE_NAPI_MANAGER
        if (errorCode != ERRCODE_SUCCESS) {
            HandleSyncErrCode(env, errorCode);
            return UndefinedNapiValue(env);
        }
#endif
    }

    auto asyncContext = CreateSingleLocationAsyncContext(env, requestConfig, singleLocatorCallbackHost);
    NAPI_ASSERT(env, asyncContext != nullptr, "asyncContext is null.");
    return DoAsyncWork(env, asyncContext, argc, argv, objectArgsNum);
}

LocationErrCode UnSubscribeLocationChange(sptr<ILocatorCallback>& callback)
{
    LBSLOGI(LOCATION_NAPI, "UnSubscribeLocationChange");
    return g_locatorProxy->StopLocating(callback);
}

LocationErrCode UnSubscribeCacheLocationChange(sptr<ICachedLocationsCallback>& callback)
{
    LBSLOGI(LOCATION_NAPI, "UnSubscribeCacheLocationChange");
    return g_locatorProxy->UnregisterCachedLocationCallback(callback);
}

bool IsCallbackEquals(const napi_env& env, const napi_value& handler, const napi_ref& savedCallback)
{
    napi_value handlerTemp = nullptr;
    if (savedCallback == nullptr || handler == nullptr) {
        return false;
    }
    NAPI_CALL_BASE(env, napi_get_reference_value(env, savedCallback, &handlerTemp), false);
    bool isEqual = false;
    NAPI_CALL_BASE(env, napi_strict_equals(env, handlerTemp, handler, &isEqual), false);
    return isEqual;
}

bool OnLocationServiceStateCallback(const napi_env& env, const size_t argc, const napi_value* argv)
{
    napi_valuetype valueType;
#ifdef ENABLE_NAPI_MANAGER
    if (argc < PARAM2) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM1], &valueType), false);
    if (valueType != napi_function) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
#else
    NAPI_ASSERT_BASE(env, argc == PARAM2, "number of parameters is wrong", INPUT_PARAMS_ERROR);
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM1], &valueType), false);
    NAPI_ASSERT_BASE(env, valueType == napi_function,
        "callback should be function, mismatch for param.", INPUT_PARAMS_ERROR);
#endif
    if (g_switchCallbacks.IsCallbackInMap(env, argv[PARAM1])) {
        LBSLOGE(LOCATION_NAPI, "This request already exists");
        return false;
    }
    auto switchCallbackHost =
        sptr<LocationSwitchCallbackHost>(new (std::nothrow) LocationSwitchCallbackHost());
    if (switchCallbackHost != nullptr) {
        napi_ref handlerRef = nullptr;
        NAPI_CALL_BASE(env, napi_create_reference(env, argv[PARAM1], 1, &handlerRef), false);
        g_switchCallbacks.AddCallback(env, handlerRef, switchCallbackHost);
        LocationErrCode errorCode = SubscribeLocationServiceState(env, handlerRef, switchCallbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
#else
            LBSLOGE(LOCATION_NAPI, "can not subscribe LocationServiceState.");
#endif
            return false;
        }
    }
    return true;
}

bool OnCachedGnssLocationsReportingCallback(const napi_env& env, const size_t argc, const napi_value* argv)
{
    napi_valuetype valueType;
#ifdef ENABLE_NAPI_MANAGER
    if (argc < PARAM3) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM2], &valueType), false);
    if (valueType != napi_function) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
#else
    NAPI_ASSERT_BASE(env, argc == PARAM3, "number of parameters is wrong", INPUT_PARAMS_ERROR);
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM2], &valueType), false);
    NAPI_ASSERT_BASE(env, valueType == napi_function,
        "callback should be function, mismatch for param.", INPUT_PARAMS_ERROR);
#endif
    // the third params should be handler
    if (g_cachedLocationCallbacks.IsCallbackInMap(env, argv[PARAM2])) {
        LBSLOGE(LOCATION_NAPI, "This request already exists");
        return false;
    }
    auto cachedCallbackHost =
        sptr<CachedLocationsCallbackHost>(new (std::nothrow) CachedLocationsCallbackHost());
    if (cachedCallbackHost != nullptr) {
        napi_ref handlerRef = nullptr;
        NAPI_CALL_BASE(env, napi_create_reference(env, argv[PARAM2], PARAM1, &handlerRef), false);
        g_cachedLocationCallbacks.AddCallback(env, handlerRef, cachedCallbackHost);
        LocationErrCode errorCode = SubscribeCacheLocationChange(env, argv[PARAM1], handlerRef, cachedCallbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
#else
            LBSLOGE(LOCATION_NAPI, "can not subscribe CacheLocationChange.");
#endif
            return false;
        }
    }
    return true;
}

bool OnGnssStatusChangeCallback(const napi_env& env, const size_t argc, const napi_value* argv)
{
    napi_valuetype valueType;
#ifdef ENABLE_NAPI_MANAGER
    if (argc < PARAM2) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM1], &valueType), false);
    if (valueType != napi_function) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
#else
    NAPI_ASSERT_BASE(env, argc == PARAM2, "number of parameters is wrong", INPUT_PARAMS_ERROR);
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM1], &valueType), false);
    NAPI_ASSERT_BASE(env, valueType == napi_function,
        "callback should be function, mismatch for param.", INPUT_PARAMS_ERROR);
#endif
    if (g_gnssStatusInfoCallbacks.IsCallbackInMap(env, argv[PARAM1])) {
        LBSLOGE(LOCATION_NAPI, "This request already exists");
        return false;
    }
    auto gnssCallbackHost =
        sptr<GnssStatusCallbackHost>(new (std::nothrow) GnssStatusCallbackHost());
    if (gnssCallbackHost != nullptr) {
        napi_ref handlerRef = nullptr;
        NAPI_CALL_BASE(env, napi_create_reference(env, argv[PARAM1], PARAM1, &handlerRef), false);
        g_gnssStatusInfoCallbacks.AddCallback(env, handlerRef, gnssCallbackHost);
        LocationErrCode errorCode = SubscribeGnssStatus(env, handlerRef, gnssCallbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
#else
            LBSLOGE(LOCATION_NAPI, "can not subscribe GnssStatus.");
#endif
            return false;
        }
    }
    return true;
}

bool OnLocationChangeCallback(const napi_env& env, const size_t argc, const napi_value* argv)
{
    napi_valuetype valueType;
#ifdef ENABLE_NAPI_MANAGER
    if (argc < PARAM3) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM2], &valueType), false);
    if (valueType != napi_function) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
#else
    NAPI_ASSERT_BASE(env, argc == PARAM3, "number of parameters is wrong", INPUT_PARAMS_ERROR);
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM2], &valueType), false);
    NAPI_ASSERT_BASE(env, valueType == napi_function,
        "callback should be function, mismatch for param.", INPUT_PARAMS_ERROR);
#endif
    // the third params should be handler
    if (g_locationCallbacks.IsCallbackInMap(env, argv[PARAM2])) {
        LBSLOGE(LOCATION_NAPI, "This request already exists");
        return false;
    }
    auto locatorCallbackHost =
        sptr<LocatorCallbackHost>(new (std::nothrow) LocatorCallbackHost());
    if (locatorCallbackHost != nullptr) {
        napi_ref handlerRef = nullptr;
        NAPI_CALL_BASE(env, napi_create_reference(env, argv[PARAM2], 1, &handlerRef), false);
        g_locationCallbacks.AddCallback(env, handlerRef, locatorCallbackHost);
        // argv[1]:request params, argv[2]:handler
        LocationErrCode errorCode = SubscribeLocationChange(env, argv[PARAM1], handlerRef, locatorCallbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
#else
            LBSLOGE(LOCATION_NAPI, "can not subscribe LocationChange.");
#endif
            return false;
        }
    }
    return true;
}

bool OnNmeaMessageChangeCallback(const napi_env& env, const size_t argc, const napi_value* argv)
{
    napi_valuetype valueType;
#ifdef ENABLE_NAPI_MANAGER
    if (argc < PARAM2) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM1], &valueType), false);
    if (valueType != napi_function) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
#else
    NAPI_ASSERT_BASE(env, argc == PARAM2, "number of parameters is wrong", INPUT_PARAMS_ERROR);
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM1], &valueType), false);
    NAPI_ASSERT_BASE(env, valueType == napi_function,
        "callback should be function, mismatch for param.", INPUT_PARAMS_ERROR);
#endif
    if (g_nmeaCallbacks.IsCallbackInMap(env, argv[PARAM1])) {
        LBSLOGE(LOCATION_NAPI, "This request already exists");
        return false;
    }
    auto nmeaCallbackHost =
        sptr<NmeaMessageCallbackHost>(new (std::nothrow) NmeaMessageCallbackHost());
    if (nmeaCallbackHost != nullptr) {
        napi_ref handlerRef = nullptr;
        NAPI_CALL_BASE(env, napi_create_reference(env, argv[PARAM1], PARAM1, &handlerRef), false);
        g_nmeaCallbacks.AddCallback(env, handlerRef, nmeaCallbackHost);
#ifdef ENABLE_NAPI_MANAGER
        LocationErrCode ret = SubscribeNmeaMessageV9(env, handlerRef, nmeaCallbackHost);
        if (ret != ERRCODE_SUCCESS) {
            HandleSyncErrCode(env, ret);
            return false;
        }
#else
        LocationErrCode errorCode = SubscribeNmeaMessage(env, handlerRef, nmeaCallbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
            return false;
        }
#endif
    }
    return true;
}

bool OnCountryCodeChangeCallback(const napi_env& env, const size_t argc, const napi_value* argv)
{
    napi_valuetype valueType;
#ifdef ENABLE_NAPI_MANAGER
    if (argc < PARAM2) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM1], &valueType), false);
    if (valueType != napi_function) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
#else
    NAPI_ASSERT_BASE(env, argc == PARAM2, "number of parameters is wrong", INPUT_PARAMS_ERROR);
    NAPI_CALL_BASE(env, napi_typeof(env, argv[PARAM1], &valueType), false);
    NAPI_ASSERT_BASE(env, valueType == napi_function,
        "callback should be function, mismatch for param.", INPUT_PARAMS_ERROR);
#endif
    if (g_countryCodeCallbacks.IsCallbackInMap(env, argv[PARAM1])) {
        LBSLOGE(LOCATION_NAPI, "This request already exists");
        return false;
    }
    auto callbackHost =
        sptr<CountryCodeCallbackHost>(new (std::nothrow) CountryCodeCallbackHost());
    if (callbackHost) {
        napi_ref handlerRef = nullptr;
        NAPI_CALL_BASE(env, napi_create_reference(env, argv[PARAM1], 1, &handlerRef), false);
        g_countryCodeCallbacks.AddCallback(env, handlerRef, callbackHost);
        LocationErrCode errorCode = SubscribeCountryCodeChange(env, handlerRef, callbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
            return false;
#endif
        }
    }
    return true;
}

bool OnFenceStatusChangeCallback(const napi_env& env, const size_t argc, const napi_value* argv)
{
#ifdef ENABLE_NAPI_MANAGER
    if (argc < PARAM3) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return false;
    }
#else
    NAPI_ASSERT_BASE(env, argc == PARAM3, "number of parameters is wrong", INPUT_PARAMS_ERROR);
#endif
    // the third params should be handler
    LocationErrCode errorCode = SubscribeFenceStatusChange(env, argv[PARAM1], argv[PARAM2]);
    if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
        HandleSyncErrCode(env, errorCode);
        return false;
#endif
    }
    return true;
}

napi_value On(napi_env env, napi_callback_info cbinfo)
{
    InitOnFuncMap();
    size_t argc = PARAM3;
    napi_value argv[PARAM3] = {0};
    napi_value thisVar = nullptr;
    LBSLOGI(LOCATION_NAPI, "On function entry");
    NAPI_CALL(env, napi_get_cb_info(env, cbinfo, &argc, argv, &thisVar, nullptr));
    napi_valuetype eventName = napi_undefined;
    NAPI_CALL(env, napi_typeof(env, argv[PARAM0], &eventName));
#ifdef ENABLE_NAPI_MANAGER
    if (eventName != napi_string) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return UndefinedNapiValue(env);
    }
#else
    NAPI_ASSERT(env, eventName == napi_string, "type mismatch for parameter 1");
#endif
    
    NAPI_ASSERT(env, g_locatorProxy != nullptr, "locator instance is null.");

    char type[64] = {0}; // max length
    size_t typeLen = 0;
    NAPI_CALL(env, napi_get_value_string_utf8(env, argv[PARAM0], type, sizeof(type), &typeLen));
    std::string event = type;
    LBSLOGI(LOCATION_NAPI, "Subscribe event: %{public}s", event.c_str());
    auto onCallbackFunc = g_onFuncMap.find(event);
    if (onCallbackFunc != g_onFuncMap.end() && onCallbackFunc->second != nullptr) {
        auto memberFunc = onCallbackFunc->second;
        (*memberFunc)(env, argc, argv);
    }
    return UndefinedNapiValue(env);
}

bool OffAllLocationServiceStateCallback(const napi_env& env)
{
    std::map<napi_env, std::map<napi_ref, sptr<LocationSwitchCallbackHost>>> callbackMap =
        g_switchCallbacks.GetCallbackMap();
    auto iter = callbackMap.find(env);
    if (iter == callbackMap.end()) {
        return false;
    }
    for (auto innerIter = iter->second.begin(); innerIter != iter->second.end(); innerIter++) {
        auto callbackHost = innerIter->second;
        if (callbackHost == nullptr) {
            continue;
        }
        LocationErrCode errorCode = UnSubscribeLocationServiceState(callbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
            return false;
#endif
        }
        callbackHost->DeleteHandler();
        callbackHost = nullptr;
    }
    g_switchCallbacks.DeleteCallbackByEnv(env);
    return true;
}

bool OffAllLocationChangeCallback(const napi_env& env)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
#endif
            return false;
    }
    std::map<napi_env, std::map<napi_ref, sptr<LocatorCallbackHost>>> callbackMap =
        g_locationCallbacks.GetCallbackMap();
    auto iter = callbackMap.find(env);
    if (iter == callbackMap.end()) {
        return false;
    }
    for (auto innerIter = iter->second.begin(); innerIter != iter->second.end(); innerIter++) {
        auto callbackHost = innerIter->second;
        if (callbackHost == nullptr) {
            continue;
        }
        auto locatorCallback = sptr<ILocatorCallback>(callbackHost);
        LocationErrCode errorCode = UnSubscribeLocationChange(locatorCallback);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
            return false;
#endif
        }
        callbackHost->DeleteAllCallbacks();
        callbackHost = nullptr;
    }
    g_locationCallbacks.DeleteCallbackByEnv(env);
    return true;
}

bool OffAllGnssStatusChangeCallback(const napi_env& env)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
#endif
            return false;
    }
    std::map<napi_env, std::map<napi_ref, sptr<GnssStatusCallbackHost>>> callbackMap =
        g_gnssStatusInfoCallbacks.GetCallbackMap();
    auto iter = callbackMap.find(env);
    if (iter == callbackMap.end()) {
        return false;
    }
    for (auto innerIter = iter->second.begin(); innerIter != iter->second.end(); innerIter++) {
        auto callbackHost = innerIter->second;
        if (callbackHost == nullptr) {
            continue;
        }
        LocationErrCode errorCode = UnSubscribeGnssStatus(callbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
            return false;
#endif
        }
        callbackHost->DeleteHandler();
        callbackHost = nullptr;
    }
    g_gnssStatusInfoCallbacks.DeleteCallbackByEnv(env);
    return true;
}

bool OffAllNmeaMessageChangeCallback(const napi_env& env)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
        HandleSyncErrCode(env, errorCode);
#endif
        return false;
    }
    std::map<napi_env, std::map<napi_ref, sptr<NmeaMessageCallbackHost>>> callbackMap =
        g_nmeaCallbacks.GetCallbackMap();
    auto iter = callbackMap.find(env);
    if (iter == callbackMap.end()) {
        return false;
    }
    for (auto innerIter = iter->second.begin(); innerIter != iter->second.end(); innerIter++) {
        auto callbackHost = innerIter->second;
        if (callbackHost == nullptr) {
            continue;
        }
#ifdef ENABLE_NAPI_MANAGER
        LocationErrCode ret = UnSubscribeNmeaMessageV9(callbackHost);
        if (ret != ERRCODE_SUCCESS) {
            HandleSyncErrCode(env, ret);
            return false;
        }
#else
        UnSubscribeNmeaMessage(callbackHost);
#endif
        callbackHost->DeleteHandler();
        callbackHost = nullptr;
    }
    g_nmeaCallbacks.DeleteCallbackByEnv(env);
    return true;
}

bool OffAllCachedGnssLocationsReportingCallback(const napi_env& env)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
        HandleSyncErrCode(env, errorCode);
#endif
        return false;
    }
    std::map<napi_env, std::map<napi_ref, sptr<CachedLocationsCallbackHost>>> callbackMap =
        g_cachedLocationCallbacks.GetCallbackMap();
    auto iter = callbackMap.find(env);
    if (iter == callbackMap.end()) {
        return false;
    }
    for (auto innerIter = iter->second.begin(); innerIter != iter->second.end(); innerIter++) {
        auto callbackHost = innerIter->second;
        if (callbackHost == nullptr) {
            continue;
        }
        auto cachedCallback = sptr<ICachedLocationsCallback>(callbackHost);
        LocationErrCode errorCode = UnSubscribeCacheLocationChange(cachedCallback);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
            return false;
#endif
        }
        callbackHost->DeleteHandler();
        callbackHost = nullptr;
    }
    g_cachedLocationCallbacks.DeleteCallbackByEnv(env);
    return true;
}

bool OffAllCountryCodeChangeCallback(const napi_env& env)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
        HandleSyncErrCode(env, errorCode);
#endif
        return false;
    }
    std::map<napi_env, std::map<napi_ref, sptr<CountryCodeCallbackHost>>> callbackMap =
        g_countryCodeCallbacks.GetCallbackMap();
    auto iter = callbackMap.find(env);
    if (iter == callbackMap.end()) {
        return false;
    }
    for (auto innerIter = iter->second.begin(); innerIter != iter->second.end(); innerIter++) {
        auto callbackHost = innerIter->second;
        if (callbackHost == nullptr) {
            continue;
        }
        LocationErrCode errorCode = UnsubscribeCountryCodeChange(callbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
            return false;
#endif
        }
        callbackHost->DeleteHandler();
        callbackHost = nullptr;
    }
    g_countryCodeCallbacks.DeleteCallbackByEnv(env);
    return true;
}

bool OffLocationServiceStateCallback(const napi_env& env, const napi_value& handler)
{
    auto switchCallbackHost = g_switchCallbacks.GetCallbackPtr(env, handler);
    if (switchCallbackHost) {
        LocationErrCode errorCode = UnSubscribeLocationServiceState(switchCallbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
            return false;
#endif
        }
        g_switchCallbacks.DeleteCallback(env, handler);
        switchCallbackHost->DeleteHandler();
        switchCallbackHost = nullptr;
        return true;
    }
    return false;
}

bool OffLocationChangeCallback(const napi_env& env, const napi_value& handler)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
        HandleSyncErrCode(env, errorCode);
#endif
        return false;
    }
    auto locatorCallbackHost = g_locationCallbacks.GetCallbackPtr(env, handler);
    if (locatorCallbackHost) {
        auto locatorCallback = sptr<ILocatorCallback>(locatorCallbackHost);
        LocationErrCode errorCode = UnSubscribeLocationChange(locatorCallback);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
            return false;
#endif
        }
        g_locationCallbacks.DeleteCallback(env, handler);
        locatorCallbackHost->DeleteAllCallbacks();
        locatorCallbackHost = nullptr;
        return true;
    }
    return false;
}

bool OffGnssStatusChangeCallback(const napi_env& env, const napi_value& handler)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
        HandleSyncErrCode(env, errorCode);
#endif
        return false;
    }
    auto gnssCallbackHost = g_gnssStatusInfoCallbacks.GetCallbackPtr(env, handler);
    if (gnssCallbackHost) {
        LocationErrCode errorCode = UnSubscribeGnssStatus(gnssCallbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
            return false;
#endif
        }
        g_gnssStatusInfoCallbacks.DeleteCallback(env, handler);
        gnssCallbackHost->DeleteHandler();
        gnssCallbackHost = nullptr;
        return true;
    }
    return false;
}

bool OffNmeaMessageChangeCallback(const napi_env& env, const napi_value& handler)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
        HandleSyncErrCode(env, errorCode);
#endif
        return false;
    }
    auto nmeaCallbackHost = g_nmeaCallbacks.GetCallbackPtr(env, handler);
    if (nmeaCallbackHost) {
#ifdef ENABLE_NAPI_MANAGER
        LocationErrCode ret = UnSubscribeNmeaMessageV9(nmeaCallbackHost);
        if (ret != ERRCODE_SUCCESS) {
            HandleSyncErrCode(env, ret);
            return false;
        }
#else
        UnSubscribeNmeaMessage(nmeaCallbackHost);
#endif
        g_nmeaCallbacks.DeleteCallback(env, handler);
        nmeaCallbackHost->DeleteHandler();
        nmeaCallbackHost = nullptr;
        return true;
    }
    return false;
}

bool OffCachedGnssLocationsReportingCallback(const napi_env& env, const napi_value& handler)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
        HandleSyncErrCode(env, errorCode);
#endif
        return false;
    }
    auto cachedCallbackHost = g_cachedLocationCallbacks.GetCallbackPtr(env, handler);
    if (cachedCallbackHost) {
        auto cachedCallback = sptr<ICachedLocationsCallback>(cachedCallbackHost);
        LocationErrCode errorCode = UnSubscribeCacheLocationChange(cachedCallback);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
            return false;
#endif
        }
        g_cachedLocationCallbacks.DeleteCallback(env, handler);
        cachedCallbackHost->DeleteHandler();
        cachedCallbackHost = nullptr;
        return true;
    }
    return false;
}

bool OffCountryCodeChangeCallback(const napi_env& env, const napi_value& handler)
{
    LocationErrCode errorCode = CheckLocationSwitchState();
    if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
        HandleSyncErrCode(env, errorCode);
#endif
        return false;
    }
    auto callbackHost = g_countryCodeCallbacks.GetCallbackPtr(env, handler);
    if (callbackHost) {
        LocationErrCode errorCode = UnsubscribeCountryCodeChange(callbackHost);
        if (errorCode != ERRCODE_SUCCESS) {
#ifdef ENABLE_NAPI_MANAGER
            HandleSyncErrCode(env, errorCode);
            return false;
#endif
        }
        g_countryCodeCallbacks.DeleteCallback(env, handler);
        callbackHost->DeleteHandler();
        callbackHost = nullptr;
        return true;
    }
    return false;
}

napi_value Off(napi_env env, napi_callback_info cbinfo)
{
    InitOffFuncMap();
    size_t argc = PARAM2;
    napi_value argv[PARAM3] = {0};
    napi_value thisVar = nullptr;
    NAPI_CALL(env, napi_get_cb_info(env, cbinfo, &argc, argv, &thisVar, nullptr));
    NAPI_ASSERT(env, g_locatorProxy != nullptr, "locator instance is null.");
#ifdef ENABLE_NAPI_MANAGER
    if (argc < PARAM1) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return UndefinedNapiValue(env);
    }
#endif
    napi_valuetype eventName = napi_undefined;
    NAPI_CALL(env, napi_typeof(env, argv[PARAM0], &eventName));
#ifdef ENABLE_NAPI_MANAGER
    if (eventName != napi_string) {
        HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
        return UndefinedNapiValue(env);
    }
#else
    NAPI_ASSERT(env, eventName == napi_string, "type mismatch for parameter 1");
#endif
    LBSLOGI(LOCATION_NAPI, "Off function entry");

    char type[64] = {0};
    size_t typeLen = 0;
    NAPI_CALL(env, napi_get_value_string_utf8(env, argv[PARAM0], type, sizeof(type), &typeLen));
    std::string event = type;
    LBSLOGI(LOCATION_NAPI, "Unsubscribe event: %{public}s", event.c_str());
    if (argc == PARAM1) {
        auto offAllCallbackFunc = g_offAllFuncMap.find(event);
        if (offAllCallbackFunc != g_offAllFuncMap.end() && offAllCallbackFunc->second != nullptr) {
            auto memberFunc = offAllCallbackFunc->second;
            (*memberFunc)(env);
        }
    } else if (argc == PARAM2) {
        napi_valuetype valueType;
        NAPI_CALL(env, napi_typeof(env, argv[PARAM1], &valueType));
#ifdef ENABLE_NAPI_MANAGER
        if (valueType != napi_function) {
            HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
            return UndefinedNapiValue(env);
        }
#else
        NAPI_ASSERT(env, valueType == napi_function, "callback should be function, mismatch for param.");
#endif
        auto offCallbackFunc = g_offFuncMap.find(event);
        if (offCallbackFunc != g_offFuncMap.end() && offCallbackFunc->second != nullptr) {
            auto singleMemberFunc = offCallbackFunc->second;
            (*singleMemberFunc)(env, argv[PARAM1]);
        }
#ifdef ENABLE_NAPI_MANAGER
    } else if (argc == PARAM3 && event == "gnssFenceStatusChange") {
        LocationErrCode errorCode = UnSubscribeFenceStatusChange(env, argv[PARAM1], argv[PARAM2]);
        if (errorCode != ERRCODE_SUCCESS) {
            HandleSyncErrCode(env, errorCode);
        }
#else
    } else if (argc == PARAM3 && event == "fenceStatusChange") {
        UnSubscribeFenceStatusChange(env, argv[PARAM1], argv[PARAM2]);
#endif
    }
    return UndefinedNapiValue(env);
}

napi_value GetCurrentLocation(napi_env env, napi_callback_info cbinfo)
{
    size_t argc = PARAM3;
    napi_value argv[PARAM3] = {0};
    napi_value thisVar = nullptr;

    NAPI_CALL(env, napi_get_cb_info(env, cbinfo, &argc, argv, &thisVar, nullptr));

    napi_valuetype valueType = napi_undefined;
    NAPI_ASSERT(env, g_locatorProxy != nullptr, "locator instance is null.");
    LBSLOGI(LOCATION_NAPI, "GetCurrentLocation enter");

    if (argc == PARAM1) {
        NAPI_CALL(env, napi_typeof(env, argv[PARAM0], &valueType));
#ifdef ENABLE_NAPI_MANAGER
        if (valueType != napi_function && valueType != napi_object) {
            HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
            return UndefinedNapiValue(env);
        }
#else
        NAPI_ASSERT(env, valueType == napi_function || valueType == napi_object, "type mismatch for parameter 2");
#endif
    }
    if (argc == PARAM2) {
        napi_valuetype valueType1 = napi_undefined;
        NAPI_CALL(env, napi_typeof(env, argv[PARAM0], &valueType));
        NAPI_CALL(env, napi_typeof(env, argv[PARAM1], &valueType1));
#ifdef ENABLE_NAPI_MANAGER
        if (valueType != napi_object || valueType1 != napi_function) {
            HandleSyncErrCode(env, ERRCODE_INVALID_PARAM);
            return UndefinedNapiValue(env);
        }
#else
        NAPI_ASSERT(env, valueType == napi_object, "type mismatch for parameter 1");
        NAPI_ASSERT(env, valueType1 == napi_function, "type mismatch for parameter 2");
#endif
    }
    return RequestLocationOnce(env, argc, argv);
}

LocationErrCode CheckLocationSwitchState()
{
    LBSLOGE(LOCATION_NAPI, "CheckLocationSwitchState enter");
    int state = DISABLED;
    LocationErrCode errorCode = g_locatorProxy->IsLocationEnabled(state);
    if (errorCode != ERRCODE_SUCCESS) {
        LBSLOGE(LOCATION_NAPI, "CheckLocationSwitchState errorCode");
        return errorCode;
    }
    if (state == DISABLED) {
        LBSLOGE(LOCATION_NAPI, "CheckLocationSwitchState ERRCODE_SWITCH_OFF");
        return ERRCODE_SWITCH_OFF;
    }
    LBSLOGE(LOCATION_NAPI, "CheckLocationSwitchState ERRCODE_SUCCESS");
    return ERRCODE_SUCCESS;
}
}  // namespace Location
}  // namespace OHOS
