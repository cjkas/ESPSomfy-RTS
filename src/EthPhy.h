#ifndef ETH_PHY_H
#define ETH_PHY_H
#include <stdint.h>
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif

// Ethernet needs the RMII MAC, which only the original ESP32 has.  The C3, S3
// and C6 build without it.
#if defined(CONFIG_ETH_USE_ESP32_EMAC) && CONFIG_ETH_USE_ESP32_EMAC
#define HAS_ETHERNET 1
#else
#define HAS_ETHERNET 0
#endif

// PHY chip ids as stored in NVS and backups and sent to the web UI.  They are
// Arduino 2.x's eth_phy_type_t values, frozen: Arduino 3.x renumbered that
// enum, so an id is translated by name when Ethernet starts, never cast.
enum class eth_phy_id_t : uint8_t {
  LAN8720 = 0,
  TLK110 = 1,
  RTL8201 = 2,
  DP83848 = 3,
  DM9051 = 4, // An SPI chip.  The RMII MAC can't drive it.
  KSZ8041 = 5,
  KSZ8081 = 6
};

#if HAS_ETHERNET
#include <ETH.h>
// Sets type to the framework's PHY for a stored id.  False for an id the RMII
// MAC can't drive.
inline bool ethPhyType(uint8_t id, eth_phy_type_t &type) {
  switch(static_cast<eth_phy_id_t>(id)) {
    case eth_phy_id_t::LAN8720: type = ETH_PHY_LAN8720; return true;
    case eth_phy_id_t::TLK110: type = ETH_PHY_TLK110; return true;
    case eth_phy_id_t::RTL8201: type = ETH_PHY_RTL8201; return true;
    case eth_phy_id_t::DP83848: type = ETH_PHY_DP83848; return true;
    case eth_phy_id_t::KSZ8041: type = ETH_PHY_KSZ8041; return true;
    case eth_phy_id_t::KSZ8081: type = ETH_PHY_KSZ8081; return true;
    default: return false;
  }
}
#endif
#endif
