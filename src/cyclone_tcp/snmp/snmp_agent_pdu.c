/**
 * @file snmp_agent_pdu.c
 * @brief SNMP agent (PDU processing)
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
#define TRACE_LEVEL SNMP_TRACE_LEVEL

//Dependencies
#include "core/net.h"
#include "snmp/snmp_agent.h"
#include "snmp/snmp_agent_pdu.h"
#include "snmp/snmp_agent_misc.h"
#include "snmp/snmp_agent_object.h"
#include "mibs/mib2_module.h"
#include "mibs/snmp_mib_module.h"
#include "mibs/snmp_usm_mib_module.h"
#include "core/crypto.h"
#include "encoding/asn1.h"
#include "encoding/oid.h"
#include "debug.h"

//Check TCP/IP stack configuration
#if (SNMP_AGENT_SUPPORT == ENABLED)

//snmpUnavailableContexts.0 object (1.3.6.1.6.3.12.1.4.0)
static const uint8_t snmpUnavailableContextsObject[9] = {43, 6, 1, 6, 3, 12, 1, 4, 0};
//snmpUnknownContexts.0 object (1.3.6.1.6.3.12.1.5.0)
static const uint8_t snmpUnknownContextsObject[9] = {43, 6, 1, 6, 3, 12, 1, 5, 0};


/**
 * @brief Process PDU
 * @param[in] context Pointer to the SNMP agent context
 * @return Error code
 **/

error_t snmpProcessPdu(SnmpAgentContext *context)
{
   error_t error;

   //Parse PDU header
   error = snmpParsePduHeader(&context->request);
   //Any error to report?
   if(error)
      return error;

   //Initialize response message
   snmpInitMessage(&context->response);

   //Check PDU type
   switch(context->request.pduType)
   {
   case SNMP_PDU_GET_REQUEST:
   case SNMP_PDU_GET_NEXT_REQUEST:
      //Process GetRequest-PDU or GetNextRequest-PDU
      error = snmpProcessGetRequestPdu(context);
      break;
   case SNMP_PDU_GET_BULK_REQUEST:
      //Process GetBulkRequest-PDU
      error = snmpProcessGetBulkRequestPdu(context);
      break;
   case SNMP_PDU_SET_REQUEST:
      //Process SetRequest-PDU
      error = snmpProcessSetRequestPdu(context);
      break;
#if (SNMP_AGENT_INFORM_SUPPORT == ENABLED)
   case SNMP_PDU_GET_RESPONSE:
      //Process GetResponse-PDU
      error = snmpProcessGetResponsePdu(context);
      break;
   case SNMP_PDU_REPORT:
      //Process Report-PDU
      error = snmpProcessReportPdu(context);
      break;
#endif
   default:
      //Invalid PDU type
      error = ERROR_INVALID_TYPE;
      break;
   }

   //Check status code
   if(!error)
   {
      //A GetResponse-PDU is generated by a protocol entity only upon receipt
      //of the GetRequest-PDU, GetNextRequest-PDU, GetBulkRequest-PDU or
      //SetRequest-PDU
      if(context->request.pduType == SNMP_PDU_GET_REQUEST ||
         context->request.pduType == SNMP_PDU_GET_NEXT_REQUEST ||
         context->request.pduType == SNMP_PDU_GET_BULK_REQUEST ||
         context->request.pduType == SNMP_PDU_SET_REQUEST)
      {
         //Total number of SNMP Get-Response PDUs which have been generated
         //by the SNMP protocol entity
         MIB2_INC_COUNTER32(snmpGroup.snmpOutGetResponses, 1);

         //Format PDU header
         error = snmpWritePduHeader(&context->response);
      }
   }

   //Return status code
   return error;
}


/**
 * @brief Process GetRequest-PDU or GetNextRequest-PDU
 * @param[in] context Pointer to the SNMP agent context
 * @return Error code
 **/

error_t snmpProcessGetRequestPdu(SnmpAgentContext *context)
{
   error_t error;
   int_t index;
   size_t n;
   size_t length;
   const uint8_t *p;
   SnmpVarBind var;

   //Check PDU type
   if(context->request.pduType == SNMP_PDU_GET_REQUEST)
   {
      //Debug message
      TRACE_INFO("Parsing GetRequest-PDU...\r\n");

      //Total number of SNMP Get-Request PDUs which have been accepted and
      //processed by the SNMP protocol entity
      MIB2_INC_COUNTER32(snmpGroup.snmpInGetRequests, 1);
   }
   else if(context->request.pduType == SNMP_PDU_GET_NEXT_REQUEST)
   {
      //Debug message
      TRACE_INFO("Parsing GetNextRequest-PDU...\r\n");

      //Total number of SNMP Get-NextRequest PDUs which have been accepted
      //and processed by the SNMP protocol entity
      MIB2_INC_COUNTER32(snmpGroup.snmpInGetNexts, 1);
   }

   //Enforce access policy
   if(context->user.mode != SNMP_ACCESS_READ_ONLY &&
      context->user.mode != SNMP_ACCESS_READ_WRITE)
   {
      //Total number of SNMP messages delivered to the SNMP protocol entity
      //which represented an SNMP operation which was not allowed by the SNMP
      MIB2_INC_COUNTER32(snmpGroup.snmpInBadCommunityUses, 1);
      SNMP_MIB_INC_COUNTER32(snmpGroup.snmpInBadCommunityUses, 1);

      //Report an error
      return ERROR_ACCESS_DENIED;
   }

   //Initialize response message
   error = snmpInitResponse(context);
   //Any error to report?
   if(error)
      return error;

   //Point to the first variable binding of the request
   p = context->request.varBindList;
   length = context->request.varBindListLen;

   //Lock access to MIB bases
   snmpLockMib(context);

   //Loop through the list
   for(index = 1; length > 0; index++)
   {
      //Parse variable binding
      error = snmpParseVarBinding(p, length, &var, &n);
      //Failed to parse variable binding?
      if(error)
         break;

      //Make sure that the object identifier is valid
      error = oidCheck(var.oid, var.oidLen);
      //Invalid object identifier?
      if(error)
         break;

      //GetRequest-PDU?
      if(context->request.pduType == SNMP_PDU_GET_REQUEST)
      {
         //Retrieve object value
         error = snmpGetObjectValue(context, &context->request, &var);
      }
      //GetNextRequest-PDU?
      else
      {
         //Search the MIB for the next object
         error = snmpGetNextObject(context, &context->request, &var);

         //SNMPv1 version?
         if(context->request.version == SNMP_VERSION_1)
         {
            //Check status code
            if(error == NO_ERROR)
            {
               //Retrieve object value
               error = snmpGetObjectValue(context, &context->request, &var);
            }
            else
            {
               //Stop immediately
               break;
            }
         }
         //SNMPv2c or SNMPv3 version?
         else
         {
            //Check status code
            if(error == NO_ERROR)
            {
               //Retrieve object value
               error = snmpGetObjectValue(context, &context->request, &var);
            }
            else if(error == ERROR_OBJECT_NOT_FOUND)
            {
               //The variable binding's value field is set to endOfMibView
               var.objClass = ASN1_CLASS_CONTEXT_SPECIFIC;
               var.objType = SNMP_EXCEPTION_END_OF_MIB_VIEW;
               var.valueLen = 0;

               //Catch exception
               error = NO_ERROR;
            }
            else
            {
               //Stop immediately
               break;
            }
         }
      }

      //Failed to retrieve object value?
      if(error)
      {
         //SNMPv1 version?
         if(context->request.version == SNMP_VERSION_1)
         {
            //Stop immediately
            break;
         }
         //SNMPv2c or SNMPv3 version?
         else
         {
            //Catch exception
            if(error == ERROR_ACCESS_DENIED ||
               error == ERROR_OBJECT_NOT_FOUND)
            {
               //The variable binding's value field is set to noSuchObject
               var.objClass = ASN1_CLASS_CONTEXT_SPECIFIC;
               var.objType = SNMP_EXCEPTION_NO_SUCH_OBJECT;
               var.valueLen = 0;
            }
            else if(error == ERROR_INSTANCE_NOT_FOUND)
            {
               //The variable binding's value field is set to noSuchInstance
               var.objClass = ASN1_CLASS_CONTEXT_SPECIFIC;
               var.objType = SNMP_EXCEPTION_NO_SUCH_INSTANCE;
               var.valueLen = 0;
            }
            else
            {
               //Stop immediately
               break;
            }
         }
      }
      else
      {
         //Total number of MIB objects which have been retrieved successfully
         //by the SNMP protocol entity as the result of receiving valid SNMP
         //Get-Request and Get-NextRequest PDUs
         MIB2_INC_COUNTER32(snmpGroup.snmpInTotalReqVars, 1);
      }

      //Append variable binding to the list
      error = snmpWriteVarBinding(context, &var);
      //Any error to report?
      if(error)
         break;

      //Advance data pointer
      p += n;
      length -= n;
   }

   //Unlock access to MIB bases
   snmpUnlockMib(context);

   //Check status code
   if(error)
   {
      //Set error-status and error-index fields
      error = snmpTranslateStatusCode(&context->response, error, index);
      //If the parsing of the request fails, the SNMP agent discards the message
      if(error)
         return error;

      //Check whether an alternate Response-PDU should be sent
      if(context->response.version != SNMP_VERSION_1 &&
         context->response.errorStatus == SNMP_ERROR_TOO_BIG)
      {
         //The alternate Response-PDU is formatted with the same value in its
         //request-id field as the received GetRequest-PDU and an empty
         //variable-bindings field
         context->response.varBindListLen = 0;
      }
      else
      {
         //The Response-PDU is re-formatted with the same values in its request-id
         //and variable-bindings fields as the received GetRequest-PDU
         error = snmpCopyVarBindingList(context);
         //Any error to report?
         if(error)
            return error;
      }
   }

   //Successful processing
   return NO_ERROR;
}


/**
 * @brief Process GetBulkRequest-PDU
 * @param[in] context Pointer to the SNMP agent context
 * @return Error code
 **/

error_t snmpProcessGetBulkRequestPdu(SnmpAgentContext *context)
{
#if (SNMP_V2C_SUPPORT == ENABLED || SNMP_V3_SUPPORT == ENABLED)
   error_t error;
   int_t index;
   size_t n;
   size_t m;
   size_t length;
   bool_t endOfMibView;
   const uint8_t *p;
   const uint8_t *next;
   SnmpVarBind var;

   //Debug message
   TRACE_INFO("Parsing GetBulkRequest-PDU...\r\n");

   //Make sure the SNMP version identifier is valid
   if(context->request.version == SNMP_VERSION_1)
   {
      //The SNMP version is not acceptable
      return ERROR_INVALID_TYPE;
   }

   //Enforce access policy
   if(context->user.mode != SNMP_ACCESS_READ_ONLY &&
      context->user.mode != SNMP_ACCESS_READ_WRITE)
   {
      //Total number of SNMP messages delivered to the SNMP protocol entity
      //which represented an SNMP operation which was not allowed by the SNMP
      MIB2_INC_COUNTER32(snmpGroup.snmpInBadCommunityUses, 1);
      SNMP_MIB_INC_COUNTER32(snmpGroup.snmpInBadCommunityUses, 1);

      //Report an error
      return ERROR_ACCESS_DENIED;
   }

   //Initialize response message
   error = snmpInitResponse(context);
   //Any error to report?
   if(error)
      return error;

   //Point to the first variable binding of the request
   p = context->request.varBindList;
   length = context->request.varBindListLen;

   //Lock access to MIB bases
   snmpLockMib(context);

   //Loop through the list
   for(index = 1; length > 0; index++)
   {
      //The non-repeaters field specifies the number of non-repeating objects
      //at the start of the variable binding list
      if((index - 1) == context->request.nonRepeaters)
      {
         //Pointer to the first variable binding that will be processed during
         //the next iteration
         next = context->response.varBindList + context->response.varBindListLen;

         //Actual size of the variable binding list
         m = context->response.varBindListLen;

         //This flag tells whether all variable bindings have the value field
         //set to endOfMibView for a given iteration
         endOfMibView = TRUE;

         //If the max-repetitions field is zero, the list is trimmed to the
         //first non-repeating variable bindings
         if(context->request.maxRepetitions == 0)
            break;
      }

      //Parse variable binding
      error = snmpParseVarBinding(p, length, &var, &n);
      //Failed to parse variable binding?
      if(error)
         break;

      //Make sure that the object identifier is valid
      error = oidCheck(var.oid, var.oidLen);
      //Invalid object identifier?
      if(error)
         break;

      //Search the MIB for the next object
      error = snmpGetNextObject(context, &context->request, &var);

      //Check status code
      if(error == NO_ERROR)
      {
         //Next object found
         endOfMibView = FALSE;
         //Retrieve object value
         error = snmpGetObjectValue(context, &context->request, &var);
      }
      else if(error == ERROR_OBJECT_NOT_FOUND)
      {
         //The variable binding's value field is set to endOfMibView
         var.objClass = ASN1_CLASS_CONTEXT_SPECIFIC;
         var.objType = SNMP_EXCEPTION_END_OF_MIB_VIEW;
         var.valueLen = 0;

         //Catch exception
         error = NO_ERROR;
      }
      else
      {
         //Stop immediately
         break;
      }

      //Failed to retrieve object value?
      if(error)
      {
         //Catch exception
         if(error == ERROR_ACCESS_DENIED ||
            error == ERROR_OBJECT_NOT_FOUND)
         {
            //The variable binding's value field is set to noSuchObject
            var.objClass = ASN1_CLASS_CONTEXT_SPECIFIC;
            var.objType = SNMP_EXCEPTION_NO_SUCH_OBJECT;
            var.valueLen = 0;
         }
         else if(error == ERROR_INSTANCE_NOT_FOUND)
         {
            //The variable binding's value field is set to noSuchInstance
            var.objClass = ASN1_CLASS_CONTEXT_SPECIFIC;
            var.objType = SNMP_EXCEPTION_NO_SUCH_INSTANCE;
            var.valueLen = 0;
         }
         else
         {
            //Stop immediately
            break;
         }
      }
      else
      {
         //Total number of MIB objects which have been retrieved successfully
         //by the SNMP protocol entity as the result of receiving valid SNMP
         //Get-Request and Get-NextRequest PDUs
         MIB2_INC_COUNTER32(snmpGroup.snmpInTotalReqVars, 1);
      }

      //Append variable binding to the list
      error = snmpWriteVarBinding(context, &var);
      //Any error to report?
      if(error)
         break;

      //Advance data pointer
      p += n;
      length -= n;

      //Next iteration?
      if(length == 0 && index > context->request.nonRepeaters)
      {
         //Decrement repeat counter
         context->request.maxRepetitions--;

         //Last iteration?
         if(!context->request.maxRepetitions)
            break;
         //All variable bindings have the value field set to endOfMibView?
         if(endOfMibView)
            break;

         //Point to the first variable binding to be processed
         p = next;
         //Number of bytes to be processed
         length = context->response.varBindListLen - m;
         //Rewind index
         index = context->request.nonRepeaters;
      }
   }

   //Unlock access to MIB bases
   snmpUnlockMib(context);

   //Check status code
   if(error == ERROR_BUFFER_OVERFLOW)
   {
      //If the size of the message containing the requested number of variable
      //bindings would be greater than the maximum message size, then the
      //response is generated with a lesser number of variable bindings
   }
   else if(error)
   {
      //Set error-status and error-index fields
      error = snmpTranslateStatusCode(&context->response, error, index);
      //If the parsing of the request fails, the SNMP agent discards the message
      if(error)
         return error;

      //The Response-PDU is re-formatted with the same values in its request-id
      //and variable-bindings fields as the received GetRequest-PDU
      error = snmpCopyVarBindingList(context);
      //Any error to report?
      if(error)
         return error;
   }

   //Successful processing
   return NO_ERROR;
#else
   //Not implemented
   return ERROR_NOT_IMPLEMENTED;
#endif
}


/**
 * @brief Process SetRequest-PDU
 * @param[in] context Pointer to the SNMP agent context
 * @return Error code
 **/

error_t snmpProcessSetRequestPdu(SnmpAgentContext *context)
{
   error_t error;
   int_t index;
   size_t n;
   size_t length;
   const uint8_t *p;
   SnmpVarBind var;

   //Debug message
   TRACE_INFO("Parsing SetRequest-PDU...\r\n");

   //Total number of SNMP Set-Request PDUs which have been accepted and
   //processed by the SNMP protocol entity
   MIB2_INC_COUNTER32(snmpGroup.snmpInSetRequests, 1);

   //Enforce access policy
   if(context->user.mode != SNMP_ACCESS_WRITE_ONLY &&
      context->user.mode != SNMP_ACCESS_READ_WRITE)
   {
      //Total number of SNMP messages delivered to the SNMP protocol entity
      //which represented an SNMP operation which was not allowed by the SNMP
      MIB2_INC_COUNTER32(snmpGroup.snmpInBadCommunityUses, 1);
      SNMP_MIB_INC_COUNTER32(snmpGroup.snmpInBadCommunityUses, 1);

      //Report an error
      return ERROR_ACCESS_DENIED;
   }

   //Initialize response message
   error = snmpInitResponse(context);
   //Any error to report?
   if(error)
      return error;

   //The variable bindings are processed as a two phase operation. In the
   //first phase, each variable binding is validated
   p = context->request.varBindList;
   length = context->request.varBindListLen;

   //Lock access to MIB bases
   snmpLockMib(context);

   //Loop through the list
   for(index = 1; length > 0; index++)
   {
      //Parse variable binding
      error = snmpParseVarBinding(p, length, &var, &n);
      //Failed to parse variable binding?
      if(error)
         break;

      //Assign object value
      error = snmpSetObjectValue(context, &context->request, &var, FALSE);
      //Any error to report?
      if(error)
         break;

      //Advance data pointer
      p += n;
      length -= n;
   }

   //If all validations are successful, then each variable is altered in
   //the second phase
   if(!error)
   {
      //The changes are committed to the MIB base during the second phase
      p = context->request.varBindList;
      length = context->request.varBindListLen;

      //Loop through the list
      for(index = 1; length > 0; index++)
      {
         //Parse variable binding
         error = snmpParseVarBinding(p, length, &var, &n);
         //Failed to parse variable binding?
         if(error)
            break;

         //Assign object value
         error = snmpSetObjectValue(context, &context->request, &var, TRUE);
         //Any error to report?
         if(error)
            break;

         //Total number of MIB objects which have been altered successfully
         //by the SNMP protocol entity as the result of receiving valid
         //SNMP Set-Request PDUs
         MIB2_INC_COUNTER32(snmpGroup.snmpInTotalSetVars, 1);

         //Advance data pointer
         p += n;
         length -= n;
      }
   }

   //Unlock access to MIB bases
   snmpUnlockMib(context);

   //Any error to report?
   if(error)
   {
      //Set error-status and error-index fields
      error = snmpTranslateStatusCode(&context->response, error, index);
      //If the parsing of the request fails, the SNMP agent discards the message
      if(error)
         return error;
   }

   //The SNMP agent sends back a GetResponse-PDU of identical form
   error = snmpCopyVarBindingList(context);
   //Return status code
   return error;
}


/**
 * @brief Format Report-PDU
 * @param[in] context Pointer to the SNMP agent context
 * @param[in] errorIndication Error indication
 * @return Error code
 **/

error_t snmpFormatReportPdu(SnmpAgentContext *context, error_t errorIndication)
{
   error_t error;

#if (SNMP_V3_SUPPORT == ENABLED)
   size_t n;
   uint32_t counter;
   SnmpVarBind var;

   //Initialize SNMP message
   snmpInitMessage(&context->response);

   //SNMP version identifier
   context->response.version = context->request.version;

   //Message identifier
   context->response.msgId = context->request.msgId;
   //Maximum message size supported by the sender
   context->response.msgMaxSize = SNMP_MAX_MSG_SIZE;
   //Bit fields which control processing of the message
   context->response.msgFlags = 0;
   //Security model used by the sender
   context->response.msgSecurityModel = SNMP_SECURITY_MODEL_USM;

   //Authoritative engine identifier
   context->response.msgAuthEngineId = context->contextEngine;
   context->response.msgAuthEngineIdLen = context->contextEngineLen;

   //Number of times the SNMP engine has rebooted
   context->response.msgAuthEngineBoots = context->engineBoots;
   //Number of seconds since last reboot
   context->response.msgAuthEngineTime = context->engineTime;

   //Context engine identifier
   context->response.contextEngineId = context->contextEngine;
   context->response.contextEngineIdLen = context->contextEngineLen;

   //Context name
   context->response.contextName = context->contextName;
   context->response.contextNameLen = osStrlen(context->contextName);

   //PDU type
   context->response.pduType = SNMP_PDU_REPORT;
   //Request identifier
   context->response.requestId = context->request.requestId;

   //If the message is considered to be outside of the time window, the error
   //must be reported with a securityLevel of authNoPriv (refer to RFC 3414,
   //section 3.2)
   if(errorIndication == ERROR_NOT_IN_TIME_WINDOW)
   {
      //Bit fields which control processing of the message
      context->response.msgFlags = context->request.msgFlags &
         (SNMP_MSG_FLAG_AUTH | SNMP_MSG_FLAG_PRIV);

      //User name
      context->response.msgUserName = context->request.msgUserName;
      context->response.msgUserNameLen = context->request.msgUserNameLen;

      //Authentication parameters
      context->response.msgAuthParameters = NULL;
      context->response.msgAuthParametersLen = context->request.msgAuthParametersLen;

      //Privacy parameters
      context->response.msgPrivParameters = context->privParameters;
      context->response.msgPrivParametersLen = context->request.msgPrivParametersLen;
   }

   //Make room for the message header at the beginning of the buffer
   error = snmpComputeMessageOverhead(&context->response);
   //Any error to report?
   if(error)
      return error;

   //Initialize counter value
   counter = 1;

   //Check error indication
   switch(errorIndication)
   {
   case ERROR_UNSUPPORTED_SECURITY_LEVEL:
      //Total number of packets received by the SNMP engine which were dropped
      //because they requested a securityLevel that was unknown to the SNMP
      //engine or otherwise unavailable
      SNMP_USM_MIB_INC_COUNTER32(usmStatsUnsupportedSecLevels , 1);
      SNMP_USM_MIB_GET_COUNTER32(counter, usmStatsUnsupportedSecLevels);

      //Add the usmStatsUnsupportedSecLevels counter in the varBindList
      var.oid = usmStatsUnsupportedSecLevelsObject;
      var.oidLen = sizeof(usmStatsUnsupportedSecLevelsObject);
      break;

   case ERROR_NOT_IN_TIME_WINDOW:
      //Total number of packets received by the SNMP engine which were dropped
      //because they appeared outside of the authoritative SNMP engine's window
      SNMP_USM_MIB_INC_COUNTER32(usmStatsNotInTimeWindows , 1);
      SNMP_USM_MIB_GET_COUNTER32(counter, usmStatsNotInTimeWindows);

      //Add the usmStatsNotInTimeWindows counter in the varBindList
      var.oid = usmStatsNotInTimeWindowsObject;
      var.oidLen = sizeof(usmStatsNotInTimeWindowsObject);
      break;

   case ERROR_UNKNOWN_USER_NAME:
      //Total number of packets received by the SNMP engine which were dropped
      //because they referenced a user that was not known to the SNMP engine
      SNMP_USM_MIB_INC_COUNTER32(usmStatsUnknownUserNames , 1);
      SNMP_USM_MIB_GET_COUNTER32(counter, usmStatsUnknownUserNames);

      //Add the usmStatsUnknownUserNames counter in the varBindList
      var.oid = usmStatsUnknownUserNamesObject;
      var.oidLen = sizeof(usmStatsUnknownUserNamesObject);
      break;

   case ERROR_UNKNOWN_ENGINE_ID:
      //Total number of packets received by the SNMP engine which were dropped
      //because they referenced an snmpEngineID that was not known to the SNMP
      //engine
      SNMP_USM_MIB_INC_COUNTER32(usmStatsUnknownEngineIDs , 1);
      SNMP_USM_MIB_GET_COUNTER32(counter, usmStatsUnknownEngineIDs);

      //Add the usmStatsUnknownEngineIDs counter in the varBindList
      var.oid = usmStatsUnknownEngineIdsObject;
      var.oidLen = sizeof(usmStatsUnknownEngineIdsObject);
      break;

   case ERROR_AUTHENTICATION_FAILED:
      //Total number of packets received by the SNMP engine which were dropped
      //because they didn't contain the expected digest value
      SNMP_USM_MIB_INC_COUNTER32(usmStatsWrongDigests , 1);
      SNMP_USM_MIB_GET_COUNTER32(counter, usmStatsWrongDigests);

      //Add the usmStatsWrongDigests counter in the varBindList
      var.oid = usmStatsWrongDigestsObject;
      var.oidLen = sizeof(usmStatsWrongDigestsObject);
      break;

   case ERROR_DECRYPTION_FAILED:
      //Total number of packets received by the SNMP engine which were dropped
      //because they could not be decrypted
      SNMP_USM_MIB_INC_COUNTER32(usmStatsDecryptionErrors , 1);
      SNMP_USM_MIB_GET_COUNTER32(counter, usmStatsDecryptionErrors);

      //Add the usmStatsDecryptionErrors counter in the varBindList
      var.oid = usmStatsDecryptionErrorsObject;
      var.oidLen = sizeof(usmStatsDecryptionErrorsObject);
      break;

   case ERROR_UNAVAILABLE_CONTEXT:
      //Total number of packets received by the SNMP engine which were dropped
      //because the context contained in the message was unavailable
      counter = 1;

      //Add the snmpUnavailableContexts counter in the varBindList
      var.oid = snmpUnavailableContextsObject;
      var.oidLen = sizeof(snmpUnavailableContextsObject);
      break;

   case ERROR_UNKNOWN_CONTEXT:
      //Total number of packets received by the SNMP engine which were dropped
      //because the context contained in the message was unknown
      counter = 1;

      //Add the snmpUnknownContexts counter in the varBindList
      var.oid = snmpUnknownContextsObject;
      var.oidLen = sizeof(snmpUnknownContextsObject);
      break;

   default:
      //Just for sanity's sake...
      var.oid = NULL;
      var.oidLen = 0;
      break;
   }

   //Encode the object value using ASN.1 rules
   error = snmpEncodeUnsignedInt32(counter, context->response.buffer, &n);
   //Any error to report?
   if(error)
      return error;

   //The counter is encoded in ASN.1 format
   var.objClass = ASN1_CLASS_APPLICATION;
   var.objType = MIB_TYPE_COUNTER32;
   var.value = context->response.buffer;
   var.valueLen = n;

   //Append the variable binding list to the varBindList
   error = snmpWriteVarBinding(context, &var);
   //Any error to report?
   if(error)
      return error;

   //Format PDU header
   error = snmpWritePduHeader(&context->response);
#else
   //SNMPv3 is not supported
   error = ERROR_NOT_IMPLEMENTED;
#endif

   //Return status code
   return error;
}

#endif
