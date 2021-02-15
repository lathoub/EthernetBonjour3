//  Copyright (C) 2010 Georg Kaindl
//  http://gkaindl.com
//
//  This file is part of Arduino EthernetBonjour.
//
//  EthernetBonjour is free software: you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public License
//  as published by the Free Software Foundation, either version 3 of
//  the License, or (at your option) any later version.
//
//  EthernetBonjour is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with EthernetBonjour. If not, see
//  <http://www.gnu.org/licenses/>.
//

//  Illustrates how to register a Bonjour service.

#include <Ethernet3.h>
#include <EthernetUdp3.h>

#include <EthernetBonjour3.h>

#define RESET_PIN  26
#define CS_PIN     5

// you can find this written on the board of some Arduino Ethernets or shields
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

MDNS_NAMESPACE::EthernetBonjour3Class<EthernetUDP> EthernetBonjour("Arduino");

void setup()
{
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println("Booting");

  Ethernet.setRstPin(RESET_PIN);
  Ethernet.setCsPin(CS_PIN);
  Ethernet.init(4); // maxSockNum = 4 Socket 0...3 -> RX/TX Buffer 4k
  Serial.println("Resetting Wiz W5500 Ethernet Board...  ");
  Ethernet.hardreset();

  Ethernet.begin(mac);
  Serial.print("Ethernet with ");
  Serial.println(Ethernet.localIP());

  // Initialize the Bonjour/MDNS library. You can now reach or ping this
  // Arduino via the host name "arduino.local", provided that your operating
  // system is Bonjour-enabled (such as MacOS X).
  // Always call this before any other method!
  EthernetBonjour.begin(Ethernet.localIP());

  Serial.println("EthernetBonjour starting");

  // Now let's register the service we're offering (a web service) via Bonjour!
  // To do so, we call the addServiceRecord() method. The first argument is the
  // name of our service instance and its type, separated by a dot. In this
  // case, the service type is _http. There are many other service types, use
  // google to look up some common ones, but you can also invent your own
  // service type, like _mycoolservice - As long as your clients know what to
  // look for, you're good to go.
  // The second argument is the port on which the service is running. This is
  // port 80 here, the standard HTTP port.
  // The last argument is the protocol type of the service, either TCP or UDP.
  // Of course, our service is a TCP service.
  // With the service registered, it will show up in a Bonjour-enabled web
  // browser. As an example, if you are using Apple's Safari, you will now see
  // the service under Bookmarks -> Bonjour (Provided that you have enabled
  // Bonjour in the "Bookmarks" preferences in Safari).
  EthernetBonjour.addServiceRecord("Arduino._apple-midi",
                                   5004,
                                   MDNS_NAMESPACE::MDNSServiceUDP);
}

void loop()
{
  EthernetBonjour.run();
}
