/**
 * @file test_smoke.cpp
 * @brief Smoke test — verifies DOPE library links and basic init works.
 */

#include <gtest/gtest.h>
#include "dope/dope_api.h"
#include "dope/dope_types.h"

TEST(Smoke, BCE_InitAndModeIsIdle)
{
    BCE_Init();
    EXPECT_EQ(BCE_GetMode(), BCE_Mode::IDLE);
}

TEST(Smoke, BCE_FaultsAfterUpdateWithNoSensors)
{
    BCE_Init();

    // Feed an empty SensorFrame — no valid sensor data
    SensorFrame frame{};
    BCE_Update(&frame);

    // No valid range should always assert NO_RANGE after update.
    EXPECT_NE(BCE_GetFaultFlags() & BCE_Fault::NO_RANGE, 0u);
}
