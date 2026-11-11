#pragma once

namespace asha
{

// Minimal ATT/GATT server: GAP, GATT (Service Changed) and Device Information
// (including the PnP ID).
//
// The adapter is a GATT client (central), but hearing aids also query the
// central. The Starkey aids, for example, send an ATT "Read By Type" request
// for the PnP ID (0x2A50) right after the connection. Without a GATT server
// that request is never answered, the 30 s ATT transaction timeout expires
// and the aid drops the link (reason 0x13).
//
// Call once from bt_main(), after l2cap_init() and gatt_client_init(), and
// before hci_power_control(HCI_POWER_ON).
void gatt_server_init();

} // namespace asha
