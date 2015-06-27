/******************************************************************************
 *  Nano-RK, a real-time operating system for sensor networks.
 *  Copyright (C) 2007, Real-Time and Multimedia Lab, Carnegie Mellon University
 *  All rights reserved.
 *
 *  This is the Open Source Version of Nano-RK included as part of a Dual
 *  Licensing Model. If you are unsure which license to use please refer to:
 *  http://www.nanork.org/nano-RK/wiki/Licensing
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.0 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *******************************************************************************/

#include <nrk.h>
#include <include.h>
#include <ulib.h>
#include <stdio.h>
#include <stdlib.h>
#include <avr/sleep.h>
#include <hal.h>
#include <bmac.h>
#include <nrk_error.h>

#define NUM_SLAVES	2
#define MAC_ADDR	(NUM_SLAVES+1)	

nrk_task_type WHACKY_TASK;
NRK_STK whacky_task_stack[NRK_APP_STACKSIZE];
void whacky_task (void);

void nrk_create_taskset ();
uint8_t whacky_buf[RF_MAX_PAYLOAD_SIZE];

uint8_t rx_buf[RF_MAX_PAYLOAD_SIZE];
uint8_t tx_buf[RF_MAX_PAYLOAD_SIZE];

void nrk_register_drivers();

int main ()
{
	uint16_t div;
	nrk_setup_ports ();
	nrk_setup_uart (UART_BAUDRATE_115K2);

	nrk_init();

	nrk_led_clr(0);
	nrk_led_clr(1);
	nrk_led_clr(2);
	nrk_led_clr(3);

	nrk_time_set(0, 0);

	bmac_task_config();

	nrk_create_taskset();
	nrk_start();
	return 0;
}

void whacky_task ()
{
	uint8_t i, len;
	int8_t rssi, val;
	uint8_t *local_buf;
	uint8_t slave_id;
	uint8_t timeout;
	char option;
	int8_t round_num;
	int8_t score;
	uint8_t old_slave_id;
	nrk_sig_t uart_rx_signal;

	//Timer management
	nrk_time_t start_time, end_time;

	printf ("whacky_task PID=%d\r\n", nrk_get_pid ());

	// This shows you how to wait until a key is pressed to start


	// init bmac on channel 25 
	bmac_init (17);
	// This sets the next RX buffer.
	// This can be called at anytime before releasing the packet
	// if you wish to do a zero-copy buffer switch
	bmac_rx_pkt_set_buffer (rx_buf, RF_MAX_PAYLOAD_SIZE);

	while(1){

		option='z';
		nrk_kprintf( PSTR("Press 's' to start\r\n" ));
		// Get the signal for UART RX
		uart_rx_signal=nrk_uart_rx_signal_get();
		// Register task to wait on signal
		nrk_signal_register(uart_rx_signal); 
		/* Modify this logic to start your code, when you press 's'*/ 

		do{
			if(nrk_uart_data_ready(NRK_DEFAULT_UART))
				option=getchar();
			else nrk_event_wait(SIG(uart_rx_signal));
		} while(option!='s');



		for(slave_id=0;slave_id<NUM_SLAVES;slave_id++)
		{	//nrk_time_t time={1,0};
			//nrk_wait(time);
			sprintf (tx_buf, "POLL: %u t", slave_id);
			val=bmac_tx_pkt(tx_buf, strlen(tx_buf)+1);
			//nrk_wait(time);
		}
		round_num=0;
		//Sets the round num to 0 for keeping count of 10 rounds
		score=0;
		//Sets score to zero to start scoring
		// initialize the slave id
		slave_id = 0;
		old_slave_id=0;

		while (round_num < 10) 
		{
			round_num++;
			printf("Round: %d\r\n",(round_num));
			slave_id=rand()%NUM_SLAVES;
			slave_id=(slave_id==old_slave_id)?(slave_id+1):slave_id;
			slave_id=slave_id%NUM_SLAVES;
			old_slave_id=slave_id;	
			printf("slave_id: %u\r\n",slave_id);

			sprintf (tx_buf, "POLL: %u m", slave_id);
			nrk_led_set (BLUE_LED);
 			//nrk_time_t time={1,0};
                        //nrk_wait(time);
			val=bmac_tx_pkt(tx_buf, strlen(tx_buf)+1);
			if(val != NRK_OK)
			{
				nrk_kprintf(PSTR("Could not Transmit!\r\n"));
			}
                        //nrk_wait(time);
			// Task gets control again after TX complete
			nrk_kprintf (PSTR ("Sent Poll Request.\r\n"));
			nrk_led_clr (BLUE_LED);

			nrk_kprintf(PSTR("Waiting for Response\r\n"));
			// Get the RX packet 
			nrk_led_set (ORANGE_LED);

			// Wait until an RX packet is received
			timeout = 0;
			nrk_time_get(&start_time);
			while(1)
			{
				nrk_time_t time={1,0};
				nrk_wait(time);

				//nrk_spin_wait_us(1000000);

				nrk_kprintf(PSTR("Calling Ready\r\n"));
				if(bmac_rx_pkt_ready())
				{
					nrk_kprintf(PSTR("Called Ready\r\n"));
					local_buf = bmac_rx_pkt_get (&len, &rssi);
					printf ("Got RX packet len=%d local_buf=%p\r\n", len,local_buf);
					//for (i = 0; i < len; i++)
					//printf ("%c %d", local_buf[i]);
					// printf ("]\r\n");

					if(local_buf[2]=='s' && local_buf[0]==(slave_id+0x30)){
						nrk_kprintf(PSTR("Hit!!!\r\n"));	
						score+=(11-round_num);
						nrk_led_clr (ORANGE_LED);
						// Release the RX buffer so future packets can arrive 
						bmac_rx_pkt_release();
						break;
					}else{

						//recieve "a" from mole
 					//	nrk_time_t time={1,0};
                                          //      nrk_wait(time);
						sprintf (tx_buf, "POLL: %u s", slave_id);
						val=bmac_tx_pkt(tx_buf, strlen(tx_buf)+1);
                                            //    nrk_wait(time);
						bmac_rx_pkt_release();
					
					}
				}
				// Implement timeouts
				nrk_time_get(&end_time);
				nrk_time_t elapsed={0,0};
				nrk_time_sub(&elapsed,end_time,start_time);
				if((elapsed.secs*1000+elapsed.nano_secs/1000000)>800*round_num)
			
					{
						nrk_time_t time={1,0};
                               			nrk_wait(time);
						timeout = 1;
						printf("Sent Time out lower %u\r\n",slave_id);
						sprintf (tx_buf, "POLL: %u t", slave_id);
						val=bmac_tx_pkt(tx_buf, strlen(tx_buf)+1);
                                		nrk_wait(time);
						if(val==NRK_OK)
						nrk_kprintf(PSTR("OK"));
						else
						nrk_kprintf(PSTR("NOT OK"));
						// bmac_rx_pkt_release();
						break;
				}

			
			}
			if(timeout == 1)
			{
				nrk_kprintf(PSTR("Rx Timed Out!\r\n"));
			}
			printf("Score: %d\r\n",score);
		}
		nrk_kprintf(PSTR("GAME OVER\r\n"));
		printf("Final Score: %d\r\n",score);
	}
}
void nrk_create_taskset()
{
	WHACKY_TASK.task = whacky_task;
	nrk_task_set_stk( &WHACKY_TASK, whacky_task_stack, NRK_APP_STACKSIZE);
	WHACKY_TASK.prio = 2;
	WHACKY_TASK.FirstActivation = TRUE;
	WHACKY_TASK.Type = BASIC_TASK;
	WHACKY_TASK.SchType = PREEMPTIVE;
	WHACKY_TASK.period.secs = 1;
	WHACKY_TASK.period.nano_secs = 0;
	WHACKY_TASK.cpu_reserve.secs = 0;
	WHACKY_TASK.cpu_reserve.nano_secs = 0;
	WHACKY_TASK.offset.secs = 0;
	WHACKY_TASK.offset.nano_secs = 0;
	nrk_activate_task (&WHACKY_TASK);
	printf ("create done\r\n");
}
