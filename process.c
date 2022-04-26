#include "3140_concur.h"
#include "realtime.h"
#include "3140_concur.h"
#include <stdlib.h>
#include <MKL46Z4.h>
#include <assert.h>
#include <stdbool.h>

process_t *current_process = NULL;

volatile realtime_t current_time;

int process_deadline_met;
int process_deadline_miss;

bool first_select = true;

unsigned int *new_sp = NULL;

struct process_state {
	unsigned int * sp; // Current Stack pointer
	unsigned int * orig_sp; // Original Stack pointer
	int n; // Size allocated

	bool is_rt;
	realtime_t start;
	realtime_t deadline;
};

typedef struct node {
	process_t *val;
	struct node *prev;
	struct node *next;
} node;

typedef struct double_linked_list {
	struct node *list_start;
	struct node *list_end;
} double_linked_list;

struct double_linked_list scheduler = {
	.list_start = NULL,
	.list_end = NULL
};

struct double_linked_list rt_scheduler = {
	.list_start = NULL,
	.list_end = NULL
};

// adds element to the beginning of the list
void add_elem_begin(double_linked_list * list, node * elem) {
	if(list->list_start == NULL){
		assert(list->list_end == NULL);
		elem->next = NULL;
		elem->prev = NULL;
		list->list_start = elem;
	}else{
		assert(list->list_start->prev == NULL);
		elem->next = list->list_start;
		elem->prev = NULL;
		list->list_start->prev = elem;
		list->list_start = elem;
	}
}

void PIT1_Service(){
	current_time.msec ++;
	if(current_time.msec >= 1000){
		current_time.sec++;
		current_time.msec = 0;
	}
}

// returns:
// 1 iff a is greater/later than b
// -1 iff b is less than/before b
// 0 iff a==b
int compare_realtimes(realtime_t a, realtime_t b) {
	int a_ms = a.sec + 1000*a.msec;
	int b_ms = b.sec + 1000*b.msec;

	if(a_ms > b_ms) {
		return 1;
	} else if(a_ms < b_ms) {
		return -1;
	} else return 0;
}

// adds element to the end of the list
void add_elem_end(double_linked_list * list, node * elem) {
	if(list->list_start == NULL) {
		assert(list->list_end == NULL);
		list->list_start = elem;
		list->list_end = elem;

		elem->next = NULL;
		elem->prev = NULL;
	} else {
		assert(list->list_end->next == NULL);
		list->list_end->next = elem;
		elem->prev = list->list_end;

		list->list_end = elem;
		elem->next = NULL;
	}
}

// precondition: list is sorted by increasing deadline
// adds element to list, sorted by deadline
void add_elem_rt_sorted(double_linked_list * list, node * elem) {
	assert(elem->val->is_rt == true);
	if(list->list_start == NULL) {
		assert(list->list_end == NULL);
		list->list_start = elem;
		list->list_end = elem;

		elem->next = NULL;
		elem->prev = NULL;
	} else {
		/*
		assert(list->list_end->next == NULL);
		list->list_end->next = elem;
		elem->prev = list->list_end;

		list->list_end = elem;
		elem->next = NULL;
		*/

		node *cur_node;

		// special case
		if(compare_realtimes(list->list_start->val->deadline, elem->val->deadline) != -1) {
			add_elem_begin(list, elem);
		} else {
			cur_node = list->list_start;
			while(cur_node->next != NULL && compare_realtimes(cur_node->next->val->deadline, elem->val->deadline) == -1) {
				cur_node = cur_node->next;
			}

			// reached end of list
			if(cur_node->next == NULL) {
				add_elem_end(list, elem);
			} else { //cur_node is last node with earlier deadline than new node
				cur_node->next->prev = elem;
				elem->next = cur_node->next;
				elem->prev = cur_node;
				cur_node->next = elem;
			}
		}
	}
}


// removes and returns first element of list, null if list is empty
node * remove_first_elem(double_linked_list * list) {
	struct node *elem;
	if(list->list_start == NULL) {
		assert(list->list_end == NULL);
		elem = NULL;
	} else {
		assert(list->list_start->prev == NULL);
		elem = list->list_start;
		list->list_start = list->list_start->next;
		elem->next = NULL;
		assert(elem->prev == NULL);
		if(list->list_start != NULL) {
			list->list_start->prev = NULL;
		} else {
			list->list_end = NULL;
		}
	}

	return elem;
}

// removes and returns last element of list, null if list is empty
node * remove_last_elem(double_linked_list * list) {
	struct node *elem;
	if(list->list_start == NULL) {
		assert(list->list_end == NULL);
		elem = NULL;
	} else {
		assert(list->list_end->next == NULL);
		elem = list->list_end;
		list->list_end = list->list_end->prev;
		elem->prev = NULL;
		assert(elem->next == NULL);
		if(list->list_end != NULL) {
			list->list_end->next = NULL;
		} else {
			list->list_start = NULL;
		}
	}

	return elem;
}

int process_create (void(*f) (void), int n){
	// Make an element for the queue containing info about the process
	node *new_elem_ptr = malloc(sizeof(node));
	new_elem_ptr->val = malloc(sizeof(process_t));

	if(new_elem_ptr == NULL){
		return -1;
	}

	new_elem_ptr->val->sp = process_stack_init(f, n);
	new_elem_ptr->val->orig_sp = new_elem_ptr->val->sp;
	new_elem_ptr->val->n = n;
	new_elem_ptr->val->is_rt = false;
	new_elem_ptr->prev = NULL;
	new_elem_ptr->next = NULL;

	if(new_elem_ptr->val->sp == NULL) return -1; // If the new stack cannot be allocated, return and error

	add_elem_end(&scheduler, new_elem_ptr); // Add the new element to the queue

	return 0;
}

realtime_t add_realtime(realtime_t a, realtime_t b) {
	realtime_t sum;
	if(a.msec + b.msec >= 1000) {
		sum.sec = a.sec + b.sec + 1;
		sum.msec = a.msec + b.msec - 1000;
	} else {
		sum.sec = a.sec + b.sec;
		sum.msec = a.msec + b.msec;
	}
	return sum;
}

int process_rt_create (void(*f) (void), int n, realtime_t *start, realtime_t *deadline) {

	// Make an element for the queue containing info about the process
	node *new_elem_ptr = malloc(sizeof(node));
	new_elem_ptr->val = malloc(sizeof(process_t));

	if(new_elem_ptr == NULL || new_elem_ptr->val == NULL){
		return -1;
	}
	new_elem_ptr->val->sp = process_stack_init(f, n);
	new_elem_ptr->val->orig_sp = new_elem_ptr->val->sp;
	new_elem_ptr->val->n = n;
	new_elem_ptr->val->is_rt = true;
	new_elem_ptr->val->start = add_realtime(current_time, *start);
	new_elem_ptr->val->deadline = add_realtime(new_elem_ptr->val->start, *deadline);
	new_elem_ptr->prev = NULL;
	new_elem_ptr->next = NULL;

	if(new_elem_ptr->val->sp == NULL) return -1; // If the new stack cannot be allocated, return and error

	add_elem_rt_sorted(&rt_scheduler, new_elem_ptr); // Add the new element to the queue

	return 0;
}

void process_start (void){
	current_time.sec = 0;
	current_time.msec = 0; //Set clock time to 0;

	NVIC_EnableIRQ(PIT_IRQn); // Enable PIT Interupts

	SIM->SCGC6 = SIM_SCGC6_PIT_MASK; // Enable clock to PIT
	PIT->MCR = 0x00; // Enable PIT timers

	PIT->CHANNEL[0].LDVAL = 0x00004E20; // 20k cycles @ 10Mhz = 2 ms before switching processes
	PIT->CHANNEL[0].TCTRL |= (1 << 1); // Enable interrupts for channel 0

	PIT->CHANNEL[1].LDVAL = 0x000028F6; // 10,486 cycles @ 10.4Mhz = 1 ms per interupt
	PIT->CHANNEL[1].TCTRL |= (1 << 1); // Enable interrupts for channel 1

	PIT->CHANNEL[0].TCTRL |= 0x1; //Enable channel 0
	PIT->CHANNEL[1].TCTRL |= 0x1; //Enable channel 1

	scheduler.list_start->next = NULL;
	scheduler.list_start->prev = NULL;
	scheduler.list_end->next = NULL;
	scheduler.list_end->prev = NULL;

	process_begin(); // Initialize the first process
}

node * rt_process_select(double_linked_list * list, realtime_t cur_time) {
	node *cur_node;
	if(list->list_start == NULL) {
		assert(list->list_end == NULL);
		return NULL;
	} else {
		cur_node = list->list_start;
		while(cur_node->next != NULL && compare_realtimes(cur_node->val->start, cur_time) == 1) {
			cur_node = cur_node->next;
		}

		// reached end of list, i.e. no node in list has start time before or equal to cur_time
		if(cur_node->next == NULL) {
			return NULL;
		} else { // cur_node is the realtime process with earliest deadline that also has start time before or equal to current time
			// remove cur_node from the list
			if(cur_node == list->list_start) {
				return remove_first_elem(list);
			} else if(cur_node == list->list_end) {
				return remove_last_elem(list);
			} else {
				// remove node from list, then return it
				cur_node->prev->next = cur_node->next;
				cur_node->next->prev = cur_node->prev;

				cur_node->prev = NULL;
				cur_node->next = NULL;
				return cur_node;
			}
		}
	}
}

unsigned int * process_select(unsigned int * cursp){
	node *fst = remove_first_elem(&scheduler); //Check the first element
	if(cursp == NULL){ // Either the running process finished or it is the first time process_select is being called
		if(!first_select){ //If its not the first time, free memory from the running process
			if(current_process->is_rt){
				add_elem_begin(&scheduler, fst);
			}
			process_stack_free(current_process->orig_sp, current_process->n);
			free(current_process);
			current_process = NULL;
		}else{ // If it is the first time, put the element back in the front of the queue
			add_elem_begin(&scheduler, fst);
			first_select = false;
		}
	}else{
		current_process->sp = cursp;
		//Put running process back into queue
		if(current_process->is_rt){
			add_elem_begin(&scheduler, fst);
		}else{
			add_elem_end(&scheduler, fst);
		}
	}

	// Both queues are empty, we are done
	if(scheduler.list_start == NULL && rt_scheduler.list_start == NULL){
		return NULL;
	}

	node *nxt = rt_process_select(&rt_scheduler, current_time);
	//There is a RT process ready
	if(nxt != NULL){
		current_process = nxt->val;
		free(nxt);
	}else if(scheduler.list_start != NULL){ // There are no RT ready but there are non-RT
		current_process = scheduler.list_start->val;
	}else{ // There are no RT ready and there are also no non-RT
		//Wait for next RT process to be ready
		while(nxt == NULL){
			// poll pit timer to update the timer
			if(PIT->CHANNEL[1].TFLG == 1){
				PIT1_Service();
				PIT->CHANNEL[1].TFLG = 1;
			}
			nxt = rt_process_select(&rt_scheduler, current_time);
		}
		current_process = nxt->val;
		free(nxt);
	}

	//Return the stack pointer of the new process
	return current_process->sp;
}
