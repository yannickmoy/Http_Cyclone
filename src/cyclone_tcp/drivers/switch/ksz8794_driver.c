/**
 * @file ksz8794_driver.c
 * @brief KSZ8794 4-port Ethernet switch driver
 *
 * @section License
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2010-2020 Oryx Embedded SARL. All rights reserved.
 *
 * This file is part of CycloneTCP Open.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * @author Oryx Embedded SARL (www.oryx-embedded.com)
 * @version 1.9.7b
 **/

//Switch to the appropriate trace level
#define TRACE_LEVEL NIC_TRACE_LEVEL

//Dependencies
#include "core/net.h"
#include "core/ethernet_misc.h"
#include "drivers/switch/ksz8794_driver.h"
#include "debug.h"


/**
 * @brief KSZ8794 Ethernet switch driver
 **/

const PhyDriver ksz8794PhyDriver =
{
   ksz8794Init,
   ksz8794Tick,
   ksz8794EnableIrq,
   ksz8794DisableIrq,
   ksz8794EventHandler,
   ksz8794TagFrame,
   ksz8794UntagFrame
};


/**
 * @brief Tail tag rules (host to KSZ8794)
 **/

const uint8_t ksz8794IngressTailTag[4] =
{
   0,
   KSZ8794_TAIL_TAG_ENCODE(1),
   KSZ8794_TAIL_TAG_ENCODE(2),
   KSZ8794_TAIL_TAG_ENCODE(3)
};


/**
 * @brief KSZ8794 Ethernet switch initialization
 * @param[in] interface Underlying network interface
 * @return Error code
 **/

error_t ksz8794Init(NetInterface *interface)
{
   uint_t port;
   uint8_t temp;

   //Debug message
   TRACE_INFO("Initializing KSZ8794...\r\n");

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Initialize SPI
      interface->spiDriver->init();

      //Wait for the serial interface to be ready
      do
      {
         //Read CHIP_ID0 register
         temp = ksz8794ReadSwitchReg(interface, KSZ8794_CHIP_ID0);

         //The returned data is invalid until the serial interface is ready
      } while(temp != KSZ8794_CHIP_ID0_FAMILY_ID_DEFAULT);

#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
      //Enable tail tag feature
      temp = ksz8794ReadSwitchReg(interface, KSZ8794_GLOBAL_CTRL10);
      temp |= KSZ8794_GLOBAL_CTRL10_TAIL_TAG_EN;
      ksz8794WriteSwitchReg(interface, KSZ8794_GLOBAL_CTRL10, temp);
#else
      //Disable tail tag feature
      temp = ksz8794ReadSwitchReg(interface, KSZ8794_GLOBAL_CTRL10);
      temp &= ~KSZ8794_GLOBAL_CTRL10_TAIL_TAG_EN;
      ksz8794WriteSwitchReg(interface, KSZ8794_GLOBAL_CTRL10, temp);
#endif

      //Loop through ports
      for(port = KSZ8794_PORT1; port <= KSZ8794_PORT3; port++)
      {
#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
         //Port separation mode?
         if(interface->port != 0)
         {
            //Disable packet transmission and switch address learning
            temp = ksz8794ReadSwitchReg(interface, KSZ8794_PORTn_CTRL2(port));
            temp &= ~KSZ8794_PORTn_CTRL2_TRANSMIT_EN;
            temp |= KSZ8794_PORTn_CTRL2_RECEIVE_EN;
            temp |= KSZ8794_PORTn_CTRL2_LEARNING_DIS;
            ksz8794WriteSwitchReg(interface, KSZ8794_PORTn_CTRL2(port), temp);
         }
         else
#endif
         {
            //Enable transmission, reception and switch address learning
            temp = ksz8794ReadSwitchReg(interface, KSZ8794_PORTn_CTRL2(port));
            temp |= KSZ8794_PORTn_CTRL2_TRANSMIT_EN;
            temp |= KSZ8794_PORTn_CTRL2_RECEIVE_EN;
            temp &= ~KSZ8794_PORTn_CTRL2_LEARNING_DIS;
            ksz8794WriteSwitchReg(interface, KSZ8794_PORTn_CTRL2(port), temp);
         }
      }

      //Dump switch registers for debugging purpose
      ksz8794DumpSwitchReg(interface);
   }
   else
   {
      //Loop through ports
      for(port = KSZ8794_PORT1; port <= KSZ8794_PORT3; port++)
      {
         //Debug message
         TRACE_DEBUG("Port %u:\r\n", port);
         //Dump PHY registers for debugging purpose
         ksz8794DumpPhyReg(interface, port);
      }
   }

   //Force the TCP/IP stack to poll the link state at startup
   interface->phyEvent = TRUE;
   //Notify the TCP/IP stack of the event
   osSetEvent(&netEvent);

   //Successful initialization
   return NO_ERROR;
}


/**
 * @brief Get link state
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @return Link state
 **/

bool_t ksz8794GetLinkState(NetInterface *interface, uint8_t port)
{
   uint16_t status;
   bool_t linkState;

   //Check port number
   if(port >= KSZ8794_PORT1 && port <= KSZ8794_PORT3)
   {
      //SPI slave mode?
      if(interface->spiDriver != NULL)
      {
         //Read port status 2 register
         status = ksz8794ReadSwitchReg(interface, KSZ8794_PORTn_STAT2(port));

         //Retrieve current link state
         linkState = (status & KSZ8794_PORTn_STAT2_LINK_GOOD) ? TRUE : FALSE;
      }
      else
      {
         //Read status register
         status = ksz8794ReadPhyReg(interface, port, KSZ8794_BMSR);

         //Retrieve current link state
         linkState = (status & KSZ8794_BMSR_LINK_STATUS) ? TRUE : FALSE;
      }
   }
   else
   {
      //The specified port number is not valid
      linkState = FALSE;
   }

   //Return link status
   return linkState;
}


/**
 * @brief KSZ8794 timer handler
 * @param[in] interface Underlying network interface
 **/

void ksz8794Tick(NetInterface *interface)
{
   uint_t port;
   bool_t linkState;

#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
   //Port separation mode?
   if(interface->port != 0)
   {
      uint_t i;
      NetInterface *virtualInterface;

      //Loop through network interfaces
      for(i = 0; i < NET_INTERFACE_COUNT; i++)
      {
         //Point to the current interface
         virtualInterface = &netInterface[i];

         //Check whether the current virtual interface is attached to the
         //physical interface
         if(virtualInterface == interface ||
            virtualInterface->parent == interface)
         {
            //Retrieve current link state
            linkState = ksz8794GetLinkState(interface, virtualInterface->port);

            //Link up or link down event?
            if(linkState != virtualInterface->linkState)
            {
               //Set event flag
               interface->phyEvent = TRUE;
               //Notify the TCP/IP stack of the event
               osSetEvent(&netEvent);
            }
         }
      }
   }
   else
#endif
   {
      //Initialize link state
      linkState = FALSE;

      //Loop through ports
      for(port = KSZ8794_PORT1; port <= KSZ8794_PORT3; port++)
      {
         //Retrieve current link state
         if(ksz8794GetLinkState(interface, port))
         {
            linkState = TRUE;
         }
      }

      //Link up or link down event?
      if(linkState != interface->linkState)
      {
         //Set event flag
         interface->phyEvent = TRUE;
         //Notify the TCP/IP stack of the event
         osSetEvent(&netEvent);
      }
   }
}


/**
 * @brief Enable interrupts
 * @param[in] interface Underlying network interface
 **/

void ksz8794EnableIrq(NetInterface *interface)
{
}


/**
 * @brief Disable interrupts
 * @param[in] interface Underlying network interface
 **/

void ksz8794DisableIrq(NetInterface *interface)
{
}


/**
 * @brief KSZ8794 event handler
 * @param[in] interface Underlying network interface
 **/

void ksz8794EventHandler(NetInterface *interface)
{
   uint_t port;
   bool_t linkState;

#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
   //Port separation mode?
   if(interface->port != 0)
   {
      uint_t i;
      uint16_t status;
      NetInterface *virtualInterface;

      //Loop through network interfaces
      for(i = 0; i < NET_INTERFACE_COUNT; i++)
      {
         //Point to the current interface
         virtualInterface = &netInterface[i];

         //Check whether the current virtual interface is attached to the
         //physical interface
         if(virtualInterface == interface ||
            virtualInterface->parent == interface)
         {
            //Get the port number associated with the current interface
            port = virtualInterface->port;

            //Valid port?
            if(port >= KSZ8794_PORT1 && port <= KSZ8794_PORT3)
            {
               //Retrieve current link state
               linkState = ksz8794GetLinkState(interface, port);

               //Link up event?
               if(linkState && !virtualInterface->linkState)
               {
                  //Adjust MAC configuration parameters for proper operation
                  interface->linkSpeed = NIC_LINK_SPEED_100MBPS;
                  interface->duplexMode = NIC_FULL_DUPLEX_MODE;
                  interface->nicDriver->updateMacConfig(interface);

                  //Read port status 3 register
                  status = ksz8794ReadSwitchReg(interface,
                     KSZ8794_PORTn_CTRL11_STAT3(port));

                  //Check current operation mode
                  switch(status & KSZ8794_PORTn_CTRL11_STAT3_OP_MODE)
                  {
                  //10BASE-T half-duplex
                  case KSZ8794_PORTn_CTRL11_STAT3_OP_MODE_10BT_HD:
                     virtualInterface->linkSpeed = NIC_LINK_SPEED_10MBPS;
                     virtualInterface->duplexMode = NIC_HALF_DUPLEX_MODE;
                     break;
                  //10BASE-T full-duplex
                  case KSZ8794_PORTn_CTRL11_STAT3_OP_MODE_10BT_FD:
                     virtualInterface->linkSpeed = NIC_LINK_SPEED_10MBPS;
                     virtualInterface->duplexMode = NIC_FULL_DUPLEX_MODE;
                     break;
                  //100BASE-TX half-duplex
                  case KSZ8794_PORTn_CTRL11_STAT3_OP_MODE_100BTX_HD:
                     virtualInterface->linkSpeed = NIC_LINK_SPEED_100MBPS;
                     virtualInterface->duplexMode = NIC_HALF_DUPLEX_MODE;
                     break;
                  //100BASE-TX full-duplex
                  case KSZ8794_PORTn_CTRL11_STAT3_OP_MODE_100BTX_FD:
                     virtualInterface->linkSpeed = NIC_LINK_SPEED_100MBPS;
                     virtualInterface->duplexMode = NIC_FULL_DUPLEX_MODE;
                     break;
                  //Unknown operation mode
                  default:
                     //Debug message
                     TRACE_WARNING("Invalid operation mode!\r\n");
                     break;
                  }

                  //Update link state
                  virtualInterface->linkState = TRUE;

                  //Process link state change event
                  nicNotifyLinkChange(virtualInterface);
               }
               //Link down event
               else if(!linkState && virtualInterface->linkState)
               {
                  //Update link state
                  virtualInterface->linkState = FALSE;

                  //Process link state change event
                  nicNotifyLinkChange(virtualInterface);
               }
            }
         }
      }
   }
   else
#endif
   {
      //Initialize link state
      linkState = FALSE;

      //Loop through ports
      for(port = KSZ8794_PORT1; port <= KSZ8794_PORT3; port++)
      {
         //Retrieve current link state
         if(ksz8794GetLinkState(interface, port))
         {
            linkState = TRUE;
         }
      }

      //Link up event?
      if(linkState)
      {
         //Adjust MAC configuration parameters for proper operation
         interface->linkSpeed = NIC_LINK_SPEED_100MBPS;
         interface->duplexMode = NIC_FULL_DUPLEX_MODE;
         interface->nicDriver->updateMacConfig(interface);

         //Update link state
         interface->linkState = TRUE;
      }
      else
      {
         //Update link state
         interface->linkState = FALSE;
      }

      //Process link state change event
      nicNotifyLinkChange(interface);
   }
}


/**
 * @brief Add tail tag to Ethernet frame
 * @param[in] interface Underlying network interface
 * @param[in] buffer Multi-part buffer containing the payload
 * @param[in,out] offset Offset to the first payload byte
 * @param[in] port Switch port identifier
 * @param[in,out] type Ethernet type
 * @return Error code
 **/

error_t ksz8794TagFrame(NetInterface *interface, NetBuffer *buffer,
   size_t *offset, uint8_t port, uint16_t *type)
{
   error_t error;

   //Initialize status code
   error = NO_ERROR;

#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Valid port?
      if(port <= KSZ8794_PORT3)
      {
         size_t length;
         const uint8_t *tailTag;

         //The one byte tail tagging is used to indicate the destination port
         tailTag = &ksz8794IngressTailTag[port];

         //Retrieve the length of the frame
         length = netBufferGetLength(buffer) - *offset;

         //The host controller should manually add padding to the packet before
         //inserting the tail tag
         error = ethPadFrame(buffer, &length);

         //Check status code
         if(!error)
         {
            //The tail tag is inserted at the end of the packet, just before
            //the CRC
            error = netBufferAppend(buffer, tailTag, sizeof(uint8_t));
         }
      }
      else
      {
         //Invalid port identifier
         error = ERROR_WRONG_IDENTIFIER;
      }
   }
#endif

   //Return status code
   return error;
}


/**
 * @brief Decode tail tag from incoming Ethernet frame
 * @param[in] interface Underlying network interface
 * @param[in,out] frame Pointer to the received Ethernet frame
 * @param[in,out] length Length of the frame, in bytes
 * @param[out] port Switch port identifier
 * @return Error code
 **/

error_t ksz8794UntagFrame(NetInterface *interface, uint8_t **frame,
   size_t *length, uint8_t *port)
{
   error_t error;

   //Initialize status code
   error = NO_ERROR;

#if (ETH_PORT_TAGGING_SUPPORT == ENABLED)
   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Valid Ethernet frame received?
      if(*length >= (sizeof(EthHeader) + sizeof(uint8_t)))
      {
         uint8_t *tailTag;

         //The tail tag is inserted at the end of the packet, just before
         //the CRC
         tailTag = *frame + *length - sizeof(uint8_t);

         //The one byte tail tagging is used to indicate the source port
         *port = KSZ8794_TAIL_TAG_DECODE(*tailTag);

         //Strip tail tag from Ethernet frame
         *length -= sizeof(uint8_t);
      }
      else
      {
         //Drop the received frame
         error = ERROR_INVALID_LENGTH;
      }
   }
   else
#endif
   {
      //Tail tagging mode cannot be enabled through MDC/MDIO interface
      *port = 0;
   }

   //Return status code
   return error;
}


/**
 * @brief Write PHY register
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @param[in] address PHY register address
 * @param[in] data Register value
 **/

void ksz8794WritePhyReg(NetInterface *interface, uint8_t port,
   uint8_t address, uint16_t data)
{
   //Write the specified PHY register
   interface->nicDriver->writePhyReg(SMI_OPCODE_WRITE, port, address, data);
}


/**
 * @brief Read PHY register
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 * @param[in] address PHY register address
 * @return Register value
 **/

uint16_t ksz8794ReadPhyReg(NetInterface *interface, uint8_t port,
   uint8_t address)
{
   //Read the specified PHY register
   return interface->nicDriver->readPhyReg(SMI_OPCODE_READ, port, address);
}


/**
 * @brief Dump PHY registers for debugging purpose
 * @param[in] interface Underlying network interface
 * @param[in] port Port number
 **/

void ksz8794DumpPhyReg(NetInterface *interface, uint8_t port)
{
   uint8_t i;

   //Loop through PHY registers
   for(i = 0; i < 32; i++)
   {
      //Display current PHY register
      TRACE_DEBUG("%02" PRIu8 ": 0x%04" PRIX16 "\r\n", i,
         ksz8794ReadPhyReg(interface, port, i));
   }

   //Terminate with a line feed
   TRACE_DEBUG("\r\n");
}


/**
 * @brief Write switch register
 * @param[in] interface Underlying network interface
 * @param[in] address Switch register address
 * @param[in] data Register value
 **/

void ksz8794WriteSwitchReg(NetInterface *interface, uint16_t address,
   uint8_t data)
{
   uint16_t command;

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Set up a write operation
      command = KSZ8794_SPI_CMD_WRITE;
      //Set register address
      command |= (address << 1) & KSZ8794_SPI_CMD_ADDR;

      //Pull the CS pin low
      interface->spiDriver->assertCs();

      //Write 16-bit command
      interface->spiDriver->transfer(MSB(command));
      interface->spiDriver->transfer(LSB(command));

      //Write data
      interface->spiDriver->transfer(data);

      //Terminate the operation by raising the CS pin
      interface->spiDriver->deassertCs();
   }
   else
   {
      //The MDC/MDIO interface does not have access to all the configuration
      //registers. It can only access the standard MIIM registers
   }
}


/**
 * @brief Read switch register
 * @param[in] interface Underlying network interface
 * @param[in] address Switch register address
 * @return Register value
 **/

uint8_t ksz8794ReadSwitchReg(NetInterface *interface, uint16_t address)
{
   uint8_t data;
   uint16_t command;

   //SPI slave mode?
   if(interface->spiDriver != NULL)
   {
      //Set up a read operation
      command = KSZ8794_SPI_CMD_READ;
      //Set register address
      command |= (address << 1) & KSZ8794_SPI_CMD_ADDR;

      //Pull the CS pin low
      interface->spiDriver->assertCs();

      //Write 16-bit command
      interface->spiDriver->transfer(MSB(command));
      interface->spiDriver->transfer(LSB(command));

      //Read data
      data = interface->spiDriver->transfer(0xFF);

      //Terminate the operation by raising the CS pin
      interface->spiDriver->deassertCs();
   }
   else
   {
      //The MDC/MDIO interface does not have access to all the configuration
      //registers. It can only access the standard MIIM registers
      data = 0;
   }

   //Return register value
   return data;
}


/**
 * @brief Dump switch registers for debugging purpose
 * @param[in] interface Underlying network interface
 **/

void ksz8794DumpSwitchReg(NetInterface *interface)
{
   uint16_t i;

   //Loop through switch registers
   for(i = 0; i < 256; i++)
   {
      //Display current switch register
      TRACE_DEBUG("0x%02" PRIX16 " (%02" PRIu16 ") : 0x%02" PRIX8 "\r\n",
         i, i, ksz8794ReadSwitchReg(interface, i));
   }

   //Terminate with a line feed
   TRACE_DEBUG("\r\n");
}
