/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2018-2019 Mike Dunston

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

#pragma once

#include "ESP32CommandStation.h"
#include <executor/StateFlow.hxx>
#include <openlcb/SimpleStack.hxx>

#ifndef HC12_UART_NUM
#define HC12_UART_NUM 1
#endif

class HC12Radio : public StateFlowBase {
public:
  HC12Radio(openlcb::SimpleCanStack *stack) : StateFlowBase(stack->service()) {
#if HC12_RADIO_ENABLED
    start_flow(STATE(init));
#endif
  }
  void send(const std::string &text);
private:
  StateFlowTimer timer_{this};
  uart_port_t uart_{(uart_port_t)HC12_UART_NUM};
  DCCPPProtocolConsumer consumer_;
  const uint64_t updateInterval_{MSEC_TO_NSEC(250)};
  STATE_FLOW_STATE(init);
  STATE_FLOW_STATE(update);
};

extern unique_ptr<HC12Radio> hc12;