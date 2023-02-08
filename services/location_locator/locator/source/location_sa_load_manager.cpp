/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#include "location_sa_load_manager.h"

#include "if_system_ability_manager.h"
#include "iservice_registry.h"
#include "system_ability_definition.h"

#include "location_log.h"

namespace OHOS {
namespace Location {
DECLARE_SINGLE_INSTANCE_IMPLEMENT(LocationSaLoadManager);

LocationErrCode LocationSaLoadManager::LoadLocationSa(int32_t systemAbilityId)
{
    LBSLOGI(LOCATOR, "%{public}s enter, systemAbilityId = [%{public}d] loading", __func__, systemAbilityId);
    sptr<ISystemAbilityManager> samgr =
        SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (samgr == nullptr) {
        LBSLOGE(LOCATOR, "%{public}s: get system ability manager failed!", __func__);
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    auto locationSaLoadCallback = sptr<LocationSaLoadCallback>(new LocationSaLoadCallback());
    int32_t ret = samgr->LoadSystemAbility(systemAbilityId, locationSaLoadCallback);
    if (ret != ERR_OK) {
        LBSLOGE(LOCATOR, "%{public}s: Failed to load system ability, SA Id = [%{public}d], ret = [%{public}d].",
            __func__, systemAbilityId, ret);
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    return ERRCODE_SUCCESS;
}

LocationErrCode LocationSaLoadManager::UnloadLocationSa(int32_t systemAbilityId)
{
    LBSLOGI(LOCATOR, "%{public}s enter, systemAbilityId = [%{public}d] unloading", __func__, systemAbilityId);
    sptr<ISystemAbilityManager> samgr =
        SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (samgr == nullptr) {
        LBSLOGE(LOCATOR, "%{public}s: get system ability manager failed!", __func__);
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    int32_t ret = samgr->UnloadSystemAbility(systemAbilityId);
    if (ret != ERR_OK) {
        LBSLOGE(LOCATOR, "%{public}s: Failed to unload system ability, SA Id = [%{public}d], ret = [%{public}d].",
            __func__, systemAbilityId, ret);
        return ERRCODE_SERVICE_UNAVAILABLE;
    }
    return ERRCODE_SUCCESS;
}

void LocationSaLoadCallback::OnLoadSystemAbilitySuccess(
    int32_t systemAbilityId, const sptr<IRemoteObject> &remoteObject)
{
    LBSLOGI(LOCATOR, "LocationSaLoadManager Load SA success, systemAbilityId = [%{public}d]", systemAbilityId);
}

void LocationSaLoadCallback::OnLoadSystemAbilityFail(int32_t systemAbilityId)
{
    LBSLOGI(LOCATOR, "LocationSaLoadManager Load SA failed, systemAbilityId = [%{public}d]", systemAbilityId);
}
}; // namespace Location
}; // namespace OHOS