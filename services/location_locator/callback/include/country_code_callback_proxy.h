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

#ifndef COUNTRY_CODE_CALLBACK_PROXY_H
#define COUNTRY_CODE_CALLBACK_PROXY_H

#include "iremote_proxy.h"
#include "i_country_code_callback.h"
#include "country_code.h"

namespace OHOS {
namespace Location {
class CountryCodeCallbackProxy : public IRemoteProxy<ICountryCodeCallback> {
public:
    explicit CountryCodeCallbackProxy(const sptr<IRemoteObject> &impl);
    ~CountryCodeCallbackProxy() = default;
    void OnCountryCodeChange(const std::shared_ptr<CountryCode>& country) override;
private:
    static inline BrokerDelegator<CountryCodeCallbackProxy> delegator_;
};
} // namespace Location
} // namespace OHOS
#endif // COUNTRY_CODE_CALLBACK_PROXY_H