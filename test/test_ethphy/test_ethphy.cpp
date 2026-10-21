#include <unity.h>
// As on the original ESP32, so the translation is built.
#define CONFIG_ETH_USE_ESP32_EMAC 1
#include "EthPhy.h"

void setUp() {}
void tearDown() {}

static eth_phy_type_t translate(eth_phy_id_t id) {
  eth_phy_type_t type = ETH_PHY_MAX;
  TEST_ASSERT_TRUE(ethPhyType(static_cast<uint8_t>(id), type));
  return type;
}

/*********************************************************************
 * Stored ids
 ********************************************************************/
static void test_ids_match_stored_values() {
  // NVS, backups and the web UI hold these numbers.  Changing one moves
  // every existing Ethernet board to another chip.
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(eth_phy_id_t::LAN8720));
  TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(eth_phy_id_t::TLK110));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(eth_phy_id_t::RTL8201));
  TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(eth_phy_id_t::DP83848));
  TEST_ASSERT_EQUAL_UINT8(4, static_cast<uint8_t>(eth_phy_id_t::DM9051));
  TEST_ASSERT_EQUAL_UINT8(5, static_cast<uint8_t>(eth_phy_id_t::KSZ8041));
  TEST_ASSERT_EQUAL_UINT8(6, static_cast<uint8_t>(eth_phy_id_t::KSZ8081));
}

/*********************************************************************
 * ethPhyType
 ********************************************************************/
static void test_translates_each_rmii_chip_by_name() {
  TEST_ASSERT_EQUAL(ETH_PHY_LAN8720, translate(eth_phy_id_t::LAN8720));
  TEST_ASSERT_EQUAL(ETH_PHY_TLK110, translate(eth_phy_id_t::TLK110));
  TEST_ASSERT_EQUAL(ETH_PHY_RTL8201, translate(eth_phy_id_t::RTL8201));
  TEST_ASSERT_EQUAL(ETH_PHY_DP83848, translate(eth_phy_id_t::DP83848));
  TEST_ASSERT_EQUAL(ETH_PHY_KSZ8041, translate(eth_phy_id_t::KSZ8041));
  TEST_ASSERT_EQUAL(ETH_PHY_KSZ8081, translate(eth_phy_id_t::KSZ8081));
}
static void test_lan8720_is_not_the_framework_zero() {
  // The default board stores 0.  Arduino 3.x's 0 is ETH_PHY_GENERIC.
  TEST_ASSERT_NOT_EQUAL(0, translate(eth_phy_id_t::LAN8720));
}
static void test_rejects_dm9051() {
  eth_phy_type_t type = ETH_PHY_MAX;
  TEST_ASSERT_FALSE(ethPhyType(static_cast<uint8_t>(eth_phy_id_t::DM9051), type));
}
static void test_rejects_unknown_ids() {
  eth_phy_type_t type = ETH_PHY_MAX;
  TEST_ASSERT_FALSE(ethPhyType(7, type));
  TEST_ASSERT_FALSE(ethPhyType(255, type));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ids_match_stored_values);
  RUN_TEST(test_translates_each_rmii_chip_by_name);
  RUN_TEST(test_lan8720_is_not_the_framework_zero);
  RUN_TEST(test_rejects_dm9051);
  RUN_TEST(test_rejects_unknown_ids);
  return UNITY_END();
}
