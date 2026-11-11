#pragma once

namespace asha
{

// Bluetooth Classic probe, used only by the pico_asha_classic firmware variant.
//
// The probe does not stream audio. It reports (through the log events shown
// in the control application) which Bluetooth Classic devices answer to an
// inquiry, their class of device and name, and which audio related services
// (A2DP, HFP, HSP, AVRCP, SPP) they publish over SDP.

// Register the HCI event handler. Call before hci_power_control().
void classic_probe_init();

// Start the periodic inquiry. Call once the HCI state is HCI_STATE_WORKING.
void classic_probe_start();

} // namespace asha
