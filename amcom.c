#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "amcom.h"

/// Start of packet character
const uint8_t  AMCOM_SOP         = 0xA1;
const uint16_t AMCOM_INITIAL_CRC = 0xFFFF;

static uint16_t AMCOM_UpdateCRC(uint8_t byte, uint16_t crc)
{
    byte ^= (uint8_t)(crc & 0x00ff);
    byte ^= (uint8_t)(byte << 4);
    return ((((uint16_t)byte << 8) | (uint8_t)(crc >> 8)) ^ (uint8_t)(byte >> 4) ^ ((uint16_t)byte << 3));
}


void AMCOM_InitReceiver(AMCOM_Receiver* receiver, AMCOM_PacketHandler packetHandlerCallback, void* userContext) {
    receiver->payloadCounter = 0;
    receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
    receiver->packetHandler = packetHandlerCallback;
    receiver->userContext = userContext;
}

void AMCOM_ResetReceiver(AMCOM_Receiver * receiver){
    receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
    receiver->payloadCounter = 0;
}

size_t AMCOM_Serialize(uint8_t packetType, const void* payload, size_t payloadSize, uint8_t* destinationBuffer) {
    size_t size = 0;
    /* HEADER */
    destinationBuffer[0] = AMCOM_SOP;
    destinationBuffer[1] = packetType;
    destinationBuffer[2] = payloadSize;
    size += 3;

    //Initial calculations of CRC
    uint16_t crc = AMCOM_INITIAL_CRC;
    crc = AMCOM_UpdateCRC(packetType, crc);
    crc = AMCOM_UpdateCRC(payloadSize, crc);
    /* END OF HEADER */

    /* PAYLOAD */
    const uint8_t *charPayload = (const uint8_t*)payload;
    for(size_t i = 0; i < payloadSize + 0; i++){
        if(i>200) continue;
        destinationBuffer[i+5] = charPayload[i];
        crc = AMCOM_UpdateCRC(charPayload[i], crc);
        size++;
    }

    /* END OF PAYLOAD */

    //Add CRC
    destinationBuffer[3] = (uint8_t)crc;
    destinationBuffer[4] = (uint8_t)(crc>>8);
    size +=2;

    return size;
}

void AMCOM_Deserialize(AMCOM_Receiver* receiver, const void* data, size_t dataSize) {
    uint16_t crcCheck;
    const uint8_t* d = (const uint8_t*)data;
    for(size_t i=0; i<dataSize; i++){

        switch(receiver->receivedPacketState){
            //Starting packet
            //Getting SOP header field
            case AMCOM_PACKET_STATE_EMPTY :
                if(d[i] == AMCOM_SOP){
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_SOP;
                    receiver->receivedPacket.header.sop = d[i];
                }
                break;

            //Getting TYPE header field
            case AMCOM_PACKET_STATE_GOT_SOP :
                receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_TYPE;
                receiver->receivedPacket.header.type = d[i];
                break;

            //Getting LENGTH header field
            case AMCOM_PACKET_STATE_GOT_TYPE :
                if(d[i] <= AMCOM_MAX_PAYLOAD_SIZE){
                    //Correct LENGTH header field
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_LENGTH;
                    receiver->receivedPacket.header.length = d[i];
                    break;
                }
                //Incorrect LENGTH header field
                AMCOM_ResetReceiver(receiver);
                break;

            //Getting first CRC header field byte
            case AMCOM_PACKET_STATE_GOT_LENGTH :
                receiver->receivedPacket.header.crc = d[i] << 8;
                receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_CRC_LO;
                break;

            //Getting second CRC header field byte
            case AMCOM_PACKET_STATE_GOT_CRC_LO :
                receiver->receivedPacket.header.crc |= d[i];

                if(receiver->receivedPacket.header.length == 0){
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_WHOLE_PACKET;
                    break;
                }else{
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GETTING_PAYLOAD;
                    break;
                }

            //Getting PAYLOAD
            case AMCOM_PACKET_STATE_GETTING_PAYLOAD :
                receiver->receivedPacket.payload[receiver->payloadCounter] = d[i];
                receiver->payloadCounter++;
                if(receiver->receivedPacket.header.length == receiver->payloadCounter){
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_WHOLE_PACKET;
                }
                break;

            //Displaying packet
            case AMCOM_PACKET_STATE_GOT_WHOLE_PACKET :
                crcCheck = AMCOM_INITIAL_CRC;
                crcCheck = AMCOM_UpdateCRC(receiver->receivedPacket.header.type,crcCheck);
                crcCheck = AMCOM_UpdateCRC(receiver->receivedPacket.header.length,crcCheck);
                //Calculate CRC
                for(uint8_t j=0; j < receiver->receivedPacket.header.length; j++){
                    crcCheck = AMCOM_UpdateCRC(receiver->receivedPacket.payload[j],crcCheck);
                }
                uint8_t temp = crcCheck;
                crcCheck = crcCheck >> 8;
                crcCheck |= temp << 8;
                if(receiver->receivedPacket.header.crc == crcCheck){
                    receiver->packetHandler(&receiver->receivedPacket,receiver->userContext);
                }
                AMCOM_ResetReceiver(receiver);
                break;

            default: break;
        }
    }
    if(receiver->receivedPacketState == AMCOM_PACKET_STATE_GOT_WHOLE_PACKET){
        crcCheck = AMCOM_INITIAL_CRC;
        crcCheck = AMCOM_UpdateCRC(receiver->receivedPacket.header.type,crcCheck);
        crcCheck = AMCOM_UpdateCRC(receiver->receivedPacket.header.length,crcCheck);
        //Calculate CRC
        for(uint8_t j=0; j < receiver->receivedPacket.header.length; j++){
            crcCheck = AMCOM_UpdateCRC(receiver->receivedPacket.payload[j],crcCheck);
        }
        uint8_t temp = crcCheck;
        crcCheck = crcCheck >> 8;
        crcCheck |= temp << 8;
        if(receiver->receivedPacket.header.crc == crcCheck){
            receiver->packetHandler(&receiver->receivedPacket,receiver->userContext);
        }
        AMCOM_ResetReceiver(receiver);
    }
}