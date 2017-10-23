/*
** FAAD2 - Freeware Advanced Audio (AAC) Decoder including SBR decoding
** Copyright (C) 2003-2005 M. Bakker, Nero AG, http://www.nero.com
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** Any non-GPL usage of this software or parts of this software is strictly
** forbidden.
**
** The "appropriate copyright message" mentioned in section 2c of the GPLv2
** must read: "Code from FAAD2 is copyright (c) Nero AG, www.nero.com"
**
** Commercial non-GPL licensing of this software is possible.
** For more info contact Nero AG through Mpeg4AAClicense@nero.com.
**
** $Id: main.c,v 1.85 2008/09/22 17:55:09 menno Exp $
**/

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define off_t __int64
#else
#include <time.h>
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h> 
#include <neaacdec.h>
#include <mp4ff.h>

#include "audio.h"

#define u32 unsigned int

#ifndef min
#define min(a,b) ( (a) < (b) ? (a) : (b) )
#endif

#define MAX_CHANNELS 6 /* make this higher to support files with
                          more channels */


static int quiet = 0;

static void faad_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;

    if (!quiet)
    {
        va_start(ap, fmt);

        vfprintf(stream, fmt, ap);

        va_end(ap);
    }
}

/* FAAD file buffering routines */
typedef struct {
    long bytes_into_buffer;
    long bytes_consumed;
    long file_offset;
    unsigned char *buffer;
    int at_eof;
    FILE *infile;
} aac_buffer;


int find_sync(unsigned char *buf,int len)
{

	int ret,i,j;
	int find_start = 0;

	if(len < 2)
		return -1;

	for(i=0;i<len;i++){
		if(buf[i] == 0xff && (i <len-2) && ((buf[i+1] & 0xf6)==0xf0) )
			return i;

	}
	return -1;

}



static int fill_buffer(aac_buffer *b)
{
	int bread;

	u32 sz = FAAD_MIN_STREAMSIZE*MAX_CHANNELS;

	if (b->bytes_consumed > 0)
	{

	        memmove((void*)b->buffer, (void*)(b->buffer + b->bytes_consumed),
	            b->bytes_into_buffer*sizeof(unsigned char));

	        bread = fread((void*)(b->buffer + sz - b->bytes_consumed), 1,
	            b->bytes_consumed, b->infile);

			b->bytes_into_buffer -= b->bytes_consumed;
			b->bytes_into_buffer += bread;
	}

	return 1;
}


void print_hex(unsigned char *buf,int len)
{
	int i;

	for(i=0;i<len;i++)
		faad_fprintf(stderr, " %02x",buf[i]);

	faad_fprintf(stderr, " \n");
}



struct acc_head_info{
	unsigned char version,layer,protect,profile,sf_index,priv,ch_cfg,frame_sz;
	u32 frame_len,buff_full;
};
void print_head(unsigned char *buf,struct acc_head_info *head)
{
	unsigned char version,layer,protect,profile,sf_index,priv,ch_cfg,frame_sz;
	u32 frame_len,buff_full;

	head->version = version = (buf[1]&0x08) >> 3;
	head->layer = layer=(buf[1]&0x06) >> 1;
	head->protect =protect= (buf[1]&0x01);

	head->profile=profile= (buf[2]&0xc0) >> 6;
	head->sf_index= sf_index=(buf[2]&0x3c) >> 2;
	head->priv=priv= (buf[2]&0x02) >> 1;

	head->ch_cfg =ch_cfg=( ((buf[2]&0x01)<< 2)  | ((buf[3]&0xc0)>>6) ) & 0x0f;
	head->frame_len =frame_len= ( ((u32)buf[3]&0x03) << 11) | ((u32)buf[4] << 3) | ((buf[5]&0xe0) >> 5);
	head->buff_full =buff_full= (((u32)buf[5]&0x1f) << 6) | ((buf[6]&0xfc)>>2);
	head->frame_sz = frame_sz=buf[6] &0x03;

	
	faad_fprintf(stderr, "head infor: version %s,layer %d, protect %s, profile %d,\
					sf_index %d,priv %d,ch_cfg %d,frame len %d,buff_full %d,frame_sz %d\n",(version==0?"MPEG4":"MPEG2"), \
					layer,(protect==1?"no CRC":"CRC"),profile+1,sf_index,priv,ch_cfg,frame_len,buff_full,frame_sz+1);
	
}


void set_bits(unsigned char *buf,int offset,int cnt,u32 value)
{
	u32 byte_off,bit_off,i;

	for(i=0;i<cnt;i++){
		
		byte_off = (offset+i)/8;
		bit_off = 7 - ((offset+i)%8);

		if(( value & ((u32)1 << (cnt-1-i)) ) != 0 )
			buf[byte_off] |= (1 << bit_off);
		else
			buf[byte_off] &= (~(1 << bit_off));
	}
}

NeAACDecHandle faad_open(unsigned char *init_pt,int init_len)
{

	NeAACDecHandle hDecoder = NULL;
	NeAACDecConfigurationPtr config;
	unsigned long samplerate;
	unsigned char channels;

	hDecoder = NeAACDecOpen();

	config = NeAACDecGetCurrentConfiguration(hDecoder);

	config->defSampleRate = 0;
	config->defObjectType = LC;
	config->outputFormat = FAAD_FMT_16BIT;
	config->downMatrix = 0;
	config->useOldADTSFormat = 0;
	//config->dontUpSampleImplicitSBR = 1;
	NeAACDecSetConfiguration(hDecoder, config);


	if (( NeAACDecInit(hDecoder, init_pt,init_len, &samplerate, &channels)) < 0){

		faad_fprintf(stderr, "Error initializing decoder library.\n");
	
		NeAACDecClose(hDecoder);
		return NULL;
	}
	return hDecoder;
}

static int decodeAACfile(char *aacfile)
{
	int tagsize;
	unsigned long samplerate;
	unsigned char channels;
	void *sample_buffer;

	audio_file *aufile;

	FILE *adtsFile;
	unsigned char *adtsData,*init_pt,*initData;
	int adtsDataSize;

	NeAACDecHandle hDecoder = NULL;
	NeAACDecFrameInfo frameInfo;
	NeAACDecConfigurationPtr config;


	int bread, fileread;
	int header_type = 0;
	int bitrate = 0;
	float length = 0;

	int first_time = 1;
	static int count  = 0;
	int tmp = 0,ret,init_len;
	int err = 0,str_ready = 0;
	time_t timer;
	struct tm *tblock;
	u32 dec_cnt = 0,valid_cnt = 0;
	struct acc_head_info acc_head;
	aac_buffer b;
	u32 init_cnt = 0;
	
	bread = FAAD_MIN_STREAMSIZE*MAX_CHANNELS;

	memset(&b, 0, sizeof(aac_buffer));

	if(aacfile == NULL)
		b.infile = stdin;
	else
		b.infile = fopen(aacfile, "rb");
	
	if (b.infile == NULL){
	    faad_fprintf(stderr, "Error opening file: %s\n", aacfile);
	    return 1;
	}

	initData = malloc(FAAD_MIN_STREAMSIZE*MAX_CHANNELS);
	if ((!initData) || !(b.buffer = (unsigned char*)malloc(FAAD_MIN_STREAMSIZE*MAX_CHANNELS))){
	    faad_fprintf(stderr, "Memory allocation error\n");
	    return 0;
	}
	memset(b.buffer, 0, FAAD_MIN_STREAMSIZE*MAX_CHANNELS);
	memset(initData, 0, FAAD_MIN_STREAMSIZE*MAX_CHANNELS);

	
	b.bytes_into_buffer = fread(b.buffer, 1, bread, b.infile);
	 faad_fprintf(stderr, "first read %d from acc\n",b.bytes_into_buffer);


	if(hDecoder)
		NeAACDecClose(hDecoder);

	ret = find_sync(b.buffer,bread);
	if(ret >=0){	
		init_pt = b.buffer + ret;
		init_len = bread - ret;
	}
	else{
		init_pt = b.buffer;
		init_len = bread;		
	}
	hDecoder = faad_open(init_pt,init_len);
	if(hDecoder == NULL)
		return -1;

	do
	{
		faad_fprintf(stderr,"start decoder %d\n",dec_cnt++);
		if(dec_cnt == 100){
			//set_bits(b.buffer,30,13,6802);
			//set_bits(b.buffer,43,11,178);
		}

		print_hex(b.buffer,10);
		print_head(b.buffer,&acc_head);


			sample_buffer = NeAACDecDecode(hDecoder, &frameInfo,
			    		b.buffer, (acc_head.frame_len > bread? bread:acc_head.frame_len));    //b.bytes_into_buffer);


		if (frameInfo.error == 0){
			err = 0;
			valid_cnt++;
			
			faad_fprintf(stderr,"decoder OK,bytesconsumed %d,valid_cnt %d\n",frameInfo.bytesconsumed,valid_cnt);

			if(str_ready != 1 && valid_cnt >= 10 && frameInfo.bytesconsumed > 0){
				str_ready = 1;
				memcpy(initData,b.buffer,frameInfo.bytesconsumed);
			}
		}
		else{
			err++;
			
			
			ret  = find_sync(b.buffer+1,bread-1);
			count++;
			faad_fprintf(stderr,"err %d: tmp %d, find sync at %d, %s\n",count,tmp,ret+1,NeAACDecGetErrorMessage(frameInfo.error));
			//print_hex(b.buffer,10);

			if(ret >=0 ){
				frameInfo.bytesconsumed = ret + 1;
			}
			else{
				frameInfo.bytesconsumed = bread;
			}

			if((err >= 5) && (ret >=0)){
				err = 0;
				init_cnt++;
				faad_fprintf(stderr,"init aac again %d,str_ready %d\n",init_cnt,str_ready);
				NeAACDecClose(hDecoder);
				if(!str_ready)
					hDecoder = faad_open(b.buffer + frameInfo.bytesconsumed,bread - frameInfo.bytesconsumed);
				else
					hDecoder = faad_open(initData,bread);
			}
			
		}
		b.bytes_consumed = frameInfo.bytesconsumed;
		fill_buffer(&b);

	} while (1); //while (sample_buffer != NULL);

	faad_fprintf(stderr,"done,sample_buffer %s\n",(sample_buffer==NULL ? "is null": "not null"));

	NeAACDecClose(hDecoder);

	fclose(b.infile);


	if (b.buffer)
	    free(b.buffer);

	return frameInfo.error;
}


void test(int n,struct siginfo *siginfo,void *myact)  
{  
	faad_fprintf(stderr,"faad exit\n",n);

	faad_fprintf(stderr,"signal number:%d\n",n);
	faad_fprintf(stderr,"siginfo signo:%d\n",siginfo->si_signo); 
	faad_fprintf(stderr,"siginfo errno:%d\n",siginfo->si_errno);  
	faad_fprintf(stderr,"siginfo code:%d\n",siginfo->si_code);  
	exit(0);  
} 

int main(int argc, char *argv[])
{
	int result;
	char *aac_file = NULL;

	struct sigaction act;  
	
	sigemptyset(&act.sa_mask);	
	act.sa_flags=SA_SIGINFO;	
	act.sa_sigaction=test;	
	if(sigaction(SIGSEGV,&act,NULL) < 0)  
		 faad_fprintf(stderr,"install signal error\n");	

	if(argc == 2)
		aac_file = argv[1];
	decodeAACfile(aac_file);

	return 0;
}
