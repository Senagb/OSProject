#include				"global.h"
#include				"syscalls.h"
#include				"protos.h"
#include				"string.h"
#include				"defs.h"
#define			MAXPGSIZE			1024
using namespace std;

/*-----------------------------------------------------------variables-------------------------------------------------------------------*/
FRAMETABLE *phys_frames[PHYS_MEM_PGS ];
INT32 inc_shadow;
SHADOWTABLE *shadowList = NULL;
/*must be initialized in os_init*/
FRAMETABLE *page_list;
//Disk Map INIT
INT16 bitMap[MAX_DISKS][MAX_SECTORS] = { 0 };
/*suspend queue for disks*/
PCB* suspend_on_disks;

/*------------------------------------------------------------memory--------------------------------------------------------------------*/
/*
 * paging_handle :
 * handles page faults
 * */
void paging_handle(INT32 status) {

	//check if the page table isn't inialized
	if (Z502_PAGE_TBL_LENGTH == 0) {
		Z502_PAGE_TBL_LENGTH = VIRTUAL_MEM_PGS;
		Z502_PAGE_TBL_ADDR = (UINT16 *) calloc(sizeof(INT16), VIRTUAL_MEM_PGS);

//		for (int i = 0; i < PROCESS_PAGE; ++i) {
//			INT32 empty_frame = get_empty_frame(status);
//			current_PCB->num_pages_assign--;
//			current_PCB->page_pointer[current_PCB->counter] = empty_frame;
//			current_PCB->counter++;
//			phys_frames[empty_frame]->frame_number = empty_frame;
//			phys_frames[empty_frame]->p_id = current_PCB->ID;
//			phys_frames[empty_frame]->page = status;
//			//set the physical page number
//			Z502_PAGE_TBL_ADDR[status] = (UINT16) empty_frame; //set the frame number
//			Z502_PAGE_TBL_ADDR[status] |= (UINT16) PTBL_VALID_BIT; //set the valid bit
//
//		}

	}
	//get an empty frame
	INT32 empty_frame = get_empty_frame(status);
	//check of the vaild_bit of that page need to set and number of frame assigned to the process >0
	if ((Z502_PAGE_TBL_ADDR[status] & (UINT16) PTBL_VALID_BIT) == 0
			& (current_PCB->num_pages_assign > 0 && empty_frame != -1)) {
		//Decrease the number of physical pages of the process
		current_PCB->num_pages_assign--;
		current_PCB->page_pointer[current_PCB->counter] = empty_frame;
		current_PCB->counter++;
		phys_frames[empty_frame]->frame_number = empty_frame;
		phys_frames[empty_frame]->p_id = current_PCB->ID;
		phys_frames[empty_frame]->page = status;
		//set the physical page number
		Z502_PAGE_TBL_ADDR[status] = (UINT16) empty_frame; //set the frame number
		Z502_PAGE_TBL_ADDR[status] |= (UINT16) PTBL_VALID_BIT; //set the valid bit
	}
	//need page replacement
	else if ((Z502_PAGE_TBL_ADDR[status] & (UINT16) PTBL_VALID_BIT) == 0
			& (current_PCB->num_pages_assign == 0 || empty_frame == -1)) {
		paging_replacemnt(status);
		switch_context(SWITCH_CONTEXT_SAVE_MODE, true);

	}
	/*set memory printer*/
	INT32 state;
	for (int i = 0; i < PHYS_MEM_PGS ; i++) {
		state = 0;
		INT32 valid = Z502_PAGE_TBL_ADDR[phys_frames[i]->page] & PTBL_VALID_BIT;
		INT32 ref = Z502_PAGE_TBL_ADDR[phys_frames[i]->page]
				& PTBL_REFERENCED_BIT;
		INT32 modi = Z502_PAGE_TBL_ADDR[phys_frames[i]->page]
				& PTBL_MODIFIED_BIT;

		if (valid != 0)
			state += 4;
		if (ref != 0)
			state += 1;
		if (modi != 0)
			state += 2;

		MP_setup(i, phys_frames[i]->p_id, phys_frames[i]->page, state);
	}
	MP_print_line();

}
/*select page to replace it with the new page*/
void paging_replacemnt(INT32 status) {
//get the victim
	int victim_page = -1, frame_num;

	bool need_to_write = false;

	for (int i = 0; i < PROCESS_PAGE; ++i) {
		int temp_page_num = phys_frames[current_PCB->page_pointer[i]]->page;
		if (Z502_PAGE_TBL_ADDR[temp_page_num] & PTBL_MODIFIED_BIT == 0) {
			victim_page = temp_page_num;
			frame_num = current_PCB->page_pointer[i];
		}

	}

//cannot find a victim that not modified
	if (victim_page == -1) {
		victim_page = phys_frames[current_PCB->page_pointer[2]]->page;
		frame_num = current_PCB->page_pointer[2];
		need_to_write = true;
	}

//write the victim page
	if (need_to_write) {
		memory_to_disk(phys_frames[frame_num], status);
	}

// read the new page required
	disk_to_memory(phys_frames[frame_num], status);

	phys_frames[frame_num]->page = status;
	Z502_PAGE_TBL_ADDR[victim_page] = (UINT16) 0;
	Z502_PAGE_TBL_ADDR[status] = (UINT16) frame_num;
	Z502_PAGE_TBL_ADDR[status] |= (UINT16) PTBL_VALID_BIT;
}

/*get empty frame */
INT32 get_empty_frame(INT32 status) {
	for (int i = 0; i < PHYS_MEM_PGS ; ++i) {
		if (phys_frames[i]->used_Bit == 0) {
			phys_frames[i]->used_Bit = 1;
			return i;
		}
	}
	return -1;
}
// Add a shadow table entry
void add_to_Shadow(SHADOWTABLE *entry) {
	SHADOWTABLE *list = shadowList;
	inc_shadow++;
	entry->id = inc_shadow;

//if shadow table is empty
	if (list == NULL) {
		shadowList = entry;
		return;
	}
//get last element in the list
	while (list->next != NULL) {
		list = (SHADOWTABLE*) list->next;
	}
//make last pointer point to new entry
	list->next = entry;
}
// Remove shadow table entry
void remove_from_Shadow(INT32 removeID) {
	SHADOWTABLE *list = shadowList;
	SHADOWTABLE *prev = NULL;
//loop over the table
	while (list != NULL) {
		//if this entry id is removeID
		if (list->id == removeID) {
			if (prev == NULL) {
				shadowList = (SHADOWTABLE*) list->next;
				return;
			} else if (list->next == NULL) {
				prev->next = NULL;
				return;
			} else {
				prev->next = list->next;
				return;
			}
		}
		prev = list;
		list = (SHADOWTABLE*) list->next;
	}
}

/*get empty disk to write in*/
void get_empty_disk(INT16 *disk, INT16 *sector) {
	INT16 disks, sectors;

	for (disks = 1; disks <= MAX_DISKS; disks++) {
		for (sectors = 1; sectors <= MAX_SECTORS; sectors++) {
			if (bitMap[disks][sectors] == 0) {
				bitMap[disks][sectors] = 1;
				*disk = disks;
				*sector = sectors;
				return;
			}
		}
	}
	*disk = -1;
	*sector = -1;
	return;
}
/*write from memory to disk*/
void memory_to_disk(FRAMETABLE *frame, INT32 page) {
	INT16 disk_id, sector;

//get empty sector
	get_empty_disk(&disk_id, &sector);
	/*make shadow table entry*/
	SHADOWTABLE*entry = (SHADOWTABLE*) (malloc(sizeof(SHADOWTABLE)));
	entry->p_ID = frame->p_id;
	entry->page = frame->page;
	entry->frame = frame->frame_number;
	entry->disk = disk_id;
	entry->sector = sector;
	entry->next = NULL;
	/*add to shadow table*/
	CALL( add_to_Shadow(entry));
	/*read data from memory and write it to disk*/
	char DATA[PGSIZE ];
	Z502_PAGE_TBL_ADDR[frame->page] = frame->frame_number;
	/*set valid bit*/
	Z502_PAGE_TBL_ADDR[frame->page] |= PTBL_VALID_BIT;
	/*read data from memory */
	ZCALL( MEM_READ( (frame->page*PGSIZE), (INT32*)DATA));
//Clear page table
	Z502_PAGE_TBL_ADDR[frame->page] = 0;
//Set new page attributes
	CALL( update_Page_attr(page, frame->frame_number));
//Write Data to Disk
	write_to_Disk(disk_id, sector, (char*) DATA, true);

}
void disk_to_memory(FRAMETABLE *frame, INT32 page_num) {
	SHADOWTABLE *temp;
	INT16 disk_id, sector;
	char DATA[PGSIZE ];
	memset(&DATA[0], 0, sizeof(DATA));
	/*physical frame i want to put data in*/
	INT32 id = frame->p_id;
	/*page i want to read*/
	INT32 page = frame->page;

	temp = shadowList;
	/*loop to get this page number*/
	while (temp != NULL) {
		if (temp->page == page && temp->p_ID == id) {
			//Read the Disk
			read_from_Disk(temp->disk, temp->sector, DATA, true);
			//Write to memory location
			Z502_PAGE_TBL_ADDR[page_num] = frame->frame_number;
			Z502_PAGE_TBL_ADDR[page_num] |= PTBL_VALID_BIT;
			/*write data come from disk to memory*/
			ZCALL( MEM_WRITE(page*PGSIZE, (INT32*)DATA));
			/*remove this entry from shadow table*/
			remove_from_Shadow(temp->id);
			return;
		}
		temp = (SHADOWTABLE*) temp->next;
	}

}
/*set that frame contain this page*/
void update_Page_attr(INT32 page, INT32 frame) {
	phys_frames[frame]->page = page;
	phys_frames[frame]->p_id = current_PCB->ID;
}

/*---------------------------------------------------------------Disk methods---------------------------------------------------------*/
/*
 * write to disk
 */
void write_to_Disk(INT16 disk_id, INT16 sector, char data[PGSIZE ],
		bool Just_P_handle) {
	INT32 Temp = 0;
	INT32 Temp1 = 0;
	INT32 DISK = (INT32) disk_id;
	INT32 SECT = (INT32) sector;
	SP_setup_action(SP_ACTION_MODE, "DISK_WRITE");
	ZCALL( MEM_WRITE( Z502DiskSetID, &DISK ));
//busy waiting until disk become free
	ZCALL( MEM_READ( Z502DiskStatus, &Temp ));
	while (Temp != DEVICE_FREE) {
		ZCALL( MEM_WRITE( Z502DiskSetID, &DISK ));
		ZCALL( MEM_READ( Z502DiskStatus, &Temp ));
	}
	ZCALL( MEM_WRITE( Z502DiskSetSector, &SECT ));
	ZCALL( MEM_WRITE( Z502DiskSetBuffer, (INT32*)data ));
//Specify a write
	Temp = 1;
	ZCALL( MEM_WRITE( Z502DiskSetAction, &Temp ));
	Temp = 0;                        // Must be set to 0
	ZCALL( MEM_WRITE( Z502DiskStart, &Temp ));
	current_PCB->disk = disk_id;
	current_PCB->STATE = BLOCKED_ONDISK;
	CALL( suspend_Process(-1, &Temp1,true,Just_P_handle));
}
/*read from disk*/
void read_from_Disk(INT16 disk_id, INT16 sector, char data[PGSIZE ], bool JUP) {
	INT32 Temp = 0;
	INT32 Temp1 = 0;
	SP_setup_action(SP_ACTION_MODE, "DISK_READ");
	INT32 DISK = disk_id;
	INT32 SECT = sector;
	memset(&data[0], 0, sizeof(data));

	ZCALL( MEM_WRITE( Z502DiskSetID, &DISK));
	ZCALL( MEM_READ( Z502DiskStatus, &Temp ));
//Wait until Disk is free
	while (Temp != DEVICE_FREE) {
		ZCALL( MEM_WRITE( Z502DiskSetID, &DISK));
		ZCALL( MEM_READ( Z502DiskStatus, &Temp ));
	}

	ZCALL( MEM_WRITE( Z502DiskSetSector, &SECT ));
	ZCALL( MEM_WRITE( Z502DiskSetBuffer, (INT32*)data ));
//Specify a read
	Temp = 0;
	ZCALL( MEM_WRITE( Z502DiskSetAction, &Temp ));
	Temp = 0;                        // Must be set to 0
	ZCALL( MEM_WRITE( Z502DiskStart, &Temp ));
	current_PCB->disk = disk_id;
	current_PCB->STATE = BLOCKED_ONDISK;
	CALL( suspend_Process(-1, &Temp1,true,JUP));
}

/*
 * DISK HANDLER
 * calls wakeup_Disks This will wake up processes who are using the disk that
 *	interrupted
 *
 */
INT32 disk_handler(INT32 diskStatus, INT32 disk) {
	INT32 temp;
	temp = wakeup_finished_Disks(disk);
	/*count number of waked processes*/
	return temp;
}

/*loop over suspend queue caused by disks and wake all processes waiting for this disk*/
INT32 wakeup_finished_Disks(INT32 disk) {
	map<int, int>::const_iterator it;
	int i = 0;
	int id = 0;
	int count = 0;
	for (it = suspendQueue_proIdToExistance.begin();
			i < SP_MAX_NUMBER_OF_PIDS
					&& it != suspendQueue_proIdToExistance.end(); ++it) {
		if ((*it).second == 1) {
			id = proIdToPCB[(*it).first]->ID;
			if (proIdToPCB[(*it).first]->STATE == SUSPEND_AND_BLOCKED_ONDISK
					&& proIdToPCB[(*it).first]->disk == disk) {
				/*remove from suspend queue*/
				suspendQueue_proIdToExistance[id] = -1;
				/*add to ready queue*/
				proIdToPCB[id]->STATE = READY;
				add_to_readyQueue(proIdToPCB[id]);
				count++;
			}
			i++;
		}
	}
	return count;
}

/*check tags*/
int check_Tags(char *tag1, char *tag) {
	for (int i = 0; i < MAX_TAG_LENGTH; ++i) {
		if (tag1[i] != tag[i])
			return -1;
	}
	return 0;
}
/*define shared area
 * */
void define_shared_area(INT32 starting_address_shared, INT32 num_Shared_pages,
		char * tag, INT32 *num_sharers, INT32 * error) {

	Sharing new_one;
	new_one.starting_address_shared = starting_address_shared;

	new_one.num_sharers = num_sharers;

	new_one.num_Shared_pages = num_Shared_pages;
	new_one.next = NULL;
	*error = ERR_SUCCESS;
	Sharing *temp_area = current_PCB->areas.next;

	printf("%s \n", tag);

	printf(
			"current pid = %d \n starting address =%d \n number of shared pages = %d \n sharers = %d\n",
			current_PCB->ID, starting_address_shared, num_Shared_pages,
			*num_sharers);

	while (temp_area != NULL) {
		int temp = check_Tags((*temp_area).tag, tag);
		if (temp == 0
				&& starting_address_shared
						== (*temp_area).starting_address_shared)
			*error = ERR_BAD_PARAM;

		temp_area = (*temp_area).next;
	}

	if (*error == ERR_SUCCESS) {
		temp_area = &new_one;
	}
	INT16 virtual_page_number;
	virtual_page_number = (INT16) (
			(starting_address_shared >= 0) ?
					starting_address_shared / PGSIZE : -1);

	INT32 page_offset;
	page_offset = starting_address_shared % PGSIZE;

	for (int i = 0; i < num_Shared_pages; ++i) {
		INT32 vi_address;
		vi_address = virtual_page_number * PGSIZE;
		vi_address += page_offset;
		MEM_WRITE( vi_address, (INT32 *)tag);
		virtual_page_number += 1;
	}
}
