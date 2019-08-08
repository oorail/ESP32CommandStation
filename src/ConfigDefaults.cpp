/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2019 Mike Dunston

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses
**********************************************************************/

#include <utils/constants.hxx>

///////////////////////////////////////////////////////////////////////////////
// Enabling this will print all RailCom packet data as it arrives at the hub.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST_FALSE(enable_railcom_packet_dump);

///////////////////////////////////////////////////////////////////////////////
// This flag will clear the stored configuration data causing the command
// station to regenerate the configuration from scratch. This is usually not
// necessary.
///////////////////////////////////////////////////////////////////////////////
#if !defined(ESP32_FORCE_FACTORY_RESET_ON_STARTUP) || \
    !ESP32_FORCE_FACTORY_RESET_ON_STARTUP
DEFAULT_CONST_FALSE(cs_force_factory_reset);
#else
DEFAULT_CONST_TRUE(cs_force_factory_reset);
#endif

///////////////////////////////////////////////////////////////////////////////
// This flag will force a factory reset by removing the LCC_CDI_FILE and
// LCC_CONFIG_FILE before starting the OpenMRN stack. This should not normally
// be required.
///////////////////////////////////////////////////////////////////////////////
#if !defined(LCC_FORCE_FACTORY_RESET_ON_STARTUP) || \
    !LCC_FORCE_FACTORY_RESET_ON_STARTUP
DEFAULT_CONST_FALSE(lcc_force_factory_reset);
#else
DEFAULT_CONST_TRUE(lcc_force_factory_reset);
#endif

///////////////////////////////////////////////////////////////////////////////
// This is the number of pending dcc::Packet objects that the RMT driver will
// allow to be queued for outbound delivery.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST(rmt_packet_queue_ops, 10);
DEFAULT_CONST(rmt_packet_queue_prog, 5);
