/*
 * defines.h
 *
 *  Created on: Dec 2, 2012
 *      Author: amira
 */

#ifndef DEFINES_H_
#define DEFINES_H_
#include        "stdio.h"
#include		"stdlib.h"
#include <iostream>
#include <queue>
#include <map>
#include <string.h>
#include <string>
//#include "global.h"
#include "syscalls.h"
#include "vector"
#include <semaphore.h>

using namespace std;

//defines

#define                  DELETED 0;
#define                NOT_DELETED 1;
#define			          DEBUG 				1
#define                  LOCK                     1
#define                  UNLOCK                   0
#define                  SUSPEND                 TRUE
#define                 NOT_SUSPEND              FALSE
#define 				Time_slice                50
#define 				MAX_NUM_PROCESSES         5
#define 				MAX_PRIO				  100
#define			         RECEIVE			      60
#define                  SUSPEND_AND_RECIVEING     11
#define                 Waiting_For_RECEIVE        13
#define                 READY                      17
#define                 SUSPEND_AND_BLOCKED        23
#define                 SUSPEND_AND_BLOCKED_ONDISK        24
#define                 BLOCKED_ONDISK              25

#define                 BLOCKED                    27
#define		        	MAX_DISKS				12
#define			        MAX_SECTORS				1600
#define                 MAX_NUM_PHY_PAGES       4096
#define                 PROCESS_PAGE             10

#define                 MAX_TAG_LENGTH           32
struct MSG {
	char message[200];
	INT32 dest_ID;
	INT32 src_ID;
	INT32 Length;
};

struct Sharing {

	INT32 starting_address_shared;
	INT32 num_Shared_pages;
	INT32 *num_sharers;
	char * tag;
	Sharing * next;

};

struct PCB {
	char * PROCESS_NAME;
	INT32 ID;
	INT32 STATE;
	INT32 BASE_PRIORITY;
	INT32 time;
	void* CONTEXT;
	vector<INT32> listOfChildern;
	INT32 numberOfChildern;
	INT32 STATE_MSG;
	INT32 NUM_MSG;
	MSG* INBOX;
	INT32 parent;
	INT32 disk;
	INT32 num_pages_assign;
	INT32 page_pointer[VIRTUAL_MEM_PGS];
	Sharing areas;
	INT32 counter;
};
/*			FUNCTION CALLS*/

class ComparePriority {
public:
	bool operator()(PCB*& pcb1, PCB*& pcb2) // t2 has highest prio than t1 if t2 is earlier than t1
			{
		if (pcb2->BASE_PRIORITY == pcb1->BASE_PRIORITY) {
			if (pcb2->ID < pcb1->ID)
				return true;

		}

		else if (pcb2->BASE_PRIORITY < pcb1->BASE_PRIORITY)
			return true;
		return false;
	}
};
;

class CompareWakeUpTime {
public:
	bool operator()(PCB*& pcb1, PCB*& pcb2) // t2 has highest prio than t1 if t2 is earlier than t1
			{
		if (pcb2->time < pcb1->time)
			return true;
		return false;
	}
};

//variables
extern INT32 left_time;
extern sem_t messages_sema;
extern map<string, int> proNametoProId;
extern map<int, PCB*> proIdToPCB;
extern map<void*, PCB*> ContextToPCB;
extern map<int, int> readyQueue_proIdToExistance;
extern map<int, int> timerQueue_proIdToExistance;
extern map<int, int> suspendQueue_proIdToExistance;
extern PCB *current_PCB;
extern priority_queue<PCB*, vector<PCB*>, ComparePriority> ready_queue;
extern priority_queue<PCB*, vector<PCB*>, CompareWakeUpTime> timer_queue;
//extern list<PCB*> suspend_queue;
extern int num_pids, processID;
extern int events;
extern bool svc_or_interrupt;
//methods

/*dispatcher*/
PCB *getReady();
void disp_run_readyProcess(bool kill_or_save);
void switch_context(bool save_or_kill, bool fault);

/*PQ methods*/
void add_to_readyQueue(PCB* pcb);
void add_to_timerQueue(PCB* pcb);
void remove_from_readyQueue(INT32 process_id, bool *result);
void remove_from_timerQueue(PCB* pcb);
void timer_to_readyQueue(PCB* pcb);
void ready_to_timerQueue(PCB* pcb);
void clean(INT32 process_id);
void remove_processes_finised_sleep(INT32 current_time, bool need);
/*process methods*/
void terminate_Process(INT32 process_ID, INT32 *error);
void create_Process(char * process_name, void *starting_address,
		INT32 initial_priority, INT32 *process_id, INT32 *error);
PCB* create_tests(char* process_name, INT32 initial_priority, bool Notfirst,
		void* context);
void get_Process_Id(char* process_name, INT32 *process_id, INT32 *error);
void suspend_Process(INT32 process_ID, INT32 *error, bool iAmSuspend,
		bool JUST);
void resume_Process(INT32 process_ID, INT32 *error, bool iAmResume);
void change_priority(INT32 process_ID, INT32 new_priority, INT32 *error);
void get_currentTime(INT32* currentTime);
void Start_Timer(INT32 Time);
void getFirstSleepTime(INT32 time);
bool check_PID_existance(INT32 pid);
bool check_process_existance(INT32 id, list<PCB*> list);

/*lock methods*/
void lock_readyQueue();
void unlock_readyQueue();
void lock_run_process();
void unlock_run_process();
void lock_timerQueue();
void unlock_timerQueue();
void lock_z502_timer();
void unlock_z502_timer();
void lock_svc();
void unlock_svc();

/*MESSAGES*/
void send_Message(INT32 dest_ID, char *message, INT32 msg_Len, INT32 *error);
void receive_Message(INT32 src_ID, char *message, INT32 msg_rcvLen,
		INT32 *msg_sndLen, INT32 *sender_ID, INT32 *error);
MSG* get_msg(INT32 src_ID);
MSG* get_msg1(INT32 src_ID);
void suspend_process_msg(INT32 src_ID, char *message, INT32*error);
void get_msg_Inbox(char *message, INT32 *msg_sndLen, INT32 *sender_ID);

/*------------------------------------------------phase2----------------------------------------------------*/

typedef struct {
	INT32 p_id;
	INT32 page;
	INT32 frame_number;
	INT32 used_Bit;
	INT32 ref_Time;
	void *next;
} FRAMETABLE;

typedef struct {
	INT32 id;
	INT32 frame;
	INT32 sector;
	INT32 page;
	INT32 p_ID;
	INT32 disk;
	void *next;
} SHADOWTABLE;

extern INT16 bitMap[MAX_DISKS][MAX_SECTORS];
extern SHADOWTABLE *shadow_list;
/*must be initialized in os_init*/
extern FRAMETABLE *page_list;
extern PCB* suspend_on_disks;
extern INT16 bitMap[MAX_DISKS][MAX_SECTORS];
extern UINT16 *Z502_PAGE_TBL_ADDR;
extern INT16 Z502_PAGE_TBL_LENGTH;
extern INT32 SYS_CALL_CALL_TYPE;
extern FRAMETABLE *phys_frames[PHYS_MEM_PGS];
/*--------------------------------------------page handling------------------------------------------------*/
void paging_handle(INT32 page_request);
void paging_replacemnt(INT32 page_request);
void check_size(INT32 page_size);
INT32 get_empty_frame(INT32 status);
FRAMETABLE get_full_frame(INT32 page_request);
void set_ref_time(INT32 frame);
/*disks*/
void add_to_Shadow(SHADOWTABLE *entry);
void remove_from_Shadow(INT32 id);
void add_to_suspend(PCB* pcb);
void rm_from_suspend(INT32 id);
void get_empty_disk(INT16 *disk, INT16 *sector);
void write_to_Disk(INT16 disk_id, INT16 sector, char data[PGSIZE], bool j);
void read_from_Disk(INT16 disk_id, INT16 sector, char data[PGSIZE], bool j);
INT32 wakeup_finished_Disks(INT32 disk);
void memory_to_disk(FRAMETABLE *table, INT32 page);
void disk_to_memory(FRAMETABLE *table, INT32 page);
INT32 disk_handler(INT32 disk_status, INT32 disk);
void update_Page_attr(INT32 page, INT32 frame);

void define_shared_area(INT32 starting_address_shared, INT32 num_Shared_pages,
		char * tag, INT32 *num_sharers, INT32 * error);

int check_Tags(char *tag1, char *tag);

#endif /* DEFINES_H_ */
