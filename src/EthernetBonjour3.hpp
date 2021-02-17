//  Copyright (C) 2010 Georg Kaindl
//  http://gkaindl.com
//
//  This file is part of Arduino EthernetBonjour3, ported from
//  Arduino EthernetBonjour to be used with ethernet shield v2
//  (with W5500 chip).
//  Need Ethernet3 lib (because Adafruits Ethernet2 has no Multicast
//  UDP Support
//
//  EthernetBonjour3 is free software: you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public License
//  as published by the Free Software Foundation, either version 3 of
//  the License, or (at your option) any later version.
//
//  EthernetBonjour3 is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with EthernetBonjour3. If not, see
//  <http://www.gnu.org/licenses/>.
//

#include <string.h>
#include <stdlib.h>

#include "EthernetBonjour3_Namespace.h"

BEGIN_MDNS_NAMESPACE

#define MDNS_DEFAULT_NAME "arduino"
#define MDNS_TLD ".local"
#define DNS_SD_SERVICE "_services._dns-sd._udp.local"
#define MDNS_SERVER_PORT (5353)
#define MDNS_NQUERY_RESEND_TIME (1000)	// 1 second, name query resend timeout
#define MDNS_SQUERY_RESEND_TIME (10000) // 10 seconds, service query resend timeout
#define MDNS_RESPONSE_TTL (2*60)		// two minutes (in seconds)
#define MDNS_RESPONSE_TTL_10 (10*60)	// ten minutes (in seconds)

#define MDNS_MAX_SERVICES_PER_PACKET (6)

static uint8_t mdnsMulticastIPAddr[] = {224, 0, 0, 251};

typedef enum _MDNSPacketType_t
{
	MDNSPacketTypeMyIPAnswer,
	MDNSPacketTypeNoIPv6AddrAvailable,
	MDNSPacketTypeServiceRecord,
	MDNSPacketTypeServiceRecordRelease,
	MDNSPacketTypeNameQuery,
	MDNSPacketTypeServiceQuery,
} MDNSPacketType_t;

typedef struct _DNSHeader_t
{
	uint16_t xid;
	uint8_t recursionDesired : 1;
	uint8_t truncated : 1;
	uint8_t authoritiveAnswer : 1;
	uint8_t opCode : 4;
	uint8_t queryResponse : 1;
	uint8_t responseCode : 4;
	uint8_t checkingDisabled : 1;
	uint8_t authenticatedData : 1;
	uint8_t zReserved : 1;
	uint8_t recursionAvailable : 1;
	uint16_t queryCount;
	uint16_t answerCount;
	uint16_t authorityCount;
	uint16_t additionalCount;
} __attribute__((__packed__)) DNSHeader_t;

typedef enum _DNSOpCode_t
{
	DNSOpQuery = 0,
	DNSOpIQuery = 1,
	DNSOpStatus = 2,
	DNSOpNotify = 4,
	DNSOpUpdate = 5
} DNSOpCode_t;

template <class UdpClass>
EthernetBonjour3Class<UdpClass>::EthernetBonjour3Class(const char *bonjourName)
{
	memset(&this->_mdnsData, 0, sizeof(MDNSDataInternal_t));
	memset(&this->_serviceRecords, 0, sizeof(this->_serviceRecords));

	this->_state = MDNSStateIdle;

	this->_bonjourName = NULL;
	this->setBonjourName(bonjourName);

	this->_resolveNames[0] = NULL;
	this->_resolveNames[1] = NULL;

	this->_lastAnnounceMillis = 0;
}

// return values:
// 1 on success
// 0 otherwise
template <class UdpClass>
int EthernetBonjour3Class<UdpClass>::begin(IPAddress localIP)
{
	// if we were called very soon after the board was booted, we need to give the
	// EthernetShield (WIZnet) some time to come up. Hence, we delay until millis() is at
	// least 3000. This is necessary, so that if we need to add a service record directly
	// after begin, the announce packet does not get lost in the bowels of the WIZnet chip.
	//while (millis() < 3000) delay(100);

	_localIP = localIP;

	Serial.println(localIP);
	Serial.println(_localIP);

	return _socket.beginMulticast(mdnsMulticastIPAddr, MDNS_SERVER_PORT);
}

// return values:
// 1 on success
// 0 otherwise
template <class UdpClass>
int EthernetBonjour3Class<UdpClass>::_initQuery(uint8_t idx, const char *name, unsigned long timeout)
{
	Serial.println("_initQuery");

	int statusCode = 0;

	if (NULL == this->_resolveNames[idx] && NULL != ((0 == idx) ? (void *)this->_nameFoundCallback : (void *)this->_serviceFoundCallback))
	{
		this->_resolveNames[idx] = (uint8_t *)name;

		if (timeout)
			this->_resolveTimeouts[idx] = millis() + timeout;
		else
			this->_resolveTimeouts[idx] = 0;

		statusCode = (MDNSSuccess == this->_sendMDNSMessage(0,
															0,
															(idx == 0) ? MDNSPacketTypeNameQuery : MDNSPacketTypeServiceQuery,
															0));
	}
	else
		free((void *)name);

	return statusCode;
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::_cancelQuery(uint8_t idx)
{
	if (NULL != this->_resolveNames[idx])
	{
		free(this->_resolveNames[idx]);
		this->_resolveNames[idx] = NULL;
	}
}

// return values:
// 1 on success
// 0 otherwise
template <class UdpClass>
int EthernetBonjour3Class<UdpClass>::resolveName(const char *name, unsigned long timeout)
{
	this->cancelResolveName();

	char *n = (char *)malloc(strlen(name) + 7);
	if (NULL == n)
		return 0;

	strcpy(n, name);
	strcat(n, MDNS_TLD);

	return this->_initQuery(0, n, timeout);
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::setNameResolvedCallback(BonjourNameFoundCallback newCallback)
{
	this->_nameFoundCallback = newCallback;
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::cancelResolveName()
{
	this->_cancelQuery(0);
}

template <class UdpClass>
int EthernetBonjour3Class<UdpClass>::isResolvingName()
{
	return (NULL != this->_resolveNames[0]);
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::setServiceFoundCallback(BonjourServiceFoundCallback newCallback)
{
	this->_serviceFoundCallback = newCallback;
}

// return values:
// 1 on success
// 0 otherwise
template <class UdpClass>
int EthernetBonjour3Class<UdpClass>::startDiscoveringService(const char *serviceName,
															 MDNSServiceProtocol_t proto,
															 unsigned long timeout)
{
	this->stopDiscoveringService();

	char *n = (char *)malloc(strlen(serviceName) + 13);
	if (NULL == n)
		return 0;

	strcpy(n, serviceName);

	const uint8_t *srv_type = this->_postfixForProtocol(proto);
	if (srv_type)
		strcat(n, (const char *)srv_type);

	this->_resolveServiceProto = proto;

	return this->_initQuery(1, n, timeout);
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::stopDiscoveringService()
{
	this->_cancelQuery(1);
}

template <class UdpClass>
int EthernetBonjour3Class<UdpClass>::isDiscoveringService()
{
	return (NULL != this->_resolveNames[1]);
}

// return value:
// A DNSError_t (DNSSuccess on success, something else otherwise)
// in "int" mode: positive on success, negative on error
template <class UdpClass>
MDNSError_t EthernetBonjour3Class<UdpClass>::_sendMDNSMessage(uint32_t peerAddress, uint32_t xid, int type,
															  int serviceRecord)
{
	MDNSError_t statusCode = MDNSSuccess;
	uint16_t ptr = 0;

	DNSHeader_t dnsHeaderBuf;
	DNSHeader_t *dnsHeader = &dnsHeaderBuf;

	uint8_t *buf;

	memset(dnsHeader, 0, sizeof(DNSHeader_t));

	dnsHeader->xid = __htons(xid);
	dnsHeader->opCode = DNSOpQuery;

	switch (type)
	{
	case MDNSPacketTypeServiceRecordRelease:
	case MDNSPacketTypeMyIPAnswer:
		dnsHeader->answerCount = __htons(1);
		dnsHeader->queryResponse = 1;
		dnsHeader->authoritiveAnswer = 1;
		break;
	case MDNSPacketTypeServiceRecord:
		dnsHeader->answerCount = __htons(4);
		dnsHeader->additionalCount = __htons(1);
		dnsHeader->queryResponse = 1;
		dnsHeader->authoritiveAnswer = 1;
		break;
	case MDNSPacketTypeNameQuery:
	case MDNSPacketTypeServiceQuery:
		dnsHeader->queryCount = __htons(1);
		break;
	case MDNSPacketTypeNoIPv6AddrAvailable:
		dnsHeader->queryCount = __htons(1);
		dnsHeader->additionalCount = __htons(1);
		dnsHeader->responseCode = 0x03;
		dnsHeader->authoritiveAnswer = 1;
		dnsHeader->queryResponse = 1;
		break;
	}

	Serial.print("_sendMDNSMessage ");
	Serial.print("peerAddress:");
	Serial.print(peerAddress);
	Serial.print(" xid:");
	Serial.print(xid);
	Serial.print(" type:");
	Serial.print(type);
	Serial.print(" serviceRecord:");
	Serial.println(serviceRecord);

	_socket.beginPacket(mdnsMulticastIPAddr, MDNS_SERVER_PORT);
	_socket.write((uint8_t *)dnsHeader, sizeof(DNSHeader_t));

	ptr += sizeof(DNSHeader_t);
	buf = (uint8_t *)dnsHeader;

	// construct the answer section
	switch (type)
	{
	case MDNSPacketTypeMyIPAnswer:
	{
		this->_writeMyIPAnswerRecord(&ptr, buf, sizeof(DNSHeader_t));
		break;
	}

	case MDNSPacketTypeServiceRecord:
	{

		// SRV location record
		this->_writeServiceRecordName(serviceRecord, &ptr, buf, sizeof(DNSHeader_t), 0);

		buf[0] = 0x00;
		buf[1] = 0x21; // SRV record
		buf[2] = 0x80; // cache flush
		buf[3] = 0x01; // class IN

		// ttl
		*((uint32_t *)&buf[4]) = __htonl(MDNS_RESPONSE_TTL_10);

		// data length
		*((uint16_t *)&buf[8]) = __htons(8 + strlen((char *)this->_bonjourName));

		_socket.write((uint8_t *)buf, 10);
		ptr += 10;
		// priority and weight
		buf[0] = buf[1] = buf[2] = buf[3] = 0;

		// port
		*((uint16_t *)&buf[4]) = __htons(this->_serviceRecords[serviceRecord]->port);

		_socket.write((uint8_t *)buf, 6);
		ptr += 6;
		// target
		this->_writeDNSName(this->_bonjourName, &ptr, buf, sizeof(DNSHeader_t), 1);

		// TXT record
		this->_writeServiceRecordName(serviceRecord, &ptr, buf, sizeof(DNSHeader_t), 0);

		buf[0] = 0x00;
		buf[1] = 0x10; // TXT record
		buf[2] = 0x80; // cache flush
		buf[3] = 0x01; // class IN

		// ttl
		*((uint32_t *)&buf[4]) = __htonl(MDNS_RESPONSE_TTL_10);

		_socket.write((uint8_t *)buf, 8);
		ptr += 8;

		// data length && text
		if (NULL == this->_serviceRecords[serviceRecord]->textContent)
		{
			buf[0] = 0x00;
			buf[1] = 0x01;
			buf[2] = 0x00;

			_socket.write((uint8_t *)buf, 3);
			ptr += 3;
		}
		else
		{
			int slen = strlen((char *)this->_serviceRecords[serviceRecord]->textContent);
			*((uint16_t *)buf) = __htons(slen);
			_socket.write((uint8_t *)buf, 2);
			ptr += 2;

			_socket.write((uint8_t *)this->_serviceRecords[serviceRecord]->textContent, slen);
			ptr += slen;
		}

		// PTR record (for the dns-sd service in general)
		this->_writeDNSName((const uint8_t *)DNS_SD_SERVICE, &ptr, buf,
							sizeof(DNSHeader_t), 1);

		buf[0] = 0x00;
		buf[1] = 0x0c; // PTR record
		buf[2] = 0x00; // no cache flush
		buf[3] = 0x01; // class IN

		// ttl
		*((uint32_t *)&buf[4]) = __htonl(MDNS_RESPONSE_TTL);

		// data length.
		uint16_t dlen = strlen((char *)this->_serviceRecords[serviceRecord]->servName) + 2;
		*((uint16_t *)&buf[8]) = __htons(dlen);

		_socket.write((uint8_t *)buf, 10);
		ptr += 10;

		this->_writeServiceRecordName(serviceRecord, &ptr, buf, sizeof(DNSHeader_t), 1);

		// PTR record (our service)
		this->_writeServiceRecordPTR(serviceRecord, &ptr, buf, sizeof(DNSHeader_t), MDNS_RESPONSE_TTL_10);

		// finally, our IP address as additional record
		this->_writeMyIPAnswerRecord(&ptr, buf, sizeof(DNSHeader_t));

		break;
	}

	case MDNSPacketTypeServiceRecordRelease:
	{
		// just send our service PTR with a TTL of zero
		this->_writeServiceRecordPTR(serviceRecord, &ptr, buf, sizeof(DNSHeader_t), 0);
		break;
	}
	case MDNSPacketTypeNameQuery:
	case MDNSPacketTypeServiceQuery:
	{
		// construct a query for the currently set _resolveNames[0]
		this->_writeDNSName(
			(type == MDNSPacketTypeServiceQuery) ? this->_resolveNames[1] : this->_resolveNames[0],
			&ptr, buf, sizeof(DNSHeader_t), 1);

		buf[0] = buf[2] = 0x0;
		buf[1] = (type == MDNSPacketTypeServiceQuery) ? 0x0c : 0x01;
		buf[3] = 0x1;

		//		_socket.write((uint8_t *)buf, sizeof(DNSHeader_t));
		//		ptr += sizeof(DNSHeader_t);

		_socket.write((uint8_t *)buf, 4);
		ptr += 4;

		this->_resolveLastSendMillis[(type == MDNSPacketTypeServiceQuery) ? 1 : 0] = millis();

		break;
	}
	case MDNSPacketTypeNoIPv6AddrAvailable:
	{
		// since the WIZnet doesn't have IPv6, we will respond with a Not Found message
		this->_writeDNSName(this->_bonjourName, &ptr, buf, sizeof(DNSHeader_t), 1);

		buf[0] = buf[2] = 0x0;
		buf[1] = 0x1c; // AAAA record
		buf[3] = 0x01;

		_socket.write((uint8_t *)buf, 4);
		ptr += 4;

		// send our IPv4 address record as additional record, in case the peer wants it.
		this->_writeMyIPAnswerRecord(&ptr, buf, sizeof(DNSHeader_t));

		break;
	}
	}

	_socket.endPacket();

errorReturn:

	return statusCode;
}

// return value:
// A DNSError_t (DNSSuccess on success, something else otherwise)
// in "int" mode: positive on success, negative on error
template <class UdpClass>
MDNSError_t EthernetBonjour3Class<UdpClass>::_processMDNSQuery()
{
	MDNSError_t statusCode = MDNSSuccess;

	DNSHeader_t dnsHeaderBuf;
	DNSHeader_t *dnsHeader = &dnsHeaderBuf;

	int i, j;
	uint8_t *buf;
	uint32_t xid;
	uint16_t udp_len, qCnt, aCnt, aaCnt, addCnt;
	uint8_t recordsAskedFor[NumMDNSServiceRecords + 2];
	uint8_t recordsFound[2];
	uint8_t wantsIPv6Addr = 0;
	uint8_t *udpBuffer = NULL;
	uintptr_t ptr;

	memset(recordsAskedFor, 0, sizeof(uint8_t) * (NumMDNSServiceRecords + 2));
	memset(recordsFound, 0, sizeof(uint8_t) * 2);

	udp_len = _socket.parsePacket();
	if (0 == udp_len)
	{
		statusCode = MDNSTryLater;
		goto errorReturn;
	}

	udpBuffer = (uint8_t *)malloc(udp_len); //allocate memory to hold _remaining UDP packet
	if (NULL == udpBuffer)
	{
		_socket.flush();
		statusCode = MDNSOutOfMemory;
		goto errorReturn;
	}
	_socket.read((uint8_t *)udpBuffer, udp_len); //read _remaining UDP packet from W5100/W5200 into memory
	ptr = (uintptr_t)udpBuffer;

	buf = (uint8_t *)dnsHeader;
	memcpy((uint8_t *)buf, (uint16_t *)ptr, sizeof(DNSHeader_t));

	xid = __ntohs(dnsHeader->xid);
	qCnt = __ntohs(dnsHeader->queryCount);
	aCnt = __ntohs(dnsHeader->answerCount);
	aaCnt = __ntohs(dnsHeader->authorityCount);
	addCnt = __ntohs(dnsHeader->additionalCount);

	Serial.print("_processMDNSQuery");
	Serial.print(" len: ");
	Serial.print(udp_len + 42);
	Serial.print(" dnsHeader->queryResponse: ");
	Serial.print(dnsHeader->queryResponse);
	Serial.print(" dnsHeader->opCode: ");
	Serial.print(dnsHeader->opCode);
	Serial.println("");

	if (0 == dnsHeader->queryResponse &&
		DNSOpQuery == dnsHeader->opCode &&
		MDNS_SERVER_PORT == _socket.remotePort())
	{
		Serial.print("Message is a query");
		Serial.print(" qCnt: ");
		Serial.print(qCnt);
		Serial.print(" aCnt: ");
		Serial.print(aCnt);
		Serial.println("");

		// process an MDNS query
		int offset = sizeof(DNSHeader_t);
		uint8_t *buf = (uint8_t *)dnsHeader;
		int rLen = 0, tLen = 0;

		// read over the query section
		for (i = 0; i < qCnt; i++)
		{
			// construct service name data structures for comparison
			const uint8_t *servNames[NumMDNSServiceRecords + 2];
			int servLens[NumMDNSServiceRecords + 2];
			uint8_t servNamePos[NumMDNSServiceRecords + 2];
			uint8_t servMatches[NumMDNSServiceRecords + 2];

			// first entry is our own MDNS name, the rest are our services
			servNames[0] = (const uint8_t *)this->_bonjourName;
			servNamePos[0] = 0;
			servLens[0] = strlen((char *)this->_bonjourName);
			servMatches[0] = 1;

			// second entry is our own the general DNS-SD service
			servNames[1] = (const uint8_t *)DNS_SD_SERVICE;
			servNamePos[1] = 0;
			servLens[1] = strlen((char *)DNS_SD_SERVICE);
			servMatches[1] = 1;

			for (j = 2; j < NumMDNSServiceRecords + 2; j++)
			{
				if (NULL != this->_serviceRecords[j - 2] &&
					NULL != this->_serviceRecords[j - 2]->servName)
				{
					servNames[j] = this->_serviceRecords[j - 2]->servName;
					servLens[j] = strlen((char *)servNames[j]);
					servMatches[j] = 1;
					servNamePos[j] = 0;
				}
				else
				{
					servNames[j] = NULL;
					servLens[j] = 0;
					servMatches[j] = 0;
					servNamePos[j] = 0;
				}
			}

			tLen = 0;
			do
			{
				memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 1);
				offset += 1;

				rLen = buf[0]; // 1st byte has the length
				tLen += 1;

				if (rLen > 128)
				{ // handle DNS name compression, kinda, sorta

					memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 1);
					offset += 1;

					for (j = 0; j < NumMDNSServiceRecords + 2; j++)
					{
						if (servNamePos[j] && servNamePos[j] != buf[0])
						{
							servMatches[j] = 0; // no match
						}
					}

					tLen += 1;
				}
				else if (rLen > 0)
				{
					int tr = rLen, ir;

					while (tr > 0)
					{
						ir = (tr > sizeof(DNSHeader_t)) ? sizeof(DNSHeader_t) : tr;

						memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), ir);
						offset += ir;
						tr -= ir;

						for (j = 0; j < NumMDNSServiceRecords + 2; j++)
						{
/*	Serial.print("recordsAskedFor[j]:");
	Serial.print(recordsAskedFor[j]);

	Serial.print("  servMatches[");
	Serial.print(j);
	Serial.print("]:");
	Serial.println(servMatches[j]);
*/
							if (!recordsAskedFor[j] && servMatches[j])
							{
/*
Serial.println("jajajajajajajaja ^^^ ^^^^ ^^^^^ ^^^^ ^^^^ ^^^^ ^^^^ ^^^^ ^^^^ ^^^^^^^^^^^");
Serial.print(" servNames[");
Serial.print(j);
Serial.print("]: ");
for (int r = 0; r < servLens[j]; r++)
	Serial.print((char)servNames[j][r]);
Serial.println("");
Serial.print("buf: ");
for (int r = 0; r < ir; r++)
	Serial.print((char)buf[r]);
Serial.println("");
*/
								auto match = this->_matchStringPart(&servNames[j], &servLens[j], buf, ir);
if (match)
		Serial.println("match match match -------------------------------");

								servMatches[j] &= match;
if (match) {
	Serial.print("servMatches[");
	Serial.print(j);
	Serial.print("]: ");
	Serial.println(servMatches[j]);
}

							}
						}
					}

					tLen += rLen;
				}
			} while (rLen > 0 && rLen <= 128);

Serial.println("*  *  *  *  *  *  *  *  * *  *  *  *  *  *  *");
for (j = 0; j < NumMDNSServiceRecords + 2; j++)
{
	Serial.print("recordsAskedFor[j]:");
	Serial.print(recordsAskedFor[j]);
				Serial.print("  servMatches[");
				Serial.print(j);
				Serial.print("]:");
	Serial.println(servMatches[j]);
}

			// if this matched a name of ours (and there are no characters left), then
			// check whether this is an A record query (for our own name) or a PTR record query
			// (for one of our services).
			// if so, we'll note to send a record

			memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 4);
			offset += 4;

			for (j = 0; j < NumMDNSServiceRecords + 2; j++)
			{
				if (!recordsAskedFor[j] && servNames[j] && servMatches[j] && 0 == servLens[j])
				{
Serial.println("#################################################################################");

					if (0 == servNamePos[j])
						servNamePos[j] = offset - 4 - tLen;

					if (buf[0] == 0 &&
						buf[3] == 0x01 &&
						(buf[2] == 0x00 ||
						 buf[2] == 0x80))
					{

						if ((0 == j && 0x01 == buf[1]) || (0 < j && (0x0c == buf[1] || 0x10 == buf[1] || 0x21 == buf[1])))
							recordsAskedFor[j] = 1;
						else if (0 == j && 0x1c == buf[1])
							wantsIPv6Addr = 1;
					}
				}
			}
		}
	}
	else if (1 == dnsHeader->queryResponse &&
			 DNSOpQuery == dnsHeader->opCode &&
			 MDNS_SERVER_PORT == _socket.remotePort() &&
			 (NULL != this->_resolveNames[0] || NULL != this->_resolveNames[1]))
	{
		Serial.print("Message is a response");

		int offset = sizeof(DNSHeader_t);
		uint8_t *buf = (uint8_t *)dnsHeader;
		int rLen = 0, tLen = 0;

		uint8_t *ptrNames[MDNS_MAX_SERVICES_PER_PACKET];
		uint16_t ptrOffsets[MDNS_MAX_SERVICES_PER_PACKET];
		uint16_t ptrPorts[MDNS_MAX_SERVICES_PER_PACKET];
		uint8_t ptrIPs[MDNS_MAX_SERVICES_PER_PACKET];
		uint8_t servIPs[MDNS_MAX_SERVICES_PER_PACKET][5];
		uint8_t *servTxt[MDNS_MAX_SERVICES_PER_PACKET];
		memset(servIPs, 0, sizeof(uint8_t) * MDNS_MAX_SERVICES_PER_PACKET * 5);
		memset(servTxt, 0, sizeof(uint8_t *) * MDNS_MAX_SERVICES_PER_PACKET);

		const uint8_t *ptrNamesCmp[MDNS_MAX_SERVICES_PER_PACKET];
		int ptrLensCmp[MDNS_MAX_SERVICES_PER_PACKET];
		uint8_t ptrNamesMatches[MDNS_MAX_SERVICES_PER_PACKET];

		uint8_t checkAARecords = 0;
		memset(ptrNames, 0, sizeof(uint8_t *) * MDNS_MAX_SERVICES_PER_PACKET);

		const uint8_t *servNames[2];
		uint8_t servNamePos[2];
		int servLens[2];
		uint8_t servMatches[2];
		uint8_t firstNamePtrByte = 0;
		uint8_t partMatched[2];
		uint8_t lastWasCompressed[2];
		uint8_t servWasCompressed[2];

		servNamePos[0] = servNamePos[1] = 0;

		for (i = 0; i < qCnt + aCnt + aaCnt + addCnt; i++)
		{

			for (j = 0; j < 2; j++)
			{
				if (NULL != this->_resolveNames[j])
				{
					servNames[j] = this->_resolveNames[j];
					servLens[j] = strlen((const char *)this->_resolveNames[j]);
					servMatches[j] = 1;
				}
				else
				{
					servNames[j] = NULL;
					servLens[j] = servMatches[j] = 0;
				}
			}

			for (j = 0; j < MDNS_MAX_SERVICES_PER_PACKET; j++)
			{
				if (NULL != ptrNames[j])
				{
					ptrNamesCmp[j] = ptrNames[j];
					ptrLensCmp[j] = strlen((const char *)ptrNames[j]);
					ptrNamesMatches[j] = 1;
				}
			}

			partMatched[0] = partMatched[1] = 0;
			lastWasCompressed[0] = lastWasCompressed[1] = 0;
			servWasCompressed[0] = servWasCompressed[1] = 0;
			firstNamePtrByte = 0;
			tLen = 0;

			do
			{
				memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 1);
				offset += 1;
				rLen = buf[0];
				tLen += 1;

				if (rLen > 128)
				{ // handle DNS name compression, kinda, sorta...

					memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 1);
					offset += 1;

					for (j = 0; j < 2; j++)
					{
						if (servNamePos[j] && servNamePos[j] != buf[0])
							servMatches[j] = 0;
						else
							servWasCompressed[j] = 1;

						lastWasCompressed[j] = 1;
					}

					tLen += 1;

					if (0 == firstNamePtrByte)
						firstNamePtrByte = buf[0];
				}
				else if (rLen > 0)
				{
					if (i < qCnt)
						offset += rLen;
					else
					{
						int tr = rLen, ir;

						if (0 == firstNamePtrByte)
							firstNamePtrByte = offset - 1; // -1, since we already read length (1 byte)

						while (tr > 0)
						{
							ir = (tr > sizeof(DNSHeader_t)) ? sizeof(DNSHeader_t) : tr;
							memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), ir);
							offset += ir;
							tr -= ir;

							for (j = 0; j < 2; j++)
							{
								if (!recordsFound[j] && servMatches[j] && servNames[j])
									servMatches[j] &= this->_matchStringPart(&servNames[j], &servLens[j],
																			 buf, ir);
								if (!partMatched[j])
									partMatched[j] = servMatches[j];

								lastWasCompressed[j] = 0;
							}

							for (j = 0; j < MDNS_MAX_SERVICES_PER_PACKET; j++)
							{
								if (NULL != ptrNames[j] && ptrNamesMatches[j])
								{
									// only compare the part we have. this is incorrect, but good enough,
									// since actual MDNS implementations won't go here anyways, as they
									// should use name compression. This is just so that multiple Arduinos
									// running this MDNSResponder code should be able to find each other's
									// services.
									if (ptrLensCmp[j] >= ir)
										ptrNamesMatches[j] &= this->_matchStringPart(&ptrNamesCmp[j],
																					 &ptrLensCmp[j], buf, ir);
								}
							}
						}

						tLen += rLen;
					}
				}
			} while (rLen > 0 && rLen <= 128);

			// if this matched a name of ours (and there are no characters left), then
			// check wether this is an A record query (for our own name) or a PTR record query
			// (for one of our services).
			// if so, we'll note to send a record
			if (i < qCnt)
				offset += 4;
			else if (i >= qCnt)
			{
				if (i >= qCnt + aCnt && !checkAARecords)
					break;

				uint8_t packetHandled = 0;

				memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 4);
				offset += 4;
				if (i < qCnt + aCnt)
				{
					for (j = 0; j < 2; j++)
					{
						if (0 == servNamePos[j])
							servNamePos[j] = offset - 4 - tLen;

						if (servNames[j] &&
							((servMatches[j] && 0 == servLens[j]) ||
							 (partMatched[j] && lastWasCompressed[j]) ||
							 (servWasCompressed[j] && servMatches[j])))
						{ // somewhat handle compression by guessing

							if (buf[0] == 0 && buf[1] == ((0 == j) ? 0x01 : 0x0c) &&
								(buf[2] == 0x00 || buf[2] == 0x80) && buf[3] == 0x01)
							{
								recordsFound[j] = 1;

								// this is an A or PTR type response. Parse it as such.

								memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 6);
								offset += 6;
								//uint32_t ttl = ethutil_ntohl(*(uint32_t*)buf);
								uint16_t dataLen = __ntohs(*(uint16_t *)&buf[4]);

								if (0 == j && 4 == dataLen)
								{
									// ok, this is the IP address. report it via callback.

									memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 4);

									this->_finishedResolvingName((char *)this->_resolveNames[0],
																 (const byte *)buf);
								}
								else if (1 == j)
								{
									uint8_t k;
									for (k = 0; k < MDNS_MAX_SERVICES_PER_PACKET; k++)
										if (NULL == ptrNames[k])
											break;

									if (k < MDNS_MAX_SERVICES_PER_PACKET)
									{
										int l = dataLen - 2; // -2: data compression of service postfix

										uint8_t *ptrName = (uint8_t *)malloc(l);

										if (ptrName)
										{

											memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 1);
											memcpy((uint8_t *)ptrName, (uint16_t *)(ptr + offset + 1), l - 1);

											if (buf[0] < l - 1)
												ptrName[buf[0]]; // this catches uncompressed names
											else
												ptrName[l - 1] = '\0';

											ptrNames[k] = ptrName;
											ptrOffsets[k] = (uint16_t)(offset);

											checkAARecords = 1;
										}
									}
								}
								offset += dataLen;
								packetHandled = 1;
							}
						}
					}
				}
				else if (i >= qCnt + aCnt + aaCnt)
				{
					//  check whether we find a service description
					if (buf[1] == 0x21)
					{
						for (j = 0; j < MDNS_MAX_SERVICES_PER_PACKET; j++)
						{
							if (ptrNames[j] &&
								((firstNamePtrByte && firstNamePtrByte == ptrOffsets[j]) ||
								 (0 == ptrLensCmp[j] && ptrNamesMatches[j])))
							{
								// we have found the matching SRV location packet to a previous SRV domain

								memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 6);
								offset += 6;

								//uint32_t ttl = ethutil_ntohl(*(uint32_t*)buf);
								uint16_t dataLen = __ntohs(*(uint16_t *)&buf[4]);

								if (dataLen >= 8)
								{

									memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 8);
									ptrPorts[j] = __ntohs(*(uint16_t *)&buf[4]);

									if (buf[6] > 128)
									{ // target is a compressed name
										ptrIPs[j] = buf[7];
									}
									else
									{ // target is uncompressed
										ptrIPs[j] = offset + 6;
									}
								}
								offset += dataLen;
								packetHandled = 1;

								break;
							}
						}
					}
					else if (buf[1] == 0x10)
					{ // txt record
						for (j = 0; j < MDNS_MAX_SERVICES_PER_PACKET; j++)
						{
							if (ptrNames[j] &&
								((firstNamePtrByte && firstNamePtrByte == ptrOffsets[j]) ||
								 (0 == ptrLensCmp[j] && ptrNamesMatches[j])))
							{

								memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 6);
								offset += 6;

								//uint32_t ttl = ethutil_ntohl(*(uint32_t*)buf);
								uint16_t dataLen = __ntohs(*(uint16_t *)&buf[4]);

								// if there's a content to this txt record, save it for delivery
								if (dataLen > 1 && NULL == servTxt[j])
								{
									servTxt[j] = (uint8_t *)malloc(dataLen + 1);
									if (NULL != servTxt[j])
									{

										memcpy((uint8_t *)servTxt[j], (uint16_t *)(ptr + offset), dataLen);

										// zero-terminate
										servTxt[j][dataLen] = '\0';
									}
								}
								offset += dataLen;
								packetHandled = 1;

								break;
							}
						}
					}
					else if (buf[1] == 0x01)
					{ // A record (IPv4 address)
						for (j = 0; j < MDNS_MAX_SERVICES_PER_PACKET; j++)
						{
							if (0 == servIPs[j][0])
							{
								servIPs[j][0] = firstNamePtrByte ? firstNamePtrByte : 255;

								memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 6);
								offset += 6;

								uint16_t dataLen = __ntohs(*(uint16_t *)&buf[4]);
								if (4 == dataLen)
								{
									memcpy((uint8_t *)&servIPs[j][1], (uint16_t *)(ptr + offset), 4);
								}
								offset += dataLen;
								packetHandled = 1;

								break;
							}
						}
					}
				}

				// eat the answer
				if (!packetHandled)
				{
					offset += 4; // ttl
					memcpy((uint8_t *)buf, (uint16_t *)(ptr + offset), 2);
					offset += 2 + __ntohs(*(uint16_t *)buf); // skip over content
				}
			}
		}

		// deliver the services discovered in this packet
		if (NULL != this->_resolveNames[1])
		{
			char *typeName = (char *)this->_resolveNames[1];
			char *p = (char *)this->_resolveNames[1];
			while (*p && *p != '.')
				p++;
			*p = '\0';

			for (i = 0; i < MDNS_MAX_SERVICES_PER_PACKET; i++)
				if (ptrNames[i])
				{
					const uint8_t *ipAddr = NULL;
					const uint8_t *fallbackIpAddr = NULL;

					for (j = 0; j < MDNS_MAX_SERVICES_PER_PACKET; j++)
					{
						if (servIPs[j][0] == ptrIPs[i] || servIPs[j][0] == 255)
						{
							// the || part is such a hack, but it will work as long as there's only
							// one A record per MDNS packet. fucking DNS name compression.
							ipAddr = &servIPs[j][1];

							break;
						}
						else if (NULL == fallbackIpAddr && 0 != servIPs[j][0])
							fallbackIpAddr = &servIPs[j][1];
					}

					// if we can't find a matching IP, we try to use the first one we found.
					if (NULL == ipAddr)
						ipAddr = fallbackIpAddr;

					if (ipAddr && this->_serviceFoundCallback)
					{
						this->_serviceFoundCallback(typeName,
													this->_resolveServiceProto,
													(const char *)ptrNames[i],
													(const byte *)ipAddr,
													(unsigned short)ptrPorts[i],
													(const char *)servTxt[i]);
					}
				}
			*p = '.';
		}

		uint8_t k;
		for (k = 0; k < MDNS_MAX_SERVICES_PER_PACKET; k++)
			if (NULL != ptrNames[k])
			{
				free(ptrNames[k]);
				if (NULL != servTxt[k])
					free(servTxt[k]);
			}
	}
	free(udpBuffer);

errorReturn:
	// now, handle the requests
	for (j = 0; j < NumMDNSServiceRecords + 2; j++)
	{
		if (recordsAskedFor[j])
		{
			if (0 == j)
				(void)this->_sendMDNSMessage(_socket.remoteIP(), xid, (int)MDNSPacketTypeMyIPAnswer, 0);
			else if (1 == j)
			{
				uint8_t k = 2;
				for (k = 0; k < NumMDNSServiceRecords; k++)
					recordsAskedFor[k + 2] = 1;
			}
			else if (NULL != this->_serviceRecords[j - 2])
				(void)this->_sendMDNSMessage(_socket.remoteIP(), xid, (int)MDNSPacketTypeServiceRecord, j - 2);
		}
	}

	// if we were asked for our IPv6 address, say that we don't have any
	if (wantsIPv6Addr)
		(void)this->_sendMDNSMessage(_socket.remoteIP(), xid, (int)MDNSPacketTypeNoIPv6AddrAvailable, 0);

	return statusCode;
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::run()
{
	uint8_t i;
	unsigned long now = millis();

	// first, look for MDNS queries to handle
	(void)_processMDNSQuery();

	// are we querying a name or service? if so, should we resend the packet or time out?
	for (i = 0; i < 2; i++)
	{
		if (NULL != this->_resolveNames[i])
		{
			// Hint: _resolveLastSendMillis is updated in _sendMDNSMessage
			if (now - this->_resolveLastSendMillis[i] > ((i == 0) ? (uint32_t)MDNS_NQUERY_RESEND_TIME : (uint32_t)MDNS_SQUERY_RESEND_TIME))
				(void)this->_sendMDNSMessage(0,
											 0,
											 (0 == i) ? MDNSPacketTypeNameQuery : MDNSPacketTypeServiceQuery,
											 0);

			if (this->_resolveTimeouts[i] > 0 && now > this->_resolveTimeouts[i])
			{
				if (i == 0)
					this->_finishedResolvingName((char *)this->_resolveNames[0], NULL);
				else if (i == 1)
				{
					if (this->_serviceFoundCallback)
					{
						char *typeName = (char *)this->_resolveNames[1];
						char *p = (char *)this->_resolveNames[1];
						while (*p && *p != '.')
							p++;
						*p = '\0';

						this->_serviceFoundCallback(typeName,
													this->_resolveServiceProto,
													NULL,
													NULL,
													0,
													NULL);
					}
				}

				if (NULL != this->_resolveNames[i])
				{
					free(this->_resolveNames[i]);
					this->_resolveNames[i] = NULL;
				}
			}
		}
	}

	// now, should we re-announce our services again?
	unsigned long announceTimeOut = MDNS_RESPONSE_TTL / 4;
	if ((now - this->_lastAnnounceMillis) > 1000 * announceTimeOut)
	{
		for (i = 0; i < NumMDNSServiceRecords; i++)
		{
			if (NULL != this->_serviceRecords[i])
			{
				(void)this->_sendMDNSMessage(0, 0, (int)MDNSPacketTypeServiceRecord, i);
			}
		}

		this->_lastAnnounceMillis = now;
	}
}

// return values:
// 1 on success
// 0 otherwise
template <class UdpClass>
int EthernetBonjour3Class<UdpClass>::setBonjourName(const char *bonjourName)
{
	if (NULL == bonjourName)
		return 0;

	if (this->_bonjourName != NULL)
		free(this->_bonjourName);

	this->_bonjourName = (uint8_t *)malloc(strlen(bonjourName) + 7);
	if (NULL == this->_bonjourName)
		return 0;

	strcpy((char *)this->_bonjourName, bonjourName);
	strcpy((char *)this->_bonjourName + strlen(bonjourName), MDNS_TLD);

	return 1;
}

// return values:
// 1 on success
// 0 otherwise
template <class UdpClass>
int EthernetBonjour3Class<UdpClass>::addServiceRecord(const char *name, uint16_t port,
													  MDNSServiceProtocol_t proto)
{
#if defined(__MK20DX128__) || defined(__MK20DX256__)
	return this->addServiceRecord(name, port, proto, NULL); //works for Teensy 3 (32-bit Arm Cortex)
#else
	return this->addServiceRecord(name, port, proto, ""); //works for Teensy 2 (8-bit Atmel)
#endif
}

// return values:
// 1 on success
// 0 otherwise
template <class UdpClass>
int EthernetBonjour3Class<UdpClass>::addServiceRecord(const char *name, uint16_t port,
													  MDNSServiceProtocol_t proto, const char *textContent)
{
	int i, status = 0;
	MDNSServiceRecord_t *record = NULL;

	if (NULL != name && 0 != port)
	{
		for (i = 0; i < NumMDNSServiceRecords; i++)
		{
			if (NULL == this->_serviceRecords[i])
			{
				record = (MDNSServiceRecord_t *)malloc(sizeof(MDNSServiceRecord_t));
				if (NULL != record)
				{
					record->name = record->textContent = NULL;

					record->name = (uint8_t *)malloc(strlen((char *)name) + 1);
					memset(record->name, 0, strlen((char *)name) + 1);
					if (NULL == record->name)
						goto errorReturn;

					if (NULL != textContent)
					{
						record->textContent = (uint8_t *)malloc(strlen((char *)textContent) + 1);
						memset(record->textContent, 0, strlen((char *)textContent) + 1);
						if (NULL == record->textContent)
							goto errorReturn;

						strcpy((char *)record->textContent, textContent);
					}

					record->port = port;
					record->proto = proto;
					strcpy((char *)record->name, name);

					uint8_t *s = this->_findFirstDotFromRight(record->name);
					record->servName = (uint8_t *)malloc(strlen((char *)s) + 13);
					memset(record->servName, 0, strlen((char *)s) + 13);
					if (record->servName)
					{
						strcpy((char *)record->servName, (const char *)s);

						const uint8_t *srv_type = this->_postfixForProtocol(proto);
						if (srv_type)
							strcat((char *)record->servName, (const char *)srv_type);
					}

					this->_serviceRecords[i] = record;

					status = (MDNSSuccess ==
							  this->_sendMDNSMessage(0, 0, (int)MDNSPacketTypeServiceRecord, i));

					break;
				}
			}
		}
	}

	return status;

errorReturn:
	if (NULL != record)
	{
		if (NULL != record->name)
			free(record->name);
		if (NULL != record->servName)
			free(record->servName);
		if (NULL != record->textContent)
			free(record->textContent);

		free(record);
	}

	return 0;
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::_removeServiceRecord(int idx)
{
	if (NULL != this->_serviceRecords[idx])
	{
		(void)this->_sendMDNSMessage(0, 0, (int)MDNSPacketTypeServiceRecordRelease, idx);

		if (NULL != this->_serviceRecords[idx]->textContent)
			free(this->_serviceRecords[idx]->textContent);

		if (NULL != this->_serviceRecords[idx]->servName)
			free(this->_serviceRecords[idx]->servName);

		free(this->_serviceRecords[idx]->name);
		free(this->_serviceRecords[idx]);

		this->_serviceRecords[idx] = NULL;
	}
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::removeServiceRecord(uint16_t port, MDNSServiceProtocol_t proto)
{
	this->removeServiceRecord(NULL, port, proto);
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::removeServiceRecord(const char *name, uint16_t port,
														  MDNSServiceProtocol_t proto)
{
	int i;
	for (i = 0; i < NumMDNSServiceRecords; i++)
		if (port == this->_serviceRecords[i]->port &&
			proto == this->_serviceRecords[i]->proto &&
			(NULL == name || 0 == strcmp((char *)this->_serviceRecords[i]->name, name)))
		{
			this->_removeServiceRecord(i);
			break;
		}
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::removeAllServiceRecords()
{
	int i;
	for (i = 0; i < NumMDNSServiceRecords; i++)
		this->_removeServiceRecord(i);
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::_writeDNSName(const uint8_t *name, uint16_t *pPtr,
													uint8_t *buf, int bufSize, int zeroTerminate)
{
	uint16_t ptr = *pPtr;
	uint8_t *p1 = (uint8_t *)name, *p2, *p3;
	int i, c, len;

	while (*p1)
	{
		c = 1;
		p2 = p1;
		while (0 != *p2 && '.' != *p2)
		{
			p2++;
			c++;
		};

		p3 = buf;
		i = c;
		len = bufSize - 1;
		*p3++ = (uint8_t)--i;
		while (i-- > 0)
		{
			*p3++ = *p1++;

			if (--len <= 0)
			{
				_socket.write((uint8_t *)buf, bufSize);
				ptr += bufSize;
				len = bufSize;
				p3 = buf;
			}
		}

		while ('.' == *p1)
			++p1;

		if (len != bufSize)
		{
			_socket.write((uint8_t *)buf, bufSize - len);
			ptr += bufSize - len;
		}
	}

	if (zeroTerminate)
	{
		buf[0] = 0;
		_socket.write((uint8_t *)buf, 1);
		ptr += 1;
	}

	*pPtr = ptr;
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::_writeMyIPAnswerRecord(uint16_t *pPtr, uint8_t *buf, int bufSize)
{
	uint16_t ptr = *pPtr;

	this->_writeDNSName(this->_bonjourName, &ptr, buf, bufSize, 1);

	buf[0] = 0x00;
	buf[1] = 0x01;
	buf[2] = 0x80; // cache flush: true
	buf[3] = 0x01;
	_socket.write((uint8_t *)buf, 4);
	ptr += 4;

	*((uint32_t *)buf) = __htonl(MDNS_RESPONSE_TTL_10);
	*((uint16_t *)&buf[4]) = __htons(4); // data length

	uint8_t myIp[4];
	IPAddress myIpBuf;
	myIpBuf = _localIP;
	myIp[0] = myIpBuf[0];
	myIp[1] = myIpBuf[1];
	myIp[2] = myIpBuf[2];
	myIp[3] = myIpBuf[3];

	memcpy(&buf[6], &myIp, 4); // our IP address

	_socket.write((uint8_t *)buf, 10);
	ptr += 10;

	*pPtr = ptr;
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::_writeServiceRecordName(int recordIndex, uint16_t *pPtr, uint8_t *buf,
															  int bufSize, int tld)
{
	uint16_t ptr = *pPtr;

	uint8_t *name = tld ? this->_serviceRecords[recordIndex]->servName : this->_serviceRecords[recordIndex]->name;

	this->_writeDNSName(name, &ptr, buf, bufSize, tld);

	if (0 == tld)
	{
		const uint8_t *srv_type =
			this->_postfixForProtocol(this->_serviceRecords[recordIndex]->proto);

		if (NULL != srv_type)
		{
			srv_type++; // eat the dot at the beginning
			this->_writeDNSName(srv_type, &ptr, buf, bufSize, 1);
		}
	}

	*pPtr = ptr;
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::_writeServiceRecordPTR(int recordIndex, uint16_t *pPtr, uint8_t *buf,
															 int bufSize, uint32_t ttl)
{
	uint16_t ptr = *pPtr;

	this->_writeServiceRecordName(recordIndex, &ptr, buf, bufSize, 1);

	buf[0] = 0x00;
	buf[1] = 0x0c; // PTR record
	buf[2] = 0x00; // no cache flush
	buf[3] = 0x01; // class IN

	// ttl
	*((uint32_t *)&buf[4]) = __htonl(ttl);

	// data length (+13 = "._tcp.local" or "._udp.local" + 1  byte zero termination)
	*((uint16_t *)&buf[8]) =
		__htons(strlen((char *)this->_serviceRecords[recordIndex]->name) + 13);

	_socket.write((uint8_t *)buf, 10);
	ptr += 10;

	this->_writeServiceRecordName(recordIndex, &ptr, buf, bufSize, 0);

	*pPtr = ptr;
}

template <class UdpClass>
uint8_t *EthernetBonjour3Class<UdpClass>::_findFirstDotFromRight(const uint8_t *str)
{
	const uint8_t *p = str + strlen((char *)str);
	while (p > str && '.' != *p--)
		;
	return (uint8_t *)&p[2];
}

template <class UdpClass>
int EthernetBonjour3Class<UdpClass>::_matchStringPart(const uint8_t **pCmpStr, int *pCmpLen, const uint8_t *buf,
													  int dataLen)
{
	int matches = 1;

	if (*pCmpLen >= dataLen)
		matches &= (0 == memcmp(*pCmpStr, buf, dataLen));
	else
		matches = 0;

	*pCmpStr += dataLen;
	*pCmpLen -= dataLen;
	if ('.' == **pCmpStr)
		(*pCmpStr)++, (*pCmpLen)--;

	return matches;
}

template <class UdpClass>
const uint8_t *EthernetBonjour3Class<UdpClass>::_postfixForProtocol(MDNSServiceProtocol_t proto)
{
	const uint8_t *srv_type = NULL;
	switch (proto)
	{
	case MDNSServiceTCP:
		srv_type = (uint8_t *)"._tcp" MDNS_TLD;
		break;
	case MDNSServiceUDP:
		srv_type = (uint8_t *)"._udp" MDNS_TLD;
		break;
	}

	return srv_type;
}

template <class UdpClass>
void EthernetBonjour3Class<UdpClass>::_finishedResolvingName(char *name, const byte ipAddr[4])
{
	if (NULL != this->_nameFoundCallback)
	{
		if (NULL != name)
		{
			uint8_t *n = this->_findFirstDotFromRight((const uint8_t *)name);
			*(n - 1) = '\0';
		}

		this->_nameFoundCallback((const char *)name, ipAddr);
	}

	free(this->_resolveNames[0]);
	this->_resolveNames[0] = NULL;
}

END_MDNS_NAMESPACE