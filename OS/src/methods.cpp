/*
 * methods.cpp
 *
 *  Created on: Dec 1, 2012
 *      Author: amira
 */

#include <string.h>
#include <string>
#include "global.h"
#include "syscalls.h"
#include "z502.h"
#include "vector"
#include "protos.h"
#include "algorithm"
#include "queue"
#include "defs.h"
using namespace std;
/*maps between processes attributes*/
map<string, int> proNametoProId;
map<int, PCB*> proIdToPCB;
map<void*, PCB*> ContextToPCB;
/*---------------------------------*/
PCB *current_PCB;
/*ready and timer queue*/
priority_queue<PCB*, vector<PCB*>, ComparePriority> ready_queue;
priority_queue<PCB*, vector<PCB*>, CompareWakeUpTime> timer_queue;
map<int, int> readyQueue_proIdToExistance;
map<int, int> timerQueue_proIdToExistance;
/*----------------------------------------------------------------*/
/*suspended processes */
map<int, int> suspendQueue_proIdToExistance;
list<PCB*> suspend_queue;
int num_pids, processID;
/*------------------------------------------*/
/*messages*/
map<INT32, MSG*> receive_suspended;
vector<MSG*> suspended_processes;
vector<MSG*> mailbox;
/*---------------------------------*/
bool svc_or_interrupt = false;
INT32 left_time = -1;

/*-----------------------------------------------------------process handling------------------------------------------------------------*/
/*
 *Terminate process
 *terminate the calling process if the given process id -1
 *terminate the calling process and its children processes if the given process id -2
 *terminate the process with given id
 * */
void terminate_Process(INT32 process_id, INT32 *error) {

	//terminate itself Only
	if (process_id == current_PCB->ID)
		process_id = -1;
	if (process_id == -1) { // terminate self
		SP_setup_action(SP_ACTION_MODE, "TERMINATE");
		SP_setup(SP_TERMINATED_MODE, current_PCB->ID);
		clean(current_PCB->ID);
		CALL(switch_context(SWITCH_CONTEXT_KILL_MODE,false));

		error = ERR_SUCCESS;

	} else if (process_id == -2) { //terminate itself and its children
		SP_setup_action(SP_ACTION_MODE, "TERMINATE");
		SP_setup(SP_TERMINATED_MODE, current_PCB->ID);

		//terminate the children
		vector<INT32> temp = current_PCB->listOfChildern;
		for (int i = 0; i < temp.size(); ++i) {
			int t = temp.at(i);
			if (proIdToPCB[t] != 0) {
				PCB * turn = proIdToPCB[t];
				SP_setup_action(SP_ACTION_MODE, "TERMINATE");
				SP_setup(SP_TERMINATED_MODE, turn->ID);
				CALL(clean(turn->ID));
				ZCALL(Z502_DESTROY_CONTEXT( &turn->CONTEXT ));

			}
		}
		//terminate the parent

		CALL(clean(current_PCB->ID));
		CALL(switch_context(SWITCH_CONTEXT_KILL_MODE,false));
		*error = ERR_SUCCESS;
	} else {
		//terminate certain id
		if (readyQueue_proIdToExistance[process_id] != 0
				|| suspendQueue_proIdToExistance[process_id] != 0
				|| timerQueue_proIdToExistance[process_id] != 0) {
			PCB * turn = proIdToPCB[process_id];
			INT32 parent = turn->parent;
			if (parent != -1) {
				PCB *turn1 = proIdToPCB[parent];
				turn1->numberOfChildern--;
			}
			SP_setup_action(SP_ACTION_MODE, "TERMINATE");
			SP_setup(SP_TERMINATED_MODE, turn->ID);

			CALL( clean(process_id));
			ZCALL(Z502_DESTROY_CONTEXT( &turn->CONTEXT ));
			*error = ERR_SUCCESS;
		} else {
			//the entered id isn't valid
			*error = ERR_BAD_PARAM;
		}
	}
}

/*
 * Create process
 * create new process with the given information
 * process name
 * starting address
 * initial priority
 * if this name already exits or the parent has the valid number of the children error is returned
 * */
void create_Process(char* process_name, void *starting_address,
		INT32 initial_priority, INT32 *process_id, INT32 *error) {
	if (proNametoProId[process_name] == 0 && initial_priority > 0
			&& current_PCB->numberOfChildern < MAX_NUM_PROCESSES
			&& num_pids < 100) {
		num_pids++;
		processID = (processID + 1) % 100; // calculate the new process id assume that the max number that handled by this machine at time 100
		PCB *newpcb = (PCB *) (malloc(sizeof(PCB)));
		newpcb->ID = processID;
		*process_id = newpcb->ID;
		//print
		SP_setup(SP_NEW_MODE, (int) *process_id);
		//set the PCB fields
		newpcb->BASE_PRIORITY = initial_priority;
		newpcb->time = 0;
		newpcb->PROCESS_NAME = process_name;
		newpcb->parent = current_PCB->ID;
		newpcb->STATE = READY;
		newpcb->num_pages_assign = PROCESS_PAGE;
		newpcb->areas.next = NULL;
		newpcb->counter = 0;
		for (int i = 0; i < VIRTUAL_MEM_PGS; ++i) {
			newpcb->page_pointer[i] = -1;
		}
		void* context;
		ZCALL( Z502_MAKE_CONTEXT(&context, starting_address, USER_MODE));
		newpcb->CONTEXT = context;
		//Initialize the maps
		proNametoProId[newpcb->PROCESS_NAME] = newpcb->ID;
		proIdToPCB[newpcb->ID] = newpcb;
		ContextToPCB[context] = newpcb;
		CALL(add_to_readyQueue(newpcb));
		readyQueue_proIdToExistance[newpcb->ID] = 1;
		current_PCB->listOfChildern.push_back(newpcb->ID);
		*error = ERR_SUCCESS;
		current_PCB->numberOfChildern++;
	} else {
		//error
		*error = ERR_BAD_PARAM;
	}
}

/*
 * Get process id
 * this method return the process id of the given process name
 * if this name isn't found in the existing processes error
 * */
void get_Process_Id(char* process_name, INT32 *process_id, INT32 *error) {
	// need the id of the running process
	if (strcmp(process_name, "") == 0) {
		*process_id = current_PCB->ID;
		*error = 0;
	} else {
		if (proNametoProId[process_name] != 0) {
			*process_id = proNametoProId[process_name];
			*error = ERR_SUCCESS;
		} else {
			*error = ERR_BAD_PARAM;
		}
	}

}

/*
 * SUSPEND PROCESS
 *
 *	This method takes the process ID to be suspended from
 *	the OS.Z502_SWITCH_CONTEXT
 *	If we are suspending the current process, then
 * 	switch context or IDLE is called depending on any PCB's
 *	being ready to run. This routine passes success or fail
 *	to the OS, but returns no other values when called.
 *
 */
void suspend_Process(INT32 process_ID, INT32 *error, bool iAmSuspend,
		bool just_P_handle) {
	//check if this pid exist or not
	bool check = true;
	if (process_ID != -1)
		check = check_PID_existance(process_ID);
	//return error
	if (!check) {
		(*error) = ERR_BAD_PARAM;
		return;
	}
	//if pid -1, suspend self
	if (process_ID == -1) {
		//if current already terminated
		if (current_PCB != NULL)
			(*error) = ERR_SUCCESS;
		else
			(*error) = ERR_BAD_PARAM;
		//add to suspend queue
		if (!iAmSuspend) {
			current_PCB->STATE = Waiting_For_RECEIVE;
			readyQueue_proIdToExistance[current_PCB->ID] = -1;
		} else {
			if (current_PCB->STATE == Waiting_For_RECEIVE)
				current_PCB->STATE = SUSPEND_AND_RECIVEING;
			else if (current_PCB->STATE == READY) {
				readyQueue_proIdToExistance[current_PCB->ID] = -1;
				current_PCB->STATE = SUSPEND;
			}/*---------------------------phase 2 -------------------------------*/
			else if (current_PCB->STATE == BLOCKED_ONDISK) {
				readyQueue_proIdToExistance[current_PCB->ID] = -1;
				current_PCB->STATE = SUSPEND_AND_BLOCKED_ONDISK;
			}
			/*--------------------------------------------------------------------*/

			else {
				timerQueue_proIdToExistance[current_PCB->ID] = -1;
				current_PCB->STATE = SUSPEND_AND_BLOCKED;
			}
		}

		suspend_queue.push_back(current_PCB);
		suspendQueue_proIdToExistance[current_PCB->ID] = 1;

		//switch context to new ready process
		CALL(switch_context(SWITCH_CONTEXT_SAVE_MODE,just_P_handle));
	} else {

		//READY TO SUSPEND process
		//it is already in the ready queue
		bool result = 0;
		PCB* tempPCB = proIdToPCB[process_ID];
		CALL(remove_from_readyQueue(process_ID, &result));
		if (result) {
			(*error) = ERR_SUCCESS;
			tempPCB->STATE = SUSPEND;
			suspendQueue_proIdToExistance[process_ID] = 1;
		}		//if in the timer queue
		else if (timerQueue_proIdToExistance[process_ID] == 1) {
			CALL( remove_from_timerQueue(proIdToPCB[process_ID]));
			tempPCB->STATE = SUSPEND_AND_BLOCKED;
			suspendQueue_proIdToExistance[process_ID] = 1;
		} else if (suspendQueue_proIdToExistance[process_ID] == 1) {
			if (tempPCB->STATE == SUSPEND
					|| tempPCB->STATE == SUSPEND_AND_BLOCKED
					|| tempPCB->STATE == SUSPEND_AND_RECIVEING)
				(*error) = ERR_BAD_PARAM;
			else if (tempPCB->STATE == Waiting_For_RECEIVE) {
				tempPCB->STATE = SUSPEND_AND_RECIVEING;
				(*error) = ERR_SUCCESS;
			}
		}
		//return error if it isn't in ready queue and not in the timer queue
		else {
			(*error) = ERR_BAD_PARAM;

		}
	}
}
/*
 * RESUME PROCESS
 *	This method takes in the process ID to be resumed from suspend queue
 *	 and return error if this process isn't suspended.
 */
void resume_Process(INT32 process_ID, INT32 *error, bool iAmResume) {
	bool check = check_PID_existance(process_ID);
	//process don't exists
	if (check) {
		//print
		PCB* temp = proIdToPCB[process_ID];
		if (suspendQueue_proIdToExistance[process_ID] == 1) {
			if (!iAmResume) {
				if (temp->STATE == Waiting_For_RECEIVE) {
					//remove from	suspend queue
					suspend_queue.remove(temp);
					suspendQueue_proIdToExistance[process_ID] = -1;
					//add to ready queue
					add_to_readyQueue(temp);
					receive_suspended[process_ID] = NULL;
					temp->STATE = READY;
				} else if (temp->STATE == SUSPEND_AND_RECIVEING) {
					receive_suspended[process_ID] = NULL;
					temp->STATE = SUSPEND;
				}
			} else {
				if (temp->STATE == SUSPEND) {
					suspend_queue.remove(temp);
					suspendQueue_proIdToExistance[process_ID] = -1;
					//add to ready queue
					add_to_readyQueue(temp);
					temp->STATE = READY;
				} else if (temp->STATE == SUSPEND_AND_BLOCKED) {
					suspend_queue.remove(temp);
					suspendQueue_proIdToExistance[process_ID] = -1;
					//add to ready queue
					add_to_timerQueue(temp);
					temp->STATE = BLOCKED;
				} else if (temp->STATE == Waiting_For_RECEIVE) {
					(*error) = ERR_BAD_PARAM;
					return;
				} else if (temp->STATE == SUSPEND_AND_RECIVEING) {
					temp->STATE = Waiting_For_RECEIVE;

				}
			}
			(*error) = ERR_SUCCESS;
		} else {
			(*error) = ERR_BAD_PARAM;
		}
	} else {
		(*error) = ERR_BAD_PARAM;
	}
}

/* CHANGE PRIORITY
 *
 *	This method updates the priority of a process on the Ready Queue.
 *	returns error if this process doesn't exist
 *
 */
void change_priority(INT32 process_ID, INT32 new_priority, INT32 *error) {
	PCB *pcb;
	//error
	SP_setup(SP_PRIORITY_MODE, new_priority);
	if (new_priority > MAX_PRIO || new_priority < 0) {
		(*error) = ERR_BAD_PARAM;

	} else {
		//if pid -1, UPDATE SELF
		if (process_ID == -1) {
			//UPDATE SELF, or return error
			current_PCB->BASE_PRIORITY = new_priority;
		} else {		//else, update PID
			bool check = check_PID_existance(process_ID);
			if (!check) {
				(*error) = ERR_BAD_PARAM;

			} else if (timerQueue_proIdToExistance[process_ID] == 1) {
				pcb = proIdToPCB[process_ID];
				pcb->BASE_PRIORITY = new_priority;
				proIdToPCB[process_ID] = pcb;
			} //in suspend queue
			else {
				pcb = proIdToPCB[process_ID];
				pcb->BASE_PRIORITY = new_priority;
				proIdToPCB[process_ID] = pcb;
			}

		}
	}
}
/*-------------------------------------------------end of process handling------------------------------------------------------*/

/*dispatcher methods*/

/*choose ready process from readyQueue*/
PCB *getReady() {
	//check if ready queue isn't empty
	lock_readyQueue();
	if (!ready_queue.empty()) {
		PCB* temp = ready_queue.top();
		//check if this process exists or deleted from map
		int id = temp->ID;
		int check = readyQueue_proIdToExistance[id];
		/*check == 0 means already deleted from this queue*/
		while (check == -1 && !ready_queue.empty()) {
			ready_queue.pop();
			temp = ready_queue.top();
			id = temp->ID;
			check = readyQueue_proIdToExistance[id];
		}
		if (check != -1) {
			unlock_readyQueue();
			return temp;
		}
	}
	unlock_readyQueue();
	return NULL;
}

/*switch context ::
 * if ready queue is empty so check timer queue
 * for processes finished it's sleep time,
 * else pop from ready queue and switch context with the new process context
 *
 * */
void switch_context(bool save_or_kill, bool fault) {
	INT32 Temp = 0;
	INT32 c = 0;
	getReady(); // clean the ready queue
	if (ready_queue.empty()) {
		INT32 time;
		/*get current time*/
		get_currentTime(&time);
		/*remove finished processes */
		remove_processes_finised_sleep(time, true); //clean the timer queue
		/*end*/
	}
	/*if ready queue isn't empty*/
	if (!ready_queue.empty()) {
		if (fault)
			add_to_readyQueue(current_PCB);
		/*get ready process*/
		current_PCB = getReady();
		ready_queue.pop();
		ZCALL( Z502_SWITCH_CONTEXT( save_or_kill, &current_PCB->CONTEXT ));
	}
	/*if ready queue is empty and timer queue not*/
	else if (!timer_queue.empty()) {
		current_PCB = 0;
		ZCALL(Z502_IDLE());
		/*recursive call*/
		CALL(switch_context(SWITCH_CONTEXT_SAVE_MODE, false));
	}
	/*------------------------------------phase 2-----------------------------------------*/
	/*check suspend queue*/
	else if (!suspend_queue.empty() & !fault) {
		map<int, int>::const_iterator it;
		int i = 0;
		int count = 0;
		for (it = suspendQueue_proIdToExistance.begin();
				i < SP_MAX_NUMBER_OF_PIDS
						&& it != suspendQueue_proIdToExistance.end(); ++it) {
			if ((*it).second == 1) {
				count = 1;
				break;
			}
		}
		if (count == 0) {
			ZCALL(Z502_HALT());
		}
		for (int i = 0; i < MAX_DISKS; i++) {
			ZCALL( MEM_WRITE( Z502DiskSetID, &i));
			ZCALL( MEM_READ( Z502DiskStatus, &Temp ));
			if (Temp == DEVICE_FREE) {
				c = c + wakeup_finished_Disks(i);
			}
		}
		switch_context(SWITCH_CONTEXT_SAVE_MODE, false);
	}
	/*if ready and timer queue is empty so halt system*/
	else if (!fault) {

		ZCALL(Z502_HALT());
	}
}

/*------------------------------------------------------------end of dispatcher methods------------------------------------------------------*/

/*-------------------------------------------------------------helping methods---------------------------------------------------------------*/
/*
 * Get the Current time
 * give the current time of the machine
 * */
void get_currentTime(INT32* currentTime) {
	lock_z502_timer();
	ZCALL(MEM_READ( Z502ClockStatus, currentTime));
	unlock_z502_timer();
}

/*
 * Start Timer
 * start the timer by the given time
 * */
void Start_Timer(INT32 Time) {
	INT32 Status;
	lock_z502_timer();
	ZCALL( MEM_READ( Z502TimerStatus, &Status ));
	ZCALL( MEM_WRITE( Z502TimerStart, &Time ));
	unlock_z502_timer();
}

/*-----------------------------------QUEUE METHODS----------------------------------------*/

/*get first wake up time from the timer queue*/
void getFirstSleepTime(INT32 first_time) {
	//lock timer queue
	if (!timer_queue.empty() && timer_queue.top() != NULL) {
		PCB *temp = timer_queue.top();
		int id = temp->ID;
		int check = timerQueue_proIdToExistance[id];
		/*check == 0 means already deleted from this queue*/
		while (check == 0 && !timer_queue.empty()) {
			temp = timer_queue.top();
			timer_queue.pop();
			id = temp->ID;
			check = timerQueue_proIdToExistance[id];
		}

		if (check != 0) {
			first_time = temp->time;
		}
	}
	first_time = -1;

}
void add_to_readyQueue(PCB* pcb) {
	//lock ready queue
	lock_readyQueue();
	//put pcb in ready queue
	ready_queue.push(pcb);
	//put in map that this pcb exists
	int id = pcb->ID;
	readyQueue_proIdToExistance[id] = 1;
	//unlock ready queue
	unlock_readyQueue();
}
void add_to_timerQueue(PCB* pcb) {
	//add to timer queue
	timer_queue.push(pcb);
	//put in the map this pcb exists
	timerQueue_proIdToExistance[pcb->ID] = 1;
	//unlock timer queue

}
/*process want to sleep so remove from ready to timer queue*/
void ready_to_timerQueue(PCB* pcb) {
	//put in the map that this process moved from ready
	readyQueue_proIdToExistance[pcb->ID] = -1;
	getReady();
	//put it in the timer queue
	timer_queue.push(pcb);
	//put in the map this pcb exists
	timerQueue_proIdToExistance[pcb->ID] = 1;
	//unlock timer queue
}
/*
 * Clean
 *
 * this method clears all maps from the given id and called on remove
 * */
void clean(INT32 id) {
	num_pids--;
	PCB* temp = proIdToPCB[id];
	proNametoProId.erase(temp->PROCESS_NAME);
	proIdToPCB.erase(id);
	ContextToPCB.erase(temp->CONTEXT);
	readyQueue_proIdToExistance[id] = -1;
	timerQueue_proIdToExistance[id] = -1;
	suspendQueue_proIdToExistance[id] = -1;
}
/**
 * check if process exists in the System
 */
bool check_PID_existance(INT32 pid) {
	map<int, PCB*>::const_iterator it = proIdToPCB.find(pid);
	return it != proIdToPCB.end();
}

/**
 * remove from ready queue :
 * so mark in the readyQueue_proIdToExistance that this process deleted
 */
void remove_from_readyQueue(INT32 process_id, bool *re) {
	if (readyQueue_proIdToExistance[process_id] == 1) {
		readyQueue_proIdToExistance[process_id] = -1;
		*re = true;
	} else {
		re = false;
	}
}
/**
 * remove processes finished it's sleep time :
 * so check each process in the timer queue if it's wake up time <= current time
 * so add it to the ready queue .
 */
void remove_processes_finised_sleep(INT32 current_time, bool need) {
	priority_queue<PCB*> tempQueue;
	INT32 Status;
//clean timer queue by remove all the process in it and put the process of the finished time and remove what isn't still in the system
	while (!timer_queue.empty()) {
		PCB *temp = timer_queue.top();
		INT32 time = temp->time;

		timer_queue.pop();
		int id = temp->ID;
		if (time <= current_time && timerQueue_proIdToExistance[id] != -1) {
			temp->STATE = READY;
			CALL(add_to_readyQueue(temp));
			int id = temp->ID;
			timerQueue_proIdToExistance[id] = -1;
		} else {
			int id = temp->ID;
			if (timerQueue_proIdToExistance[id] != -1) {
				tempQueue.push(temp);
			}
		}
	}
	int size = tempQueue.size();
	int i = 0;
	while (i < size) {
		PCB* temp = tempQueue.top();
		timer_queue.push(temp);
		tempQueue.pop();
		i++;
	}
	//start the timer of the timer isn't working and the timer queue contain candidates
	if (size > 0 && need) {
		ZCALL( MEM_READ( Z502TimerStatus, &Status ));
		int sleep = timer_queue.top()->time - current_time;
		if (sleep > 0 && Status == DEVICE_FREE) {
			CALL(Start_Timer(sleep));
		}

	}
}
/*end of queue methods */

/*create process for test and put it in the ready queue */
PCB* create_tests(char* process_name, INT32 initial_priority, bool Notfirst,
		void * context) {
	num_pids++;
	processID = (processID + 1) % 100;
	PCB *newpcb = (PCB *) (malloc(sizeof(PCB)));
	newpcb->ID = processID;
	newpcb->BASE_PRIORITY = initial_priority;
	newpcb->time = 0;
	newpcb->PROCESS_NAME = process_name;
	newpcb->CONTEXT = context;
	newpcb->parent = -1;
	newpcb->num_pages_assign = PROCESS_PAGE;
	newpcb->areas.next = NULL;
	newpcb->counter = 0;
	for (int i = 0; i < VIRTUAL_MEM_PGS; ++i) {
		newpcb->page_pointer[i] = -1;
	}

	proNametoProId[newpcb->PROCESS_NAME] = newpcb->ID;
	proIdToPCB[newpcb->ID] = newpcb;
	ContextToPCB[context] = newpcb;
	if (Notfirst) {
		add_to_readyQueue(newpcb);
		readyQueue_proIdToExistance[newpcb->ID] = 1;
	}
	return newpcb;
}
void remove_from_timerQueue(PCB* pcb) {
	timerQueue_proIdToExistance[pcb->ID] = -1;
}
/*----------------------------------------------MESSAGES---------------------------------------------*/

/*send message :
 * if dest_ID = -1 then send to any process,if there is process suspended and want to receive from -1 then resume this process
 * else push it in the mailbox
 * if dest_ID is already blocked to receive message then resume this process and add msg to it's inbox
 *
 * */
void send_Message(INT32 dest_ID, char *message, INT32 msg_Len, INT32 *error) {
	bool check = true;
	if (dest_ID != -1)
		/*check if this dest id is in the system*/
		check = check_PID_existance(dest_ID);
	//return error
	if (!check) {
		(*error) = ERR_BAD_PARAM;
		return;
	}
	/*check if the msg length is > 200 so there is error*/
	if (msg_Len > 200) {
		(*error) = ERR_BAD_PARAM;
		return;
	}
	/*if process exceeded number of sent messages return error*/
	if (current_PCB->NUM_MSG == 10) {
		(*error) = ERR_BAD_PARAM;
		return;
	}
	/*increase number of messages*/
	proIdToPCB[current_PCB->ID]->NUM_MSG += 1;
	/*NO errors so construct the message*/
	MSG *msg = (MSG*) (malloc(sizeof(MSG)));
	msg->dest_ID = dest_ID;
	msg->src_ID = current_PCB->ID;
	msg->Length = msg_Len;
	memset(msg->message, 0, 200);
	strcpy(msg->message, message);
	//print
//	if (DEBUG) {
//		SP_setup(SP_TARGET_MODE, current_PCB->ID);
//		SP_setup(SP_SEND_MODE, current_PCB->ID);
////		SP_print_header();
//		SP_print_line();
//	}
	/*if destination is blocked until receive so resume this process*/
	if (receive_suspended[dest_ID] != NULL) {
		/*add it to message inbox  */
		proIdToPCB[dest_ID]->INBOX = msg;
		receive_suspended[dest_ID] = NULL;
		/*resume process*/
		resume_Process(dest_ID, error, false);
		return;
	} /*sent to all processes*/
	if (dest_ID == -1) {
		//check suspended processes
		int index = -1;
		int dest = 0;
		int dest1;
		/*if there is suspended processes and want to receive from -1*/
		if (!suspended_processes.empty()) {
			for (int i = 0; i < suspended_processes.size(); i++) {
				if (suspended_processes.at(i)->src_ID == -1) {
					dest1 = suspended_processes.at(i)->dest_ID;
					if (dest1
							!= current_PCB->ID&&receive_suspended[dest1]!=NULL) {index = i;
					dest = dest1;
					break;
				}

			}
		}
		/*if process found so resume this process*/
		if (index != -1) {
			/*remove from suspended processes*/
			suspended_processes.erase(suspended_processes.begin() + index);
			/*remove from map*/
			receive_suspended[dest] = NULL;
			/*put it in the destination INBOX*/
			proIdToPCB[dest]->INBOX = msg;
			/*resume process*/
			resume_Process(dest, error,false);
			return;
		}

	}
}
					/*else add it to the mail box*/
	mailbox.push_back(msg);
	/*if message sent to specified process*/
	if (dest_ID != -1) {
		/*make it's status RECEIVE*/
		proIdToPCB[dest_ID]->STATE_MSG = RECEIVE;
	}
	*error = ERR_SUCCESS;
	return;

}
/*suspend current process */
void suspend_process_msg(INT32 src_ID, char *message, INT32*error) {
	MSG *msg = (MSG*) (malloc(sizeof(MSG)));
	msg->src_ID = src_ID;
	msg->dest_ID = current_PCB->ID;
	memset(msg->message, 0, 200);
	strcpy(msg->message, message);
	/*add to suspended map*/
	receive_suspended[current_PCB->ID] = msg;
	/*push in the suspended list*/
	suspended_processes.push_back(msg);
	/*suspend this process*/
	suspend_Process(-1, error, false, false);

}
/*receive message ( current process want to receive message from src_ID ):
 *if this process state is RECEIVE so take message from mailbox
 *if src_ID = -1 so receive from any process :
 *     check if mailbox has MSG to this process or to any process
 *      if found so take this MSG from mailbox
 *      else suspend this process
 *if src_ID !=-1 so suspend this process
 * */
void receive_Message(INT32 src_ID, char *message, INT32 msg_rcvLen,
		INT32 *msg_sndLen, INT32 *sender_ID, INT32 *error) {
	MSG* Recv = NULL;
	bool check = true;
	if (src_ID != -1)
		/*check errors*/
		check = check_PID_existance(src_ID);
	//return error
	if (!check) {
		(*error) = ERR_BAD_PARAM;
		return;
	}
	/*check if the msg length is > 200 so there is error*/
	if (msg_rcvLen > 200) {
		(*error) = ERR_BAD_PARAM;
		return;
	}
	/*check if there is message sent to this process*/
	if (current_PCB->STATE_MSG == RECEIVE) {
		Recv = get_msg(current_PCB->ID);
		*error = ERR_SUCCESS;
	} else /*receive from any process*/
	if (src_ID == -1) {
		/*if mailbox isn't empty so take any */
		if (!mailbox.empty()) {
			/*receive message sent to this process or to broadcast*/
			Recv = get_msg1(current_PCB->ID);
			if (Recv != NULL) {
				current_PCB->INBOX = Recv;
				proIdToPCB[current_PCB->ID]->INBOX = Recv;
			}
			if (Recv == NULL) {
				CALL(suspend_process_msg(src_ID, message, error);)
			}
		} /*mailbox is empty so suspend this process until some process send the message to it*/
		else {
			/*suspend process*/
			CALL(suspend_process_msg(src_ID, message, error));
		}
	}
	/*Receive from specified ID but it's state isn't RECEIVE*/
	else {
		CALL(suspend_process_msg(src_ID, message, error));
	}
	/*put message in process INBOX*/
	if (Recv != NULL) {
		current_PCB->INBOX = Recv;
		proIdToPCB[current_PCB->ID]->INBOX = Recv;
		*msg_sndLen = Recv->Length;
		strcpy(message, Recv->message);
		*sender_ID = Recv->src_ID;
		if (*msg_sndLen > msg_rcvLen) {
			(*error) = ERR_BAD_PARAM;
			return;
		} else
			*error = ERR_SUCCESS;
	}

}

/*get message from this src_ID or -1*/
MSG* get_msg1(INT32 dest) {
	vector<MSG*> temp;
	MSG* temp_msg = NULL;
	int index = -1;
	for (int i = 0; i < mailbox.size(); i++) {
		MSG* msg = mailbox.at(i);
		if (msg->src_ID != dest
				&& (msg->dest_ID == dest || msg->dest_ID == -1)) {
			temp_msg = msg;
			index = i;
			break;
		}
	}
	if (index != -1)
		mailbox.erase(mailbox.begin() + index);
	return temp_msg;
}
/*get message from mailbox with src_ID*/
MSG* get_msg(INT32 src_ID) {
	vector<MSG*> temp;
	MSG* temp_msg = NULL;
	int index = -1;
	for (int i = 0; i < mailbox.size(); i++) {
		MSG* msg = mailbox.at(i);
		if (msg->dest_ID == src_ID) {
			temp_msg = msg;
			index = i;
		}

	}
	if (index != -1) {
		mailbox.erase(mailbox.begin() + index);
	}
	current_PCB->STATE_MSG = 0;
	proIdToPCB[current_PCB->ID]->STATE_MSG = 0;
	return temp_msg;
}
/*get MSG from INBOX if INBOX isn't empty*/
void get_msg_Inbox(char *message, INT32 *msg_sndLen, INT32 *sender_ID) {
	if (current_PCB->INBOX == NULL)
		return;
	MSG *ptrMsg = current_PCB->INBOX;
	*msg_sndLen = ptrMsg->Length;
	strcpy(message, ptrMsg->message);
	*sender_ID = ptrMsg->src_ID;
	current_PCB->INBOX = NULL;
	proIdToPCB[current_PCB->ID]->INBOX = NULL;

}
/*-------------------------------------------------------------------end of message methods-----------------------------------------*/
/*--------------------------------------------------------------------LOCK METHODS--------------------------------------------------*/
void lock_readyQueue() {
	INT32 LockResult = 0;
	while (LockResult == 0)
		ZCALL(
				Z502_READ_MODIFY(MEMORY_INTERLOCK_BASE, LOCK, SUSPEND, &LockResult));
}

void unlock_readyQueue() {
	INT32 LockResult;

	ZCALL(
			Z502_READ_MODIFY(MEMORY_INTERLOCK_BASE, UNLOCK, SUSPEND, &LockResult));

}
void lock_run_process() {
	INT32 LockResult = 0;
	while (LockResult == 0)
		ZCALL(
				Z502_READ_MODIFY(MEMORY_INTERLOCK_BASE + 1, LOCK, SUSPEND, &LockResult));
}
void unlock_run_process() {
	INT32 LockResult;
	ZCALL(
			Z502_READ_MODIFY(MEMORY_INTERLOCK_BASE + 1, UNLOCK, SUSPEND, &LockResult));

}
void lock_timerQueue() {
	INT32 LockResult = 0;
	while (LockResult == 0) {
		ZCALL(
				Z502_READ_MODIFY(MEMORY_INTERLOCK_BASE + 2, LOCK, SUSPEND, &LockResult));
		DoSleep(100);
	}
}
void unlock_timerQueue() {
	INT32 LockResult;
	ZCALL(
			Z502_READ_MODIFY(MEMORY_INTERLOCK_BASE + 2, UNLOCK, SUSPEND, &LockResult));

}
void lock_z502_timer() {
	INT32 LockResult = 0;
	while (LockResult == 0)
		ZCALL(
				Z502_READ_MODIFY(MEMORY_INTERLOCK_BASE + 3, LOCK, SUSPEND, &LockResult));

}
void unlock_z502_timer() {
	INT32 LockResult;

	ZCALL(
			Z502_READ_MODIFY(MEMORY_INTERLOCK_BASE + 3, UNLOCK, SUSPEND, &LockResult));
}
void lock_svc() {
	INT32 LockResult = 0;
	while (LockResult == 0) {
		ZCALL(
				Z502_READ_MODIFY(MEMORY_INTERLOCK_BASE + 4, LOCK, SUSPEND, &LockResult));
		DoSleep(100);
	}
}
void unlock_svc() {
	INT32 LockResult;

	ZCALL(
			Z502_READ_MODIFY(MEMORY_INTERLOCK_BASE + 4, UNLOCK, SUSPEND, &LockResult));

}
/*----------------------------------------------------------------end of LOCK methods-------------------------------------------------------*/
