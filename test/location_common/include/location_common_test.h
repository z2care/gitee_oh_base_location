/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#ifndef LOCATION_COMMON_TEST_H
#define LOCATION_COMMON_TEST_H

#include <gtest/gtest.h>

#include "message_parcel.h"
#ifdef FEATURE_GEOCODE_SUPPORT
#include "geo_address.h"
#endif

namespace OHOS {
namespace Location {
class LocationCommonTest : public testing::Test {
public:
    void SetUp();
    void TearDown();
#ifdef FEATURE_GEOCODE_SUPPORT
    void SetGeoAddress(std::unique_ptr<GeoAddress>& geoAddress);
    void VerifyGeoAddressReadFromParcel(std::unique_ptr<GeoAddress>& geoAddress);
    void VerifyGeoAddressMarshalling(MessageParcel& newParcel);
#endif
    void VerifyLocationMarshalling(MessageParcel& newParcel);
};
} // namespace Location
} // namespace OHOS
#endif // LOCATION_COMMON_TEST_H