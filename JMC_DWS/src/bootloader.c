/*
 * bootloader.c
 *
 *  Created on: Jan 31, 2018
 *      Author: tony
 */

#include "bootloader.h"
#include "serial_pack_parse.h"

Seed seed = {
		.level_one = 0,
		.level_FBL = 0
};

SecretKey secret_key = {
		.level_one = 0,
		.level_FBL = 0
};

BootloaderBusinessLogic  JMC_bootloader_logic;

static unsigned char ASAP1A_CCP_ComputeKeyFromSeed(unsigned char *seed, unsigned short sizeSeed, \
		char * key, unsigned short maxSizeKey, unsigned short *sizeKey)
{
    unsigned char i;
    char calData[4];
    char xorArray[4] = {0x48, 0x55, 0x44, 0x20}; // NL-5 Level 11 Array for

    //For avoid compiler warring
    //sizeSeed = sizeSeed;
    //maxSizeKey = maxSizeKey;

    //Loop for calculate data
    for(i = 0; i < 4; i++)
    {
        calData[i] = seed[i] ^ xorArray[i];
    }

    //Calculate Key
    key[0] = (((calData[2] & 0x03) << 6) | ((calData[3] & 0xFC) >> 2));
    key[1] = (((calData[3] & 0x03)<<6) | (calData[0] & 0x3F));
    key[2] = ((calData[0] & 0xFC) | ((calData[1] & 0xC0) >> 6));
    key[3] = ((calData[1] & 0xFC) | (calData[2] & 0x03));

    *sizeKey = 4;
    // If the return value is false the flash tool stops

    return 0x01;
}


static int get_random(int start, int end)
{
	int dis = end - start;
	srand((unsigned)time(NULL));
	return (rand()%dis + start);
}


void bootloader_logic_init(BootloaderBusinessLogic *bootloader_logic)
{
	bootloader_logic->bootloader_subseq = ExtendedSession;
	bootloader_logic->seed.level_FBL = 0;
	bootloader_logic->seed.level_one = 0;
	bootloader_logic->secret_key.level_FBL = 0;
	bootloader_logic->secret_key.level_one = 0;
	INIT_LIST_HEAD(&bootloader_logic->driver_list_head);
	INIT_LIST_HEAD(&bootloader_logic->app_list_head);
	return;
}



static int download_driver_process(BootloaderBusinessLogic *bootloader_logic, \
		const unsigned char* can_mesg, unsigned short mesg_len, \
		unsigned char* reply_mesg, unsigned short* reply_mesg_len)
{
	unsigned char counter = 0, i = 0;
	unsigned char mem_addr_bytes = 0, mem_size_bytes = 0;
	unsigned int crc32_temp = 0;
	LogicBlockNode *driver_logicblock_node = NULL;
	DataSegment *ptr_datasegment = NULL;

	if(DownloadDriver == bootloader_logic->bootloader_subseq)
	{
		/* request download for driver file */
		if(0x34 == *can_mesg)
		{
			bootloader_logic->bootloader_subseq = DownloadDriver;

			/* apply memory for logic block */
			driver_logicblock_node = (LogicBlockNode *)malloc(sizeof(LogicBlockNode));

			if(NULL == driver_logicblock_node)
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x34;
				*(reply_mesg+2) = 0x70;  //reject download
				*reply_mesg_len = 3;
				return 0;
			}

			driver_logicblock_node->logic_block_data.block_download_result = 0;
			driver_logicblock_node->logic_block_data.block_index = 0;
			driver_logicblock_node->logic_block_data.block_state = RequestDownload;
			driver_logicblock_node->logic_block_data.crc32_cal = 0;
			driver_logicblock_node->logic_block_data.file_type = 0; //driver file logic block
			driver_logicblock_node->logic_block_data.mem_addr = 0;
			driver_logicblock_node->logic_block_data.mem_size = 0;
			driver_logicblock_node->logic_block_data.MaxNumOfBlockLeng = 250;
			INIT_LIST_HEAD(&driver_logicblock_node->logic_block_data.data_segment_head);
			list_add_tail(&driver_logicblock_node->logic_block_list, &bootloader_logic->driver_list_head);

			mem_addr_bytes = *(can_mesg+2)&0x0F; //get memory address bytes
			mem_size_bytes = (*(can_mesg+2)&0xF0)>>4; //get memory size bytes

			if((*(can_mesg+1) != 0) || (mem_addr_bytes > 4) || (mem_size_bytes > 4))
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x34;
				*(reply_mesg+2) = 0x31;  //reject for out of range
				*reply_mesg_len = 3;
				return 0;
			}

			/* get memory address */
			for(i=0; i<mem_addr_bytes; i++)
			{
				if(i > 0)
				{
					driver_logicblock_node->logic_block_data.mem_addr <<= 8;
				}

				driver_logicblock_node->logic_block_data.mem_addr |= \
						*(can_mesg+3+i);
			}

			/* get memory size */
			for(i=0; i<mem_size_bytes; i++)
			{
				if(i > 0)
				{
					driver_logicblock_node->logic_block_data.mem_size <<= 8;
				}

				driver_logicblock_node->logic_block_data.mem_size |= \
						*(can_mesg+3+mem_addr_bytes+i);
			}

			ptr_datasegment = (DataSegment*)malloc(sizeof(DataSegment));

			if(NULL == ptr_datasegment)
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x34;
				*(reply_mesg+2) = 0x70;  //reject download
				*reply_mesg_len = 3;
				return 0;
			}

			ptr_datasegment->block_index = 0;
			ptr_datasegment->data = NULL;
			ptr_datasegment->mem_addr = driver_logicblock_node->logic_block_data.mem_addr;
			ptr_datasegment->mem_size = driver_logicblock_node->logic_block_data.mem_size;
			ptr_datasegment->segment_len = 0;
			list_add_tail(&ptr_datasegment->segment_list, &driver_logicblock_node->logic_block_data.data_segment_head);
			ptr_datasegment->data = (unsigned char*)malloc(ptr_datasegment->mem_size);

			if(NULL == ptr_datasegment->data)
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x34;
				*(reply_mesg+2) = 0x70;  //reject download
				*reply_mesg_len = 3;
				return 0;
			}

			/* positive response */
			*reply_mesg = 0x74;
			*(reply_mesg+1) = 0x10;
			*(reply_mesg+2) = driver_logicblock_node->logic_block_data.MaxNumOfBlockLeng;
			*reply_mesg_len = 3;
			return 0;
		}
		/* download transfer */
		else if(0x36 == *can_mesg)
		{
			/* if driver_list_head is empty */
			if(list_empty(&bootloader_logic->driver_list_head))
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x36;
				*(reply_mesg+2) = 0x72; //general programming error
				*reply_mesg_len = 3;
			}
			else
			{
				bootloader_logic->bootloader_subseq = DownloadDriver;
				driver_logicblock_node = \
						list_entry(bootloader_logic->driver_list_head.prev, LogicBlockNode, logic_block_list);
				driver_logicblock_node->logic_block_data.block_index =  *(can_mesg+1);
				driver_logicblock_node->logic_block_data.block_state = DownloadData;

				ptr_datasegment = list_entry(driver_logicblock_node->logic_block_data.data_segment_head.prev, \
						DataSegment, segment_list);
				ptr_datasegment->block_index = *(can_mesg+1);
				memcpy((ptr_datasegment->data+ptr_datasegment->segment_len), (can_mesg+2), mesg_len-2);
				crc32_temp = crc32c(driver_logicblock_node->logic_block_data.crc32_cal, \
						(ptr_datasegment->data+ptr_datasegment->segment_len), mesg_len-2);
				driver_logicblock_node->logic_block_data.crc32_cal = crc32_temp;
				ptr_datasegment->segment_len += mesg_len-2;

				if(ptr_datasegment->segment_len > ptr_datasegment->mem_size)
				{
					/* negative response */
					*reply_mesg = 0x7F;
					*(reply_mesg+1) = 0x36;
					*(reply_mesg+2) = 0x71; //request sequence error
					*reply_mesg_len = 3;
				}
				else
				{
					/* positive response */
					*reply_mesg = 0x76;
					*(reply_mesg+1) = *(can_mesg+1);
					*reply_mesg_len = 2;
				}
			}

			return 0;
		}
		/* request exit for download transfer */
		else if(0x37 == *can_mesg)
		{
			bootloader_logic->bootloader_subseq = DownloadDriver;
			driver_logicblock_node = \
					list_entry(bootloader_logic->driver_list_head.prev, LogicBlockNode, logic_block_list);
			ptr_datasegment = list_entry(driver_logicblock_node->logic_block_data.data_segment_head.prev, \
					DataSegment, segment_list);

			if(ptr_datasegment->segment_len < ptr_datasegment->mem_size)
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x37;
				*(reply_mesg+2) = 0x24;
				*reply_mesg_len = 3;
			}
			else
			{
				driver_logicblock_node->logic_block_data.block_state = FinishDownload;

				/* positive response */
				*reply_mesg = 0x77;
				*reply_mesg_len = 1;
			}

			return 0;
		}
		/* check data integrity */
		else if((0x31 == *can_mesg) && (0x01 == *(can_mesg+1)) && \
				(0x02 == *(can_mesg+2)) && (0x02 == *(can_mesg+3)))
		{
			driver_logicblock_node = \
					list_entry(bootloader_logic->driver_list_head.prev, LogicBlockNode, logic_block_list);
			driver_logicblock_node->logic_block_data.block_state = CheckingIntegrity;
			crc32_temp = (*(can_mesg+4)<<24)|(*(can_mesg+5)<<16)|(*(can_mesg+6)<<8)|*(can_mesg+7);

			if(crc32_temp == driver_logicblock_node->logic_block_data.crc32_cal)
			{
				driver_logicblock_node->logic_block_data.block_download_result = 1;
				bootloader_logic->bootloader_subseq = DownloadApplication;
				/* positive response */
				*reply_mesg = 0x71;
				*(reply_mesg+1) = 0x01;
				*(reply_mesg+2) = 0x02;
				*(reply_mesg+3) = 0x02;
				*(reply_mesg+4) = 0x00;
				*reply_mesg_len = 5;
			}
			else
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x31;
				*(reply_mesg+2) = 0x22;
				*reply_mesg_len = 3;
			}

			return 0;
		}
	}

	return -1;
}




static int download_program_process(BootloaderBusinessLogic *bootloader_logic, \
		const unsigned char* can_mesg, unsigned short mesg_len, \
		unsigned char* reply_mesg, unsigned short* reply_mesg_len)
{
	unsigned char counter = 0, i = 0;
	unsigned char mem_addr_bytes = 0, mem_size_bytes = 0;
	LogicBlockNode *app_logicblock_node = NULL;
	LogicBlockNode *driver_logicblock_node = NULL;
	DataSegment *ptr_datasegment = NULL;
	unsigned int crc32_temp = 0;
	struct list_head *temp_list_head = NULL;

	if(DownloadApplication == bootloader_logic->bootloader_subseq)
	{
		/* write finger print information */
		if((0x2E == *can_mesg) && (0xF1 == *(can_mesg+1)) && (0x5A == *(can_mesg+2)))
		{
			bootloader_logic->bootloader_subseq = DownloadApplication;
			app_logicblock_node = (LogicBlockNode*)malloc(sizeof(LogicBlockNode));

			if(NULL == app_logicblock_node)
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x2E;
				*(reply_mesg+2) = 0x72;  //general programming error
				*reply_mesg_len = 3;
			}
			else
			{
				/* application program logic block initialization */
				app_logicblock_node->logic_block_data.mem_size = 0;
				app_logicblock_node->logic_block_data.mem_addr = 0;
				app_logicblock_node->logic_block_data.file_type = 1;  //program file
				app_logicblock_node->logic_block_data.block_state = WriteFingerPrint;
				app_logicblock_node->logic_block_data.block_index = 0;
				app_logicblock_node->logic_block_data.crc32_cal = 0;
				app_logicblock_node->logic_block_data.block_download_result = 0;
				app_logicblock_node->logic_block_data.MaxNumOfBlockLeng = 250;
				app_logicblock_node->logic_block_data.finger_print.block_index = \
						&(app_logicblock_node->logic_block_data.block_index);
				app_logicblock_node->logic_block_data.finger_print.YY = *(can_mesg+3);  //year
				app_logicblock_node->logic_block_data.finger_print.MM = *(can_mesg+4);  //month
				app_logicblock_node->logic_block_data.finger_print.DD = *(can_mesg+5);  //day

				/* OBD serial number */
				memcpy(app_logicblock_node->logic_block_data.finger_print.serial_num, (can_mesg+6), 6);

				/* initial head list of data segment */
				INIT_LIST_HEAD(&app_logicblock_node->logic_block_data.data_segment_head);

				/* add application program logic block into application list */
				list_add_tail(&app_logicblock_node->logic_block_list, &bootloader_logic->app_list_head);

				/* positive response */
				*reply_mesg = 0x6E;
				*(reply_mesg+1) = 0xF1;
				*(reply_mesg+2) = 0x5A;
				*reply_mesg_len = 3;
			}

			return 0;
		}
		/* erase memory */
		else if((0x31 == *can_mesg) && (0x01 == *(can_mesg+1)) && \
				(0xFF == *(can_mesg+2)) && (0x00 == *(can_mesg+3)))
		{
			/* if app_list_head is empty*/
			if(list_empty(&bootloader_logic->app_list_head))
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x31;
				*(reply_mesg+2) = 0x72;  //general programming error
				*reply_mesg_len = 3;
			}
			else
			{
				app_logicblock_node = \
						list_entry(bootloader_logic->app_list_head.prev, LogicBlockNode, logic_block_list);
				app_logicblock_node->logic_block_data.block_state = ErasingMemory;

				/* get memory address bytes */
				mem_addr_bytes = *(can_mesg+4)&0x0F;
				/* get memory size bytes */
				mem_size_bytes = (*(can_mesg+4)&0xF0)>>4;

				if((mem_addr_bytes > 4) || (mem_size_bytes > 4))
				{
					/* negative response */
					*reply_mesg = 0x7F;
					*(reply_mesg+1) = 0x31;
					*(reply_mesg+2) = 0x72;  //general programming error
					*reply_mesg_len = 3;
					return 0;
				}

				/* get memory address */
				for(i=0; i<mem_addr_bytes; i++)
				{
					if(i > 0)
					{
						app_logicblock_node->logic_block_data.mem_addr <<= 8;
					}

					app_logicblock_node->logic_block_data.mem_addr |= \
							*(can_mesg+5+i);
				}

				/* get memory size */
				for(i=0; i<mem_size_bytes; i++)
				{
					if(i > 0)
					{
						app_logicblock_node->logic_block_data.mem_size <<= 8;
					}

					app_logicblock_node->logic_block_data.mem_size |= \
							*(can_mesg+5+mem_addr_bytes+i);
				}

				bootloader_logic->bootloader_subseq = DownloadApplication;

				/* positive response */
				*reply_mesg = 0x71;
				*(reply_mesg+1) = 0x01;
				*(reply_mesg+2) = 0xFF;
				*(reply_mesg+3) = 0x00;
				*(reply_mesg+4) = 0x00;
				*reply_mesg_len = 5;
			}

			return 0;
		}
		/* request download */
		else if(0x34 == *can_mesg)
		{
			/* if app_list_head is empty*/
			if(list_empty(&bootloader_logic->app_list_head))
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x34;
				*(reply_mesg+2) = 0x70;  //reject download
				*reply_mesg_len = 3;
			}
			else
			{
				bootloader_logic->bootloader_subseq = DownloadApplication;
				app_logicblock_node = \
						list_entry(bootloader_logic->app_list_head.prev, LogicBlockNode, logic_block_list);
				app_logicblock_node->logic_block_data.block_state = RequestDownload;

				ptr_datasegment = (DataSegment*)malloc(sizeof(DataSegment));

				if(NULL == ptr_datasegment)
				{
					/* negative response */
					*reply_mesg = 0x7F;
					*(reply_mesg+1) = 0x34;
					*(reply_mesg+2) = 0x70;  //reject download
					*reply_mesg_len = 3;
					return 0;
				}

				mem_addr_bytes = *(can_mesg+2)&0x0F; //get memory address bytes
				mem_size_bytes = (*(can_mesg+2)&0xF0)>>4; //get memory size bytes

				if((*(can_mesg+1) != 0) || (mem_addr_bytes > 4) || (mem_size_bytes > 4))
				{
					/* negative response */
					*reply_mesg = 0x7F;
					*(reply_mesg+1) = 0x34;
					*(reply_mesg+2) = 0x31;  //out of range
					*reply_mesg_len = 3;
					return 0;
				}

				/* get memory address */
				for(i=0; i<mem_addr_bytes; i++)
				{
					if(i > 0)
					{
						ptr_datasegment->mem_addr <<= 8;
					}

					ptr_datasegment->mem_addr |= \
							*(can_mesg+3+i);
				}

				/* get memory size */
				for(i=0; i<mem_size_bytes; i++)
				{
					if(i > 0)
					{
						ptr_datasegment->mem_size <<= 8;
					}

					ptr_datasegment->mem_size |= \
							*(can_mesg+3+mem_addr_bytes+i);
				}

				ptr_datasegment->data = (unsigned char*)malloc(ptr_datasegment->mem_size);

				if(NULL == ptr_datasegment->data)
				{
					/* negative response */
					*reply_mesg = 0x7F;
					*(reply_mesg+1) = 0x34;
					*(reply_mesg+2) = 0x70;  //reject download
					*reply_mesg_len = 3;
					return 0;
				}

				ptr_datasegment->segment_len = 0;
				list_add_tail(&ptr_datasegment->segment_list, \
						&app_logicblock_node->logic_block_data.data_segment_head);

				/* positive response */
				*reply_mesg = 0x74;
				*(reply_mesg+1) = 0x10;
				*(reply_mesg+2) = app_logicblock_node->logic_block_data.MaxNumOfBlockLeng;
				*reply_mesg_len = 3;
			}

			return 0;
		}
		/* download transfer */
		else if(0x36 == *can_mesg)
		{
			/* if app_list_head is empty*/
			if(list_empty(&bootloader_logic->app_list_head))
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x36;
				*(reply_mesg+2) = 0x72; //general programming error
				*reply_mesg_len = 3;
			}
			else
			{
				bootloader_logic->bootloader_subseq = DownloadApplication;
				app_logicblock_node = \
						list_entry(bootloader_logic->app_list_head.prev, LogicBlockNode, logic_block_list);
				app_logicblock_node->logic_block_data.block_index =  *(can_mesg+1);
				app_logicblock_node->logic_block_data.block_state = DownloadData;
				ptr_datasegment = list_entry(app_logicblock_node->logic_block_data.data_segment_head.prev, \
						DataSegment, segment_list);
				ptr_datasegment->block_index = *(can_mesg+1);
				memcpy((ptr_datasegment->data+ptr_datasegment->segment_len), (can_mesg+2), mesg_len-2);
				crc32_temp = crc32c(app_logicblock_node->logic_block_data.crc32_cal, \
						(ptr_datasegment->data+ptr_datasegment->segment_len), mesg_len-2);
				app_logicblock_node->logic_block_data.crc32_cal = crc32_temp;
				ptr_datasegment->segment_len += mesg_len-2;

				if(ptr_datasegment->segment_len > ptr_datasegment->mem_size)
				{
					/* negative response */
					*reply_mesg = 0x7F;
					*(reply_mesg+1) = 0x36;
					*(reply_mesg+2) = 0x24; //request sequence error
					*reply_mesg_len = 3;
				}
				else
				{
					/* positive response */
					*reply_mesg = 0x76;
					*(reply_mesg+1) = *(can_mesg+1);
					*reply_mesg_len = 2;
				}
			}

			return 0;
		}
		/* request exit for download transfer */
		else if(0x37 == *can_mesg)
		{
			bootloader_logic->bootloader_subseq = DownloadDriver;
			app_logicblock_node = \
					list_entry(bootloader_logic->app_list_head.prev, LogicBlockNode, logic_block_list);
			ptr_datasegment = list_entry(app_logicblock_node->logic_block_data.data_segment_head.prev, \
					DataSegment, segment_list);

			if(ptr_datasegment->segment_len < ptr_datasegment->mem_size)
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x37;
				*(reply_mesg+2) = 0x24;
				*reply_mesg_len = 3;
			}
			else
			{
				app_logicblock_node = \
						list_entry(bootloader_logic->app_list_head.prev, LogicBlockNode, logic_block_list);
				app_logicblock_node->logic_block_data.block_state = FinishDownload;

				/* positive response */
				*reply_mesg = 0x77;
				*reply_mesg_len = 1;
			}
			return 0;
		}
		/* check data integrity */
		else if((0x31 == *can_mesg) && (0x01 == *(can_mesg+1)) && \
				(0x02 == *(can_mesg+2)) && (0x02 == *(can_mesg+3)))
		{
			app_logicblock_node = \
					list_entry(bootloader_logic->app_list_head.prev, LogicBlockNode, logic_block_list);
			app_logicblock_node->logic_block_data.block_state = CheckingIntegrity;
			crc32_temp = (*(can_mesg+4)<<24)|(*(can_mesg+5)<<16)|(*(can_mesg+6)<<8)|*(can_mesg+7);

			if(crc32_temp == app_logicblock_node->logic_block_data.crc32_cal)
			{
				app_logicblock_node->logic_block_data.block_download_result = 1;
				/* positive response */
				*reply_mesg = 0x71;
				*(reply_mesg+1) = 0x01;
				*(reply_mesg+2) = 0x02;
				*(reply_mesg+3) = 0x02;
				*(reply_mesg+4) = 0x00;
				*reply_mesg_len = 5;
			}
			else
			{
				/* negative response */
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x31;
				*(reply_mesg+2) = 0x22;
				*reply_mesg_len = 3;
			}
			return 0;
		}
		/* check programming dependency */
		else if((0x31 == *can_mesg) && (0x01 == *(can_mesg+1)) && \
				(0xFF == *(can_mesg+2)) && (0x01 == *(can_mesg+3)))
		{
			list_for_each(temp_list_head, &bootloader_logic->app_list_head)
			{
				app_logicblock_node = list_entry(temp_list_head, \
						LogicBlockNode, logic_block_list);

				if(app_logicblock_node->logic_block_data.block_download_result != 1)
				{
					*reply_mesg = 0x7F;
					*(reply_mesg+1) = 0x31;
					*(reply_mesg+2) = 0x72;  //general programming error
					*reply_mesg_len = 3;
					return 0;
				}
			}

			list_for_each(temp_list_head, &bootloader_logic->driver_list_head)
			{
				driver_logicblock_node = list_entry(temp_list_head, \
						LogicBlockNode, logic_block_list);

				if(driver_logicblock_node->logic_block_data.block_download_result != 1)
				{
					*reply_mesg = 0x7F;
					*(reply_mesg+1) = 0x31;
					*(reply_mesg+2) = 0x72;  //general programming error
					*reply_mesg_len = 3;
					return 0;
				}
			}

			bootloader_logic->bootloader_subseq = CheckDependency;
			*reply_mesg = 0x71;
			*(reply_mesg+1) = 0x01;
			*(reply_mesg+2) = 0xFF;
			*(reply_mesg+3) = 0x01;
			*(reply_mesg+4) = 0x00;
			*reply_mesg_len = 5;

			return 0;
		}
	}

	return -1;
}




int bootloader_main_process(BootloaderBusinessLogic *bootloader_logic, \
		const unsigned char* can_mesg, unsigned short mesg_len, \
		unsigned char* reply_mesg, unsigned short* reply_mesg_len)
{
	unsigned char i, counter = 0;
	unsigned int recv_secret_key;
	LogicBlockNode *pr_logicblock_node = NULL;

	if((NULL == can_mesg) || (NULL == reply_mesg) || (mesg_len <= 0))
	{
		return -1;
	}

	switch(bootloader_logic->bootloader_subseq)
	{
	case ExtendedSession:
		if((0x10 == *can_mesg) && (0x03 == *(can_mesg+1)))
		{
			*reply_mesg = 0x50;
			*(reply_mesg+1) = 0x03;
			*(reply_mesg+2) = 0x00;
			*(reply_mesg+3) = 0x32;
			*(reply_mesg+4) = 0x01;
			*(reply_mesg+5) = 0xF4;
			*reply_mesg_len = 6;
			bootloader_logic->bootloader_subseq = \
					CheckPreprogrammingCondition;
		}
		else
		{
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x10;
			*(reply_mesg+2) = 0x13; //message length or format error
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case CheckPreprogrammingCondition:
		if((0x31 == *can_mesg) && (0x01 == *(can_mesg+1)) && (0x02 == *(can_mesg+2)) \
				&& (0x03 == *(can_mesg+3)))
		{
			*reply_mesg = 0x71;
			*(reply_mesg+1) = 0x01;
			*(reply_mesg+2) = 0x02;
			*(reply_mesg+3) = 0x03;
			*(reply_mesg+4) = 0x00;
			*reply_mesg_len = 5;
			bootloader_logic->bootloader_subseq = SetDTCoff;
		}
		else
		{
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x31;
			*(reply_mesg+2) = 0x13; //message length or format error
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case SetDTCoff:
		if((0x85 == *can_mesg) && (0x02 == *(can_mesg+1)))
		{
			*reply_mesg = 0xC5;
			*(reply_mesg+1) = 0x02;
			*reply_mesg_len = 2;
			bootloader_logic->bootloader_subseq = ForbidCommunication;
		}
		else
		{
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x85;
			*(reply_mesg+2) = 0x13;
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case ForbidCommunication:
		if((0x28 == *can_mesg) && (0x03 == *(can_mesg+1)) && (0x03 == *(can_mesg+2)))
		{
			*reply_mesg = 0x68;
			*(reply_mesg+1) = 0x03;
			*reply_mesg_len = 2;
			bootloader_logic->bootloader_subseq = ReadDataByDID;
		}
		else
		{
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x28;
			*(reply_mesg+2) = 0x13;
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case ReadDataByDID:
		if((0x22 == *can_mesg))
		{
			*reply_mesg = 0x62;
			*(reply_mesg+1) = *(can_mesg+1);
			*(reply_mesg+2) = 0xAA;
			*reply_mesg_len = 3;
			bootloader_logic->bootloader_subseq = ProgrammingSession;
		}
		else
		{
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x22;
			*(reply_mesg+2) = 0x13;
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case ProgrammingSession:
		if((0x10 == *can_mesg) && (0x02 == *(can_mesg+1)))
		{
			*reply_mesg = 0x50;
			*(reply_mesg+1) = 0x02;
			*(reply_mesg+2) = 0x00;
			*(reply_mesg+3) = 0x32;
			*(reply_mesg+4) = 0x01;
			*(reply_mesg+5) = 0xF4;
			*reply_mesg_len = 6;
			bootloader_logic->bootloader_subseq = ProgrammingSession;
		}
		else
		{
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x10;
			*(reply_mesg+2) = 0x13;
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case SafeAccessForSeed:
		if((0x27 == *can_mesg) && (0x09 == *(can_mesg+1)))
		{
			bootloader_logic->seed.level_FBL = get_random(1, 10000);  //generating seed
			*reply_mesg = 0x67;
			*(reply_mesg+1) = 0x09;
			*(reply_mesg+2) = (unsigned char) ((bootloader_logic->seed.level_FBL&0xFF000000)>>24);
			*(reply_mesg+3) = (unsigned char) ((bootloader_logic->seed.level_FBL&0x00FF0000)>>16);
			*(reply_mesg+4) = (unsigned char) ((bootloader_logic->seed.level_FBL&0x0000FF00)>>8);
			*(reply_mesg+5) = (unsigned char) (bootloader_logic->seed.level_FBL&0xFF);
			*reply_mesg_len = 6;
			bootloader_logic->bootloader_subseq = SafeAccessForKey;
		}
		else
		{
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x27;
			*(reply_mesg+2) = 0x13;
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case SafeAccessForKey:
		if((0x27 == *can_mesg) && (0x0A == *(can_mesg+1)))
		{
			recv_secret_key = *(can_mesg+2);
			recv_secret_key = (recv_secret_key<<8)|*(can_mesg+3);
			recv_secret_key = (recv_secret_key<<8)|*(can_mesg+4);
			recv_secret_key = (recv_secret_key<<8)|*(can_mesg+5);

			/* calculate secret key */
			//ASAP1A_CCP_ComputeKeyFromSeed();

			if(bootloader_logic->secret_key.level_FBL == recv_secret_key)
			{
				*reply_mesg = 0x67;
				*(reply_mesg+1) = 0x0A;
				*reply_mesg_len = 2;
				bootloader_logic->bootloader_subseq = DownloadDriver;
			}
			else
			{
				*reply_mesg = 0x7F;
				*(reply_mesg+1) = 0x27;
				*(reply_mesg+2) = 0x35;  //invalid secret key
				*reply_mesg_len = 3;
			}
		}
		else
		{
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x27;
			*(reply_mesg+2) = 0x13;  //message length or format error
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case DownloadDriver:
		download_driver_process(bootloader_logic, can_mesg, mesg_len, reply_mesg, reply_mesg_len);
		break;

	case DownloadApplication:
		 download_program_process(bootloader_logic, can_mesg, mesg_len, reply_mesg, reply_mesg_len);
		 break;

	case ResetECU:
		if((0x11 == *can_mesg) && (0x01 == *(can_mesg+1)))
		{
			*reply_mesg = 0x51;
			*(reply_mesg+1) = 0x01;
			*reply_mesg_len = 2;
			bootloader_logic->bootloader_subseq = ExtendedSessionAgain;
		}
		else
		{
			/* negative response */
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x11;
			*(reply_mesg+2) = 0x13;  //message length or format error
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case ExtendedSessionAgain:
		if((0x10 == *can_mesg) && (0x03 == *(can_mesg+1)))
		{
			*reply_mesg = 0x50;
			*(reply_mesg+1) = 0x03;
			*(reply_mesg+2) = 0x00;
			*(reply_mesg+3) = 0x32;
			*(reply_mesg+4) = 0x01;
			*(reply_mesg+5) = 0xF4;
			*reply_mesg_len = 6;
			bootloader_logic->bootloader_subseq = ResumeCommunication;
		}
		else
		{
			/* negative response */
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x10;
			*(reply_mesg+2) = 0x13;  //message length or format error
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case ResumeCommunication:
		if((0x28 == *can_mesg) && (0x00 == *(can_mesg+1)) && (0x03 == *(can_mesg+2)))
		{
			*reply_mesg = 0x68;
			*(reply_mesg+1) = 0x00;
			*reply_mesg_len = 2;
			bootloader_logic->bootloader_subseq = ResumeDTC;
		}
		else
		{
			/* negative response */
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x68;
			*(reply_mesg+2) = 0x13;  //message length or format error
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case ResumeDTC:
		if((0x85 == *can_mesg) && (0x01 == *(can_mesg+1)))
		{
			*reply_mesg = 0xC5;
			*(reply_mesg+1) = 0x01;
			*reply_mesg_len = 2;
			bootloader_logic->bootloader_subseq = DefaultSession;
		}
		else
		{
			/* negative response */
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x85;
			*(reply_mesg+2) = 0x13;  //message length or format error
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case DefaultSession:
		if((0x10 == *can_mesg) && (0x01 == *(can_mesg+1)))
		{
			*reply_mesg = 0x10;
			*(reply_mesg+1) = 0x01;
			*(reply_mesg+2) = 0x00;
			*(reply_mesg+3) = 0x32;
			*(reply_mesg+4) = 0x01;
			*(reply_mesg+5) = 0xF4;
			*reply_mesg_len = 6;
			bootloader_logic->bootloader_subseq = ClearBootloaderInfor;
		}
		else
		{
			/* negative response */
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x10;
			*(reply_mesg+2) = 0x13;  //message length or format error
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	case ClearBootloaderInfor:
		if((0x14 == *can_mesg) && (0xFF == *(can_mesg+1)) &&\
				(0xFF == *(can_mesg+2)) && (0xFF == *(can_mesg+3)))
		{
			*reply_mesg = 0x54;
			*reply_mesg_len = 1;
			bootloader_logic->bootloader_subseq = FinshBootloader;
		}
		else
		{
			/* negative response */
			*reply_mesg = 0x7F;
			*(reply_mesg+1) = 0x14;
			*(reply_mesg+2) = 0x13;  //message length or format error
			*reply_mesg_len = 3;
		}
		return 0;
		break;

	default:
		return -1;
		break;
	}

	return -1;
}

