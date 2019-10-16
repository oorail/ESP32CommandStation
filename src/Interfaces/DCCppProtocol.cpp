/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2017-2019 Mike Dunston

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

The DCC++ protocol specification is
COPYRIGHT (c) 2013-2016 Gregg E. Berman
and has been adapter for use in ESP32 COMMAND STATION.

**********************************************************************/

#include "ESP32CommandStation.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>

vector<unique_ptr<DCCPPProtocolCommand>> registeredCommands;

// <e> command handler, this command will clear all stored Turnouts, Outputs,
// Sensors and S88 Sensors (if enabled) after sending this command. Note, when
// running with the PCB configuration only turnouts will be cleared.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(ConfigErase, "e")
DCC_PROTOCOL_COMMAND_HANDLER(ConfigErase,
[](const vector<string> arguments)
{
  turnoutManager->clear();
#if ENABLE_SENSORS
  SensorManager::clear();
  SensorManager::store();
#if S88_ENABLED
  S88BusManager::clear();
  S88BusManager::store();
#endif
#endif
#if ENABLE_OUTPUTS
  OutputManager::clear();
  OutputManager::store();
#endif
  return COMMAND_SUCCESSFUL_RESPONSE;
})

// <E> command handler, this command stores all currently defined Turnouts,
// Sensors, S88 Sensors (if enabled) and Outputs to persistent storage for use
// by the Command Station in subsequent startups. Note, when running with the
// PCB configuration only turnouts will be stored.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(ConfigStore, "E")
DCC_PROTOCOL_COMMAND_HANDLER(ConfigStore,
[](const vector<string> arguments)
{
  return StringPrintf("<e %d %d %d>"
                    , turnoutManager->getTurnoutCount()
#if ENABLE_SENSORS
                    , SensorManager::store()
#if S88_ENABLED
                    + S88BusManager::store()
#endif
#else
                    , 0
#endif
#if ENABLE_OUTPUTS
                    , OutputManager::store()
#else
                    , 0
#endif
    );
})

// <R {CV} {CALLBACK} {CALLBACK-SUB}> command handler, this command attempts
// to read a CV value from the PROGRAMMING track. The returned value will be
// the actual CV value or -1 when there is a failure reading or verifying the
// CV.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(ReadCVCommand, "R")
DCC_PROTOCOL_COMMAND_HANDLER(ReadCVCommand,
[](const vector<string> arguments)
{
  uint16_t cv = std::stoi(arguments[0]);
  uint16_t callback = std::stoi(arguments[1]);
  uint16_t callbackSub = std::stoi(arguments[2]);
  int16_t value = readCV(cv);
  return StringPrintf("<r%d|%d|%d %d>",
    callback,
    callbackSub,
    cv,
    value);
})

// <W {CV} {VALUE} {CALLBACK} {CALLBACK-SUB}> command handler, this command
// attempts to write a CV value on the PROGRAMMING track. The returned value
// is either the actual CV value written or -1 if there is a failure writing or
// verifying the CV value.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(WriteCVByteProgCommand, "W")
DCC_PROTOCOL_COMMAND_HANDLER(WriteCVByteProgCommand,
[](const vector<string> arguments)
{
  uint16_t cv = std::stoi(arguments[0]);
  int16_t value = std::stoi(arguments[1]);
  uint16_t callback = std::stoi(arguments[2]);
  uint16_t callbackSub = std::stoi(arguments[3]);
  if (!writeProgCVByte(cv, value))
  {
    LOG_ERROR("[PROG] Failed to write CV %d as %d", cv, value);
    value = -1;
  }
  return StringPrintf("<r%d|%d|%d %d>", callback, callbackSub, cv, value);
})

// <W {CV} {BIT} {VALUE} {CALLBACK} {CALLBACK-SUB}> command handler, this
// command attempts to write a single bit value for a CV on the PROGRAMMING
// track. The returned value is either the actual bit value of the CV or -1 if
// there is a failure writing or verifying the CV value.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(WriteCVBitProgCommand, "B")
DCC_PROTOCOL_COMMAND_HANDLER(WriteCVBitProgCommand,
[](const vector<string> arguments)
{
  int cv = std::stoi(arguments[0]);
  uint8_t bit = std::stoi(arguments[1]);
  int8_t value = std::stoi(arguments[2]);
  uint16_t callback = std::stoi(arguments[3]);
  uint16_t callbackSub = std::stoi(arguments[4]);
  if (!writeProgCVBit(cv, bit, value))
  {
    LOG_ERROR("[PROG] Failed to write CV %d BIT %d as %d", cv, bit, value);
    value = -1;
  }
  return StringPrintf("<r%d|%d|%d %d %d>", callback, callbackSub, cv, bit
                    , value);
})

// <w {LOCO} {CV} {VALUE}> command handler, this command sends a CV write packet
// on the MAIN OPERATIONS track for a given LOCO. No verification is attempted.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(WriteCVByteOpsCommand, "w")
DCC_PROTOCOL_COMMAND_HANDLER(WriteCVByteOpsCommand,
[](const vector<string> arguments)
{
  writeOpsCVByte(std::stoi(arguments[0]), std::stoi(arguments[1])
               , std::stoi(arguments[2]));
  return COMMAND_SUCCESSFUL_RESPONSE;
})

// <w {LOCO} {CV} {BIT} {VALUE}> command handler, this command sends a CV bit
// write packet on the MAIN OPERATIONS track for a given LOCO. No verification
// is attempted.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(WriteCVBitOpsCommand, "b")
DCC_PROTOCOL_COMMAND_HANDLER(WriteCVBitOpsCommand,
[](const vector<string> arguments)
{
  writeOpsCVBit(std::stoi(arguments[0]), std::stoi(arguments[1])
              , std::stoi(arguments[2]), arguments[3][0] == '1');
  return COMMAND_SUCCESSFUL_RESPONSE;
})

string convert_loco_to_dccpp_state(TrainImpl *impl, size_t id)
{
  dcc::SpeedType speed(impl->get_speed());
  return StringPrintf("<T %d %d %d>", id, (speed.get_dcc_128() & 0x7F)
                    , speed.direction() == dcc::SpeedType::FORWARD);
}

// <s> command handler, this command sends the current status for all parts of
// the ESP32 Command Station. JMRI uses this command as a keep-alive heartbeat
// command.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(StatusCommand, "s")
DCC_PROTOCOL_COMMAND_HANDLER(StatusCommand,
[](const vector<string> arguments)
{
  wifi_mode_t mode;
  string status = StringPrintf("<iDCC++ %s: V-%s / %s %s>"
                , PROJECT_NAME
                , PROJECT_VER
                , __DATE__
                , __TIME__);
  status += trackSignal->get_state_for_dccpp();
  for (size_t id = 0; id < trainNodes->size(); id++)
  {
    auto nodeid = trainNodes->get_train_node_id(id);
    if (nodeid)
    {
      auto impl = trainNodes->get_train_impl(nodeid);
      status += convert_loco_to_dccpp_state(impl, id);
    }
  }
  status += turnoutManager->get_state_for_dccpp();
#if ENABLE_OUTPUTS
  status += OutputManager::get_state_for_dccpp();
#endif
  if (esp_wifi_get_mode(&mode) == ESP_OK)
  {
    tcpip_adapter_ip_info_t ip_info;
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)
    {
      if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info) == ESP_OK)
      {
        status += StringPrintf("<N1: " IPSTR ">", IP2STR(&ip_info.ip));
      }
      if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info) == ESP_OK)
      {
        status += StringPrintf("<N1: " IPSTR ">", IP2STR(&ip_info.ip));
      }
    }
    else if (mode != WIFI_MODE_NULL &&
             tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info) == ESP_OK)
    {
      status += StringPrintf("<N1: " IPSTR ">", IP2STR(&ip_info.ip));
    }
  }
  return status;
})

// <F> command handler, this command sends the current free heap space as response.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(FreeHeapCommand, "F")
DCC_PROTOCOL_COMMAND_HANDLER(FreeHeapCommand,
[](const vector<string> arguments)
{
  return StringPrintf("<f %d>", os_get_free_heap());
})

// <estop> command handler, this command sends an estop packet to all active
// locomotives.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(EStopCommand, "estop")
DCC_PROTOCOL_COMMAND_HANDLER(EStopCommand,
[](const vector<string> arguments)
{
  Singleton<esp32cs::EStopHandler>::instance()->set_state(true);
  return COMMAND_SUCCESSFUL_RESPONSE;
})

DECLARE_DCC_PROTOCOL_COMMAND_CLASS(CurrentDrawCommand, "c")
DCC_PROTOCOL_COMMAND_HANDLER(CurrentDrawCommand,
[](const vector<string> arguments)
{
  return trackSignal->get_state_for_dccpp();
})

DECLARE_DCC_PROTOCOL_COMMAND_CLASS(PowerOnCommand, "1")
DCC_PROTOCOL_COMMAND_HANDLER(PowerOnCommand,
[](const vector<string> arguments)
{
  trackSignal->enable_ops_output();
  // hardcoded response since enable/disable is deferred until the next
  // check interval.
  return "<p1 OPS>";
})

DECLARE_DCC_PROTOCOL_COMMAND_CLASS(PowerOffCommand, "0")
DCC_PROTOCOL_COMMAND_HANDLER(PowerOffCommand,
[](const vector<string> arguments)
{
  trackSignal->disable_ops_output();
  // hardcoded response since enable/disable is deferred until the next
  // check interval.
  return "<p0 OPS>";
})

// <t {REGISTER} {LOCO} {SPEED} {DIRECTION}> command handler, this command
// converts the provided locomotive control command into a compatible DCC
// locomotive control packet.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(ThrottleCommandAdapter, "t")
DCC_PROTOCOL_COMMAND_HANDLER(ThrottleCommandAdapter,
[](const vector<string> arguments)
{
  int reg_num = std::stoi(arguments[0]);
  uint16_t loco_addr = std::stoi(arguments[1]);
  uint8_t req_speed = std::stoi(arguments[2]);
  uint8_t req_dir = std::stoi(arguments[3]);

  TrainImpl *impl =
    trainNodes->get_train_impl(commandstation::DccMode::DCC_128_LONG_ADDRESS
                             , loco_addr);
  dcc::SpeedType speed(impl->get_speed());
  speed.set_dcc_128(req_speed);
  speed.set_direction(req_dir ? dcc::SpeedType::FORWARD : dcc::SpeedType::REVERSE);
  impl->set_speed(speed);
  return convert_loco_to_dccpp_state(impl, reg_num);
});

// <tex {LOCO} {SPEED} {DIRECTION}> command handler, this command
// converts the provided locomotive control command into a compatible DCC
// locomotive control packet.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(ThrottleExCommandAdapter, "tex")
DCC_PROTOCOL_COMMAND_HANDLER(ThrottleExCommandAdapter,
[](const vector<string> arguments)
{
  uint16_t loco_addr = std::stoi(arguments[0]);
  int8_t req_speed = std::stoi(arguments[1]);
  int8_t req_dir = std::stoi(arguments[2]);

  TrainImpl *impl =
    trainNodes->get_train_impl(commandstation::DccMode::DCC_128_LONG_ADDRESS
                             , loco_addr);
  dcc::SpeedType speed(impl->get_speed());
  if (req_speed >= 0)
  {
    speed.set_dcc_128(req_speed);
  }
  if (req_dir >= 0)
  {
    speed.set_direction(req_dir ? dcc::SpeedType::FORWARD : dcc::SpeedType::REVERSE);
  }
  impl->set_speed(speed);
  return convert_loco_to_dccpp_state(impl, 0);
})

// <f {LOCO} {BYTE} [{BYTE2}]> command handler, this command converts a
// locomotive function update into a compatible DCC function control packet.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(FunctionCommandAdapter, "f")
DCC_PROTOCOL_COMMAND_HANDLER(FunctionCommandAdapter,
[](const vector<string> arguments)
{
  uint16_t loco_addr = std::stoi(arguments[0]);
  uint8_t func_byte = std::stoi(arguments[1]);
  uint8_t first{1};
  uint8_t last{4};
  uint8_t bits{func_byte};

  auto train =
    trainNodes->get_train_impl(commandstation::DccMode::DCC_128_LONG_ADDRESS
                             , loco_addr);
  // check this is a request for functions F13-F28
  if(arguments.size() > 2)
  {
    bits = std::stoi(arguments[2]);
    if((func_byte & 0xDE) == 0xDE)
    {
      first = 13;
      last = 20;
    }
    else
    {
      first = 21;
      last = 28;
    }
  }
  else
  {
    // this is a request for functions FL,F1-F12
    // for safety this guarantees that first nibble of function byte will always
    // be of binary form 10XX which should always be the case for FL,F1-F12
    if((func_byte & 0xB0) == 0xB0)
    {
      first = 5;
      last = 8;
    }
    else if((func_byte & 0xA0) == 0xA0)
    {
      first = 9;
      last = 12;
    }
    else
    {
      train->set_fn(0, func_byte & BIT(4));
    }
  }
  for(uint8_t id = first; id <= last; id++)
  {
    train->set_fn(id, bits & BIT(id - first));
  }
  return COMMAND_NO_RESPONSE;
});

// <fex {LOCO} {FUNC} {STATE}]> command handler, this command converts a
// locomotive function update into a compatible DCC function control packet.
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(FunctionExCommandAdapter, "fex")
DCC_PROTOCOL_COMMAND_HANDLER(FunctionExCommandAdapter,
[](const vector<string> arguments)
{
  int loco_addr = std::stoi(arguments[0]);
  int function = std::stoi(arguments[1]);
  int state = std::stoi(arguments[2]);
  trainNodes->get_train_impl(commandstation::DccMode::DCC_128_LONG_ADDRESS
                           , loco_addr)->set_fn(function, state);
  return COMMAND_NO_RESPONSE;
});

// wrapper to handle the following command structures:
// CREATE: <C {ID} {LEAD LOCO} {TRAIL LOCO}  [{OTHER LOCO}]>
// DELETE: <C {ID} {LOCO}>
// DELETE: <C {ID}>
// QUERY : <C 0 {LOCO}>
// SHOW  : <C>
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(ConsistCommandAdapter, "C")
DCC_PROTOCOL_COMMAND_HANDLER(ConsistCommandAdapter,
[](const vector<string> arguments)
{
  // TODO: reimplement
  /*
  if (arguments.empty())
  {
    return locoManager->getConsistStateAsDCCpp();
  }
  else if (arguments.size() == 1 &&
           locoManager->removeLocomotiveConsist(std::stoi(arguments[1])))
  {
    return COMMAND_SUCCESSFUL_RESPONSE;
  }
  else if (arguments.size() == 2)
  {
    int8_t consistAddress = std::stoi(arguments[0]);
    uint16_t locomotiveAddress = std::stoi(arguments[1]);
    if (consistAddress == 0)
    {
      // query which consist loco is in
      auto consist = locoManager->getConsistForLoco(locomotiveAddress);
      if (consist)
      {
        return StringPrintf("<V %d %d>",
          consist->legacy_address() * consist->isDecoderAssistedConsist() ? -1 : 1,
          locomotiveAddress);
      }
    }
    else
    {
      // remove loco from consist
      auto consist = locoManager->getConsistByID(consistAddress);
      if (consist->isAddressInConsist(locomotiveAddress))
      {
        consist->removeLocomotive(locomotiveAddress);
        return COMMAND_SUCCESSFUL_RESPONSE;
      }
    }
    // if we get here either the query or remove failed
    return COMMAND_FAILED_RESPONSE;
  }
  else if (arguments.size() >= 3)
  {
    // create or update consist
    uint16_t consistAddress = std::stoi(arguments[0]);
    auto consist = locoManager->getConsistByID(consistAddress);
    if (consist)
    {
      // existing consist, need to update
      consist->releaseLocomotives();
    }
    else
    {
      // verify if all provided locos are not already in a consist
      for(int index = 1; index < arguments.size(); index++)
      {
        int32_t locomotiveAddress = std::stoi(arguments[index]);
        if(locoManager->isAddressInConsist(abs(locomotiveAddress)))
        {
          LOG_ERROR("[Consist] Locomotive %d is already in a consist.", abs(locomotiveAddress));
          return COMMAND_FAILED_RESPONSE;
        }
      }
      consist = locoManager->createLocomotiveConsist(consistAddress);
      if(!consist)
      {
        LOG_ERROR("[Consist] Unable to create new Consist");
        return COMMAND_FAILED_RESPONSE;
      }
    }
    // add locomotives to consist
    for(int index = 1; index < arguments.size(); index++)
    {
      int32_t locomotiveAddress = std::stoi(arguments[index]);
      consist->addLocomotive(abs(locomotiveAddress), locomotiveAddress > 0,
        index - 1);
    }
    return COMMAND_SUCCESSFUL_RESPONSE;
  }
  */
  return COMMAND_FAILED_RESPONSE;
})

/*
  <T ID BOARD INDEX>:          creates a new turnout ID, with specified BOARD
                               and INDEX. If turnout ID already exists, it is
                               updated with specificed BOARD and INDEX
      returns: <O> if successful and <X> if unsuccessful (ie: out of memory)

  <T ID>:                      deletes definition of turnout ID
      returns: <O> if successful and <X> if unsuccessful (ie: ID does not exist)

  <T>:                         lists all defined turnouts
      returns: <H ID ADDRESS SUBADDRESS THROW> for each defined turnout or <X>
               if no turnouts defined
  <T ID THROW>:                sets turnout ID to either the "thrown" or
                               "unthrown" position
      returns: <H ID THROW>, or <X> if turnout ID does not exist
where
  ID:         the numeric ID (0-32767) of the turnout to control
  BOARD:      the primary address of the decoder controlling this turnout
              (0-511)
  INDEX:      the subaddress of the decoder controlling this turnout (0-3)
  THROW:      0 (unthrown) or 1 (thrown)
*/
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(TurnoutCommandAdapter, "T")
DCC_PROTOCOL_COMMAND_HANDLER(TurnoutCommandAdapter,
[](const vector<string> arguments)
{
  if (arguments.empty())
  {
    // list all turnouts
    return turnoutManager->get_state_for_dccpp();
  }
  else
  {
    uint16_t turnoutID = std::stoi(arguments[0]);
    if (arguments.size() == 1 &&
        turnoutManager->removeByID(turnoutID))
    {
      // delete turnout
      return COMMAND_SUCCESSFUL_RESPONSE;
    }
    else if (arguments.size() == 2)
    {
      // throw turnout
      return turnoutManager->setByID(turnoutID, arguments[1][0] == '1');
    }
    else if (arguments.size() == 3)
    {
      // create/update turnout
      turnoutManager->createOrUpdate(turnoutID, std::stoi(arguments[1])
                                   , std::stoi(arguments[2]));
      return COMMAND_SUCCESSFUL_RESPONSE;
    }
  }
  return COMMAND_FAILED_RESPONSE;
})

/*
  <Tex ID>:              Toggle turnout by ID.
  <Tex ID ADDRESS TYPE>: Creates a Turnout by DCC address
  <Tex ADDRESS TYPE>:    Create a Turnout by DCC address with automatic
                         assignment of ID
  all will return : <O> if successful and <X> if unsuccessful.

where
  ID:         the numeric ID (0-32767) of the turnout to control
  ADDRESS:    the DCC decoder address for the turnout
  TYPE:       turnout type:
              0 : LEFT
              1 : RIGHT
              2 : WYE
              3 : MULTI
*/
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(TurnoutExCommandAdapter, "Tex")
DCC_PROTOCOL_COMMAND_HANDLER(TurnoutExCommandAdapter,
[](const vector<string> arguments)
{
  if (!arguments.empty())
  {
    if (std::stoi(arguments[0]) >= 0)
    {
      if (arguments.size() == 1)
      {
        return turnoutManager->toggleByID(std::stoi(arguments[0]));
      }
      else if (arguments.size() == 3 &&
               turnoutManager->createOrUpdate(std::stoi(arguments[0])
                                            , std::stoi(arguments[1])
                                            , -1
                                            , (TurnoutType)std::stoi(arguments[2])))
      {
        return COMMAND_SUCCESSFUL_RESPONSE;
      }
    }
    else
    {
      auto turnout = turnoutManager->getTurnoutByAddress(std::stoi(arguments[1]));
      if (turnout)
      {
        turnout->setType((TurnoutType)std::stoi(arguments[2]));
        return COMMAND_SUCCESSFUL_RESPONSE;
      }
      else if (turnoutManager->createOrUpdate(turnoutManager->getTurnoutCount() + 1
                                            , std::stoi(arguments[1])
                                            , -1
                                            , (TurnoutType)std::stoi(arguments[2])))
      {
        return COMMAND_SUCCESSFUL_RESPONSE;
      }
    }
  }
  return COMMAND_FAILED_RESPONSE;
})

/*
 <a BOARD INDEX THROW>: Throws a turnout (accessory decoder)
      returns: <H ID THROW>

Note: When this is received a Turnout will be created based on the decoded
DCC address for the accessory decoder.
where
  BOARD:      the primary address of the decoder controlling this turnout
              (0-511)
  INDEX:      the subaddress of the decoder controlling this turnout (0-3)
  THROW:      0 (unthrown) or 1 (thrown)
*/
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(AccessoryCommand, "a")
DCC_PROTOCOL_COMMAND_HANDLER(AccessoryCommand,
[](const vector<string> arguments)
{
  return turnoutManager->setByAddress(
      decodeDCCAccessoryAddress(std::stoi(arguments[0])
                              , std::stoi(arguments[1]))
    , std::stoi(arguments[2])
  );
})

#if ENABLE_SENSORS
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(SensorCommandAdapter, "S")
DCC_PROTOCOL_COMMAND_HANDLER(SensorCommandAdapter,
[](const vector<string> arguments)
{
  if(arguments.empty())
  {
    // list all sensors
    string status = SensorManager::get_state_for_dccpp();
#if S88_ENABLED
    status += S88BusManager::get_state_for_dccpp();
#endif
    status += RemoteSensorManager::get_state_for_dccpp();
    return status;
  }
  else
  {
    uint16_t sensorID = std::stoi(arguments[0]);
    if (arguments.size() == 1 && SensorManager::remove(sensorID))
    {
      // delete turnout
      return COMMAND_SUCCESSFUL_RESPONSE;
    }
    else if (arguments.size() == 3)
    {
      // create sensor
      SensorManager::createOrUpdate(sensorID, std::stoi(arguments[1])
                                  , arguments[2][0] == '1');
      return COMMAND_SUCCESSFUL_RESPONSE;
    }
  }
  return COMMAND_FAILED_RESPONSE;
})

DECLARE_DCC_PROTOCOL_COMMAND_CLASS(RemoteSensorsCommandAdapter, "RS")
DCC_PROTOCOL_COMMAND_HANDLER(RemoteSensorsCommandAdapter,
[](const vector<string> arguments)
{
  if(arguments.empty())
  {
    // list all sensors
    return RemoteSensorManager::get_state_for_dccpp();
  }
  else
  {
    uint16_t sensorID = std::stoi(arguments[0]);
    if (arguments.size() == 1 && RemoteSensorManager::remove(sensorID))
    {
      // delete remote sensor
      return COMMAND_SUCCESSFUL_RESPONSE;
    }
    else if (arguments.size() == 2)
    {
      // create/update remote sensor
      RemoteSensorManager::createOrUpdate(sensorID, std::stoi(arguments[1]));
      return COMMAND_SUCCESSFUL_RESPONSE;
    }
  }
  return COMMAND_FAILED_RESPONSE;
})

#if S88_ENABLED
DCC_PROTOCOL_COMMAND_HANDLER(S88BusCommandAdapter,
[](const vector<string> arguments)
{
  if(arguments.empty())
  {
    // list all sensor groups
    return S88BusManager::get_state_for_dccpp();
  }
  else
  {
    if (arguments.size() == 1 &&
        S88BusManager::removeBus(std::stoi(arguments[0])))
    {
      // delete sensor bus
      return COMMAND_SUCCESSFUL_RESPONSE;
    }
    else if (arguments.size() == 3 &&
             S88BusManager::createOrUpdateBus(std::stoi(arguments[0])
                                            , std::stoi(arguments[1])
                                            , std::stoi(arguments[2])))
    {
      // create sensor bus
      return COMMAND_SUCCESSFUL_RESPONSE;
    }
  }
  return COMMAND_FAILED_RESPONSE;
})
#endif // S88_ENABLED
#endif // SENSORS_ENABLED

#if ENABLE_OUTPUTS
DECLARE_DCC_PROTOCOL_COMMAND_CLASS(OutputCommandAdapter, "Z")
DCC_PROTOCOL_COMMAND_HANDLER(OutputCommandAdapter,
[](const vector<string> arguments)
{
  if(arguments.empty())
  {
    // list all outputs
    return OutputManager::get_state_for_dccpp();
  }
  else
  {
    uint16_t outputID = std::stoi(arguments[0]);
    if (arguments.size() == 1 && OutputManager::remove(outputID))
    {
      // delete output
      return COMMAND_SUCCESSFUL_RESPONSE;
    }
    else if (arguments.size() == 2)
    {
      // set output state
      return OutputManager::set(outputID, arguments[1][0] == 1);
    }
    else if (arguments.size() == 3)
    {
      // create output
      OutputManager::createOrUpdate(outputID, std::stoi(arguments[1])
                                  , std::stoi(arguments[2]));
      return COMMAND_SUCCESSFUL_RESPONSE;
    }
  }
  return COMMAND_FAILED_RESPONSE;
})

DECLARE_DCC_PROTOCOL_COMMAND_CLASS(OutputExCommandAdapter, "Zex")
DCC_PROTOCOL_COMMAND_HANDLER(OutputExCommandAdapter,
[](const vector<string> arguments)
{
  if(arguments.empty())
  {
    return COMMAND_FAILED_RESPONSE;
  }
  else
  {
    uint16_t outputID = std::stoi(arguments[0]);
    auto output = OutputManager::getOutput(outputID);
    if (output)
    {
      return output->set(!output->isActive());
    }
  }
  return COMMAND_FAILED_RESPONSE;
})
#endif // OUTPUTS_ENABLED

void DCCPPProtocolHandler::init()
{
  registerCommand(new ThrottleCommandAdapter());
  registerCommand(new ThrottleExCommandAdapter());
  registerCommand(new FunctionCommandAdapter());
  registerCommand(new FunctionExCommandAdapter());
  registerCommand(new ConsistCommandAdapter());
  registerCommand(new AccessoryCommand());
  registerCommand(new PowerOnCommand());
  registerCommand(new PowerOffCommand());
  registerCommand(new CurrentDrawCommand());
  registerCommand(new StatusCommand());
  registerCommand(new ReadCVCommand());
  registerCommand(new WriteCVByteProgCommand());
  registerCommand(new WriteCVBitProgCommand());
  registerCommand(new WriteCVByteOpsCommand());
  registerCommand(new WriteCVBitOpsCommand());
  registerCommand(new ConfigErase());
  registerCommand(new ConfigStore());
#if ENABLE_OUTPUTS
  registerCommand(new OutputCommandAdapter());
  registerCommand(new OutputExCommandAdapter());
#endif
  registerCommand(new TurnoutCommandAdapter());
  registerCommand(new TurnoutExCommandAdapter());
#if ENABLE_SENSORS
  registerCommand(new SensorCommandAdapter());
#if S88_ENABLED
  registerCommand(new S88BusCommandAdapter());
#endif
  registerCommand(new RemoteSensorsCommandAdapter());
#endif
  registerCommand(new FreeHeapCommand());
  registerCommand(new EStopCommand());
}

string DCCPPProtocolHandler::process(const string &commandString)
{
  vector<string> parts;
  esp32cs::tokenize(commandString, parts);
  string commandID = parts.front();
  parts.erase(parts.begin());
  LOG(VERBOSE, "Command: %s, argument count: %d", commandID.c_str()
    , parts.size());
  auto handler = getCommandHandler(commandID);
  if (handler)
  {
    return handler->process(parts);
  }
  LOG_ERROR("No command handler for [%s]", commandID.c_str());
  return COMMAND_FAILED_RESPONSE;
}

void DCCPPProtocolHandler::registerCommand(DCCPPProtocolCommand *cmd)
{
  for (const auto& command : registeredCommands)
  {
    if(!command->getID().compare(cmd->getID()))
    {
      LOG_ERROR("Ignoring attempt to register second command with ID: %s",
        cmd->getID().c_str());
      return;
    }
  }
  LOG(VERBOSE, "Registering interface command %s", cmd->getID().c_str());
  registeredCommands.emplace_back(cmd);
}

DCCPPProtocolCommand *DCCPPProtocolHandler::getCommandHandler(const string &id)
{
  auto command = std::find_if(registeredCommands.begin()
                            , registeredCommands.end()
                            , [id](const unique_ptr<DCCPPProtocolCommand> &cmd)
                              {
                                return cmd->getID() == id;
                              });
  if (command != registeredCommands.end())
  {
    return command->get();
  }
  return nullptr;
}

DCCPPProtocolConsumer::DCCPPProtocolConsumer()
{
  _buffer.resize(256);
}

std::string DCCPPProtocolConsumer::feed(uint8_t *data, size_t len)
{
  for(size_t i = 0; i < len; i++)
  {
    _buffer.emplace_back(data[i]);
  }
  return processData();
}

string DCCPPProtocolConsumer::processData()
{
  auto s = _buffer.begin();
  auto consumed = _buffer.begin();
  string response;
  for(; s != _buffer.end();)
  {
    s = std::find(s, _buffer.end(), '<');
    auto e = std::find(s, _buffer.end(), '>');
    if(s != _buffer.end() && e != _buffer.end())
    {
      // discard the <
      s++;
      // discard the >
      *e = 0;
      std::string str(reinterpret_cast<char*>(&*s));
      response += DCCPPProtocolHandler::process(std::move(str));
      consumed = e;
    }
    s = e;
  }
  // drop everything we used from the buffer.
  _buffer.erase(_buffer.begin(), consumed);
  return response;
}
