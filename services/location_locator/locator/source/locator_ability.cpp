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

#include "locator_ability.h"

#include "accesstoken_kit.h"
#include "event_runner.h"
#include "system_ability_definition.h"
#include "uri.h"

#include "common_event_manager.h"
#include "common_hisysevent.h"
#include "common_utils.h"
#include "constant_definition.h"
#include "country_code.h"
#ifdef FEATURE_GEOCODE_SUPPORT
#include "geo_convert_proxy.h"
#endif
#ifdef FEATURE_GNSS_SUPPORT
#include "gnss_ability_proxy.h"
#endif
#include "locator_background_proxy.h"
#include "location_config_manager.h"
#include "location_data_rdb_helper.h"
#include "location_log.h"
#include "location_sa_load_manager.h"
#include "locationhub_ipc_interface_code.h"
#ifdef FEATURE_NETWORK_SUPPORT
#include "network_ability_proxy.h"
#endif
#ifdef FEATURE_PASSIVE_SUPPORT
#include "passive_ability_proxy.h"
#endif
#include "permission_status_change_cb.h"

namespace OHOS {
namespace Location {
const bool REGISTER_RESULT = SystemAbility::MakeAndRegisterAbility(
    DelayedSingleton<LocatorAbility>::GetInstance().get());

const uint32_t EVENT_UPDATE_SA = 0x0001;
const uint32_t EVENT_INIT_REQUEST_MANAGER = 0x0002;
const uint32_t EVENT_APPLY_REQUIREMENTS = 0x0003;
const uint32_t EVENT_RETRY_REGISTER_ACTION = 0x0004;
const uint32_t EVENT_UNLOAD_SA = 0x0010;
const uint32_t RETRY_INTERVAL_UNITE = 1000;
const uint32_t RETRY_INTERVAL_OF_INIT_REQUEST_MANAGER = 5 * RETRY_INTERVAL_UNITE;
const uint32_t RETRY_INTERVAL_OF_UNLOAD_SA = 30 * RETRY_INTERVAL_UNITE;
const float_t PRECISION = 0.000001;
const std::string UNLOAD_TASK = "locatior_sa_unload";

LocatorAbility::LocatorAbility() : SystemAbility(LOCATION_LOCATOR_SA_ID, true)
{
    switchCallbacks_ = std::make_unique<std::map<pid_t, sptr<ISwitchCallback>>>();
    requests_ = std::make_shared<std::map<std::string, std::list<std::shared_ptr<Request>>>>();
    receivers_ = std::make_shared<std::map<sptr<IRemoteObject>, std::list<std::shared_ptr<Request>>>>();
    proxyMap_ = std::make_shared<std::map<std::string, sptr<IRemoteObject>>>();
    permissionMap_ = std::make_shared<std::map<uint32_t, std::shared_ptr<PermissionStatusChangeCb>>>();
    InitRequestManagerMap();
    reportManager_ = DelayedSingleton<ReportManager>::GetInstance();
    LBSLOGI(LOCATOR, "LocatorAbility constructed.");
}

LocatorAbility::~LocatorAbility() {}

void LocatorAbility::OnStart()
{
    if (state_ == ServiceRunningState::STATE_RUNNING) {
        LBSLOGI(LOCATOR, "LocatorAbility has already started.");
        return;
    }
    if (!Init()) {
        LBSLOGE(LOCATOR, "failed to init LocatorAbility");
        OnStop();
        return;
    }
    state_ = ServiceRunningState::STATE_RUNNING;
    AddSystemAbilityListener(COMMON_EVENT_SERVICE_ID);
    LBSLOGI(LOCATOR, "LocatorAbility::OnStart start ability success.");
}

void LocatorAbility::OnStop()
{
    state_ = ServiceRunningState::STATE_NOT_START;
    registerToAbility_ = false;
    LBSLOGI(LOCATOR, "LocatorAbility::OnStop ability stopped.");
}

void LocatorAbility::OnAddSystemAbility(int32_t systemAbilityId, const std::string &deviceId)
{
    if (systemAbilityId != COMMON_EVENT_SERVICE_ID) {
        LBSLOGE(LOCATOR, "systemAbilityId is not COMMON_EVENT_SERVICE_ID");
        return;
    }
    if (locatorEventSubscriber_ == nullptr) {
        LBSLOGE(LOCATOR, "OnAddSystemAbility subscribeer is nullptr");
        return;
    }
    OHOS::EventFwk::MatchingSkills matchingSkills;
    matchingSkills.AddEvent(MODE_CHANGED_EVENT);
    OHOS::EventFwk::CommonEventSubscribeInfo subscriberInfo(matchingSkills);
    bool result = OHOS::EventFwk::CommonEventManager::SubscribeCommonEvent(locatorEventSubscriber_);
    LBSLOGI(LOCATOR, "SubscribeCommonEvent locatorEventSubscriber_ result = %{public}d", result);
    if (countryCodeManager_ == nullptr) {
        countryCodeManager_ = DelayedSingleton<CountryCodeManager>::GetInstance();
    }
    if (countryCodeManager_ != nullptr) {
        countryCodeManager_->ReSubscribeEvent();
    }
}

void LocatorAbility::OnRemoveSystemAbility(int32_t systemAbilityId, const std::string& deviceId)
{
    if (systemAbilityId != COMMON_EVENT_SERVICE_ID) {
        LBSLOGE(LOCATOR, "systemAbilityId is not COMMON_EVENT_SERVICE_ID");
        return;
    }
    if (locatorEventSubscriber_ == nullptr) {
        LBSLOGE(LOCATOR, "OnRemoveSystemAbility subscribeer is nullptr");
        return;
    }
    bool result = OHOS::EventFwk::CommonEventManager::UnSubscribeCommonEvent(locatorEventSubscriber_);
    LBSLOGI(LOCATOR, "UnSubscribeCommonEvent locatorEventSubscriber_ result = %{public}d", result);
    if (countryCodeManager_ == nullptr) {
        countryCodeManager_ = DelayedSingleton<CountryCodeManager>::GetInstance();
    }
    if (countryCodeManager_ != nullptr) {
        countryCodeManager_->ReUnsubscribeEvent();
    }
}

bool LocatorAbility::Init()
{
    if (registerToAbility_) {
        return true;
    }
    LBSLOGI(LOCATOR, "LocatorAbility Init.");
    bool ret = Publish(AsObject());
    if (!ret) {
        LBSLOGE(LOCATOR, "Init add system ability failed!");
        return false;
    }

    deviceId_ = CommonUtils::InitDeviceId();
    requestManager_ = DelayedSingleton<RequestManager>::GetInstance();
    locatorHandler_ = std::make_shared<LocatorHandler>(AppExecFwk::EventRunner::Create(true));
    if (countryCodeManager_ == nullptr) {
        countryCodeManager_ = DelayedSingleton<CountryCodeManager>::GetInstance();
    }
    InitSaAbility();
    if (locatorHandler_ != nullptr) {
        locatorHandler_->SendHighPriorityEvent(EVENT_INIT_REQUEST_MANAGER, 0, RETRY_INTERVAL_OF_INIT_REQUEST_MANAGER);
    }
    RegisterAction();
    registerToAbility_ = true;
    return registerToAbility_;
}

LocatorHandler::LocatorHandler(const std::shared_ptr<AppExecFwk::EventRunner>& runner) : EventHandler(runner) {}

LocatorHandler::~LocatorHandler() {}

void LocatorHandler::ProcessEvent(const AppExecFwk::InnerEvent::Pointer& event)
{
    auto locatorAbility = DelayedSingleton<LocatorAbility>::GetInstance();
    auto requestManager = DelayedSingleton<RequestManager>::GetInstance();
    auto locationSaLoadManager = DelayedSingleton<LocationSaLoadManager>::GetInstance();
    if (locatorAbility == nullptr || requestManager == nullptr || locationSaLoadManager == nullptr) {
        LBSLOGE(LOCATOR, "GetInstance return null");
        return;
    }
    uint32_t eventId = event->GetInnerEventId();
    LBSLOGI(LOCATOR, "ProcessEvent event:%{public}d", eventId);
    switch (eventId) {
        case EVENT_UPDATE_SA: {
            if (locatorAbility != nullptr) {
                locatorAbility->UpdateSaAbilityHandler();
            }
            break;
        }
        case EVENT_RETRY_REGISTER_ACTION: {
            if (locatorAbility != nullptr) {
                locatorAbility->RegisterAction();
            }
            break;
        }
        case EVENT_INIT_REQUEST_MANAGER: {
            if (requestManager == nullptr || !requestManager->InitSystemListeners()) {
                LBSLOGE(LOCATOR, "InitSystemListeners failed");
            }
            break;
        }
        case EVENT_APPLY_REQUIREMENTS: {
            if (requestManager != nullptr) {
                requestManager->HandleRequest();
            }
            break;
        }
        case EVENT_UNLOAD_SA: {
            if (locatorAbility != nullptr) {
                locationSaLoadManager->UnloadLocationSa(LOCATION_LOCATOR_SA_ID);
            }
            break;
        }
        default:
            break;
    }
}

void LocatorAbility::InitRequestManagerMap()
{
    std::unique_lock<std::mutex> lock(requestsMutex_);
    if (requests_ != nullptr) {
#ifdef FEATURE_GNSS_SUPPORT
        std::list<std::shared_ptr<Request>> gnssList;
        requests_->insert(make_pair(GNSS_ABILITY, gnssList));
#endif
#ifdef FEATURE_NETWORK_SUPPORT
        std::list<std::shared_ptr<Request>> networkList;
        requests_->insert(make_pair(NETWORK_ABILITY, networkList));
#endif
#ifdef FEATURE_PASSIVE_SUPPORT
        std::list<std::shared_ptr<Request>> passiveList;
        requests_->insert(make_pair(PASSIVE_ABILITY, passiveList));
#endif
    }
}

std::shared_ptr<std::map<std::string, std::list<std::shared_ptr<Request>>>> LocatorAbility::GetRequests()
{
    std::unique_lock<std::mutex> lock(requestsMutex_);
    return requests_;
}

int LocatorAbility::GetActiveRequestNum()
{
    std::unique_lock<std::mutex> lock(requestsMutex_);
    int num = 0;
#ifdef FEATURE_GNSS_SUPPORT
    auto gpsListIter = requests_->find(GNSS_ABILITY);
    if (gpsListIter != requests_->end()) {
        auto list = &(gpsListIter->second);
        num += static_cast<int>(list->size());
    }
#endif
#ifdef FEATURE_NETWORK_SUPPORT
    auto networkListIter = requests_->find(NETWORK_ABILITY);
    if (networkListIter != requests_->end()) {
        auto list = &(networkListIter->second);
        num += static_cast<int>(list->size());
    }
#endif
    return num;
}

std::shared_ptr<std::map<sptr<IRemoteObject>, std::list<std::shared_ptr<Request>>>> LocatorAbility::GetReceivers()
{
    std::unique_lock<std::mutex> lock(receiversMutex_);
    return receivers_;
}

std::shared_ptr<std::map<std::string, sptr<IRemoteObject>>> LocatorAbility::GetProxyMap()
{
    std::unique_lock<std::mutex> lock(proxyMapMutex_);
    return proxyMap_;
}

void LocatorAbility::ApplyRequests()
{
    if (locatorHandler_ != nullptr) {
        locatorHandler_->SendHighPriorityEvent(EVENT_APPLY_REQUIREMENTS, 0, RETRY_INTERVAL_UNITE);
    }
}

void LocatorAbility::InitSaAbility()
{
    LBSLOGI(LOCATOR, "initSaAbility start");
    if (proxyMap_ == nullptr) {
        return;
    }
    UpdateSaAbilityHandler();
}

bool LocatorAbility::CheckSaValid()
{
    std::unique_lock<std::mutex> lock(proxyMapMutex_);
#ifdef FEATURE_GNSS_SUPPORT
    auto objectGnss = proxyMap_->find(GNSS_ABILITY);
    if (objectGnss == proxyMap_->end()) {
        LBSLOGI(LOCATOR, "gnss sa is null");
        return false;
    }
#endif
#ifdef FEATURE_NETWORK_SUPPORT
    auto objectNetwork = proxyMap_->find(NETWORK_ABILITY);
    if (objectNetwork == proxyMap_->end()) {
        LBSLOGI(LOCATOR, "network sa is null");
        return false;
    }
#endif
#ifdef FEATURE_PASSIVE_SUPPORT
    auto objectPassive = proxyMap_->find(PASSIVE_ABILITY);
    if (objectPassive == proxyMap_->end()) {
        LBSLOGI(LOCATOR, "passive sa is null");
        return false;
    }
#endif
    return true;
}

LocationErrCode LocatorAbility::UpdateSaAbility()
{
    auto event = AppExecFwk::InnerEvent::Get(EVENT_UPDATE_SA, 0);
    if (locatorHandler_ != nullptr) {
        locatorHandler_->SendHighPriorityEvent(event);
    }
    return ERRCODE_SUCCESS;
}

void LocatorAbility::UpdateSaAbilityHandler()
{
    int state = QuerySwitchState();
    LBSLOGI(LOCATOR, "update location subability enable state, switch state=%{public}d, action registered=%{public}d",
        state, isActionRegistered);
    auto locatorBackgroundProxy = DelayedSingleton<LocatorBackgroundProxy>::GetInstance();
    if (locatorBackgroundProxy == nullptr) {
        LBSLOGE(LOCATOR, "UpdateSaAbilityHandler: LocatorBackgroundProxy is nullptr");
        return;
    }
    locatorBackgroundProxy.get()->OnSaStateChange(state == ENABLED);
}

void LocatorAbility::UnloadSaAbility()
{
    if (locatorHandler_ == nullptr || countryCodeManager_ == nullptr) {
        LBSLOGE(LOCATOR, "%{public}s locatorHandler or countryCodeManager is nullptr", __func__);
        return;
    }
    locatorHandler_->RemoveTask(UNLOAD_TASK);
    if (CheckIfLocatorConnecting()) {
        return;
    }
    auto task = [this]() {
        auto instance = DelayedSingleton<LocationSaLoadManager>::GetInstance();
        if (instance == nullptr) {
            LBSLOGE(LOCATOR, "%{public}s instance is nullptr", __func__);
            return;
        }
        instance->UnloadLocationSa(LOCATION_LOCATOR_SA_ID);
    };
    if (locatorHandler_ != nullptr) {
        locatorHandler_->PostTask(task, UNLOAD_TASK, RETRY_INTERVAL_OF_UNLOAD_SA);
    }
}

bool LocatorAbility::CheckIfLocatorConnecting()
{
    return countryCodeManager_->IsCountryCodeRegistered();
}

LocationErrCode LocatorAbility::EnableAbility(bool isEnabled)
{
    LBSLOGI(LOCATOR, "EnableAbility %{public}d", isEnabled);
    int modeValue = isEnabled ? 1 : 0;
    if (modeValue == QuerySwitchState()) {
        LBSLOGD(LOCATOR, "no need to set location ability, enable:%{public}d", modeValue);
        return ERRCODE_SUCCESS;
    }
    Uri locationDataEnableUri(LOCATION_DATA_URI);
    LocationErrCode errCode = DelayedSingleton<LocationDataRdbHelper>::GetInstance()->
        SetValue(locationDataEnableUri, LOCATION_DATA_COLUMN_ENABLE, modeValue);
    if (errCode != ERRCODE_SUCCESS) {
        LBSLOGE(LOCATOR, "%{public}s: can not set state to db", __func__);
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    UpdateSaAbility();
    std::string state = isEnabled ? "enable" : "disable";
    WriteLocationSwitchStateEvent(state);
    return ERRCODE_SUCCESS;
}

LocationErrCode LocatorAbility::GetSwitchState(int& state)
{
    state = QuerySwitchState();
    return ERRCODE_SUCCESS;
}

int LocatorAbility::QuerySwitchState()
{
    int32_t state = DISABLED;
    Uri locationDataEnableUri(LOCATION_DATA_URI);
    LocationErrCode errCode = DelayedSingleton<LocationDataRdbHelper>::GetInstance()->
        GetValue(locationDataEnableUri, LOCATION_DATA_COLUMN_ENABLE, state);
    if (errCode != ERRCODE_SUCCESS) {
        LBSLOGE(LOCATOR, "%{public}s: can not query state, reset state.", __func__);
        DelayedSingleton<LocationDataRdbHelper>::GetInstance()->
            SetValue(locationDataEnableUri, LOCATION_DATA_COLUMN_ENABLE, state);
    }
    return state;
}

LocationErrCode LocatorAbility::IsLocationPrivacyConfirmed(const int type, bool& isConfirmed)
{
    return LocationConfigManager::GetInstance().GetPrivacyTypeState(type, isConfirmed);
}

LocationErrCode LocatorAbility::SetLocationPrivacyConfirmStatus(const int type, bool isConfirmed)
{
    return LocationConfigManager::GetInstance().SetPrivacyTypeState(type, isConfirmed);
}

LocationErrCode LocatorAbility::RegisterSwitchCallback(const sptr<IRemoteObject>& callback, pid_t uid)
{
    if (callback == nullptr) {
        LBSLOGE(LOCATOR, "register an invalid switch callback");
        return ERRCODE_INVALID_PARAM;
    }
    sptr<IRemoteObject::DeathRecipient> death(new (std::nothrow) SwitchCallbackDeathRecipient());
    callback->AddDeathRecipient(death);
    sptr<ISwitchCallback> switchCallback = iface_cast<ISwitchCallback>(callback);
    if (switchCallback == nullptr) {
        LBSLOGE(LOCATOR, "cast switch callback fail!");
        return ERRCODE_INVALID_PARAM;
    }
    std::unique_lock<std::mutex> lock(switchMutex_);
    switchCallbacks_->erase(uid);
    switchCallbacks_->insert(std::make_pair(uid, switchCallback));
    LBSLOGD(LOCATOR, "after uid:%{public}d register, switch callback size:%{public}s",
        uid, std::to_string(switchCallbacks_->size()).c_str());
    return ERRCODE_SUCCESS;
}

LocationErrCode LocatorAbility::UnregisterSwitchCallback(const sptr<IRemoteObject>& callback)
{
    if (callback == nullptr) {
        LBSLOGE(LOCATOR, "unregister an invalid switch callback");
        return ERRCODE_INVALID_PARAM;
    }
    sptr<ISwitchCallback> switchCallback = iface_cast<ISwitchCallback>(callback);
    if (switchCallback == nullptr) {
        LBSLOGE(LOCATOR, "cast switch callback fail!");
        return ERRCODE_INVALID_PARAM;
    }

    std::unique_lock<std::mutex> lock(switchMutex_);
    pid_t uid = -1;
    for (auto iter = switchCallbacks_->begin(); iter != switchCallbacks_->end(); iter++) {
        sptr<IRemoteObject> remoteObject = (iter->second)->AsObject();
        if (remoteObject == callback) {
            uid = iter->first;
            break;
        }
    }
    switchCallbacks_->erase(uid);
    LBSLOGD(LOCATOR, "after uid:%{public}d unregister, switch callback size:%{public}s",
        uid, std::to_string(switchCallbacks_->size()).c_str());
    return ERRCODE_SUCCESS;
}

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::SendGnssRequest(int type, MessageParcel &data, MessageParcel &reply)
{
    auto locationSaLoadManager = DelayedSingleton<LocationSaLoadManager>::GetInstance();
    if (locationSaLoadManager == nullptr) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    locationSaLoadManager->LoadLocationSa(LOCATION_GNSS_SA_ID);
    sptr<IRemoteObject> objectGnss =
            CommonUtils::GetRemoteObject(LOCATION_GNSS_SA_ID, CommonUtils::InitDeviceId());
    if (objectGnss == nullptr) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    MessageOption option;
    objectGnss->SendRequest(type, data, reply, option);
    return LocationErrCode(reply.ReadInt32());
}
#endif

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::RegisterGnssStatusCallback(const sptr<IRemoteObject>& callback, pid_t uid)
{
    LBSLOGD(LOCATOR, "uid is: %{public}d", uid);
    MessageParcel dataToStub;
    MessageParcel replyToStub;
    if (!dataToStub.WriteInterfaceToken(GnssAbilityProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    dataToStub.WriteRemoteObject(callback);
    return SendGnssRequest(static_cast<int>(GnssInterfaceCode::REG_GNSS_STATUS), dataToStub, replyToStub);
}
#endif

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::UnregisterGnssStatusCallback(const sptr<IRemoteObject>& callback)
{
    MessageParcel dataToStub;
    MessageParcel replyToStub;
    if (!dataToStub.WriteInterfaceToken(GnssAbilityProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    dataToStub.WriteRemoteObject(callback);
    return SendGnssRequest(static_cast<int>(GnssInterfaceCode::UNREG_GNSS_STATUS), dataToStub, replyToStub);
}
#endif

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::RegisterNmeaMessageCallback(const sptr<IRemoteObject>& callback, pid_t uid)
{
    MessageParcel dataToStub;
    MessageParcel replyToStub;
    if (!dataToStub.WriteInterfaceToken(GnssAbilityProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    dataToStub.WriteRemoteObject(callback);
    return SendGnssRequest(static_cast<int>(GnssInterfaceCode::REG_NMEA), dataToStub, replyToStub);
}
#endif

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::UnregisterNmeaMessageCallback(const sptr<IRemoteObject>& callback)
{
    MessageParcel dataToStub;
    MessageParcel replyToStub;
    if (!dataToStub.WriteInterfaceToken(GnssAbilityProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    dataToStub.WriteRemoteObject(callback);
    return SendGnssRequest(static_cast<int>(GnssInterfaceCode::UNREG_NMEA), dataToStub, replyToStub);
}
#endif

LocationErrCode LocatorAbility::RegisterCountryCodeCallback(const sptr<IRemoteObject>& callback, pid_t uid)
{
    if (countryCodeManager_ == nullptr) {
        LBSLOGE(LOCATOR, "RegisterCountryCodeCallback countryCodeManager_ is nullptr");
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    countryCodeManager_->RegisterCountryCodeCallback(callback, uid);
    return ERRCODE_SUCCESS;
}

LocationErrCode LocatorAbility::UnregisterCountryCodeCallback(const sptr<IRemoteObject>& callback)
{
    if (countryCodeManager_ == nullptr) {
        LBSLOGE(LOCATOR, "UnregisterCountryCodeCallback countryCodeManager_ is nullptr");
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    countryCodeManager_->UnregisterCountryCodeCallback(callback);
    return ERRCODE_SUCCESS;
}

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::RegisterCachedLocationCallback(std::unique_ptr<CachedGnssLocationsRequest>& request,
    sptr<ICachedLocationsCallback>& callback, std::string bundleName)
{
    MessageParcel dataToStub;
    MessageParcel replyToStub;
    if (!dataToStub.WriteInterfaceToken(GnssAbilityProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    dataToStub.WriteInt32(request->reportingPeriodSec);
    dataToStub.WriteBool(request->wakeUpCacheQueueFull);
    dataToStub.WriteRemoteObject(callback->AsObject());
    dataToStub.WriteString16(Str8ToStr16(bundleName));
    return SendGnssRequest(static_cast<int>(GnssInterfaceCode::REG_CACHED), dataToStub, replyToStub);
}
#endif

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::UnregisterCachedLocationCallback(sptr<ICachedLocationsCallback>& callback)
{
    MessageParcel dataToStub;
    MessageParcel replyToStub;
    if (!dataToStub.WriteInterfaceToken(GnssAbilityProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    dataToStub.WriteRemoteObject(callback->AsObject());
    return SendGnssRequest(static_cast<int>(GnssInterfaceCode::UNREG_CACHED), dataToStub, replyToStub);
}
#endif

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::GetCachedGnssLocationsSize(int& size)
{
    MessageParcel dataToStub;
    MessageParcel replyToStub;
    if (!dataToStub.WriteInterfaceToken(GnssAbilityProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    LocationErrCode errorCode =
        SendGnssRequest(static_cast<int>(GnssInterfaceCode::GET_CACHED_SIZE), dataToStub, replyToStub);
    if (errorCode == ERRCODE_SUCCESS) {
        size = replyToStub.ReadInt32();
    }
    return errorCode;
}
#endif

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::FlushCachedGnssLocations()
{
    MessageParcel dataToStub;
    MessageParcel replyToStub;
    if (!dataToStub.WriteInterfaceToken(GnssAbilityProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    return SendGnssRequest(static_cast<int>(GnssInterfaceCode::FLUSH_CACHED), dataToStub, replyToStub);
}
#endif

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::SendCommand(std::unique_ptr<LocationCommand>& commands)
{
    MessageParcel dataToStub;
    MessageParcel replyToStub;
    if (!dataToStub.WriteInterfaceToken(GnssAbilityProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    dataToStub.WriteInt32(commands->scenario);
    dataToStub.WriteString16(Str8ToStr16(commands->command));
    return SendGnssRequest(static_cast<int>(GnssInterfaceCode::SEND_COMMANDS), dataToStub, replyToStub);
}
#endif

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::AddFence(std::unique_ptr<GeofenceRequest>& request)
{
    MessageParcel dataToStub;
    MessageParcel replyToStub;
    if (!dataToStub.WriteInterfaceToken(GnssAbilityProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    dataToStub.WriteInt32(request->scenario);
    dataToStub.WriteDouble(request->geofence.latitude);
    dataToStub.WriteDouble(request->geofence.longitude);
    dataToStub.WriteDouble(request->geofence.radius);
    dataToStub.WriteDouble(request->geofence.expiration);
    return SendGnssRequest(static_cast<int>(GnssInterfaceCode::ADD_FENCE_INFO), dataToStub, replyToStub);
}
#endif

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::RemoveFence(std::unique_ptr<GeofenceRequest>& request)
{
    MessageParcel dataToStub;
    MessageParcel replyToStub;
    if (!dataToStub.WriteInterfaceToken(GnssAbilityProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    dataToStub.WriteInt32(request->scenario);
    dataToStub.WriteDouble(request->geofence.latitude);
    dataToStub.WriteDouble(request->geofence.longitude);
    dataToStub.WriteDouble(request->geofence.radius);
    dataToStub.WriteDouble(request->geofence.expiration);
    return SendGnssRequest(static_cast<int>(GnssInterfaceCode::REMOVE_FENCE_INFO), dataToStub, replyToStub);
}
#endif

LocationErrCode LocatorAbility::GetIsoCountryCode(std::shared_ptr<CountryCode>& countryCode)
{
    if (countryCodeManager_ == nullptr) {
        countryCode = nullptr;
        LBSLOGE(LOCATOR, "GetIsoCountryCode countryCodeManager_ is nullptr");
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    countryCode = countryCodeManager_->GetIsoCountryCode();
    return ERRCODE_SUCCESS;
}

#ifdef FEATURE_GNSS_SUPPORT
LocationErrCode LocatorAbility::SendLocationMockMsgToGnssSa(const sptr<IRemoteObject> obj,
    const int timeInterval, const std::vector<std::shared_ptr<Location>> &location, int msgId)
{
    if (obj == nullptr) {
        LBSLOGE(LOCATOR, "SendLocationMockMsgToGnssSa obj is nullptr");
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    std::unique_ptr<GnssAbilityProxy> gnssProxy = std::make_unique<GnssAbilityProxy>(obj);
    LocationErrCode errorCode = ERRCODE_NOT_SUPPORTED;
    if (msgId == static_cast<int>(LocatorInterfaceCode::ENABLE_LOCATION_MOCK)) {
        errorCode = gnssProxy->EnableMock();
    } else if (msgId == static_cast<int>(LocatorInterfaceCode::DISABLE_LOCATION_MOCK)) {
        errorCode = gnssProxy->DisableMock();
    } else if (msgId == static_cast<int>(LocatorInterfaceCode::SET_MOCKED_LOCATIONS)) {
        errorCode = gnssProxy->SetMocked(timeInterval, location);
    }
    return errorCode;
}
#endif

#ifdef FEATURE_NETWORK_SUPPORT
LocationErrCode LocatorAbility::SendLocationMockMsgToNetworkSa(const sptr<IRemoteObject> obj,
    const int timeInterval, const std::vector<std::shared_ptr<Location>> &location, int msgId)
{
    if (obj == nullptr) {
        LBSLOGE(LOCATOR, "SendLocationMockMsgToNetworkSa obj is nullptr");
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    std::unique_ptr<NetworkAbilityProxy> networkProxy =
        std::make_unique<NetworkAbilityProxy>(obj);
    LocationErrCode errorCode = ERRCODE_NOT_SUPPORTED;
    if (msgId == static_cast<int>(LocatorInterfaceCode::ENABLE_LOCATION_MOCK)) {
        errorCode = networkProxy->EnableMock();
    } else if (msgId == static_cast<int>(LocatorInterfaceCode::DISABLE_LOCATION_MOCK)) {
        errorCode = networkProxy->DisableMock();
    } else if (msgId == static_cast<int>(LocatorInterfaceCode::SET_MOCKED_LOCATIONS)) {
        errorCode = networkProxy->SetMocked(timeInterval, location);
    }
    return errorCode;
}
#endif

#ifdef FEATURE_PASSIVE_SUPPORT
LocationErrCode LocatorAbility::SendLocationMockMsgToPassiveSa(const sptr<IRemoteObject> obj,
    const int timeInterval, const std::vector<std::shared_ptr<Location>> &location, int msgId)
{
    if (obj == nullptr) {
        LBSLOGE(LOCATOR, "SendLocationMockMsgToNetworkSa obj is nullptr");
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    std::unique_ptr<PassiveAbilityProxy> passiveProxy =
        std::make_unique<PassiveAbilityProxy>(obj);
    LocationErrCode errorCode = ERRCODE_NOT_SUPPORTED;
    if (msgId == static_cast<int>(LocatorInterfaceCode::ENABLE_LOCATION_MOCK)) {
        errorCode = passiveProxy->EnableMock();
    } else if (msgId == static_cast<int>(LocatorInterfaceCode::DISABLE_LOCATION_MOCK)) {
        errorCode = passiveProxy->DisableMock();
    } else if (msgId == static_cast<int>(LocatorInterfaceCode::SET_MOCKED_LOCATIONS)) {
        errorCode = passiveProxy->SetMocked(timeInterval, location);
    }
    return errorCode;
}
#endif

LocationErrCode LocatorAbility::ProcessLocationMockMsg(
    const int timeInterval, const std::vector<std::shared_ptr<Location>> &location, int msgId)
{
#if !defined(FEATURE_GNSS_SUPPORT) && !defined(FEATURE_NETWORK_SUPPORT) && !defined(FEATURE_PASSIVE_SUPPORT)
    LBSLOGE(LOCATOR, "%{public}s: mock service unavailable", __func__);
    return ERRCODE_NOT_SUPPORTED;
#endif
    if (!CheckSaValid()) {
        UpdateProxyMap();
    }

    std::unique_lock<std::mutex> lock(proxyMapMutex_);
    for (auto iter = proxyMap_->begin(); iter != proxyMap_->end(); iter++) {
        auto obj = iter->second;
        if (iter->first == GNSS_ABILITY) {
#ifdef FEATURE_GNSS_SUPPORT
            SendLocationMockMsgToGnssSa(obj, timeInterval, location, msgId);
#endif
        } else if (iter->first == NETWORK_ABILITY) {
#ifdef FEATURE_NETWORK_SUPPORT
            SendLocationMockMsgToNetworkSa(obj, timeInterval, location, msgId);
#endif
        } else if (iter->first == PASSIVE_ABILITY) {
#ifdef FEATURE_PASSIVE_SUPPORT
            SendLocationMockMsgToPassiveSa(obj, timeInterval, location, msgId);
#endif
        }
    }
    return ERRCODE_SUCCESS;
}

void LocatorAbility::UpdateProxyMap()
{
    std::unique_lock<std::mutex> lock(proxyMapMutex_);
    auto locationSaLoadManager = DelayedSingleton<LocationSaLoadManager>::GetInstance();
    if (locationSaLoadManager == nullptr) {
        return;
    }
#ifdef FEATURE_GNSS_SUPPORT
    // init gnss ability sa
    locationSaLoadManager->LoadLocationSa(LOCATION_GNSS_SA_ID);
    sptr<IRemoteObject> objectGnss = CommonUtils::GetRemoteObject(LOCATION_GNSS_SA_ID, CommonUtils::InitDeviceId());
    if (objectGnss != nullptr) {
        proxyMap_->insert(make_pair(GNSS_ABILITY, objectGnss));
    } else {
        LBSLOGE(LOCATOR, "GetRemoteObject gnss sa is null");
    }
#endif
#ifdef FEATURE_NETWORK_SUPPORT
    // init network ability sa
    locationSaLoadManager->LoadLocationSa(LOCATION_NETWORK_LOCATING_SA_ID);
    sptr<IRemoteObject> objectNetwork = CommonUtils::GetRemoteObject(LOCATION_NETWORK_LOCATING_SA_ID,
        CommonUtils::InitDeviceId());
    if (objectNetwork != nullptr) {
        proxyMap_->insert(make_pair(NETWORK_ABILITY, objectNetwork));
    } else {
        LBSLOGE(LOCATOR, "GetRemoteObject network sa is null");
    }
#endif
#ifdef FEATURE_PASSIVE_SUPPORT
    // init passive ability sa
    locationSaLoadManager->LoadLocationSa(LOCATION_NOPOWER_LOCATING_SA_ID);
    sptr<IRemoteObject> objectPassive = CommonUtils::GetRemoteObject(LOCATION_NOPOWER_LOCATING_SA_ID,
        CommonUtils::InitDeviceId());
    if (objectPassive != nullptr) {
        proxyMap_->insert(make_pair(PASSIVE_ABILITY, objectPassive));
    } else {
        LBSLOGE(LOCATOR, "GetRemoteObject passive sa is null");
    }
#endif
}

LocationErrCode LocatorAbility::EnableLocationMock()
{
    int timeInterval = 0;
    std::vector<std::shared_ptr<Location>> location;
    return ProcessLocationMockMsg(timeInterval, location,
        static_cast<int>(LocatorInterfaceCode::ENABLE_LOCATION_MOCK));
}

LocationErrCode LocatorAbility::DisableLocationMock()
{
    int timeInterval = 0;
    std::vector<std::shared_ptr<Location>> location;
    return ProcessLocationMockMsg(timeInterval, location,
        static_cast<int>(LocatorInterfaceCode::DISABLE_LOCATION_MOCK));
}

LocationErrCode LocatorAbility::SetMockedLocations(
    const int timeInterval, const std::vector<std::shared_ptr<Location>> &location)
{
    return ProcessLocationMockMsg(timeInterval, location,
        static_cast<int>(LocatorInterfaceCode::SET_MOCKED_LOCATIONS));
}

LocationErrCode LocatorAbility::StartLocating(std::unique_ptr<RequestConfig>& requestConfig,
    sptr<ILocatorCallback>& callback, AppIdentity &identity)
{
#if !defined(FEATURE_GNSS_SUPPORT) && !defined(FEATURE_NETWORK_SUPPORT) && !defined(FEATURE_PASSIVE_SUPPORT)
    LBSLOGE(LOCATOR, "%{public}s: service unavailable", __func__);
    return ERRCODE_NOT_SUPPORTED;
#endif
    if (QuerySwitchState() == DISABLED) {
        ReportErrorStatus(callback, ERROR_SWITCH_UNOPEN);
    }
    if (!CheckSaValid()) {
        UpdateSaAbilityHandler();
    }
    // update offset before add request
    if (reportManager_ == nullptr || requestManager_ == nullptr) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    reportManager_->UpdateRandom();
    // generate request object according to input params
    std::shared_ptr<Request> request = std::make_shared<Request>();
    request->SetUid(identity.GetUid());
    request->SetPid(identity.GetPid());
    request->SetTokenId(identity.GetTokenId());
    request->SetFirstTokenId(identity.GetFirstTokenId());
    request->SetPackageName(identity.GetBundleName());
    request->SetRequestConfig(*requestConfig);
    request->SetLocatorCallBack(callback);
    request->SetUuid(std::to_string(CommonUtils::IntRandom(MIN_INT_RANDOM, MAX_INT_RANDOM)));
    LBSLOGI(LOCATOR, "start locating");
    requestManager_->HandleStartLocating(request);
    ReportLocationStatus(callback, SESSION_START);
    return ERRCODE_SUCCESS;
}

LocationErrCode LocatorAbility::StopLocating(sptr<ILocatorCallback>& callback)
{
#if !defined(FEATURE_GNSS_SUPPORT) && !defined(FEATURE_NETWORK_SUPPORT) && !defined(FEATURE_PASSIVE_SUPPORT)
    LBSLOGE(LOCATOR, "%{public}s: service unavailable", __func__);
    return ERRCODE_NOT_SUPPORTED;
#endif
    LBSLOGI(LOCATOR, "stop locating");
    if (requestManager_ == nullptr) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    requestManager_->HandleStopLocating(callback);
    ReportLocationStatus(callback, SESSION_STOP);
    return ERRCODE_SUCCESS;
}

LocationErrCode LocatorAbility::GetCacheLocation(std::unique_ptr<Location>& loc, AppIdentity &identity)
{
    auto lastLocation = reportManager_->GetLastLocation();
    loc = reportManager_->GetPermittedLocation(identity.GetUid(), identity.GetTokenId(),
        identity.GetFirstTokenId(), lastLocation);
    if (loc == nullptr) {
        return ERRCODE_LOCATING_FAIL;
    }
    if (fabs(loc->GetLatitude() - 0.0) > PRECISION
        && fabs(loc->GetLongitude() - 0.0) > PRECISION) {
        return ERRCODE_SUCCESS;
    }
    return ERRCODE_LOCATING_FAIL;
}

LocationErrCode LocatorAbility::ReportLocation(const std::unique_ptr<Location>& location, std::string abilityName)
{
    if (requests_ == nullptr) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    int state = DISABLED;
    LocationErrCode errorCode = GetSwitchState(state);
    if (errorCode != ERRCODE_SUCCESS) {
        return errorCode;
    }
    if (state == DISABLED) {
        LBSLOGE(LOCATOR, "location switch is off");
        return ERRCODE_SWITCH_OFF;
    }
    LBSLOGI(LOCATOR, "start report location");
    if (reportManager_->OnReportLocation(location, abilityName)) {
        return ERRCODE_SUCCESS;
    }
    return ERRCODE_SERVICE_UNAVAILABLE;
}

LocationErrCode LocatorAbility::ReportLocationStatus(sptr<ILocatorCallback>& callback, int result)
{
    int state = DISABLED;
    LocationErrCode errorCode = GetSwitchState(state);
    if (errorCode != ERRCODE_SUCCESS) {
        return errorCode;
    }
    if (state == DISABLED) {
        LBSLOGE(LOCATOR, "location switch is off");
        return ERRCODE_SWITCH_OFF;
    }
    if (reportManager_->ReportRemoteCallback(callback, ILocatorCallback::RECEIVE_LOCATION_STATUS_EVENT, result)) {
        return ERRCODE_SUCCESS;
    }
    return ERRCODE_SERVICE_UNAVAILABLE;
}

LocationErrCode LocatorAbility::ReportErrorStatus(sptr<ILocatorCallback>& callback, int result)
{
    int state = DISABLED;
    LocationErrCode errorCode = GetSwitchState(state);
    if (errorCode != ERRCODE_SUCCESS) {
        return errorCode;
    }
    if (state == DISABLED) {
        LBSLOGE(LOCATOR, "location switch is off");
        return ERRCODE_SWITCH_OFF;
    }
    if (reportManager_->ReportRemoteCallback(callback, ILocatorCallback::RECEIVE_ERROR_INFO_EVENT, result)) {
        return ERRCODE_SUCCESS;
    }
    return ERRCODE_SERVICE_UNAVAILABLE;
}

void LocatorAbility::RegisterAction()
{
    if (isActionRegistered) {
        LBSLOGI(LOCATOR, "action has already registered");
        return;
    }
    OHOS::EventFwk::MatchingSkills matchingSkills;
    matchingSkills.AddEvent(MODE_CHANGED_EVENT);
    OHOS::EventFwk::CommonEventSubscribeInfo subscriberInfo(matchingSkills);
    locatorEventSubscriber_ = std::make_shared<LocatorEventSubscriber>(subscriberInfo);

    bool result = OHOS::EventFwk::CommonEventManager::SubscribeCommonEvent(locatorEventSubscriber_);
    if (!result) {
        LBSLOGE(LOCATOR, "Failed to subscriber locator event, result = %{public}d", result);
        isActionRegistered = false;
    } else {
        LBSLOGI(LOCATOR, "success to subscriber locator event, result = %{public}d", result);
        isActionRegistered = true;
    }
}

#ifdef FEATURE_GEOCODE_SUPPORT
LocationErrCode LocatorAbility::IsGeoConvertAvailable(bool &isAvailable)
{
    MessageParcel dataParcel;
    MessageParcel replyParcel;
    if (!dataParcel.WriteInterfaceToken(GeoConvertProxy::GetDescriptor())) {
        isAvailable = false;
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    SendGeoRequest(static_cast<int>(LocatorInterfaceCode::GEO_IS_AVAILABLE), dataParcel, replyParcel);
    LocationErrCode errorCode = LocationErrCode(replyParcel.ReadInt32());
    if (errorCode == ERRCODE_SUCCESS) {
        isAvailable = replyParcel.ReadBool();
    } else {
        isAvailable = false;
    }
    return errorCode;
}
#endif

#ifdef FEATURE_GEOCODE_SUPPORT
void LocatorAbility::GetAddressByCoordinate(MessageParcel &data, MessageParcel &reply)
{
    LBSLOGI(LOCATOR, "locator_ability GetAddressByCoordinate");
    MessageParcel dataParcel;
    if (!dataParcel.WriteInterfaceToken(GeoConvertProxy::GetDescriptor())) {
        reply.WriteInt32(ERRCODE_SERVICE_UNAVAILABLE);
        return;
    }
    dataParcel.WriteDouble(data.ReadDouble()); // latitude
    dataParcel.WriteDouble(data.ReadDouble()); // longitude
    dataParcel.WriteInt32(data.ReadInt32()); // maxItems
    dataParcel.WriteInt32(data.ReadInt32()); // locale object size = 1
    dataParcel.WriteString16(data.ReadString16()); // locale.getLanguage()
    dataParcel.WriteString16(data.ReadString16()); // locale.getCountry()
    dataParcel.WriteString16(data.ReadString16()); // locale.getVariant()
    dataParcel.WriteString16(data.ReadString16()); // ""
    SendGeoRequest(static_cast<int>(LocatorInterfaceCode::GET_FROM_COORDINATE), dataParcel, reply);
}
#endif

#ifdef FEATURE_GEOCODE_SUPPORT
void LocatorAbility::GetAddressByLocationName(MessageParcel &data, MessageParcel &reply)
{
    LBSLOGI(LOCATOR, "locator_ability GetAddressByLocationName");
    MessageParcel dataParcel;
    if (!dataParcel.WriteInterfaceToken(GeoConvertProxy::GetDescriptor())) {
        reply.WriteInt32(ERRCODE_SERVICE_UNAVAILABLE);
        return;
    }
    dataParcel.WriteString16(data.ReadString16()); // description
    dataParcel.WriteDouble(data.ReadDouble()); // minLatitude
    dataParcel.WriteDouble(data.ReadDouble()); // minLongitude
    dataParcel.WriteDouble(data.ReadDouble()); // maxLatitude
    dataParcel.WriteDouble(data.ReadDouble()); // maxLongitude
    dataParcel.WriteInt32(data.ReadInt32()); // maxreplyItems
    dataParcel.WriteInt32(data.ReadInt32()); // locale object size = 1
    dataParcel.WriteString16(data.ReadString16()); // locale.getLanguage()
    dataParcel.WriteString16(data.ReadString16()); // locale.getCountry()
    dataParcel.WriteString16(data.ReadString16()); // locale.getVariant()
    dataParcel.WriteString16(data.ReadString16()); // ""
    SendGeoRequest(static_cast<int>(LocatorInterfaceCode::GET_FROM_LOCATION_NAME), dataParcel, reply);
}
#endif

#ifdef FEATURE_GEOCODE_SUPPORT
LocationErrCode LocatorAbility::SendGeoRequest(int type, MessageParcel &data, MessageParcel &reply)
{
    auto locationSaLoadManager = DelayedSingleton<LocationSaLoadManager>::GetInstance();
    if (locationSaLoadManager == nullptr) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }

    locationSaLoadManager->LoadLocationSa(LOCATION_GEO_CONVERT_SA_ID);
    sptr<IRemoteObject> remoteObject = CommonUtils::GetRemoteObject(LOCATION_GEO_CONVERT_SA_ID,
        CommonUtils::InitDeviceId());
    if (remoteObject == nullptr) {
        reply.WriteInt32(ERRCODE_SERVICE_UNAVAILABLE);
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    MessageOption option;
    remoteObject->SendRequest(type, data, reply, option);
    return ERRCODE_SUCCESS;
}
#endif

#ifdef FEATURE_GEOCODE_SUPPORT
LocationErrCode LocatorAbility::EnableReverseGeocodingMock()
{
    MessageParcel dataParcel;
    MessageParcel replyParcel;
    if (!dataParcel.WriteInterfaceToken(GeoConvertProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    SendGeoRequest(static_cast<int>(LocatorInterfaceCode::ENABLE_REVERSE_GEOCODE_MOCK), dataParcel, replyParcel);
    return LocationErrCode(replyParcel.ReadInt32());
}
#endif

#ifdef FEATURE_GEOCODE_SUPPORT
LocationErrCode LocatorAbility::DisableReverseGeocodingMock()
{
    MessageParcel dataParcel;
    MessageParcel replyParcel;
    if (!dataParcel.WriteInterfaceToken(GeoConvertProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    SendGeoRequest(static_cast<int>(LocatorInterfaceCode::DISABLE_REVERSE_GEOCODE_MOCK), dataParcel, replyParcel);
    return LocationErrCode(replyParcel.ReadInt32());
}
#endif

#ifdef FEATURE_GEOCODE_SUPPORT
LocationErrCode LocatorAbility::SetReverseGeocodingMockInfo(std::vector<std::shared_ptr<GeocodingMockInfo>>& mockInfo)
{
    MessageParcel dataParcel;
    MessageParcel replyParcel;
    if (!dataParcel.WriteInterfaceToken(GeoConvertProxy::GetDescriptor())) {
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    dataParcel.WriteInt32(mockInfo.size());
    for (size_t i = 0; i < mockInfo.size(); i++) {
        mockInfo[i]->Marshalling(dataParcel);
    }
    SendGeoRequest(static_cast<int>(LocatorInterfaceCode::SET_REVERSE_GEOCODE_MOCKINFO), dataParcel, replyParcel);
    return LocationErrCode(replyParcel.ReadInt32());
}
#endif

LocationErrCode LocatorAbility::ProxyUidForFreeze(int32_t uid, bool isProxy)
{
    LBSLOGI(LOCATOR, "Start locator proxy, uid: %{public}d, isProxy: %{public}d", uid, isProxy);
    std::unique_lock<std::mutex> lock(proxyUidsMutex_);
    if (isProxy) {
        proxyUids_.insert(uid);
    } else {
        proxyUids_.erase(uid);
    }
    return ERRCODE_SUCCESS;
}

LocationErrCode LocatorAbility::ResetAllProxy()
{
    LBSLOGI(LOCATOR, "Start locator ResetAllProxy");
    std::unique_lock<std::mutex> lock(proxyUidsMutex_);
    proxyUids_.clear();
    return ERRCODE_SUCCESS;
}

bool LocatorAbility::IsProxyUid(int32_t uid)
{
    std::unique_lock<std::mutex> lock(proxyUidsMutex_);
    return proxyUids_.find(uid) != proxyUids_.end();
}

void LocatorAbility::RegisterPermissionCallback(const uint32_t callingTokenId,
    const std::vector<std::string>& permissionNameList)
{
    std::unique_lock<std::mutex> lock(permissionMapMutex_);
    if (permissionMap_ == nullptr) {
        LBSLOGE(LOCATOR, "permissionMap is null.");
        return;
    }
    PermStateChangeScope scopeInfo;
    scopeInfo.permList = permissionNameList;
    scopeInfo.tokenIDs = {callingTokenId};
    auto callbackPtr = std::make_shared<PermissionStatusChangeCb>(scopeInfo);
    permissionMap_->erase(callingTokenId);
    permissionMap_->insert(std::make_pair(callingTokenId, callbackPtr));
    LBSLOGD(LOCATOR, "after tokenId:%{public}d register, permission callback size:%{public}s",
        callingTokenId, std::to_string(permissionMap_->size()).c_str());
    int32_t res = AccessTokenKit::RegisterPermStateChangeCallback(callbackPtr);
    if (res != SUCCESS) {
        LBSLOGE(LOCATOR, "RegisterPermStateChangeCallback failed.");
    }
}

void LocatorAbility::UnregisterPermissionCallback(const uint32_t callingTokenId)
{
    std::unique_lock<std::mutex> lock(permissionMapMutex_);
    if (permissionMap_ == nullptr) {
        LBSLOGE(LOCATOR, "permissionMap is null.");
        return;
    }
    auto iter = permissionMap_->find(callingTokenId);
    if (iter != permissionMap_->end()) {
        auto callbackPtr = iter->second;
        int32_t res = AccessTokenKit::UnRegisterPermStateChangeCallback(callbackPtr);
        if (res != SUCCESS) {
            LBSLOGE(LOCATOR, "UnRegisterPermStateChangeCallback failed.");
        }
    }
    permissionMap_->erase(callingTokenId);
    LBSLOGD(LOCATOR, "after tokenId:%{public}d unregister, permission callback size:%{public}s",
        callingTokenId, std::to_string(permissionMap_->size()).c_str());
}
} // namespace Location
} // namespace OHOS
