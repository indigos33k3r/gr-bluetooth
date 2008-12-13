/* -*- c++ -*- */
/*
 * Copyright 2004 Free Software Foundation, Inc.
 * 
 * This file is part of GNU Radio
 * 
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/*
 * config.h is generated by configure.  It contains the results
 * of probing for features, options etc.  It should be the first
 * file included in your .cc file.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <bluetooth_sniffer.h>

/*
 * Create a new instance of bluetooth_sniffer and return
 * a boost shared_ptr.  This is effectively the public constructor.
 */
bluetooth_sniffer_sptr 
bluetooth_make_sniffer (int lap, int uap)
{
  return bluetooth_sniffer_sptr (new bluetooth_sniffer (lap, uap));
}

//private constructor
bluetooth_sniffer::bluetooth_sniffer (int lap, int uap)
  : bluetooth_block ()
{
	d_LAP = lap;
	d_UAP = uap;
	d_payload_size = 0;
	d_packet_type = -1;
	d_stream_length = 0;
	d_consumed = 0;
	flag = 0;
	printf("Bluetooth packet sniffer\n\n");
}

//virtual destructor.
bluetooth_sniffer::~bluetooth_sniffer ()
{
}

int 
bluetooth_sniffer::work (int noutput_items,
			       gr_vector_const_void_star &input_items,
			       gr_vector_void_star &output_items)
{
	d_stream = (char *) input_items[0];
	d_consumed = 0;
	d_stream_length = noutput_items;
	int retval = 0;
	d_payload_size = 0;
	d_packet_type = -1;

while(d_stream_length) {
	if((noutput_items - d_consumed) > 71)
		retval = sniff_ac();
	else {
		//The flag is used to avoid being stuck with <71 input bits in file mode
		if(flag)
			d_consumed = noutput_items;
		flag = !flag;
		break;
	}

	if(-1 == retval) {
		d_consumed = noutput_items;
		break;
	}

	d_consumed += retval;

	if(126+retval <= noutput_items) {
		new_header();
	} else { //Drop out and wait to be run again
		break;
	}

	d_consumed += 126;

	retval = payload();
	if(0 == retval) {
		print_out();
		d_consumed += (d_payload_size*8);
	} else {//We're having payload trouble, dump the stream
		d_consumed = noutput_items;
		break;
	}

    d_payload_size = 0;
    d_packet_type = -1;
    d_stream_length = noutput_items - d_consumed;
}
  // Tell runtime system how many output items we produced.
  if(d_consumed >= noutput_items)
	return noutput_items;
  else
	return d_consumed;
}

/* returns the payload length */
int bluetooth_sniffer::payload_header(uint8_t *stream)
{
       return stream[3] | stream[4] << 1 | stream[5] << 2 | stream[6] << 3 | stream[7] << 4;
}

/* returns the payload length */
int bluetooth_sniffer::long_payload_header(uint8_t *stream)
{
       return stream[3] | stream[4] << 1 | stream[5] << 2 | stream[6] << 3 | stream[7] << 4 | stream[8] << 5 | stream[9] << 6 | stream[10] << 7 | stream[11] << 8;
}

/* Converts 8 bytes of grformat to a single byte */
char bluetooth_sniffer::gr_to_normal(char *stream)
{
	return stream[0] << 7 | stream[1] << 6 | stream[2] << 5 | stream[3] << 4 | stream[4] << 3 | stream[5] << 2 | stream[6] << 1 | stream[7];
}

/* Version to get round the uint8_t - char thing */
char bluetooth_sniffer::gr_to_normal(uint8_t *stream)
{
	return stream[0] << 7 | stream[1] << 6 | stream[2] << 5 | stream[3] << 4 | stream[4] << 3 | stream[5] << 2 | stream[6] << 1 | stream[7];
}

/* Pointer to start of header, UAP */
int bluetooth_sniffer::UAP_from_hec(uint8_t *packet)
{
	char byte;
	int count;
	uint8_t hec;

	hec = *(packet + 2);
	byte = *(packet + 1);

	for(count = 0; count < 10; count++)
	{
		if(2==count)
			byte = *packet;

		/*Bit 1*/
		hec ^= ((hec & 0x01)<<1);
		/*Bit 2*/
		hec ^= ((hec & 0x01)<<2);
		/*Bit 5*/
		hec ^= ((hec & 0x01)<<5);
		/*Bit 7*/
		hec ^= ((hec & 0x01)<<7);

		hec = (hec >> 1) | (((hec & 0x01) ^ (byte & 0x01)) << 7);
		byte >>= 1;
	}
	return hec;
}


/* Pointer to start of packet, length of packet in bits, UAP */
uint16_t bluetooth_sniffer::crcgen(uint8_t *packet, int length, int UAP)
{
	char byte;
	uint16_t reg, count, counter;

	reg = UAP & 0xff;
	for(count = 0; count < length; count++)
	{
		byte = packet[count];

		reg = (reg << 1) | (((reg & 0x8000)>>15) ^ (byte & 0x01));

		/*Bit 5*/
		reg ^= ((reg & 0x0001)<<5);

		/*Bit 12*/
		reg ^= ((reg & 0x0001)<<12);
	}
	return reg;
}

int bluetooth_sniffer::payload()
{
	return 1;
}

/* Looks for an AC in the stream */
int bluetooth_sniffer::sniff_ac()
{
	int LAP, jump, count, counter, size;
	char *stream = d_stream;
	int jumps[16] = {3,2,1,3,3,0,2,3,3,2,0,3,3,1,2,3};
	size = d_stream_length;
	count = 0;

	while(size > 72)
	{
		jump = jumps[stream[0] << 3 | stream[1] << 2 | stream[2] << 1 | stream[3]];
		if(0 == jump)
		{
			/* Found the start, now check the end... */
			counter = stream[62] << 9 | stream[63] << 8 | stream[64] << 7 | stream[65] << 6 | stream[66] << 5 | stream[67] << 4 | stream[68] << 3 | stream[69] << 2 | stream[70] << 1 | stream[71];

			if((0x0d5 == counter) || (0x32a == counter))
			{
				LAP = get_LAP(stream);
				if(LAP == d_LAP && check_ac(stream, LAP))
				{
					/*printf("AC\n");
					for(int x = 0; x < 72; x++)
						printf("%d", stream[x]);
					printf("\n");*/
					return count;
				}
			}
			jump = 1;
		}
		count += jump;
		stream += jump;
		size -= jump;
	}
	return -1;
}

void bluetooth_sniffer::unwhiten(uint8_t* input, uint8_t* output, int clock, int length, int skip)
{
	int count, index;
	index = d_indicies[clock & 0x3f];
	index += skip;
	index %= 127;

	for(count = 0; count < length; count++)
	{
		output[count] = input[count] ^ d_whitening_data[index];
		index += 1;
		index %= 127;
	}
}

void bluetooth_sniffer::unwhiten_char(char* input, uint8_t* output, int clock, int length, int skip)
{
	int count, index;
	index = d_indicies[clock & 0x3f];
	index += skip;
	index %= 127;

	for(count = 0; count < length; count++)
	{
		output[count] = input[count] ^ d_whitening_data[index];
		index += 1;
		index %= 127;
	}
}

void bluetooth_sniffer::new_header()
{
	char *stream = d_stream + d_consumed + 72;
	uint8_t header[18];
	uint8_t unwhitened[18];
	uint8_t UAP, ltadr;
	int count, size;

	size = d_stream_length - 126;
	//printf("Start Header");

	unfec13(stream, header, 18);

	for(count = 1; count < 64; count++)
	{
		unwhiten(header, unwhitened, count, 18, 0);

		unwhitened[0] = unwhitened[0] << 7 | unwhitened[1] << 6 | unwhitened[2] << 5 | unwhitened[3] << 4 | unwhitened[4] << 3 | unwhitened[5] << 2 | unwhitened[6] << 1 | unwhitened[7];
		unwhitened[1] = unwhitened[8] << 1 | unwhitened[9];
		unwhitened[2] = unwhitened[10] << 7 | unwhitened[11] << 6 | unwhitened[12] << 5 | unwhitened[13] << 4 | unwhitened[14] << 3 | unwhitened[15] << 2 | unwhitened[16] << 1 | unwhitened[17];

		UAP = UAP_from_hec(unwhitened);

		if(UAP != d_UAP)
			continue;

		d_packet_type = (unwhitened[0] & 0x1e) >> 1;
		uint8_t ltadrs[8] = {0, 4, 2, 6, 1, 5, 3, 7};
		ltadr = ltadrs[(unwhitened[0] & 0xe0) >> 5];

		if(1 != ltadr)
			continue;

		switch(d_packet_type)
		{
			case 0:/*printf("\nNULL Packet"); */
			break;
			case 1://printf("\nDV Slots:1");
			break;
			case 2://printf("\n\nDH1 Slots:1");
				//printf("\n%02x %02x %02x\n", unwhitened[0], unwhitened[1], unwhitened[2]);
				//DH1(size, count);
				break;
			case 3://printf("\nEV4 Slots:3");
			 break;
			case 4://printf("\nFHS Slots:1");
			 break;
			case 5://printf("\nDM3 Slots:3");
			 break;
			case 6://printf("\nHV2 Slots:1");
				//printf("\n%02x %02x %02x\n", unwhitened[0], unwhitened[1], unwhitened[2]);
				//HV2(size, count);
				break;
			case 7://printf("\nDM5 Slots:5");
				//printf("\n%02x %02x %02x\n", unwhitened[0], unwhitened[1], unwhitened[2]);
				//DM5(size, count);
				break;
			case 8://printf("\nPOLL Slots:1");
			 break;
			case 9://printf("\nAUX1 Slots:1");
			 break;
			case 10://printf("\nHV1 Slots:1");
			 break;
			case 11://printf("\nEV5 Slots:3");
			 break;
			case 12:printf("\nDM1 Slots:1 clock: %d", count);
				DM1(size, count);
				break;
			case 13://printf("\nDH3 Slots:3");
			break;
			case 14://printf("\nHV3/EV3 Slots:1"); 
				break;
			case 15://printf("\nDH5 Slots:5");
				//DH5(size, count);
				break;
		}
	}

	//printf("\nEnd Header\n\n");
}

int bluetooth_sniffer::DM1(int size, int clock)
{
	char *stream = d_stream + d_consumed + 126;
	int count, length, bitlength;
	uint16_t crc, check;
	uint8_t header[8];

	if(8 >= size)
		return 1;
	//unfec23(stream, 16);

	unwhiten_char(stream, header, clock, 8, 18);

	printf("\npayload header: ");
	for(count = 0; count < 8; count++)
	{
		printf("%d", header[count]);
	}
	printf("\n");

	crc = header[0] | header[1] << 1;
	switch (crc) {
		case 1: printf("Continuation of fragment\n");break;
		case 2: printf("Start of fragment\n");break;
	}

	length = payload_header(header);
	printf("length = %d\n", length);

	if((length+3)*12 >= size)
		return 1;

	unfec23(stream, (length+3)*8);
	uint8_t payload[(length+3)*8];
	unwhiten_char(stream, payload, clock, (length+3)*8, 18);
	int x = 0;
	for(count = 0; count < (length+3)*8; count++)
	{
		if(count == ((length+3)*8)-16) {
			printf("\nPacket CRC:");
			x = 0;}
		x <<= 1;
		x |= payload[count];
		printf("%d", payload[count]);
	}
	printf("\n");
	crc = crcgen(payload, (length+1)*8, d_UAP);
	if(crc == x)
		printf("CRC verified\n");
	else
		printf("CRC incorrect\n");
}


int bluetooth_sniffer::DM5(int size, int clock)
{
	char *stream = d_stream + d_consumed + 126;
	int count, length, bitlength;
	uint16_t crc, check;
	uint8_t header[16];

	if(16 >= size)
		return 1;
	unfec23(stream, 16);
	printf("\nwhitened payload header: ");
	for(count = 0; count < 16; count++)
	{
		printf("%d", stream[count]);
	}
	printf("\n");
	unwhiten_char(stream, header, clock, 16, 18);

	printf("\npayload header: ");
	for(count = 0; count < 16; count++)
	{
		printf("%d", header[count]);
	}
	printf("\n");

	crc = header[0] | header[1] << 1;
	printf("\nLLID -> %d", crc);

	length = long_payload_header(header);
	printf("\nclock = %d  length = %d\n", clock, length);

	if((length+4)*8 >= size)
		return 1;

	unfec23(stream, length*8);
}
