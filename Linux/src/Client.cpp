

// Example Linux Client Setup that starts the Server and interacts with the SHM interface

 
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <semaphore.h>
#include <cstdlib> /* need for exit() */
#include <stdio.h>/* printf example */
#define Sleep usleep
 
#include <vector>
#include <iostream> 
#include <string>
#include <sstream>
#include <vector>
#include <cstring>


// Struct type to store infor about the SHM Block
typedef struct {
#if defined( _MSC_VER ) 
	void          *addr;
	char          *shm_file_name;
	HANDLE         shm_file_handle;
#else
 int shm_id;
 key_t key;
#endif	
} engine_shm_t;

typedef struct   /* Proposed Shared Memory Block Structure */
{
	int   IOFLAG;			// Signals what you want the calling program to do with the SHM Contents
	int   ERRFLAG;			// Error Status of the last operation done by the Supplier code
	int   SIMMODE;			// execution mode of the model 1 = Steady State 2= Save Guess 3= Transient (ARP5571 proposed)
	int   DEBUG;            // Flag to a more verbose messaging 1=on 0= off (default)
	int   NumInputs;	// Number of Inputs Mapped thru the interface
	int   NumOutputs;	// Number of Outputs Mapped thru the interface
	char   COMMAND[1024];	// String Slot used to pass names and Commands thru the interface 
	char    ERRMSG[1024];	// String Slot used to pass error message associated with ERRFLAG
	double  INPUTS[1000];	// Array of input data values matched to the name array order
	double OUTPUTS[2000];	// Array of output data values matched to output name array order
}   ENGINE_SHM_Type;

 

using namespace std;
 
  
   int fork_return = 0;
   char *argv[1];
   char  addr_str[ 80 ];
   char cmdline[ 300 ];

int main() {
  

 fork_return = fork();
  if( fork_return < 0)  //  This is done only if there is a fork problem
  {   printf("ENGINE: Unable to create child process, exiting.\n"); exit(-1); }
   /* After fork both processes will do the following */
   
  if( fork_return == 0)//  This is done in spawned child
  { // The new Process is moved to another CPU if the enviroment variable is set
     execv("Server.linux",argv); // Launch the engine server
     cout <<"execv Error" << strerror(errno)<<endl;
     exit(-1);
   }
 /*  SET UP SHARED MEMORY BLOCK */
ENGINE_SHM_Type* SHMBlk; // Pointer to SHMBlock
engine_shm_t     data; // Instance of the memory space


// Setup the Semaphores 
string DS_NAME ="DS_";
string RC_NAME ="RC_";

sem_t *RC_Semaphore;
sem_t *DS_Semaphore;


stringstream ss;
 ss<<fork_return;

 DS_NAME.append(ss.str());
 RC_NAME.append(ss.str());

cout<< DS_NAME<<endl;
cout<< RC_NAME<<endl;
	
//RC_Semaphore = sem_open(RC_NAME.c_str(),O_CREAT,0777,0);
 
 data.key=fork_return;

 data.shm_id = shmget( data.key, sizeof(ENGINE_SHM_Type),0666 );

// This waits until the ShM Block Exists in the server, it will go positive 
  while(data.shm_id ==-1){
   data.shm_id = shmget( data.key, sizeof(ENGINE_SHM_Type),0666 );  
  }

/* Now Attach the Shm to the local instance */
    if ( data.shm_id ) {
      SHMBlk = (ENGINE_SHM_Type *) shmat( data.shm_id, NULL, 0 );
      if ( SHMBlk == (void*) -1 ) {
       cout <<" Error Attaching Shared Memory in Client"<<endl;
      }else {
      printf( "testClient-> PropIO Attached to shmid: %d\n", data.shm_id );
    }

    }

// The Data Sent Semaphore should exist now that the server has created the ShM Block
DS_Semaphore = sem_open(DS_NAME.c_str(),0);  //Set the Data Sent Semaphore created from the server
RC_Semaphore = sem_open(RC_NAME.c_str(), 0);  // Set the Run Complete Semaphore created from the server
 
if ((RC_Semaphore== SEM_FAILED)||(DS_Semaphore== SEM_FAILED)){  // Final Checks in case something happened
  cout <<" ERROR: Semaphore Setup failed "<< errno <<endl;
  exit(-9);
}


/* Setup for transactions with the SHM Block*/

	// First this is an optional example of getting all the variable names from the model
	vector<string> InputNames; vector<string> OutputNames;

	SHMBlk->IOFLAG = 1;// Signal I want to load inputs
	sem_post(DS_Semaphore);
	for (int i = 0; i < SHMBlk->NumInputs; i++) {
		while (SHMBlk->IOFLAG != 0) {
			SHMBlk->SIMMODE = 1;  // The engine side will change this to 0 when it puts a name in the Command slot
		}
		string tstr = (string)SHMBlk->COMMAND;
		InputNames.push_back(tstr);
		SHMBlk->IOFLAG = 1;
	}
 
	SHMBlk->IOFLAG = 2;// Signal I want to load inputs
	sem_post(DS_Semaphore);
	for (int i = 0; i < SHMBlk->NumOutputs; i++) {
		while (SHMBlk->IOFLAG != 0) {
			SHMBlk->SIMMODE = 2;  // The engine side will change this to 0 when it puts a name in the Command slot
		}
		string tstr = (string)SHMBlk->COMMAND;
		OutputNames.push_back(tstr);
		SHMBlk->IOFLAG = 2;
	}
	

	/*  Run a bunch of points */
	double Alts[] = { 0., 10000.0, 20000., 30000.,36089.,40000., 50000.,60000 };
	double Machs[] = { 0., .25, .5, .8 , .95 ,1., 1.2,1.5, 2.0, 2.5 };
	double PLAs[] = { 150, 100, 90, 80, 70, 50, 20, 15, 10 };
	int CASE = 0;
	cout << " Hammer out alot of points " << endl;

	int NumAlts = sizeof(Alts) / sizeof(Alts[0]);
	int NumMachs = sizeof(Machs) / sizeof(Machs[0]);
	int NumPLAs  = sizeof(PLAs) / sizeof(PLAs[0]);

	for (int i = 0; i< NumAlts; i++) {
	  cout << "\n *********** ALTITUDE : " << Alts[i] << " **************" << endl;
		for (int j = 0; j<NumMachs; j++) {
			for (int k = 0; k < NumPLAs; k++) {
				/* Set he SHM INPUTS directly */
				SHMBlk->INPUTS[0] = Alts[i];
				SHMBlk->INPUTS[1] = Machs[j];
				SHMBlk->INPUTS[2] = PLAs[k];

				SHMBlk->IOFLAG = 3; // Set the inputs on the engine side;
				sem_post(DS_Semaphore);
                                sem_wait(RC_Semaphore);
				Sleep(200);// Give a tick for memory to catch up

				// Now Run the model;
				SHMBlk->IOFLAG = 4; // Run signal;
				sem_post(DS_Semaphore);
                                sem_wait(RC_Semaphore);
				Sleep(300);

				// Get the Outputs
				SHMBlk->IOFLAG = 5; // Tell the Engine model to get the outputs;
				sem_post(DS_Semaphore);
                                sem_wait(RC_Semaphore);				Sleep(200);

				cout << "\nCASE " << CASE++ << " Completed OUTPUT below \n" << endl;
				for (int x = 0; x < InputNames.size(); x++) {
					cout << InputNames[x] << " = " << SHMBlk->INPUTS[x] << "\t";
				}
				cout << endl;
				for (int x = 0; x < OutputNames.size(); x++) {
					cout << OutputNames[x] << " = " << SHMBlk->OUTPUTS[x] << "\t";
				}
				cout << endl;
				Sleep(2000);// This lets the srceen write buffer catch up
			}
			 
		}
	}
 
 
	cout << "Run Complete"<< endl;
	
	return(0);

}
