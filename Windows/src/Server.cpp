
//  Simple Shared Memory Server Setup


#if defined( _MSC_VER )  //Windows SHM Includes
# include <windows.h>
#include <process.h>
#else  // Linux/Unix Methods
# include <sys/ipc.h>
# include <sys/shm.h>
# include <sys/types.h>
# include <windows_types.h>
#endif
#include <ctime>
#include <iostream>
#include <string>
#include <sstream>

// Struct type to store infor about the SHM Block
typedef struct {
	void          *addr;
	char          *shm_file_name;
	HANDLE         shm_file_handle;
	unsigned int   addr_was_mallocd;
} engine_shm_t;

typedef struct { /* Proposed Shared Memory Block Structure */
	int   IOFLAG;			// Signals what you want the calling program to do with the SHM Contents
	int   ERRFLAG;			// Error Status of the last operation done by the Supplier code
	int   SIMMODE;			// execution mode of the model 1 = Steady State 2= Save Guess 3= Transient (ARP5571 proposed)
	int   DEBUG;         // Flag to enable more verbose messaging 1=on 0=0 (default)
	int   NumInputs;		// Number of Inputs Mapped thru the interface
	int   NumOutputs;		// Number of Outputs Mapped thru the interface
	char   COMMAND[1024];	// String Slot used to pass names and Commands thru the interface
	char    ERRMSG[1024];	// String Slot used to pass error message associated with ERRFLAG
	double  INPUTS[1000];	// Array of input data values matched to the name array order
	double OUTPUTS[2000];	//Array of output data values matched to output name array order
}   ENGINE_SHM_Type;


using namespace std;

int main()
{


	/*  SET UP SHARED MEMORY BLOCK */
	ENGINE_SHM_Type* SHMBlk; // Pointer to SHMBlock
	engine_shm_t     data; // Instance of the memory space

	stringstream file_name;
	long PID = _getpid();
	file_name << "NPSS_SHM_FILE_MAP." << PID;
	string Fname = file_name.str();

	HANDLE file_handle = CreateFileMapping(INVALID_HANDLE_VALUE, // system pagefile
		NULL,     	    // optional security attributes
		PAGE_READWRITE, // protection for mapping object
		0,              // high-order 32 bits of object size
		sizeof(ENGINE_SHM_Type), // Size of Block
		(LPCSTR)file_name.str().c_str()); //name of file - mapping

	data.shm_file_handle = file_handle;
	data.shm_file_name = (char*)Fname.c_str();

	data.addr = MapViewOfFile(data.shm_file_handle, FILE_MAP_WRITE, 0, 0, sizeof(ENGINE_SHM_Type));

	SHMBlk = (ENGINE_SHM_Type*)data.addr; // Point the pointer to the SHM block

										  /*Set up semephores */

										  // Initialize the Semaphores
	HANDLE DS_Semaphore; // Data Sent Semaphore
	HANDLE RC_Semaphore; // Run Complete Semaphore
	LONG cMax = 10000;  // Max size Semaphone length
	LPSTR DS_Sem; //Tag for the Data Sent Semaphore
	LPSTR RC_Sem;// Tag for the Run Complete Semaphore
	DWORD dwWaitResult; //Wait parm
	LONG pOldVal;  // Used for passing Arg

				   //  this names the semaphores based on the Process ID they were created under
	char buffer[32];
	wsprintf(buffer, "%d", PID);

	string DS(buffer);
	string temp = "DS_Flag." + DS;
	DS_Sem = (LPSTR)temp.c_str();

	string temp2 = "RC_Flag." + DS;
	RC_Sem = (LPSTR)temp2.c_str();

	DS_Semaphore = CreateSemaphore(
		NULL, // default security attributes
		0, // initial count
		cMax, // maximum count
		DS_Sem); // named semaphore
	if (DS_Semaphore == NULL) {
		printf("CreateSemaphore DS_Semaphore Data Send() error: %d\n", GetLastError());
	}
	else {
		printf("CreateSemaphore DS_Semaphore is OK\n");
	}

	RC_Semaphore = CreateSemaphore(
		NULL, // default security attributes
		0, // initial count
		cMax, // maximum count
		RC_Sem); // named semaphore
	if (RC_Semaphore == NULL) {
		printf("CreateSemaphore RC_Semaphore Data Send() error: %d\n", GetLastError());
	}
	else {
		printf("CreateSemaphore RC_Semaphore is OK\n");
	}

	/*Simple Demo Engine Model I/O setup*/
	double  Inputs[3];
	double Outputs[7];
	string InputNames[] = { "Amb.Alt_in", "Amb.Mach_in", "Eint.PLA_in" };
	double ZALT, ZXM, ZPLA; // Dummy Input Variables to use for demo purposes
	string OutputNames[] = { "Eint.PLA","Perf.Fg", "Perf.Fram","Perf.Fn", "Perf.Wfuel", "ShL.Nmech", "ShH.Nmech" };
	double FG, FR, FN, WFT, N1, N2;
	int numInputs = 3;
	int numOutputs = 7; // Set array sizes in local variable

						// Set Values in the SHM Block
	SHMBlk->SIMMODE = 0;
	SHMBlk->NumInputs = numInputs;
	SHMBlk->NumOutputs = numOutputs;

	//  initializations Complete
	cout << "\n%SERVER%-> Model is loaded and awaiting client to connect! Process is:" << PID << endl;
	dwWaitResult = WaitForSingleObject(DS_Semaphore, INFINITE); // Program Waits till Data Sent Semaphore trips when client connects to memory
	cout << "\n%SERVER%-> Handshake Complete Client now Connected Server Starting" << endl;

	// Local Variables used in the Run loop
	string tstr;
	double val;
	string Cmd;
	int _MODEFLAG; // Switch for what to do
	string runCommand = "run();"; //Local variable to store the run trigger
	double ALTScalar, MACHScalar, PLAScalar;

Top_of_Run:
	_MODEFLAG = (int)SHMBlk->IOFLAG;

	switch (_MODEFLAG) {
	case 0:
		break;

	case 10: // This sends the Input variables Names when asked in a small iterative loop thru the COMMAND variable
		for (int i = 0; i<SHMBlk->NumInputs; i++) {
			char  a[1024];
			strncpy(a, InputNames[i].c_str(), 1024);
			strncpy(SHMBlk->COMMAND, a, 1024); // Copy the input name variables to the COMMAND variable in the SHM Block
			SHMBlk->IOFLAG = 0;// This signals the client you have updated the COMMAND value
			while (SHMBlk->IOFLAG == 0) {
				Sleep(10); // Wait a tick. Till the client gets the data
			}
		}
		break;

	case 12: //Set an input value directly  Name is in Command array Value is input 1  This demo not supporting this but 4868 example is below
		tstr = (string)SHMBlk->COMMAND;
		val = SHMBlk->INPUTS[0];
		cout << "\n%SERVER%->" << tstr << " = " << SHMBlk->INPUTS[0] << endl;
		break;
		/*
		RC = c4868setD(tstr.c_str(), val);
		if (!RC) {
		cout << "\n%SERVER%-> " << tstr << " set to :" << val << endl;
		}
		else {
		cout << "\n%SERVER%->** ERROR Set Variable not found!\nCan't find " << tstr << " In currently loaded model\nIt will not be returned!" << endl;
		}
		*/

	case 20: // this sends the output variable Names in a small iterative loop thru the COMMAND variable
		for (int i = 0; i<SHMBlk->NumOutputs; i++) {
			char  a[1024];
			strncpy(a, OutputNames[i].c_str(), 1024);
			strncpy(SHMBlk->COMMAND, a, 1024); // Same iterative copy but output names
			SHMBlk->IOFLAG = 0;
			while (SHMBlk->IOFLAG == 0) {
				Sleep(10); // Wait a tick. Till the client gets the data
			}
		}
		break;

	case 22: //Get a value directly  Name from in Command array Value is output to slot 1 This sample just pumps back FG but 4848 example is below
		SHMBlk->OUTPUTS[0] = FG;
		cout << "\n%SERVER%->" << SHMBlk->COMMAND << " = " << SHMBlk->OUTPUTS[0] << endl;
		break;
		/* 4868 example of this mode
		RC = c4868getD(tstr.c_str(), &val);

		if (!RC) {
		shmBLK.shm_addr->OUTPUTS[0] = val;
		}
		else {
		cout << "\n%NPSS%->** ERROR Get Variable not found!\n NPSS Can't find " << tstr << " In currently loaded model\nIt will not be returned!" << endl;
		shmBLK.shm_addr->OUTPUTS[0] = 0.0;
		}
		*/

	case 30: // Set input Values from Shm
			 // Just setting local Vars here for Demo purposes
		ZALT = SHMBlk->INPUTS[0];
		ZXM = SHMBlk->INPUTS[1];
		ZPLA = SHMBlk->INPUTS[2];
		cout << "Setting Inputs" << endl;
		break;
		/*  What a 4868 call might look like here for a larger number of variables
		for (size_t i = 0; i<SHMBlk->NumInputs; i++) {
		int RC = c4868setD(InputNames[i].c_str(), SHMBlk->INPUTS[i]);
		}*/

	case 40: // Run Command
			 /*  This is where the engine model would run() would be called
			 this is a simplified version to generate numbers */
		ALTScalar = (1 - (.9  * (ZALT) / 50000));
		MACHScalar = (1 - (.9 * (ZXM) / 2.5));
		PLAScalar = (.1 + (.9 * (ZPLA - 10.) / 140.));

		FG = 30000 * ALTScalar * MACHScalar * PLAScalar;
		FR = 400 * PLAScalar * ZXM / 32.2;
		FN = FG - FR;
		WFT = 40000 * PLAScalar;
		N1 = 12000 * PLAScalar;
		N2 = 16000 * PLAScalar;
		Sleep(1200);
		cout << "Run completed" << endl;
		break;
		/* What a 4868 run command might be using a local run variable that can be set in case 9
		cout << "\n%NPSS%->" << runCommand.c_str() << endl;
		c4868parseString(runCommand.c_str());
		*/
	case 50: // Send Output values to Shm OUTPUT BLOCK
			 // Setting SHM = to local variables for demo purposes
		SHMBlk->OUTPUTS[0] = ZPLA;
		SHMBlk->OUTPUTS[1] = FG;
		SHMBlk->OUTPUTS[2] = FR;
		SHMBlk->OUTPUTS[3] = FN;
		SHMBlk->OUTPUTS[4] = WFT;
		SHMBlk->OUTPUTS[5] = N1;
		SHMBlk->OUTPUTS[6] = N2;
		/*    What a 4868 get might look like
		for (size_t i = 0; i<OutputsDeck.size(); i++) {
		RC = c4868getD(OutputsDeck[i].c_str(), &shmBLK.shm_addr->OUTPUTS[i]);
		//    cout <<"Getting "<< Outputs[i]<<" = "<<shmBLK.shm_addr->OUTPUTS[i]<<endl;
		} */
		cout << "Outputs set" << endl;

		break;

	case 60: // parseString
			 /* If the Supplier code has a method to parse a string command this would execute here */
		Cmd = SHMBlk->COMMAND;
		cout << "\n%SERVER%->" << Cmd << endl; // Just echo string for demo purposed
		break;
		/*  What a 4868 parse string command would look like
		RC = c4868parseString(Cmd.c_str());
		*/
	case 80: // this changes the runcommand for setrunget
		tstr = (string)SHMBlk->COMMAND;
		cout << "\n%SERVER%-> Run Command Overridden now using " << tstr << endl;
		runCommand = tstr;
		break;

	
	

	case 99: // Terminate
		cout << "\n%SERVER%-> Server is shutting down" << endl;
		exit(0);
		/*  4868 type shutdown
		RC = c4868terminate();
		*/

	} // end of case select}
	  // Use some time stamps here and there to see how long things take.
	std::time_t t = std::time(0);
	cout << "\n%SERVER%> Command Completed @ " << std::asctime(std::localtime(&t)) << endl;

	/* Signal the Client using the Run Complete Semaphore */
	ReleaseSemaphore(RC_Semaphore, 1, &pOldVal);

	/* Wait for the next Data Sent signal */
	cout << "\n%SERVER%> Waiting for Next Task " << endl;

	dwWaitResult = WaitForSingleObject(DS_Semaphore, INFINITE); // Data Sent Signal will waits till released in client
	Sleep(20);
	t = std::time(0);
	cout << "\n%SERVER%> IOFLAG: " << SHMBlk->IOFLAG << " Command Received @ " << std::asctime(std::localtime(&t)) << endl;


	goto Top_of_Run; // This runs in a continuous loop but the semaphore waits keep it from running in a continuous execution spin

	return(0);


}