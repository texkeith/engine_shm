

// Example Client Setup that starts the Server and interacts with the SHM interface

#if defined( _MSC_VER )  //Windows SHM Includes
# include <windows.h>
#include <process.h>
#else  // Linux/Unix Methods
# include <sys/ipc.h>
# include <sys/shm.h>
# include <sys/types.h>
# include <windows_types.h>
#endif
#include <vector>
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

typedef struct   /* Proposed Shared Memory Block Structure */
{
	int   IOFLAG;			// Signals what you want the calling program to do with the SHM Contents
	int   ERRFLAG;			// Error Status of the last operation done by the Supplier code
	int   SIMMODE;			// execution mode of the model 1 = Steady State 2= Save Guess 3= Transient (ARP5571 proposed)
	int   DEBUG;            // Flag to a more verbose messaging 1=on 0= off (default)
	int   NumInputs;		// Number of Inputs Mapped thru the interface
	int   NumOutputs;		// Number of Outputs Mapped thru the interface
	char   COMMAND[1024];	// String Slot used to pass names and Commands thru the interface 
	char    ERRMSG[1024];	// String Slot used to pass error message associated with ERRFLAG
	double  INPUTS[1000];	// Array of input data values matched to the name array order
	double OUTPUTS[2000];	//Array of output data values matched to output name array order
}   ENGINE_SHM_Type;


PROCESS_INFORMATION pi;
STARTUPINFO         startUpInfo;
HANDLE parentPId;
DWORD FirstProcess;
DWORD dwProcessId;

using namespace std;


int main() {

	string ExeName = "Server.exe";
	/*   Start Server up   */
	if (!CreateProcess(ExeName.c_str(),          //  __in_opt     LPCTSTR lpApplicationName,
		(LPTSTR)ExeName.c_str(),      //__inout_opt  LPTSTR lpCommandLine,
		NULL,				//__in_opt     LPSECURITY_ATTRIBUTES lpProcessAttributes,
		NULL,				//__in_opt     LPSECURITY_ATTRIBUTES lpThreadAttributes,
		TRUE,				//__in         BOOL bInheritHandles,
		0,					//__in         DWORD dwCreationFlags,
		NULL,				//__in_opt     LPVOID lpEnvironment,
		NULL,				//__in_opt     LPCTSTR lpCurrentDirectory,
		&startUpInfo,		//__in         LPSTARTUPINFO lpStartupInfo,
		&pi) 				//__out        LPPROCESS_INFORMATION lpProcessInformation
		) {

		cout << "%%ERROR: Starting Server.exe" << endl;
		exit(-999);
	}
	Sleep(500);// Wait for the Server to start up and setup the memory
	long InID = pi.dwProcessId; // Get the process ID of the started server so we can hook to the SHM and semaphores
	stringstream file_name;
	file_name << "NPSS_SHM_FILE_MAP." << InID;
	string Fname = file_name.str();

	/*  Connect to SHARED MEMORY BLOCK */
	ENGINE_SHM_Type* SHMBlk; // Pointer to SHMBlock
	engine_shm_t     data; // Instance of the memory space

	data.shm_file_handle = OpenFileMapping(FILE_MAP_WRITE,
		FALSE,
		(LPCSTR)Fname.c_str()
	);
	data.addr = MapViewOfFile(
		data.shm_file_handle,       /* file-mapping object to map into address space */
		FILE_MAP_WRITE,              /* access mode                                   */
		0, 0,
		sizeof(ENGINE_SHM_Type));  /*sets number of bytes to map */

	SHMBlk = (ENGINE_SHM_Type*)data.addr; // Point the pointer to the SHM block

	cout << "%CLIENT%-> SHM Connected!" << endl;

	/* Connect to existing semaphores*/

	// Initialize the Semaphores 
	HANDLE DS_Semaphore = NULL; // Data Sent Semaphore 
	HANDLE RC_Semaphore = NULL; // Run Complete Semaphore
	LONG cMax = 1000;
	LPSTR DS_Sem;
	LPSTR RC_Sem;
	DWORD dwWaitResult; //Wait parm  
	LONG pOldVal = 0;  // Used for passing Arg 

	//  this names the semaphores based on the Process ID they were created under
	char buffer[32];
	wsprintf(buffer, "%d", InID);

	string DS(buffer);
	string temp = "DS_Flag." + DS;
	DS_Sem = (LPSTR)temp.c_str();
	string temp2 = "RC_Flag." + DS;
	RC_Sem = (LPSTR)temp2.c_str();

	// This sets up the Data Sent Semaphore
	DS_Semaphore = OpenSemaphore(SEMAPHORE_ALL_ACCESS, TRUE, DS_Sem);
	if (DS_Semaphore == NULL) {
		cout << "%CLIENT%-> ERROR connecting to Data Sent Semaphore\n";
	}
	// This sets up the Run Complete Semaphore need to have this after the client launches
	RC_Semaphore = OpenSemaphore(SEMAPHORE_ALL_ACCESS, TRUE, RC_Sem);
	if (RC_Semaphore == NULL) {
		cout << "%CLIENT%-> ERROR connecting Run Complete semaphore\n";
	}

	SHMBlk->IOFLAG = 0;
	ReleaseSemaphore(DS_Semaphore, 1, &pOldVal); // Signal Engine to be Ready 
	dwWaitResult = WaitForSingleObject(RC_Semaphore, INFINITE); // Wait till its Ready

/* Setup for transactions with the SHM Block*/

	// First this is an optional example of getting all the variable names from the model
	vector<string> InputNames; vector<string> OutputNames;

	SHMBlk->IOFLAG = 1;// Signal I want to load inputs
	ReleaseSemaphore(DS_Semaphore, 1, &pOldVal); 
	for (int i = 0; i < SHMBlk->NumInputs; i++) {
		while (SHMBlk->IOFLAG != 0) {
			SHMBlk->SIMMODE = 1;  // The engine side will change this to 0 when it puts a name in the Command slot
		}
		string tstr = (string)SHMBlk->COMMAND;
		InputNames.push_back(tstr);
		SHMBlk->IOFLAG = 1;
	}
 
	SHMBlk->IOFLAG = 2;// Signal I want to load inputs
	ReleaseSemaphore(DS_Semaphore, 1, &pOldVal);
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
				ReleaseSemaphore(DS_Semaphore, 1, &pOldVal); // Signal Data has been sent
				dwWaitResult = WaitForSingleObject(RC_Semaphore, INFINITE); // Wait till its done Run Complete
				Sleep(200);// Give a tick for memory to catch up

				// Now Run the model;
				SHMBlk->IOFLAG = 4; // Run signal;
				ReleaseSemaphore(DS_Semaphore, 1, &pOldVal); // Signal Data has been sent
 				dwWaitResult = WaitForSingleObject(RC_Semaphore, INFINITE); // Wait till its done
				Sleep(300);

				// Get the Outputs
				SHMBlk->IOFLAG = 5; // Tell the Engine model to get the outputs;
				ReleaseSemaphore(DS_Semaphore, 1, &pOldVal); // Signal Data has been sent
 				dwWaitResult = WaitForSingleObject(RC_Semaphore, INFINITE); // Wait till its done
				Sleep(200);

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
	return(0);




 



	cout << "Client" << endl;
}