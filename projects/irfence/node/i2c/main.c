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
#include <hal.h>
#include <nrk_error.h>
#include <nrk_timer.h>
#include <nrk_stack_check.h>
#include <nrk_stats.h>
#include <TWI_Master.h>



#define TWI_GEN_CALL         0x00  // The General Call address is 0

// Sample TWI transmission commands
//#define TWI_CMD_MASTER_WRITE 0x10
//#define TWI_CMD_MASTER_READ  0x20

// Sample TWI transmission states, used in the main application.
#define SEND_DATA             0x01
#define REQUEST_DATA          0x02
#define READ_DATA_FROM_BUFFER 0x03

unsigned char TWI_Act_On_Failure_In_Last_Transmission ( unsigned char TWIerrorMsg )
{
                    // A failure has occurred, use TWIerrorMsg to determine the nature of the failure
                    // and take appropriate actions.
                    // Se header file for a list of possible failures messages.
                    
                    // Here is a simple sample, where if received a NACK on the slave address,
                    // then a retransmission will be initiated.
 
  if ( (TWIerrorMsg == TWI_MTX_ADR_NACK) | (TWIerrorMsg == TWI_MRX_ADR_NACK) )
    TWI_Start_Transceiver();
    
  return TWIerrorMsg; 
}




NRK_STK Stack1[NRK_APP_STACKSIZE];
nrk_task_type TaskOne;
void Task1(void);
NRK_STK Stack2[NRK_APP_STACKSIZE];
nrk_task_type TaskTwo;
void Task2 (void);
void nrk_create_taskset();



int main ()
{
  nrk_setup_ports();
  nrk_setup_uart(UART_BAUDRATE_115K2);

  nrk_init();

  nrk_led_clr(ORANGE_LED);
  nrk_led_clr(BLUE_LED);
  nrk_led_clr(GREEN_LED);
  nrk_led_clr(RED_LED);
 
  nrk_time_set(0,0);
  nrk_create_taskset ();
  nrk_start();
  
  return 0;
}

uint8_t messageBuf[16];

void Task1()
{
nrk_time_t t={1,0};
uint16_t cnt;
uint8_t i;
cnt=0;
  unsigned char TWI_targetSlaveAddress, temp, TWI_operation=0,
                pressedButton, myCounter=0;


nrk_kprintf( PSTR("Nano-RK Version ") );
printf( "%d\r\n",NRK_VERSION );

nrk_kprintf( PSTR("TWI_Master_Init()\r\n") );
  TWI_Master_Initialise();
  //__enable_interrupt();
  sei();

  TWI_targetSlaveAddress   = 0x1E; //Addres of Compass.

  // This example code runs forever; sends a byte to the slave, then requests a byte
  // from the slave and stores it on PORTB, and starts over again. Since it is interupt
  // driven one can do other operations while waiting for the transceiver to complete.
  
  // Send initial data to slave
 

//Control Register A
  messageBuf[0] = (TWI_targetSlaveAddress<<TWI_ADR_BITS) | (FALSE<<TWI_READ_BIT);
  messageBuf[1] = 0x00;
  messageBuf[2] = 0x10;//1 Sample per reading and 15Hz frequency
  nrk_kprintf( PSTR("TWI_Start_Transceiver_With_data()\r\n") );
  TWI_Start_Transceiver_With_Data( messageBuf, 3 );

//Control Register B
  messageBuf[0] = (TWI_targetSlaveAddress<<TWI_ADR_BITS) | (FALSE<<TWI_READ_BIT);
  messageBuf[1] = 0x01;
  messageBuf[2] = 0xE0;//Highest Gain Set
  nrk_kprintf( PSTR("TWI_Start_Transceiver_With_data()\r\n") );
  TWI_Start_Transceiver_With_Data( messageBuf, 3 );


  TWI_operation = SEND_DATA; // Set the next operation

    
  for(;;){
    // Check if the TWI Transceiver has completed an operation.
    if ( ! TWI_Transceiver_Busy() )                              
    {
    // Check if the last operation was successful
      if ( TWI_statusReg.lastTransOK )
      {
			// nrk_kprintf( PSTR("tx ok\r\n") );
      // Determine what action to take now
     
   if (TWI_operation == SEND_DATA)
        { 
// Send data to slave
//Mode Register
  messageBuf[0] = (TWI_targetSlaveAddress<<TWI_ADR_BITS) | (FALSE<<TWI_READ_BIT);
  messageBuf[1] = 0x02;
  messageBuf[2] = 0x01;//Single measurement mode.
  nrk_kprintf( PSTR("TWI_Start_Transceiver_With_data()\r\n") );
  TWI_Start_Transceiver_With_Data( messageBuf, 3 );

  nrk_wait(t);

  messageBuf[0] = (TWI_targetSlaveAddress<<TWI_ADR_BITS) | (TRUE<<TWI_READ_BIT);
  messageBuf[1] = 0x06;//Read all 6 Registers.
  nrk_kprintf( PSTR("TWI_Start_Transceiver_With_data()\r\n") );
  TWI_Start_Transceiver_With_Data(messageBuf, TWI_targetSlaveAddress);
  TWI_operation = REQUEST_DATA; // Set next operation
        }

        else if (TWI_operation == REQUEST_DATA)
        { // Request data from slave
          messageBuf[0] = (TWI_targetSlaveAddress<<TWI_ADR_BITS) | (TRUE<<TWI_READ_BIT);
          TWI_Start_Transceiver_With_Data( messageBuf, TWI_targetSlaveAddress );
          TWI_operation = READ_DATA_FROM_BUFFER; // Set next operation        
        }
        else if (TWI_operation == READ_DATA_FROM_BUFFER)
        { // Get the received data from the transceiver buffer
          TWI_Get_Data_From_Transceiver( messageBuf, 6 );
					nrk_kprintf(PSTR( "master rx: " ));
					for(i=0; i<6; i++ ){
					//if(i%2==0)
					//messageBuf[i]=messageBuf[i]-1;
					//messageBuf[i]=~messageBuf[i];

					printf( "%x ",messageBuf[i]);
					nrk_kprintf(PSTR( "\r\n" ));
					}
          TWI_operation = SEND_DATA;    // Set next operation        
	nrk_wait_until_next_period();     
        }
      }
      else // Got an error during the last transmission
      {
			 //;;\rk_kprintf( PSTR("tx err\r\n") );
        //Use TWI status information to detemine cause of failure and take appropriate actions. 
        TWI_Act_On_Failure_In_Last_Transmission( TWI_Get_State_Info( ) );
      }
    } 
		
		//else
		//nrk_kprintf( PSTR("tx busy\r\n") );
    // Do something else while waiting for the TWI Transceiver to complete the current operation
    //__no_operation(); // Put own code here.
  
}
}

void Task2()
{
  printf( "Task2 PID=%u\r\n",nrk_get_pid());
  while(1) {
	nrk_led_toggle(BLUE_LED);
	nrk_wait_until_next_period();
	}
}


void
nrk_create_taskset()
{
  nrk_task_set_entry_function( &TaskOne, Task1);
  nrk_task_set_stk( &TaskOne, Stack1, NRK_APP_STACKSIZE);
  TaskOne.prio = 1;
  TaskOne.FirstActivation = TRUE;
  TaskOne.Type = BASIC_TASK;
  TaskOne.SchType = PREEMPTIVE;
  TaskOne.period.secs = 6;
  TaskOne.period.nano_secs = 0;
  TaskOne.cpu_reserve.secs = 0;
  TaskOne.cpu_reserve.nano_secs = 0;
  TaskOne.offset.secs = 0;
  TaskOne.offset.nano_secs= 0;
  nrk_activate_task (&TaskOne);

  nrk_task_set_entry_function( &TaskTwo, Task2);
  nrk_task_set_stk( &TaskTwo, Stack2, NRK_APP_STACKSIZE);
  TaskTwo.prio = 2;
  TaskTwo.FirstActivation = TRUE;
  TaskTwo.Type = BASIC_TASK;
  TaskTwo.SchType = PREEMPTIVE;
  TaskTwo.period.secs = 15;
  TaskTwo.period.nano_secs = 0;
  TaskTwo.cpu_reserve.secs = 0;
  TaskTwo.cpu_reserve.nano_secs =0;
  TaskTwo.offset.secs = 0;
  TaskTwo.offset.nano_secs= 0;
  nrk_activate_task (&TaskTwo);


}



