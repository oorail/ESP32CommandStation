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
**********************************************************************/

#include "ESP32CommandStation.h"

/**********************************************************************

The ESP32 Command Station supports optional OUTPUT control of any unused pins
for custom purposes. Pins can be activited or de-activated. The default is to
set ACTIVE pins HIGH and INACTIVE pins LOW. However, this default behavior can
be inverted for any pin in which case ACTIVE=LOW and INACTIVE=HIGH.

Definitions and state (ACTIVE/INACTIVE) for pins are retained on the ESP32 and
restored on power-up. The default is to set each defined pin to active or
inactive according to its restored state. However, the default behavior can be
modified so that any pin can be forced to be either active or inactive upon
power-up regardless of its previous state before power-down.

To have this sketch utilize one or more Arduino pins as custom outputs, first
define/edit/delete output definitions using the following variation of the "Z"
command:

  <Z ID PIN IFLAG>: creates a new output ID, with specified PIN and IFLAG values.
                    if output ID already exists, it is updated with specificed
                    PIN and IFLAG.
                    Note: output state will be immediately set to ACTIVE/INACTIVE
                    and pin will be set to HIGH/LOW according to IFLAG value
                    specifcied (see below).
        returns: <O> if successful and <X> if unsuccessful (e.g. out of memory)

  <Z ID>:           deletes definition of output ID
        returns: <O> if successful and <X> if unsuccessful (e.g. ID does not exist)

  <Z>:              lists all defined output pins
        returns: <Y ID PIN IFLAG STATE> for each defined output pin or <X> if no
        output pins defined

where

  ID: the numeric ID (0-32767) of the output
  PIN: the pin number to use for the output
  STATE: the state of the output (0=INACTIVE / 1=ACTIVE)
  IFLAG: defines the operational behavior of the output based on bits 0, 1, and
  2 as follows:

    IFLAG, bit 0:   0 = forward operation (ACTIVE=HIGH / INACTIVE=LOW)
                    1 = inverted operation (ACTIVE=LOW / INACTIVE=HIGH)

    IFLAG, bit 1:   0 = state of pin restored on power-up to either ACTIVE or
                        INACTIVE depending on state before power-down; state of
                        pin set to INACTIVE when first created.
                    1 = state of pin set on power-up, or when first created, to
                        either ACTIVE of INACTIVE depending on IFLAG, bit 2.

    IFLAG, bit 2:   0 = state of pin set to INACTIVE upon power-up or when
                        first created.
                    1 = state of pin set to ACTIVE upon power-up or when
                        first created.

Once all outputs have been properly defined, use the <E> command to store their
definitions. If you later make edits/additions/deletions to the output
definitions, you must invoke the <E> command if you want those new definitions
updated on the ESP32.  You can also clear everything stored on the ESP32 by
invoking the <e> command.

To change the state of outputs that have been defined use:

  <Z ID STATE>:     sets output ID to either ACTIVE or INACTIVE state
                    returns: <Y ID STATE>, or <X> if turnout ID does not exist
where
  ID: the numeric ID (0-32767) of the turnout to control
  STATE: the state of the output (0=INACTIVE / 1=ACTIVE)

When controlled as such, the ESP32 updates and stores the direction of each
output so that it is retained even without power. A list of the current states
of each output in the form <Y ID STATE> is generated by this sketch whenever the
<s> status command is invoked.  This provides an efficient way of initializing
the state of any outputs being monitored or controlled by a separate interface
or GUI program.

**********************************************************************/
#if ENABLE_OUTPUTS
LinkedList<Output *> outputs([](Output *output) {delete output; });

static constexpr const char * OUTPUTS_JSON_FILE = "outputs.json";

void OutputManager::init()
{
  LOG(INFO, "[Output] Initializing outputs");
  nlohmann::json root = nlohmann::json::parse(configStore->load(OUTPUTS_JSON_FILE));
  if(root.contains(JSON_COUNT_NODE))
  {
    uint16_t outputCount = root[JSON_COUNT_NODE].get<uint16_t>();
    Singleton<InfoScreen>::instance()->replaceLine(
      INFO_SCREEN_ROTATING_STATUS_LINE, "Found %02d Outputs", outputCount);
    for(auto output : root[JSON_OUTPUTS_NODE])
    {
      string data = output.dump();
      outputs.add(new Output(data));
    }
  }
  LOG(INFO, "[Output] Loaded %d outputs", outputs.length());
}

void OutputManager::clear()
{
  outputs.free();
}

uint16_t OutputManager::store()
{
  nlohmann::json root;
  uint16_t outputStoredCount = 0;
  for (const auto& output : outputs)
  {
    root[JSON_OUTPUTS_NODE].push_back(output->toJson());
    outputStoredCount++;
  }
  root[JSON_COUNT_NODE] = outputStoredCount;
  configStore->store(OUTPUTS_JSON_FILE, root);
  return outputStoredCount;
}

string OutputManager::set(uint16_t id, bool active)
{
  for (const auto& output : outputs)
  {
    if(output->getID() == id)
    {
      return output->set(active);
    }
  }
  return COMMAND_FAILED_RESPONSE;
}

Output *OutputManager::getOutput(uint16_t id)
{
  for (const auto& output : outputs)
  {
    if(output->getID() == id)
    {
      return output;
    }
  }
  return nullptr;
}

bool OutputManager::toggle(uint16_t id)
{
  for (const auto& output : outputs)
  {
    if(output->getID() == id)
    {
      output->set(!output->isActive());
      return true;
    }
  }
  return false;
}

std::string OutputManager::getStateAsJson()
{
  string state = "[";
  for (const auto& output : outputs)
  {
    state += output->toJson(true);
    state += ",";
  }
  state += "]";
  return state;
}

string OutputManager::getStateAsDCCpp()
{
  string status;
  for (const auto& output : outputs)
  {
    status += output->getStateAsDCCpp();
  }
  return status;
}

bool OutputManager::createOrUpdate(const uint16_t id, const uint8_t pin, const uint8_t flags)
{
  for (const auto& output : outputs)
  {
    if(output->getID() == id)
    {
      output->update(pin, flags);
      return true;
    }
  }
  if(is_restricted_pin(pin))
  {
    return false;
  }
  outputs.add(new Output(id, pin, flags));
  return true;
}

bool OutputManager::remove(const uint16_t id)
{
  Output *outputToRemove = nullptr;
  for (const auto& output : outputs)
  {
    if(output->getID() == id)
    {
      outputToRemove = output;
    }
  }
  if(outputToRemove != nullptr)
  {
    LOG(INFO, "[Output] Removing Output(%d)", outputToRemove->getID());
    outputs.remove(outputToRemove);
    return true;
  }
  return false;
}

Output::Output(uint16_t id, uint8_t pin, uint8_t flags) : _id(id), _pin(pin), _flags(flags), _active(false)
{
  if(bitRead(_flags, OUTPUT_IFLAG_RESTORE_STATE))
  {
    if(bitRead(_flags, OUTPUT_IFLAG_FORCE_STATE))
    {
      set(true, false);
    }
    else
    {
      set(false, false);
    }
  }
  else
  {
    set(false, false);
  }
  LOG(VERBOSE, "[Output] Output(%d) on pin %d created, flags: %s", _id, _pin, getFlagsAsString().c_str());
  pinMode(_pin, OUTPUT);
}

Output::Output(string &data)
{
  nlohmann::json object = nlohmann::json::parse(data);
  _id = object[JSON_ID_NODE].get<uint16_t>();
  _pin = object[JSON_PIN_NODE].get<uint8_t>();
  _flags = object[JSON_FLAGS_NODE].get<uint8_t>();
  if(bitRead(_flags, OUTPUT_IFLAG_RESTORE_STATE))
  {
    set(bitRead(_flags, OUTPUT_IFLAG_FORCE_STATE), false);
  }
  else
  {
    set(object[JSON_STATE_NODE].get<bool>(), false);
  }
  LOG(VERBOSE, "[Output] Output(%d) on pin %d loaded, flags: %s", _id, _pin, getFlagsAsString().c_str());
  pinMode(_pin, OUTPUT);
}

string Output::set(bool active, bool announce)
{
  _active = active;
  digitalWrite(_pin, _active);
  LOG(INFO, "[Output] Output(%d) set to %s", _id, _active ? JSON_VALUE_ON : JSON_VALUE_OFF);
  if(announce)
  {
    return StringPrintf("<Y %d %d>", _id, !_active);
  }
  return COMMAND_NO_RESPONSE;
}

void Output::update(uint8_t pin, uint8_t flags)
{
  _pin = pin;
  _flags = flags;
  if(!bitRead(_flags, OUTPUT_IFLAG_RESTORE_STATE))
  {
    set(false, false);
  }
  else
  {
    set(bitRead(_flags, OUTPUT_IFLAG_FORCE_STATE), false);
  }
  LOG(VERBOSE, "[Output] Output(%d) on pin %d updated, flags: %s", _id, _pin, getFlagsAsString().c_str());
  pinMode(_pin, OUTPUT);
}

string Output::toJson(bool readableStrings)
{
  nlohmann::json object =
  {
    { JSON_ID_NODE, _id },
    { JSON_PIN_NODE, _pin },
  };
  if(readableStrings)
  {
    object[JSON_FLAGS_NODE] = getFlagsAsString();
    object[JSON_STATE_NODE] = isActive() ? JSON_VALUE_ON : JSON_VALUE_OFF;
  }
  else
  {
    object[JSON_FLAGS_NODE] = _flags;
    object[JSON_STATE_NODE] = _active;
  }
  return object.dump();
}

string Output::getStateAsJson()
{
  return StringPrintf("<Y %d %d %d %d>", _id, _pin, _flags, !_active);
}
#endif // ENABLE_OUTPUTS