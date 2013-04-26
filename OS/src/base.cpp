/************************************************************************

 This code forms the base of the operating system you will
 build.  It has only the barest rudiments of what you will
 eventually construct; yet it contains the interfaces that
 allow test.c and z502.c to be successfully built together.

 Revision History:
 1.0 August 1990
 1.1 December 1990: Portability attempted.
 1.3 July     1992: More Portability enhancements.
 Add call to sample_code.
 1.4 December 1992: Limit (temporarily) printout in
 interrupt handler.  More portability.
 2.0 January  2000: A number of small changes.
 2.1 May      2001: Bug fixes and clear STAT_VECTOR
 2.2 July     2002: Make code appropriate for undergrads.
 Default program start is in test0.
 3.0 August   2004: Modified to support memory mapped IO
 3.1 August   2004: hardware interrupt runs on separate thread
 3.11 August  2004: Support for OS level locking
 ************************************************************************/

#include             "global.h"
#include             "syscalls.h"
#include             "protos.h"
#include             "string.h"
#include 			 "z502.h"
#include 			<map>
#include 			<string>
#include 			<stdlib.h>
#include 			<iostream>
#include 			<queue>
#include 			<list>
#include            <algorithm>
#include             <vector>
#include 			"defs.h"
/**/
using namespace std;

extern char MEMORY[];
//extern BOOL          POP_THE_STACK;
extern UINT16 *Z502_PAGE_TBL_ADDR;
extern INT16 Z502_PAGE_TBL_LENGTH;
extern INT16 Z502_PROGRAM_COUNTER;
extern INT16 Z502_INTERRUPT_MASK;
extern INT32 SYS_CALL_CALL_TYPE;
extern INT16 Z502_MODE;
extern Z502_ARG Z502_ARG1;
extern Z502_ARG Z502_ARG2;
extern Z502_ARG Z502_ARG3;
extern Z502_ARG Z502_ARG4;
extern Z502_ARG Z502_ARG5;
extern Z502_ARG Z502_ARG6;
extern void *TO_VECTOR[];
extern INT32 CALLING_ARGC;
extern char **CALLING_ARGV;
char *call_names[] = { "mem_read ", "mem_write", "read_mod ", "get_time ",
		"sleep    ", "get_pid  ", "create   ", "term_proc", "suspend  ",
		"resume   ", "ch_prior ", "send     ", "receive  ", "disk_read",
		"disk_wrt ", "def_sh_ar" };
int processIDcounter = 0;
/*
 * this method is called when the running process need to wait till
 * a certain interrupt happens
 * */
void WAIT_FOR_INTERRUPT(short reason) {
	INT32 sleepTime, currentTime = 0, Status, timef; //variabels

	switch (reason) {
	//timer interrupt is waited for
	case TIMER_INTERRUPT :
		if (DEBUG) {
			SP_print_line();
		}
		sleepTime = Z502_ARG1.VAL;
		CALL(get_currentTime(&currentTime));
		//PCB wakeUp time equal to their sum
		current_PCB->time = (sleepTime + currentTime);
		//Move to the timer Queue
		current_PCB->STATE = BLOCKED;
		CALL( ready_to_timerQueue( current_PCB ));
		//if there are elements sleeping
		//so sleep time of the first process in the timer queue = firstprocess.time-currentTime
		CALL(getFirstSleepTime(timef));
		sleepTime = timef - currentTime;
		ZCALL( MEM_READ( Z502TimerStatus, &Status ));
		//start the timer again if needed
		if (sleepTime > 0 && Status == DEVICE_FREE)
			CALL( Start_Timer( sleepTime ));
		/*switch context to run new ready process*/
		CALL( switch_context(SWITCH_CONTEXT_SAVE_MODE,false));
		break;

	default:
		break;
	}

}
/*
 * this method is called when interrupt happens
 * */
void resume_process(short reason) {
	switch (reason) {
	case TIMER_INTERRUPT :
		INT32 Status;
		//Get current CPU Time
		INT32 currentTime;
		CALL(get_currentTime(&currentTime));
		//Wake up all WAITING items that are before currentTime
		CALL(remove_processes_finised_sleep(currentTime, false));
		//Get sleeptime
		INT32 sleep, timef;
		CALL(getFirstSleepTime(timef));
		sleep = timef - (currentTime);
		//There are more items on timerQueue
		//Start the new timer
		ZCALL( MEM_READ( Z502TimerStatus, &Status ));
		//start timer again if needed
		if (sleep > 0 && Status == DEVICE_FREE)
			Start_Timer(sleep);
		break;
	default:
		break;
	}

}

/************************************************************************
 INTERRUPT_HANDLER
 When the Z502 gets a hardware interrupt, it transfers control to
 this routine in the OS.
 ************************************************************************/
void interrupt_handler(void) {
	INT32 device_id;
	INT32 status;
	INT32 Index = 0;
	/*----------------------------------------phase2------------------------------------*/
	lock_run_process();
	/*--------------------------------------------------------------------*/
	MEM_READ(Z502InterruptDevice, &device_id);
	while (device_id != -1) {
		// Get cause of interrupt
		MEM_READ(Z502InterruptDevice, &device_id);
		// Set this device as target of our query
		MEM_WRITE(Z502InterruptDevice, &device_id);
		// Now read the status of this device
		MEM_READ(Z502InterruptStatus, &status);
		switch (device_id) {
		//TIMER INTERRUPT
		case (TIMER_INTERRUPT ):
			CALL(resume_process(TIMER_INTERRUPT));
			break;
			/*-----------------------------------phase2---------------------------------*/
		case (DISK_INTERRUPT_DISK1 ):
		case (DISK_INTERRUPT_DISK2 ):
		case (DISK_INTERRUPT_DISK3 ):
		case (DISK_INTERRUPT_DISK4 ):
		case (DISK_INTERRUPT_DISK5 ):
		case (DISK_INTERRUPT_DISK6 ):
		case (DISK_INTERRUPT_DISK7 ):
		case (DISK_INTERRUPT_DISK8 ):
		case (DISK_INTERRUPT_DISK9 ):
		case (DISK_INTERRUPT_DISK10 ):
		case (DISK_INTERRUPT_DISK11 ):
		case (DISK_INTERRUPT_DISK12 ):
			disk_handler(status, device_id);
			break;
		}
		/*----------------------------phase 2--------------------*/
		// Clear out this device - we're done with it
		MEM_WRITE(Z502InterruptClear, &Index);
	}
	/*--------------------------phase2-------------------------*/
	unlock_run_process();
	/*------------------------phase2--------------------------*/
} /* End of interrupt_handler */
/***********************************************************************
 FAULT_HANDLER
 The beginning of the OS502.  Used to receive hardware faults.
 ************************************************************************/

void fault_handler(void) {
	INT32 device_id;
	INT32 status;
	INT32 Index = 0;
//
// Get cause of interrupt
	MEM_READ(Z502InterruptDevice, &device_id);
// Set this device as target of our query
	MEM_WRITE(Z502InterruptDevice, &device_id);
// Now read the status of this device
	MEM_READ(Z502InterruptStatus, &status);

	printf("Fault_handler: Found vector type %d with value %d\n", device_id,
			status);
	//Switch case on fault device ID.
	//Terminate Process and children on CPU and privledge instruction
	switch (device_id) {
	case (CPU_ERROR ):
		CALL(terminate_Process(-2, &Index));
		break;
	case (PRIVILEGED_INSTRUCTION ):
		CALL(terminate_Process(-2, &Index));
		break;
	case INVALID_MEMORY :
//		printf("%d\n", status);
		if (status >= VIRTUAL_MEM_PGS || status < 0)
			terminate_Process(-2, &Index);
		/*handle page fault*/
		paging_handle(status);
		/*switch context*/
		break;
	}

// Clear out this device - we're done with it
	MEM_WRITE(Z502InterruptClear, &Index);
} /* End of fault_handler */

/************************************************************************
 SVC
 The beginning of the OS502.  Used to receive software interrupts.
 All system calls come to this point in the code and are to be
 handled by the student written code here.
 ************************************************************************/

void svc(void) {
	while (svc_or_interrupt) {
	}
	static INT16 do_print = 10;
//	lock_run_process();
	INT16 call_type;
	INT32 Time;
	void* starting_address;
	INT32 processId, priority;
	char* processname;
	INT32 * error;
	INT32 *proID;
	INT32 starting_address_shared;
	INT32 num_Shared_pages;
	INT32 *num_sharers;
	char * tag;
	call_type = (INT16) SYS_CALL_CALL_TYPE;
	PCB pcb;
	if (do_print > 0) {
		printf("SVC handler: %s %8ld %8ld %8ld %8ld %8ld %8ld\n",
				call_names[call_type], Z502_ARG1.VAL, Z502_ARG2.VAL,
				Z502_ARG3.VAL, Z502_ARG4.VAL, Z502_ARG5.VAL, Z502_ARG6.VAL);
		do_print--;
	}

	switch (call_type) {
	case SYSNUM_GET_TIME_OF_DAY:
		CALL(get_currentTime(&Time));
		*(INT32 *) Z502_ARG1.PTR = Time;
		break;
	case SYSNUM_TERMINATE_PROCESS:
		SP_setup_action(SP_ACTION_MODE, "TERMINATE");
		processId = Z502_ARG1.VAL;
		error = (INT32 *) Z502_ARG2.PTR;
		CALL(terminate_Process(processId, error));
		break;
	case SYSNUM_CREATE_PROCESS:
		SP_setup_action(SP_ACTION_MODE, "CREATE");
		processname = (char*) Z502_ARG1.PTR;
		starting_address = Z502_ARG2.PTR;
		priority = Z502_ARG3.VAL;
		proID = (INT32 *) Z502_ARG4.PTR;	//return process id
		error = (INT32 *) Z502_ARG5.PTR;	//return error
		CALL(
				create_Process(processname, starting_address, priority, proID, error));
		break;
	case SYSNUM_GET_PROCESS_ID:
		SP_setup_action(SP_ACTION_MODE, "GET_ID");
		processname = (char *) Z502_ARG1.PTR;
		proID = (INT32 *) Z502_ARG2.PTR;	//return process id
		error = (INT32 *) Z502_ARG3.PTR;	//return error
		CALL(get_Process_Id(processname, proID, error));
		break;
	case SYSNUM_SLEEP:
		SP_setup_action(SP_ACTION_MODE, "SLEEP");
		CALL(WAIT_FOR_INTERRUPT(TIMER_INTERRUPT));
		break;
	case SYSNUM_SUSPEND_PROCESS:
		SP_setup_action(SP_ACTION_MODE, "SUSP");
		CALL(
				suspend_Process((INT32)Z502_ARG1.VAL,(INT32 *)Z502_ARG2.PTR,true,false));
		break;
	case SYSNUM_RESUME_PROCESS:
		SP_setup_action(SP_ACTION_MODE, "RESUME");

		CALL(
				resume_Process((INT32)Z502_ARG1.VAL, (INT32 *)Z502_ARG2.PTR,true));
		break;
	case SYSNUM_CHANGE_PRIORITY:
		SP_setup_action(SP_ACTION_MODE, "CH_PRIO");

		CALL(
				change_priority((INT32)Z502_ARG1.VAL,(INT32)Z502_ARG2.VAL, (INT32*)Z502_ARG3.PTR));
		break;
	case SYSNUM_SEND_MESSAGE:
		SP_setup_action(SP_ACTION_MODE, "SEND");
		CALL(
				send_Message((INT32)Z502_ARG1.VAL,(char *)Z502_ARG2.PTR, (INT32)Z502_ARG3.VAL,(INT32 *)Z502_ARG4.PTR));
		break;
		//Receive Message
	case SYSNUM_RECEIVE_MESSAGE:
		SP_setup_action(SP_ACTION_MODE, "RECIEV");

		CALL(
				receive_Message((INT32)Z502_ARG1.VAL,(char *)Z502_ARG2.PTR, (INT32)Z502_ARG3.VAL,(INT32 *)Z502_ARG4.PTR,(INT32 *)Z502_ARG5.PTR, (INT32 *)Z502_ARG6.PTR));
		break;
		/*-----------------------------------phase2-----------------------------*/
		//Disk Read
	case SYSNUM_DISK_READ:

		CALL(
				read_from_Disk( (INT32)Z502_ARG1.VAL, Z502_ARG2.VAL, (char *)Z502_ARG3.PTR,false));
		break;
		//Disk Write
	case SYSNUM_DISK_WRITE:
		CALL(
				write_to_Disk( (INT32)Z502_ARG1.VAL, Z502_ARG2.VAL, (char *)Z502_ARG3.PTR,false));
		break;

	case SYSNUM_DEFINE_SHARED_AREA:
		SP_setup_action(SP_ACTION_MODE, "SH_AREA");
		starting_address_shared = Z502_ARG1.VAL;
		num_Shared_pages = Z502_ARG2.VAL;
		tag = (char *) Z502_ARG3.PTR;
		num_sharers = (INT32 *) Z502_ARG4.PTR;
		error = (INT32 *) Z502_ARG5.PTR;
		CALL(
				define_shared_area(starting_address_shared,num_Shared_pages,tag,num_sharers,error));

		break;
		/*----------------------------------phase2---------------------------------*/
	default:
		printf("ERROR cause system call \n");
		break;
	}
	if (DEBUG) {
		map<int, int>::const_iterator it;
		int i = 0;
		for (it = readyQueue_proIdToExistance.begin();
				i < SP_MAX_NUMBER_OF_PIDS
						&& it != readyQueue_proIdToExistance.end(); ++it) {
			if ((*it).second == 1) {
				SP_setup(SP_READY_MODE, (proIdToPCB[(*it).first])->ID);
				i++;
			}
		}
		for (it = timerQueue_proIdToExistance.begin();
				i < SP_MAX_NUMBER_OF_PIDS
						&& it != timerQueue_proIdToExistance.end(); ++it) {
			if ((*it).second == 1) {
				SP_setup(SP_WAITING_MODE, (proIdToPCB[(*it).first])->ID);
				i++;
			}
		}
		for (it = suspendQueue_proIdToExistance.begin();
				i < SP_MAX_NUMBER_OF_PIDS
						&& it != suspendQueue_proIdToExistance.end(); ++it) {
			if ((*it).second == 1) {
				SP_setup(SP_SUSPENDED_MODE, (proIdToPCB[(*it).first])->ID);
				i++;
			}
		}
		int time = 0;
		get_currentTime(&time);
		SP_setup(SP_TIME_MODE, time);
		SP_setup(SP_TARGET_MODE, current_PCB->ID);
		SP_print_line();
	}
} // End of svc

/************************************************************************
 OS_SWITCH_CONTEXT_COMPLETE
 The hardware, after completing a process switch, calls this routine
 to see if the OS wants to do anything before starting the user
 process.
 ************************************************************************/
void os_switch_context_complete(void) {
	static INT16 do_print = FALSE;
	INT16 call_type;
	call_type = (INT16) SYS_CALL_CALL_TYPE;
	if (do_print == TRUE) {
		printf("os_switch_context_complete  called before user code.\n");
		do_print = FALSE;
	}

	if (call_type == SYSNUM_RECEIVE_MESSAGE) {
		CALL(
				get_msg_Inbox((char *)Z502_ARG2.PTR,(INT32 *)Z502_ARG4.PTR, (INT32 *)Z502_ARG5.PTR));
	}

} /* End of os_switch_context_complete */

/************************************************************************
 OS_INIT
 This is the first routine called after the simulation begins.  This
 is equivalent to boot code.  All the initial OS components can be
 defined and initialized here.
 ************************************************************************/

void os_init(void) {
	FILE * pFile;
	pFile = fopen("log.txt", "w+");
	SP_setup_file(SP_FILE_MODE, pFile);
	SP_print_header();
	void *next_context;
	INT32 i;

	/* Demonstrates how calling arguments are passed thru to here       */

	printf("Program called with %d arguments:", CALLING_ARGC);
	for (i = 0; i < CALLING_ARGC; i++)
		printf(" %s", CALLING_ARGV[i]);
	printf("\n");
	printf("Calling with argument 'sample' executes the sample program.\n");

	/*          Setup so handlers will come to code in base.c           */

	TO_VECTOR[TO_VECTOR_INT_HANDLER_ADDR ] = (void *) interrupt_handler;
	TO_VECTOR[TO_VECTOR_FAULT_HANDLER_ADDR ] = (void *) fault_handler;
	TO_VECTOR[TO_VECTOR_TRAP_HANDLER_ADDR ] = (void *) svc;

	// inisilaizing the frame table
	for (int i = 0; i < MAX_NUM_PHY_PAGES; ++i) {
		FRAMETABLE *ini = (FRAMETABLE *) malloc(sizeof(FRAMETABLE));
		ini->frame_number = i;
		phys_frames[i] = ini;
	}

	/*  Determine if the switch was set, and if so go to demo routine.  */
//	if ((CALLING_ARGC > 1) && (strcmp(CALLING_ARGV[1], "sample") == 0)) {
//		ZCALL(
//				Z502_MAKE_CONTEXT( &next_context, (void *)sample_code, KERNEL_MODE ));
//		ZCALL( Z502_SWITCH_CONTEXT( SWITCH_CONTEXT_KILL_MODE, &next_context ));
//	} /* This routine should never return!!           */
	/*  This should be done by a "os_make_process" routine, so that
	 test0 runs on a process recognized by the operating system.    */
//-----------------------------------------------------------------
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test0, USER_MODE ));
//	current_PCB = create_tests("test0", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1a, USER_MODE ));
//	create_tests("test1a", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1a, USER_MODE ));
//	create_tests("test1a", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1b, USER_MODE ));
//	create_tests("test1b", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1c, USER_MODE ));
//	create_tests("test1c", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1d, USER_MODE ));
//	create_tests("test1d", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1e, USER_MODE ));
//	create_tests("test1e", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1f, USER_MODE ));
//	create_tests("test1f", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1g, USER_MODE ));
//	create_tests("test1g", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1h, USER_MODE ));
//	create_tests("test1h", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1i, USER_MODE ));
//	create_tests("test1i", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1j, USER_MODE ));
//	create_tests("test1j", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1k, USER_MODE ));
//	create_tests("test1k", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1l, USER_MODE ));
//	create_tests("test1l", 1, true, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1m, USER_MODE ));
//	create_tests("test1m", 1, true, next_context);
	//-------------------------------------------------------------------
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test0, USER_MODE ));
//	current_PCB = create_tests("test0", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1a, USER_MODE ));
//	current_PCB = create_tests("test1a", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1b, USER_MODE ));
//	current_PCB = create_tests("test1b", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1c, USER_MODE ));
//	current_PCB = create_tests("test1c", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1d, USER_MODE ));
//	current_PCB = create_tests("test1d", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1e, USER_MODE ));
//	current_PCB = create_tests("test1e", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1f, USER_MODE ));
//	current_PCB = create_tests("test1f", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1g, USER_MODE ));
//	current_PCB = create_tests("test1g", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1h, USER_MODE ));
//	current_PCB = create_tests("test1h", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1i, USER_MODE ));
//	current_PCB = create_tests("test1i", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1j, USER_MODE ));
//	current_PCB = create_tests("test1j", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1k, USER_MODE ));
//	current_PCB = create_tests("test1k", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1l, USER_MODE ));
//	current_PCB = create_tests("test1l", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test1m, USER_MODE ));
//	current_PCB = create_tests("test1m", 1, false, next_context);
//	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test2b, USER_MODE ));
//	current_PCB = create_tests("test2b", 1, false, next_context);
	ZCALL( Z502_MAKE_CONTEXT( &next_context, (void *)test2g, USER_MODE ));
	current_PCB = create_tests("test2g", 1, false, next_context);
	ZCALL(
			Z502_SWITCH_CONTEXT( SWITCH_CONTEXT_SAVE_MODE, &current_PCB->CONTEXT ));

}
/* End of os_init       */
