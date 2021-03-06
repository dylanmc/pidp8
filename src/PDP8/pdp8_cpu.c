/* pdp8_cpu.c: PDP-8 CPU simulator

   Copyright (c) 1993-2011, Robert M Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   cpu          central processor

   28-Apr-07    RMS     Removed clock initialization
   30-Oct-06    RMS     Added idle and infinite loop detection
   30-Sep-06    RMS     Fixed SC value after DVI overflow (Don North)
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   06-Nov-04    RMS     Added =n to SHOW HISTORY
   31-Dec-03    RMS     Fixed bug in set_cpu_hist
   13-Oct-03    RMS     Added instruction history
                        Added TSC8-75 support (Bernhard Baehr)
   12-Mar-03    RMS     Added logical name support
   04-Oct-02    RMS     Revamped device dispatching, added device number support
   06-Jan-02    RMS     Added device enable/disable routines
   30-Dec-01    RMS     Added old PC queue
   16-Dec-01    RMS     Fixed bugs in EAE
   07-Dec-01    RMS     Revised to use new breakpoint package
   30-Nov-01    RMS     Added RL8A, extended SET/SHOW support
   16-Sep-01    RMS     Fixed bug in reset routine, added KL8A support
   10-Aug-01    RMS     Removed register from declarations
   17-Jul-01    RMS     Moved function prototype
   07-Jun-01    RMS     Fixed bug in JMS to non-existent memory
   25-Apr-01    RMS     Added device enable/disable support
   18-Mar-01    RMS     Added DF32 support
   05-Mar-01    RMS     Added clock calibration support
   15-Feb-01    RMS     Added DECtape support
   14-Apr-99    RMS     Changed t_addr to unsigned

   The register state for the PDP-8 is:

   AC<0:11>             accumulator
   MQ<0:11>             multiplier-quotient
   L                    link flag
   PC<0:11>             program counter
   IF<0:2>              instruction field
   IB<0:2>              instruction buffer
   DF<0:2>              data field
   UF                   user flag
   UB                   user buffer
   SF<0:6>              interrupt save field

   The PDP-8 has three instruction formats: memory reference, I/O transfer,
   and operate.  The memory reference format is:

     0  1  2  3  4  5  6  7  8  9 10 11
   +--+--+--+--+--+--+--+--+--+--+--+--+
   |   op   |in|zr|    page offset     |        memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+

   <0:2>        mnemonic        action

    000         AND             AC = AC & M[MA]
    001         TAD             L'AC = AC + M[MA]
    010         DCA             M[MA] = AC, AC = 0
    011         ISZ             M[MA] = M[MA] + 1, skip if M[MA] == 0
    100         JMS             M[MA] = PC, PC = MA + 1
    101         JMP             PC = MA

   <3:4>        mode            action
    00  page zero               MA = IF'0'IR<5:11>
    01  current page            MA = IF'PC<0:4>'IR<5:11>
    10  indirect page zero      MA = xF'M[IF'0'IR<5:11>]
    11  indirect current page   MA = xF'M[IF'PC<0:4>'IR<5:11>]

   where x is D for AND, TAD, ISZ, DCA, and I for JMS, JMP.

   Memory reference instructions can access an address space of 32K words.
   The address space is divided into eight 4K word fields; each field is
   divided into thirty-two 128 word pages.  An instruction can directly
   address, via its 7b offset, locations 0-127 on page zero or on the current
   page.  All 32k words can be accessed via indirect addressing and the
   instruction and data field registers.  If an indirect address is in
   locations 0010-0017 of any field, the indirect address is incremented
   and rewritten to memory before use.

   The I/O transfer format is as follows:

     0  1  2  3  4  5  6  7  8  9 10 11
   +--+--+--+--+--+--+--+--+--+--+--+--+
   |   op   |      device     | pulse  |        I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+

   The IO transfer instruction sends the the specified pulse to the
   specified I/O device.  The I/O device may take data from the AC,
   return data to the AC, initiate or cancel operations, or skip on
   status.

   The operate format is as follows:

   +--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 1| 1| 0|  |  |  |  |  |  |  |  |        operate group 1
   +--+--+--+--+--+--+--+--+--+--+--+--+
                |  |  |  |  |  |  |  |
                |  |  |  |  |  |  |  +--- increment AC  3
                |  |  |  |  |  |  +--- rotate 1 or 2    4
                |  |  |  |  |  +--- rotate left         4
                |  |  |  |  +--- rotate right           4
                |  |  |  +--- complement L              2
                |  |  +--- complement AC                2
                |  +--- clear L                         1
                +-- clear AC                            1

   +--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 1| 1| 1|  |  |  |  |  |  |  | 0|        operate group 2
   +--+--+--+--+--+--+--+--+--+--+--+--+
                |  |  |  |  |  |  |
                |  |  |  |  |  |  +--- halt             3
                |  |  |  |  |  +--- or switch register  3
                |  |  |  |  +--- reverse skip sense     1
                |  |  |  +--- skip on L != 0            1
                |  |  +--- skip on AC == 0              1
                |  +--- skip on AC < 0                  1
                +-- clear AC                            2

   +--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 1| 1| 1|  |  |  |  |  |  |  | 1|        operate group 3
   +--+--+--+--+--+--+--+--+--+--+--+--+
                |  |  |  | \______/
                |  |  |  |     |
                |  |  +--|-----+--- EAE command         3
                |  |     +--- AC -> MQ, 0 -> AC         2
                |  +--- MQ v AC --> AC                  2
                +-- clear AC                            1

  The operate instruction can be microprogrammed to perform operations
  on the AC, MQ, and link.

  This routine is the instruction decode routine for the PDP-8.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        unimplemented instruction and stop_inst flag set
        I/O error in I/O simulator

   2. Interrupts.  Interrupts are maintained by three parallel variables:

        dev_done        device done flags
        int_enable      interrupt enable flags
        int_req         interrupt requests

      In addition, int_req contains the interrupt enable flag, the
      CIF not pending flag, and the ION not pending flag.  If all
      three of these flags are set, and at least one interrupt request
      is set, then an interrupt occurs.

   3. Non-existent memory.  On the PDP-8, reads to non-existent memory
      return zero, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes outside the current field (indirect writes) need
      be checked against actual memory size.

   3. Adding I/O devices.  These modules must be modified:

        pdp8_defs.h     add device number and interrupt definitions
        pdp8_sys.c      add sim_devices table entry
*/

#include "pdp8_defs.h"

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = MA
#define UNIT_V_NOEAE    (UNIT_V_UF)                     /* EAE absent */
#define UNIT_NOEAE      (1 << UNIT_V_NOEAE)
#define UNIT_V_MSIZE    (UNIT_V_UF + 1)                 /* dummy mask */
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)
#define OP_KSF          06031                           /* for idle */

#define HIST_PC         0x40000000
#define HIST_MIN        64
#define HIST_MAX        65536

typedef struct {
    int32               pc;
    int32               ea;
    int16               ir;
    int16               opnd;
    int16               lac;
    int16               mq;
    } InstHistory;

uint16 M[MAXMEMSIZE] = { 0 };                           /* main memory */
int32 saved_LAC = 0;                                    /* saved L'AC */
int32 saved_MQ = 0;                                     /* saved MQ */
int32 saved_PC = 0;                                     /* saved IF'PC */
int32 saved_DF = 0;                                     /* saved Data Field */
int32 IB = 0;                                           /* Instruction Buffer */
int32 SF = 0;                                           /* Save Field */
int32 emode = 0;                                        /* EAE mode */
int32 gtf = 0;                                          /* EAE gtf flag */
int32 SC = 0;                                           /* EAE shift count */
int32 UB = 0;                                           /* User mode Buffer */
int32 UF = 0;                                           /* User mode Flag */
int32 OSR = 0;                                          /* Switch Register */
int32 tsc_ir = 0;                                       /* TSC8-75 IR */
int32 tsc_pc = 0;                                       /* TSC8-75 PC */
int32 tsc_cdf = 0;                                      /* TSC8-75 CDF flag */
int32 tsc_enb = 0;                                      /* TSC8-75 enabled */
int16 pcq[PCQ_SIZE] = { 0 };                            /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
int32 dev_done = 0;                                     /* dev done flags */
int32 int_enable = INT_INIT_ENABLE;                     /* intr enables */
int32 int_req = 0;                                      /* intr requests */
int32 stop_inst = 0;                                    /* trap on ill inst */
int32 (*dev_tab[DEV_MAX])(int32 IR, int32 dat);         /* device dispatch */
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
InstHistory *hst = NULL;                                /* instruction history */

extern int32 sim_interval;
extern int32 sim_int_char;
extern uint32 sim_brk_types, sim_brk_dflt, sim_brk_summ; /* breakpoint info */
extern DEVICE *sim_devices[];
extern FILE *sim_log;
extern t_bool sim_idle_enab;

/* ------------------------------------------------------------------------------------------------- */
extern uint32 switchstatus[3]; // bitfields: 3 rows of up to 12 switches
extern uint32 ledstatus[8];	// bitfields: 8 ledrows of up to 12 LEDs
void setleds(uint32 sPC, uint32 sMA, uint16 sMB, int32 sLAC, int32 sMQ, int32 sIF, int32 sDF);
/* ------------------------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------------------------------- */
int swStop = 0, swExam = 0, swDep = 0, swCont2 = 0, swStart = 0, swSingStep = 0, swAttach = 0;
char mountedFiles[8][CBUFSIZE];
char	swDevCode[4];
int	awfulHackFlag=0;	// truly terrible even for me - break out of sim and start new script in scp.c

/* --------------------------------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------------------------------- */
#include <dirent.h>	// for USB stick searching
int mountUSBStickFile(int devNo, char *devCode, char *sPath);
extern t_stat attach_cmd (int32 flag, char *cptr); // from scp
extern t_stat do_cmd (int32 flag, char *cptr); // from scp
extern t_stat spawn_cmd (int32 flag, char *cptr);
extern t_stat exit_cmd (int32 flag, char *cptr);
char xcbuf[CBUFSIZE], *xcptr;
/* --------------------------------------------------------------------------------------------------------- */



t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc);
t_bool build_dev_tab (void);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
    { ORDATA (PC, saved_PC, 15) },
    { ORDATA (AC, saved_LAC, 12) },
    { FLDATA (L, saved_LAC, 12) },
    { ORDATA (MQ, saved_MQ, 12) },
    { ORDATA (SR, OSR, 12) },
    { GRDATA (IF, saved_PC, 8, 3, 12) },
    { GRDATA (DF, saved_DF, 8, 3, 12) },
    { GRDATA (IB, IB, 8, 3, 12) },
    { ORDATA (SF, SF, 7) },
    { FLDATA (UB, UB, 0) },
    { FLDATA (UF, UF, 0) },
    { ORDATA (SC, SC, 5) },
    { FLDATA (GTF, gtf, 0) },
    { FLDATA (EMODE, emode, 0) },
    { FLDATA (ION, int_req, INT_V_ION) },
    { FLDATA (ION_DELAY, int_req, INT_V_NO_ION_PENDING) },
    { FLDATA (CIF_DELAY, int_req, INT_V_NO_CIF_PENDING) },
    { FLDATA (PWR_INT, int_req, INT_V_PWR) },
    { FLDATA (UF_INT, int_req, INT_V_UF) },
    { ORDATA (INT, int_req, INT_V_ION+1), REG_RO },
    { ORDATA (DONE, dev_done, INT_V_DIRECT), REG_RO },
    { ORDATA (ENABLE, int_enable, INT_V_DIRECT), REG_RO },
    { BRDATA (PCQ, pcq, 8, 15, PCQ_SIZE), REG_RO+REG_CIRC },
    { ORDATA (PCQP, pcq_p, 6), REG_HRO },
    { FLDATA (STOP_INST, stop_inst, 0) },
    { ORDATA (WRU, sim_int_char, 8) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_NOEAE, UNIT_NOEAE, "no EAE", "NOEAE", NULL },
    { UNIT_NOEAE, 0, "EAE", "EAE", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    { UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
    { UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
    { UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
    { UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
    { UNIT_MSIZE, 20480, NULL, "20K", &cpu_set_size },
    { UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
    { UNIT_MSIZE, 28672, NULL, "28K", &cpu_set_size },
    { UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 15, 1, 8, 12,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

t_stat sim_instr (void)
{
int32 IR, MB, IF, DF, LAC, MQ;
uint32 PC, MA;
int32 device, pulse, temp, iot_data;
t_stat reason;

/* Restore register state */

if (build_dev_tab ()) return SCPE_STOP;                 /* build dev_tab */
PC = saved_PC & 007777;                                 /* load local copies */
IF = saved_PC & 070000;
DF = saved_DF & 070000;
LAC = saved_LAC & 017777;
MQ = saved_MQ & 07777;
int_req = INT_UPDATE;
reason = 0;


/* ---PiDP add--------------------------------------------------------------------------------------------- */
int swDevice;
char sScript[256];
MA = 0;	// have to add this to avoid crash when stop switch is set at start - MA would be undefined in setleds
setleds(PC, MA, MB, LAC, MQ, IF, DF); // note MB used // light up leds for 1st time, only needed when stop switch set at start
/* ---PiDP end---------------------------------------------------------------------------------------------- */


/* Main instruction fetch/decode loop */

while (reason == 0) {                                   /* loop until halted */


/* ---PiDP add--------------------------------------------------------------------------------------------- */
awfulHackFlag = 0; // no do script pending. Did I mention awful?

ledstatus[5] |= 1<<3; // set fetch
ledstatus[5] &= ~(1<<2); // clear execute
/* ---PiDP end---------------------------------------------------------------------------------------------- */


    if (sim_interval <= 0) {                            /* check clock queue */
        if (reason = sim_process_event ())
            break;
        }

/* ---PiDP add--------------------------------------------------------------------------------------------- */

// this bit of code detects SING_INST as the special features switch.
// when DF switches are set, that raises a hacked-in-to-simh signal to ATTACH PTR <filename>
// when IF switches are set, that raises a hacked-in-to-simh signal to DO <filename> (boot script)

if ((switchstatus[2] & 0x0020)==0) //SING_STEP toggled
{
	if (swAttach==0)		// if this is the first time we detect it,
	{	
		swAttach=1;		// make this a momentary switch in software

		// 1. Scan DF to see if any devices need to be mounted (DF=0 --> nothing to mount)

		swDevice = (((switchstatus[1] >> 11) & 1)==0?4:0)
			+(((switchstatus[1] >> 10) & 1)==0?2:0)
			+(((switchstatus[1] >> 9) & 1)==0?1:0);

		if (swDevice!=0)
		{
			switch(swDevice)
			{
				case 1:	strcpy(swDevCode,"ptr"); break;	// PTR paper tape reader
				case 2:	strcpy(swDevCode,"ptp"); break;	// High speed paper tape punch
				case 3:	strcpy(swDevCode,"dt0"); break;	// TC08 DECtape (#8 is first!)
				case 4:	strcpy(swDevCode,"dt1"); break;
				case 5:	strcpy(swDevCode,"rx0"); break;	// RX8E (8/e peripheral!)
				case 6:	strcpy(swDevCode,"rx1"); break;
				case 7:	strcpy(swDevCode,"rl0"); break;	// RL8A
			}
			xcptr=&xcbuf[0];				// set string pointer to start
			mountUSBStickFile(swDevice, swDevCode, xcptr);
		}

		// 2. Scan IF to see if we need to reboot with a new bootscript

		swDevice = (((switchstatus[1] >> 8) & 1)==0?4:0)
			+(((switchstatus[1] >> 7) & 1)==0?2:0)
			+(((switchstatus[1] >> 6) & 1)==0?1:0);

		if (swDevice!=0)
		{
			sprintf(sScript,"/opt/pidp8/bootscripts/%d.script", swDevice);	// make filename
			printf("\r\n\nRebooting %s\r\n\n", sScript);
			reason = STOP_HALT;
			awfulHackFlag = swDevice;	// this triggers a do command after leaving the simulator run. 
		}

		// 3. Scan for shutdown command (Sing_Step + Sing_inst + Start)

		if (((switchstatus[2] & 0x0800)==0) && ((switchstatus[2] & 0x0010)==0))
		{
			printf("\r\nShutdown\r\n\r\n");
			reason = STOP_HALT;
			awfulHackFlag = 8;	// this triggers an exit command after leaving the simulator run. 
			if(spawn_cmd ((int32) 0, " shutdown -h -t 1 now")!=SCPE_OK)		// issue simh attach command (no sudo in buildroot)
				printf("\r\n\n\nshutdown failed\r\n\n");
		}

		// 4. Scan for host reboot command (Sing_Step + Sing_Inst + Cont)
	
		if (((switchstatus[2] & 0x0080)==0) && ((switchstatus[2] & 0x0010)==0))
		{
			printf("\r\nReboot\r\n\r\n");
			reason = STOP_HALT;
			awfulHackFlag = 8;      // this triggers an exit command after leaving the simulator run. 
			if(spawn_cmd ((int32) 0, " reboot")!=SCPE_OK) {// no sudo in buildroot env
				printf("\r\n\n\nreboot failed\r\n\n");
			}
		}
	
	
		// 5. Scan for mount command (Sing_Step + Sing_Inst + Load Add)
	
		if ((switchstatus[2] & 0x0410)==0)
		{
			printf("\r\nMount\r\n\r\n");
			if(spawn_cmd ((int32) 0, " /opt/pidp8/bin/automount")!=SCPE_OK) {// no sudo in buildroot env
				printf("\r\n\n\nmount USB drive failed\r\n\n");
			}
		}
	
		// 6. Scan for unmount command (Sing_Step + Sing_Inst + Deposit)
	
		if ((switchstatus[2] & 0x0210)==0)
		{
			printf("\r\nUnmount\r\n\r\n");
			if(spawn_cmd ((int32) 0, " /opt/pidp8/bin/unmount")!=SCPE_OK) {// no sudo in buildroot env
				printf("\r\n\n\nunmount failed\r\n\n");
			}
		}
	}
}
if (swAttach==1)		// Sing_Step switch is back to off again
	if ((switchstatus[2] & 0x0020)!=0)		// switch deactivated
		swAttach=0;				// reset 'avoid repeat' indicator

/* ---PiDP end---------------------------------------------------------------------------------------------- */


/* ---PiDP add--------------------------------------------------------------------------------------------- */

if ((switchstatus[2] & 0x0800)==0)	// START switch activated
	if (swStart==0)
	{
	        int_req = int_req & ~INT_ION;		// disable ION. says so in handbook, true?
		LAC = 0;				// Clear LAC;
		// IR = 0 				// clear IR (handbook says so but would be weird)
		MB = 0;					// clear MB. 
		MA = PC & 07777;			// transfer PC into MA  (not necessary because IR is redone in code below?
		swStop = 0;
		swStart = 1;				// single shot
	}
if (swStart==1)
	if ((switchstatus[2] & 0x0800)!=0)		// START switch deactivated
		swStart=0;				// reset 'avoid repeat' indicator


if ((switchstatus[2] & 0x0080)==0)			// CONT switch activated
	if (swCont2==0)
	{	swStop = 0;				// meaning resume execution
			// ? is this done: MB contains instruction to be executed after CONT is pressed
		swCont2 = 1;				// single shot
		goto contPoint;				// note: only for cont not for start
	}
if (swCont2==1)
	if ((switchstatus[2] & 0x0080)!=0)		// CONT switch deactivated
		swCont2=0;				// reset 'avoid repeat' indicator


if ((switchstatus[2] & 0x0400)==0)			// LOAD_ADD switch activated
{
	PC = switchstatus[0] ^ 07777;			// copy SR into PC
							// copy DF and IF too
	DF = (((switchstatus[1] >> 11) & 1)==0?4:0)
	+(((switchstatus[1] >> 10) & 1)==0?2:0)
	+(((switchstatus[1] >> 9) & 1)==0?1:0);
	DF = DF<<12;					// DF is saved in oct digit 5, so it's easy to add to PC

	IF = (((switchstatus[1] >> 8) & 1)==0?4:0)
	+(((switchstatus[1] >> 7) & 1)==0?2:0)
	+(((switchstatus[1] >> 6) & 1)==0?1:0);
	IF = IF<<12;					// DF is saved in oct digit 5, so it's easy to add to PC
}

if ((switchstatus[2] & 0x0200)==0)			// DEP switch activated
{	if (swDep==0)
	{	M[PC] = switchstatus[0] ^ 07777;
		/* ??? in 66 handbook: strictly speaking, SR goes into AC, then AC into MB. Does it clear AC afterwards? If not, needs fix */
		MB = M[PC];
		MA = PC & 07777;			// 20150315: MA trails PC on FP
		PC = (PC + 1) & 07777;			// increment PC
		swDep=1;				// avoid repeat
	}
}
if (swDep==1)
	if ((switchstatus[2] & 0x0200)!=0)		// DEP switch deactivated
		swDep=0;				// reset 'avoid repeat' indicator

if ((switchstatus[2] & 0x0100)==0)			// EXAM switch activated
{	if (swExam==0)
	{	MB = M[PC];
		MA = PC & 07777;			// 20150315: MA trails PC on FP
		PC = (PC + 1) & 07777;			// increment PC
		swExam=1;				// avoid repeat
	}
}
if (swExam==1)
	if ((switchstatus[2] & 0x0100)!=0)		// EXAM switch deactivated
		swExam=0;				// reset 'avoid repeat' indicator


// do what needs to be done in STOP mode:
if (swStop==1) 
{	setleds(PC, MA, MB, LAC, MQ, IF, DF); 		// note MB used in this call, not M[MA]
	sim_interval = sim_interval - 1;		// otherwise, CTRL-E will never be acted upon in stop mode
							// WARNING: THIS MAY LEAD TO TROUBLE. MAYBE?
	goto skip;					// a goto is period correct methinks
}

/* ---PiDP end---------------------------------------------------------------------------------------------- */


    if (int_req > INT_PENDING) {                        /* interrupt? */
        int_req = int_req & ~INT_ION;                   /* interrupts off */
        SF = (UF << 6) | (IF >> 9) | (DF >> 12);        /* form save field */
        IF = IB = DF = UF = UB = 0;                     /* clear mem ext */
        PCQ_ENTRY;                                      /* save old PC */
        M[0] = PC;                                      /* save PC in 0 */
        PC = 1;                                         /* fetch next from 1 */
        }

    MA = IF | PC;                                       /* form PC */
    if (sim_brk_summ && sim_brk_test (MA, SWMASK ('E'))) { /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
	}

    IR = M[MA];                                         /* fetch instruction */

//PC increment was moved down before, bad idea? now is where it originally was.

    PC = (PC + 1) & 07777;                              /* increment PC */

    int_req = int_req | INT_NO_ION_PENDING;             /* clear ION delay */
    sim_interval = sim_interval - 1;


/* ---PiDP add--------------------------------------------------------------------------------------------- */

setleds(PC, MA, M[MA], LAC, MQ, IF, DF); // note M[MA] used not MB

if  ((switchstatus[2] & 0x040)==0)  		// STOP switch activated
{	swStop = 1;
	goto skip;	
}


/* ---PiDP end---------------------------------------------------------------------------------------------- */

//--------------- moved to here:
//no longer    PC = (PC + 1) & 07777;                              /* increment PC */
//---------------


/* ---PiDP add--------------------------------------------------------------------------------------------- */
contPoint:	;	// goto here if CONT has been pressed to finish current instruction

// SING_STEP: swStop=0 if we're here. If SingStep then this time, let it go but trigger a stop on the next pass

if ((switchstatus[2] & 0x0010)==0)		// SING_INST switch activated
{	if (swSingStep==0)		// allow it this time,
		swSingStep=1;		// but note to block it next time!
	else				// else: this is the next time...
	{	swSingStep=0;		// reset flipflop
		swStop=1;		// and do what you do when STOP is pressed.
		goto skip;
	}
}

/* ---PiDP end---------------------------------------------------------------------------------------------- */


/* Instruction decoding.

   The opcode (IR<0:2>), indirect flag (IR<3>), and page flag (IR<4>)
   are decoded together.  This produces 32 decode points, four per
   major opcode.  For IOT, the extra decode points are not useful;
   for OPR, only the group flag (IR<3>) is used.

   AND, TAD, ISZ, DCA calculate a full 15b effective address.
   JMS, JMP calculate a 12b field-relative effective address.

   Autoindex calculations always occur within the same field as the
   instruction fetch.  The field must exist; otherwise, the instruction
   fetched would be 0000, and indirect addressing could not occur.

   Note that MA contains IF'PC.
*/

/* --------------------------------------------------------------------------------------------------------- */
ledstatus[5] |= 1<<2; // set execute
ledstatus[5] &= ~(1<<3); // clear fetch
/* --------------------------------------------------------------------------------------------------------- */


    if (hst_lnt) {                                      /* history enabled? */
        int32 ea;

        hst_p = (hst_p + 1);                            /* next entry */
        if (hst_p >= hst_lnt)
            hst_p = 0;
        hst[hst_p].pc = MA | HIST_PC;                   /* save PC, IR, LAC, MQ */
        hst[hst_p].ir = IR;
        hst[hst_p].lac = LAC;
        hst[hst_p].mq = MQ;
        if (IR < 06000) {                               /* mem ref? */
            if (IR & 0200)
                ea = (MA & 077600) | (IR & 0177);
            else ea = IF | (IR & 0177);                 /* direct addr */
            if (IR & 0400) {                            /* indirect? */
                if (IR < 04000) {                       /* mem operand? */
                    if ((ea & 07770) != 00010)
                        ea = DF | M[ea];
                    else ea = DF | ((M[ea] + 1) & 07777);
                    }
                else {                                  /* no, jms/jmp */
                    if ((ea & 07770) != 00010)
                        ea = IB | M[ea];
                    else ea = IB | ((M[ea] + 1) & 07777);
                    }
                }
            hst[hst_p].ea = ea;                         /* save eff addr */
            hst[hst_p].opnd = M[ea];                    /* save operand */
            }
        }

switch ((IR >> 7) & 037) {                              /* decode IR<0:4> */

/* Opcode 0, AND */

    case 000:                                           /* AND, dir, zero */
        MA = IF | (IR & 0177);                          /* dir addr, page zero */
        LAC = LAC & (M[MA] | 010000);
        break;

    case 001:                                           /* AND, dir, curr */
        MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
        LAC = LAC & (M[MA] | 010000);
        break;

    case 002:                                           /* AND, indir, zero */
        MA = IF | (IR & 0177);                          /* dir addr, page zero */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = DF | M[MA];
        else MA = DF | (M[MA] = (M[MA] + 1) & 07777);   /* incr before use */
        LAC = LAC & (M[MA] | 010000);
        break;

    case 003:                                           /* AND, indir, curr */
        MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = DF | M[MA];
        else MA = DF | (M[MA] = (M[MA] + 1) & 07777);   /* incr before use */
        LAC = LAC & (M[MA] | 010000);
        break;

/* Opcode 1, TAD */

    case 004:                                           /* TAD, dir, zero */
        MA = IF | (IR & 0177);                          /* dir addr, page zero */
        LAC = (LAC + M[MA]) & 017777;
        break;

    case 005:                                           /* TAD, dir, curr */
        MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
        LAC = (LAC + M[MA]) & 017777;
        break;

    case 006:                                           /* TAD, indir, zero */
        MA = IF | (IR & 0177);                          /* dir addr, page zero */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = DF | M[MA];
        else MA = DF | (M[MA] = (M[MA] + 1) & 07777);   /* incr before use */
        LAC = (LAC + M[MA]) & 017777;
        break;

    case 007:                                           /* TAD, indir, curr */
        MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = DF | M[MA];
        else MA = DF | (M[MA] = (M[MA] + 1) & 07777);   /* incr before use */
        LAC = (LAC + M[MA]) & 017777;
        break;

/* Opcode 2, ISZ */

    case 010:                                           /* ISZ, dir, zero */
        MA = IF | (IR & 0177);                          /* dir addr, page zero */
        M[MA] = MB = (M[MA] + 1) & 07777;               /* field must exist */
        if (MB == 0)
            PC = (PC + 1) & 07777;
        break;

    case 011:                                           /* ISZ, dir, curr */
        MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
        M[MA] = MB = (M[MA] + 1) & 07777;               /* field must exist */
        if (MB == 0)
            PC = (PC + 1) & 07777;
        break;

    case 012:                                           /* ISZ, indir, zero */
        MA = IF | (IR & 0177);                          /* dir addr, page zero */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = DF | M[MA];
        else MA = DF | (M[MA] = (M[MA] + 1) & 07777);   /* incr before use */
        MB = (M[MA] + 1) & 07777;
        if (MEM_ADDR_OK (MA))
            M[MA] = MB;
        if (MB == 0)
            PC = (PC + 1) & 07777;
        break;

    case 013:                                           /* ISZ, indir, curr */
        MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = DF | M[MA];
        else MA = DF | (M[MA] = (M[MA] + 1) & 07777);   /* incr before use */
        MB = (M[MA] + 1) & 07777;
        if (MEM_ADDR_OK (MA))
            M[MA] = MB;
        if (MB == 0)
            PC = (PC + 1) & 07777;
        break;

/* Opcode 3, DCA */

    case 014:                                           /* DCA, dir, zero */
        MA = IF | (IR & 0177);                          /* dir addr, page zero */
        M[MA] = LAC & 07777;
        LAC = LAC & 010000;
        break;

    case 015:                                           /* DCA, dir, curr */
        MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
        M[MA] = LAC & 07777;
        LAC = LAC & 010000;
        break;

    case 016:                                           /* DCA, indir, zero */
        MA = IF | (IR & 0177);                          /* dir addr, page zero */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = DF | M[MA];
        else MA = DF | (M[MA] = (M[MA] + 1) & 07777);   /* incr before use */
        if (MEM_ADDR_OK (MA))
            M[MA] = LAC & 07777;
        LAC = LAC & 010000;
        break;

    case 017:                                           /* DCA, indir, curr */
        MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = DF | M[MA];
        else MA = DF | (M[MA] = (M[MA] + 1) & 07777);   /* incr before use */
        if (MEM_ADDR_OK (MA))
            M[MA] = LAC & 07777;
        LAC = LAC & 010000;
        break;

/* Opcode 4, JMS.  From Bernhard Baehr's description of the TSC8-75:

   (In user mode) the current JMS opcode is moved to the ERIOT register, the ECDF
   flag is cleared. The address of the JMS instruction is loaded into the ERTB
   register and the TSC8-75 I/O flag is raised. When the TSC8-75 is enabled, the
   target addess of the JMS is loaded into PC, but nothing else (loading of IF, UF,
   clearing the interrupt inhibit flag, storing of the return address in the first
   word of the subroutine) happens. When the TSC8-75 is disabled, the JMS is performed
   as usual. */

    case 020:                                           /* JMS, dir, zero */
        PCQ_ENTRY;
        MA = IR & 0177;                                 /* dir addr, page zero */
        if (UF) {                                       /* user mode? */
            tsc_ir = IR;                                /* save instruction */
            tsc_cdf = 0;                                /* clear flag */
            }
        if (UF && tsc_enb) {                            /* user mode, TSC enab? */
            tsc_pc = (PC - 1) & 07777;                  /* save PC */
            int_req = int_req | INT_TSC;                /* request intr */
            }
        else {                                          /* normal */
            IF = IB;                                    /* change IF */
            UF = UB;                                    /* change UF */
            int_req = int_req | INT_NO_CIF_PENDING;     /* clr intr inhibit */
            MA = IF | MA;
            if (MEM_ADDR_OK (MA))
                M[MA] = PC;
            }
        PC = (MA + 1) & 07777;
        break;

    case 021:                                           /* JMS, dir, curr */
        PCQ_ENTRY;
        MA = (MA & 007600) | (IR & 0177);               /* dir addr, curr page */
        if (UF) {                                       /* user mode? */
            tsc_ir = IR;                                /* save instruction */
            tsc_cdf = 0;                                /* clear flag */
            }
        if (UF && tsc_enb) {                            /* user mode, TSC enab? */
            tsc_pc = (PC - 1) & 07777;                  /* save PC */
            int_req = int_req | INT_TSC;                /* request intr */
            }
        else {                                          /* normal */
            IF = IB;                                    /* change IF */
            UF = UB;                                    /* change UF */
            int_req = int_req | INT_NO_CIF_PENDING;     /* clr intr inhibit */
            MA = IF | MA;
            if (MEM_ADDR_OK (MA))
                M[MA] = PC;
            }
        PC = (MA + 1) & 07777;
        break;

    case 022:                                           /* JMS, indir, zero */
        PCQ_ENTRY;
        MA = IF | (IR & 0177);                          /* dir addr, page zero */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = M[MA];
        else MA = (M[MA] = (M[MA] + 1) & 07777);        /* incr before use */
        if (UF) {                                       /* user mode? */
            tsc_ir = IR;                                /* save instruction */
            tsc_cdf = 0;                                /* clear flag */
            }
        if (UF && tsc_enb) {                            /* user mode, TSC enab? */
            tsc_pc = (PC - 1) & 07777;                  /* save PC */
            int_req = int_req | INT_TSC;                /* request intr */
            }
        else {                                          /* normal */
            IF = IB;                                    /* change IF */
            UF = UB;                                    /* change UF */
            int_req = int_req | INT_NO_CIF_PENDING;     /* clr intr inhibit */
            MA = IF | MA;
            if (MEM_ADDR_OK (MA))
                M[MA] = PC;
            }
        PC = (MA + 1) & 07777;
        break;

    case 023:                                           /* JMS, indir, curr */
        PCQ_ENTRY;
        MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = M[MA];
        else MA = (M[MA] = (M[MA] + 1) & 07777);        /* incr before use */
        if (UF) {                                       /* user mode? */
            tsc_ir = IR;                                /* save instruction */
            tsc_cdf = 0;                                /* clear flag */
            }
        if (UF && tsc_enb) {                            /* user mode, TSC enab? */
            tsc_pc = (PC - 1) & 07777;                  /* save PC */
            int_req = int_req | INT_TSC;                /* request intr */
            }
        else {                                          /* normal */
            IF = IB;                                    /* change IF */
            UF = UB;                                    /* change UF */
            int_req = int_req | INT_NO_CIF_PENDING;     /* clr intr inhibit */
            MA = IF | MA;
            if (MEM_ADDR_OK (MA))
                M[MA] = PC;
            }
        PC = (MA + 1) & 07777;
        break;

/* Opcode 5, JMP.  From Bernhard Baehr's description of the TSC8-75:

   (In user mode) the current JMP opcode is moved to the ERIOT register, the ECDF
   flag is cleared. The address of the JMP instruction is loaded into the ERTB
   register and the TSC8-75 I/O flag is raised. Then the JMP is performed as usual
   (including the setting of IF, UF and clearing the interrupt inhibit flag). */


    case 024:                                           /* JMP, dir, zero */
        PCQ_ENTRY;
        MA = IR & 0177;                                 /* dir addr, page zero */
        if (UF) {                                       /* user mode? */
            tsc_ir = IR;                                /* save instruction */
            tsc_cdf = 0;                                /* clear flag */
            if (tsc_enb) {                              /* TSC8 enabled? */
                tsc_pc = (PC - 1) & 07777;              /* save PC */
                int_req = int_req | INT_TSC;            /* request intr */
                }
            }
        IF = IB;                                        /* change IF */
        UF = UB;                                        /* change UF */
        int_req = int_req | INT_NO_CIF_PENDING;         /* clr intr inhibit */
        PC = MA;
        break;

/* If JMP direct, also check for idle (KSF/JMP *-1) and infinite loop */

    case 025:                                           /* JMP, dir, curr */
        PCQ_ENTRY;
        MA = (MA & 007600) | (IR & 0177);               /* dir addr, curr page */
        if (UF) {                                       /* user mode? */
            tsc_ir = IR;                                /* save instruction */
            tsc_cdf = 0;                                /* clear flag */
            if (tsc_enb) {                              /* TSC8 enabled? */
                tsc_pc = (PC - 1) & 07777;              /* save PC */
                int_req = int_req | INT_TSC;            /* request intr */
                }
            }
        if (sim_idle_enab &&                            /* idling enabled? */
            (IF == IB)) {                               /* to same bank? */
            if (MA == ((PC - 2) & 07777)) {             /* 1) JMP *-1? */
                if (!(int_req & (INT_ION|INT_TTI)) &&   /*    iof, TTI flag off? */
                    (M[IB|((PC - 2) & 07777)] == OP_KSF)) /*  next is KSF? */
                    sim_idle (TMR_CLK, FALSE);          /* we're idle */
                }                                       /* end JMP *-1 */
            else if (MA == ((PC - 1) & 07777)) {        /* 2) JMP *? */
                if (!(int_req & INT_ION))               /*    iof? */
                    reason = STOP_LOOP;                 /* then infinite loop */
                else if (!(int_req & INT_ALL))          /*    ion, not intr? */
                    sim_idle (TMR_CLK, FALSE);          /* we're idle */
                }                                       /* end JMP */
            }                                           /* end idle enabled */
        IF = IB;                                        /* change IF */
        UF = UB;                                        /* change UF */
        int_req = int_req | INT_NO_CIF_PENDING;         /* clr intr inhibit */
        PC = MA;
        break;

    case 026:                                           /* JMP, indir, zero */
        PCQ_ENTRY;
        MA = IF | (IR & 0177);                          /* dir addr, page zero */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = M[MA];
        else MA = (M[MA] = (M[MA] + 1) & 07777);        /* incr before use */
        if (UF) {                                       /* user mode? */
            tsc_ir = IR;                                /* save instruction */
            tsc_cdf = 0;                                /* clear flag */
            if (tsc_enb) {                              /* TSC8 enabled? */
                tsc_pc = (PC - 1) & 07777;              /* save PC */
                int_req = int_req | INT_TSC;            /* request intr */
                }
            }
        IF = IB;                                        /* change IF */
        UF = UB;                                        /* change UF */
        int_req = int_req | INT_NO_CIF_PENDING;         /* clr intr inhibit */
        PC = MA;
        break;

    case 027:                                           /* JMP, indir, curr */
        PCQ_ENTRY;
        MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
        if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
            MA = M[MA];
        else MA = (M[MA] = (M[MA] + 1) & 07777);        /* incr before use */
        if (UF) {                                       /* user mode? */
            tsc_ir = IR;                                /* save instruction */
            tsc_cdf = 0;                                /* clear flag */
            if (tsc_enb) {                              /* TSC8 enabled? */
                tsc_pc = (PC - 1) & 07777;              /* save PC */
                int_req = int_req | INT_TSC;            /* request intr */
                }
            }
        IF = IB;                                        /* change IF */
        UF = UB;                                        /* change UF */
        int_req = int_req | INT_NO_CIF_PENDING;         /* clr intr inhibit */
        PC = MA;
        break;

/* Opcode 7, OPR group 1 */

    case 034:case 035:                                  /* OPR, group 1 */
        switch ((IR >> 4) & 017) {                      /* decode IR<4:7> */
        case 0:                                         /* nop */
            break;
        case 1:                                         /* CML */
            LAC = LAC ^ 010000;
            break;
        case 2:                                         /* CMA */
            LAC = LAC ^ 07777;
            break;
        case 3:                                         /* CMA CML */
            LAC = LAC ^ 017777;
            break;
        case 4:                                         /* CLL */
            LAC = LAC & 07777;
            break;
        case 5:                                         /* CLL CML = STL */
            LAC = LAC | 010000;
            break;
        case 6:                                         /* CLL CMA */
            LAC = (LAC ^ 07777) & 07777;
            break;
        case 7:                                         /* CLL CMA CML */
            LAC = (LAC ^ 07777) | 010000;
            break;
        case 010:                                       /* CLA */
            LAC = LAC & 010000;
            break;
        case 011:                                       /* CLA CML */
            LAC = (LAC & 010000) ^ 010000;
            break;
        case 012:                                       /* CLA CMA = STA */
            LAC = LAC | 07777;
            break;
        case 013:                                       /* CLA CMA CML */
            LAC = (LAC | 07777) ^ 010000;
            break;
        case 014:                                       /* CLA CLL */
            LAC = 0;
            break;
        case 015:                                       /* CLA CLL CML */
            LAC = 010000;
            break;
        case 016:                                       /* CLA CLL CMA */
            LAC = 07777;
            break;
        case 017:                                       /* CLA CLL CMA CML */
            LAC = 017777;
            break;
            }                                           /* end switch opers */

        if (IR & 01)                                    /* IAC */
            LAC = (LAC + 1) & 017777;
        switch ((IR >> 1) & 07) {                       /* decode IR<8:10> */
        case 0:                                         /* nop */
            break;
        case 1:                                         /* BSW */
            LAC = (LAC & 010000) | ((LAC >> 6) & 077) | ((LAC & 077) << 6);
            break;
        case 2:                                         /* RAL */
            LAC = ((LAC << 1) | (LAC >> 12)) & 017777;
            break;
        case 3:                                         /* RTL */
            LAC = ((LAC << 2) | (LAC >> 11)) & 017777;
            break;
        case 4:                                         /* RAR */
            LAC = ((LAC >> 1) | (LAC << 12)) & 017777;
            break;
        case 5:                                         /* RTR */
            LAC = ((LAC >> 2) | (LAC << 11)) & 017777;
            break;
        case 6:                                         /* RAL RAR - undef */
            LAC = LAC & (IR | 010000);                  /* uses AND path */
            break;
        case 7:                                         /* RTL RTR - undef */
            LAC = (LAC & 010000) | (MA & 07600) | (IR & 0177);
            break;                                      /* uses address path */
            }                                           /* end switch shifts */
        break;                                          /* end group 1 */

/* OPR group 2.  From Bernhard Baehr's description of the TSC8-75:

   (In user mode) HLT (7402), OSR (7404) and microprogrammed combinations with
   HLT and OSR: Additional to raising a user mode interrupt, the current OPR
   opcode is moved to the ERIOT register and the ECDF flag is cleared. */

    case 036:case 037:                                  /* OPR, groups 2, 3 */
        if ((IR & 01) == 0) {                           /* group 2 */
            switch ((IR >> 3) & 017) {                  /* decode IR<6:8> */
            case 0:                                     /* nop */
                break;
            case 1:                                     /* SKP */
                PC = (PC + 1) & 07777;
                break;
            case 2:                                     /* SNL */
                if (LAC >= 010000)
                    PC = (PC + 1) & 07777;
                break;
            case 3:                                     /* SZL */
                if (LAC < 010000)
                    PC = (PC + 1) & 07777;
                break;
            case 4:                                     /* SZA */
                if ((LAC & 07777) == 0)
                    PC = (PC + 1) & 07777;
                break;
            case 5:                                     /* SNA */
                if ((LAC & 07777)
                    != 0) PC = (PC + 1) & 07777;
                break;
            case 6:                                     /* SZA | SNL */
                if ((LAC == 0) || (LAC >= 010000))
                    PC = (PC + 1) & 07777;
                break;
            case 7:                                     /* SNA & SZL */
                if ((LAC != 0) && (LAC < 010000))
                    PC = (PC + 1) & 07777;
                break;
            case 010:                                   /* SMA */
                if ((LAC & 04000) != 0)
                    PC = (PC + 1) & 07777;
                break;
            case 011:                                   /* SPA */
                if ((LAC & 04000) == 0)
                    PC = (PC + 1) & 07777;
                break;
            case 012:                                   /* SMA | SNL */
                if (LAC >= 04000)
                    PC = (PC + 1) & 07777;
                break;
            case 013:                                   /* SPA & SZL */
                if (LAC < 04000)
                    PC = (PC + 1) & 07777;
                break;
            case 014:                                   /* SMA | SZA */
                if (((LAC & 04000) != 0) || ((LAC & 07777) == 0))
                    PC = (PC + 1) & 07777;
                break;
            case 015:                                   /* SPA & SNA */
                if (((LAC & 04000) == 0) && ((LAC & 07777) != 0))
                    PC = (PC + 1) & 07777;
                break;
            case 016:                                   /* SMA | SZA | SNL */
                if ((LAC >= 04000) || (LAC == 0))
                    PC = (PC + 1) & 07777;
                break;
            case 017:                                   /* SPA & SNA & SZL */
                if ((LAC < 04000) && (LAC != 0))
                    PC = (PC + 1) & 07777;
                break;
                }                                       /* end switch skips */
            if (IR & 0200)                              /* CLA */
                LAC = LAC & 010000;
            if ((IR & 06) && UF) {                      /* user mode? */
                int_req = int_req | INT_UF;             /* request intr */
                tsc_ir = IR;                            /* save instruction */
                tsc_cdf = 0;                            /* clear flag */
                }
            else {
                if (IR & 04){                           /* OSR */
//--- PiDP bug fix 20150822----------------------------------------------------------------
//OSR never got updated when PDP-8 is running
//separate bug, not fixed yet: OSR should be updated by DEP and LOAD_ADD switch handlers I think
// OSR should probably be loaded with switchstatus[0] in every cycle. Doing it here is just a temp fix.
//-----------------------------------------------------------------------------------------

 		    OSR = switchstatus[0] ^ 07777;
                    LAC = LAC | OSR;
		}
                if (IR & 02)                            /* HLT */
//--- PiDP change--------------------------------------------------------------------------
//                    reason = STOP_HALT;
//-----------------------------------------------------------------------------------------
			swStop=1;	// don't step out of simulation, just do STOP
//--- end of PiDP change--------------------------------------------------------------------------

                }
            break;
            }                                           /* end if group 2 */

/* OPR group 3 standard

   MQA!MQL exchanges AC and MQ, as follows:

        temp = MQ;
        MQ = LAC & 07777;
        LAC = LAC & 010000 | temp;
*/

        temp = MQ;                                      /* group 3 */
        if (IR & 0200)                                  /* CLA */
            LAC = LAC & 010000;
        if (IR & 0020) {                                /* MQL */
            MQ = LAC & 07777;
            LAC = LAC & 010000;
            }
        if (IR & 0100)                                  /* MQA */
            LAC = LAC | temp;
        if ((IR & 0056) && (cpu_unit.flags & UNIT_NOEAE)) {
            reason = stop_inst;                         /* EAE not present */
            break;
            }

/* OPR group 3 EAE

   The EAE operates in two modes:

        Mode A, PDP-8/I compatible
        Mode B, extended capability

   Mode B provides eight additional subfunctions; in addition, some
   of the Mode A functions operate differently in Mode B.

   The mode switch instructions are decoded explicitly and cannot be
   microprogrammed with other EAE functions (SWAB performs an MQL as
   part of standard group 3 decoding).  If mode switching is decoded,
   all other EAE timing is suppressed.
*/

        if (IR == 07431) {                              /* SWAB */
            emode = 1;                                  /* set mode flag */
            break;
            }
        if (IR == 07447) {                              /* SWBA */
            emode = gtf = 0;                            /* clear mode, gtf */
            break;
            }

/* If not switching modes, the EAE operation is determined by the mode
   and IR<6,8:10>:

   <6:10>       mode A          mode B          comments

   0x000        NOP             NOP
   0x001        SCL             ACS
   0x010        MUY             MUY             if mode B, next = address
   0x011        DVI             DVI             if mode B, next = address
   0x100        NMI             NMI             if mode B, clear AC if
                                                 result = 4000'0000
   0x101        SHL             SHL             if mode A, extra shift
   0x110        ASR             ASR             if mode A, extra shift
   0x111        LSR             LSR             if mode A, extra shift
   1x000        SCA             SCA
   1x001        SCA + SCL       DAD
   1x010        SCA + MUY       DST
   1x011        SCA + DVI       SWBA            NOP if not detected earlier
   1x100        SCA + NMI       DPSZ            
   1x101        SCA + SHL       DPIC            must be combined with MQA!MQL
   1x110        SCA + ASR       DCM             must be combined with MQA!MQL
   1x111        SCA + LSR       SAM

   EAE instructions which fetch memory operands use the CPU's DEFER
   state to read the first word; if the address operand is in locations
   x0010 - x0017, it is autoincremented.
*/

        if (emode == 0)                                 /* mode A? clr gtf */
            gtf = 0;
        switch ((IR >> 1) & 027) {                      /* decode IR<6,8:10> */

        case 020:                                       /* mode A, B: SCA */
            LAC = LAC | SC;
            break;
        case 000:                                       /* mode A, B: NOP */
            break;

        case 021:                                       /* mode B: DAD */
            if (emode) {
                MA = IF | PC;
                if ((MA & 07770) != 00010)              /* indirect; autoinc? */
                    MA = DF | M[MA];
                else MA = DF | (M[MA] = (M[MA] + 1) & 07777); /* incr before use */
                MQ = MQ + M[MA];
                MA = DF | ((MA + 1) & 07777);
                LAC = (LAC & 07777) + M[MA] + (MQ >> 12);
                MQ = MQ & 07777;
                PC = (PC + 1) & 07777;
                break;
                }
            LAC = LAC | SC;                             /* mode A: SCA then */
        case 001:                                       /* mode B: ACS */
            if (emode) {
                SC = LAC & 037;
                LAC = LAC & 010000;
                }
            else {                                      /* mode A: SCL */
                SC = (~M[IF | PC]) & 037;
                PC = (PC + 1) & 07777;
                }
            break;

        case 022:                                       /* mode B: DST */
            if (emode) {
                MA = IF | PC;
                if ((MA & 07770) != 00010)              /* indirect; autoinc? */
                    MA = DF | M[MA];
                else MA = DF | (M[MA] = (M[MA] + 1) & 07777); /* incr before use */
                if (MEM_ADDR_OK (MA))
                    M[MA] = MQ & 07777;
                MA = DF | ((MA + 1) & 07777);
                if (MEM_ADDR_OK (MA))
                    M[MA] = LAC & 07777;
                PC = (PC + 1) & 07777;
                break;
                }
            LAC = LAC | SC;                             /* mode A: SCA then */
        case 002:                                       /* MUY */
            MA = IF | PC;
            if (emode) {                                /* mode B: defer */
                if ((MA & 07770) != 00010)              /* indirect; autoinc? */
                    MA = DF | M[MA];
                else MA = DF | (M[MA] = (M[MA] + 1) & 07777); /* incr before use */
                }
            temp = (MQ * M[MA]) + (LAC & 07777);
            LAC = (temp >> 12) & 07777;
            MQ = temp & 07777;
            PC = (PC + 1) & 07777;
            SC = 014;                                   /* 12 shifts */
            break;

        case 023:                                       /* mode B: SWBA */
            if (emode)
                break;
            LAC = LAC | SC;                             /* mode A: SCA then */
        case 003:                                       /* DVI */
            MA = IF | PC;
            if (emode) {                                /* mode B: defer */
                if ((MA & 07770) != 00010)              /* indirect; autoinc? */
                    MA = DF | M[MA];
                else MA = DF | (M[MA] = (M[MA] + 1) & 07777); /* incr before use */
                }
            if ((LAC & 07777) >= M[MA]) {               /* overflow? */
                LAC = LAC | 010000;                     /* set link */
                MQ = ((MQ << 1) + 1) & 07777;           /* rotate MQ */
                SC = 0;                                 /* no shifts */
                }
            else {
                temp = ((LAC & 07777) << 12) | MQ;
                MQ = temp / M[MA];
                LAC = temp % M[MA];
                SC = 015;                               /* 13 shifts */
                }
            PC = (PC + 1) & 07777;
            break;

        case 024:                                       /* mode B: DPSZ */
            if (emode) {
                if (((LAC | MQ) & 07777) == 0)
                    PC = (PC + 1) & 07777;
                break;
                }
            LAC = LAC | SC;                             /* mode A: SCA then */
        case 004:                                       /* NMI */
            temp = (LAC << 12) | MQ;                    /* preserve link */
            for (SC = 0; ((temp & 017777777) != 0) &&
                (temp & 040000000) == ((temp << 1) & 040000000); SC++)
                temp = temp << 1;
            LAC = (temp >> 12) & 017777;
            MQ = temp & 07777;
            if (emode && ((LAC & 07777) == 04000) && (MQ == 0))
                LAC = LAC & 010000;                     /* clr if 4000'0000 */
            break;

        case 025:                                       /* mode B: DPIC */
            if (emode) {
                temp = (LAC + 1) & 07777;               /* SWP already done! */
                LAC = MQ + (temp == 0);
                MQ = temp;
                break;
                }
            LAC = LAC | SC;                             /* mode A: SCA then */
        case 5:                                         /* SHL */
            SC = (M[IF | PC] & 037) + (emode ^ 1);      /* shift+1 if mode A */
            if (SC > 25)                                /* >25? result = 0 */
                temp = 0;
            else temp = ((LAC << 12) | MQ) << SC;       /* <=25? shift LAC:MQ */
            LAC = (temp >> 12) & 017777;
            MQ = temp & 07777;
            PC = (PC + 1) & 07777;
            SC = emode? 037: 0;                         /* SC = 0 if mode A */
            break;

        case 026:                                       /* mode B: DCM */
            if (emode) {
                temp = (-LAC) & 07777;                  /* SWP already done! */
                LAC = (MQ ^ 07777) + (temp == 0);
                MQ = temp;
                break;
                }
            LAC = LAC | SC;                             /* mode A: SCA then */
        case 6:                                         /* ASR */
            SC = (M[IF | PC] & 037) + (emode ^ 1);      /* shift+1 if mode A */
            temp = ((LAC & 07777) << 12) | MQ;          /* sext from AC0 */
            if (LAC & 04000)
                temp = temp | ~037777777;
            if (emode && (SC != 0))
                gtf = (temp >> (SC - 1)) & 1;
            if (SC > 25)
                temp = (LAC & 04000)? -1: 0;
            else temp = temp >> SC;
            LAC = (temp >> 12) & 017777;
            MQ = temp & 07777;
            PC = (PC + 1) & 07777;
            SC = emode? 037: 0;                         /* SC = 0 if mode A */
            break;

        case 027:                                       /* mode B: SAM */
            if (emode) {
                temp = LAC & 07777;
                LAC = MQ + (temp ^ 07777) + 1;          /* L'AC = MQ - AC */
                gtf = (temp <= MQ) ^ ((temp ^ MQ) >> 11);
                break;
                }
            LAC = LAC | SC;                             /* mode A: SCA then */
        case 7:                                         /* LSR */
            SC = (M[IF | PC] & 037) + (emode ^ 1);      /* shift+1 if mode A */
            temp = ((LAC & 07777) << 12) | MQ;          /* clear link */
            if (emode && (SC != 0))
                gtf = (temp >> (SC - 1)) & 1;
            if (SC > 24)                                /* >24? result = 0 */
                temp = 0;
            else temp = temp >> SC;                     /* <=24? shift AC:MQ */
            LAC = (temp >> 12) & 07777;
            MQ = temp & 07777;
            PC = (PC + 1) & 07777;
            SC = emode? 037: 0;                         /* SC = 0 if mode A */
            break;
            }                                           /* end switch */
        break;                                          /* end case 7 */

/* Opcode 6, IOT.  From Bernhard Baehr's description of the TSC8-75:

   (In user mode) Additional to raising a user mode interrupt, the current IOT
   opcode is moved to the ERIOT register. When the IOT is a CDF instruction (62x1),
   the ECDF flag is set, otherwise it is cleared. */

    case 030:case 031:case 032:case 033:                /* IOT */
        if (UF) {                                       /* privileged? */
            int_req = int_req | INT_UF;                 /* request intr */
            tsc_ir = IR;                                /* save instruction */
            if ((IR & 07707) == 06201)                  /* set/clear flag */
                tsc_cdf = 1;
            else tsc_cdf = 0;
            break;
            }
        device = (IR >> 3) & 077;                       /* device = IR<3:8> */

/* --------------------------------------------------------------------------------------------------------- */
// the IOT ION, IOF do not light pause, anything else does:
if (device>02)
	ledstatus[6] |= 1<<8; // set pause led
/* --------------------------------------------------------------------------------------------------------- */

        pulse = IR & 07;                                /* pulse = IR<9:11> */
        iot_data = LAC & 07777;                         /* AC unchanged */
        switch (device) {                               /* decode IR<3:8> */

        case 000:                                       /* CPU control */
            switch (pulse) {                            /* decode IR<9:11> */

            case 0:                                     /* SKON */
                if (int_req & INT_ION)
                    PC = (PC + 1) & 07777;
                int_req = int_req & ~INT_ION;
                break;

            case 1:                                     /* ION */
                int_req = (int_req | INT_ION) & ~INT_NO_ION_PENDING;
                break;

            case 2:                                     /* IOF */
                int_req = int_req & ~INT_ION;
                break;

            case 3:                                     /* SRQ */
                if (int_req & INT_ALL)
                    PC = (PC + 1) & 07777;
                break;

            case 4:                                     /* GTF */
                LAC = (LAC & 010000) |
                      ((LAC & 010000) >> 1) | (gtf << 10) |
                      (((int_req & INT_ALL) != 0) << 9) |
                      (((int_req & INT_ION) != 0) << 7) | SF;
                break;

            case 5:                                     /* RTF */
                gtf = ((LAC & 02000) >> 10);
                UB = (LAC & 0100) >> 6;
                IB = (LAC & 0070) << 9;
                DF = (LAC & 0007) << 12;
                LAC = ((LAC & 04000) << 1) | iot_data;
                int_req = (int_req | INT_ION) & ~INT_NO_CIF_PENDING;
                break;

            case 6:                                     /* SGT */
                if (gtf)
                    PC = (PC + 1) & 07777;
                break;

            case 7:                                     /* CAF */
                gtf = 0;
                emode = 0;
                int_req = int_req & INT_NO_CIF_PENDING;
                dev_done = 0;
                int_enable = INT_INIT_ENABLE;
                LAC = 0;
                reset_all (1);                          /* reset all dev */
                break;
                }                                       /* end switch pulse */
            break;                                      /* end case 0 */

        case 020:case 021:case 022:case 023:
        case 024:case 025:case 026:case 027:            /* memory extension */

/* --------------------------------------------------------------------------------------------------------- */
// Memory extension does not trigger IOP pauses --> do not light pause
/* --------------------------------------------------------------------------------------------------------- */

            switch (pulse) {                            /* decode IR<9:11> */

            case 1:                                     /* CDF */
                DF = (IR & 0070) << 9;
                break;

            case 2:                                     /* CIF */
                IB = (IR & 0070) << 9;
                int_req = int_req & ~INT_NO_CIF_PENDING;
                break;

            case 3:                                     /* CDF CIF */
                DF = IB = (IR & 0070) << 9;
                int_req = int_req & ~INT_NO_CIF_PENDING;
                break;

            case 4:
                switch (device & 07) {                  /* decode IR<6:8> */

                case 0:                                 /* CINT */
                    int_req = int_req & ~INT_UF;
                    break;

                case 1:                                 /* RDF */
                    LAC = LAC | (DF >> 9);
                        break;

                case 2:                                 /* RIF */
                    LAC = LAC | (IF >> 9);
                    break;

                case 3:                                 /* RIB */
                    LAC = LAC | SF;
                    break;

                case 4:                                 /* RMF */
                    UB = (SF & 0100) >> 6;
                    IB = (SF & 0070) << 9;
                    DF = (SF & 0007) << 12;
                    int_req = int_req & ~INT_NO_CIF_PENDING;
                    break;

                case 5:                                 /* SINT */
                    if (int_req & INT_UF)
                        PC = (PC + 1) & 07777;
                    break;

                case 6:                                 /* CUF */
                    UB = 0;
                    int_req = int_req & ~INT_NO_CIF_PENDING;
                    break;

                case 7:                                 /* SUF */
                    UB = 1;
                    int_req = int_req & ~INT_NO_CIF_PENDING;
                    break;
                    }                                   /* end switch device */
                break;
            
            default:
                reason = stop_inst;
                break;
                }                                       /* end switch pulse */
            break;                                      /* end case 20-27 */

        case 010:                                       /* power fail */
            switch (pulse) {                            /* decode IR<9:11> */

            case 1:                                     /* SBE */
                break;

            case 2:                                     /* SPL */
                if (int_req & INT_PWR)
                    PC = (PC + 1) & 07777;
                break;

            case 3:                                     /* CAL */
                int_req = int_req & ~INT_PWR;
                break;

            default:
                reason = stop_inst;
                break;
                }                                       /* end switch pulse */
            break;                                      /* end case 10 */

        default:                                        /* I/O device */
            if (dev_tab[device]) {                      /* dev present? */

/* --------------------------------------------------------------------------------------------------------- */
// Any other device will trigger IOP --> light pause:
ledstatus[6] |= 1<<8; // set pause led
/* --------------------------------------------------------------------------------------------------------- */

                iot_data = dev_tab[device] (IR, iot_data);
                LAC = (LAC & 010000) | (iot_data & 07777);
                if (iot_data & IOT_SKP)
                    PC = (PC + 1) & 07777;
/* --------------------------------------------------------------------------------------------------------- */
		else
		{
// simh does not distinguish between the various Data Break types. 
// WC, CA and Break are lit up jointly on the PiDP. Although this can be improved upon.

//ledstatus[5] |= 1<<0; // set WC led
//ledstatus[6] |= 1<<11; // set CA led
//ledstatus[6] |= 1<<10; // set Break led
		}
/* --------------------------------------------------------------------------------------------------------- */

                if (iot_data >= IOT_REASON)
                    reason = iot_data >> IOT_V_REASON;
                }
            else reason = stop_inst;                    /* stop on flag */
            break;
            }                                           /* end switch device */
/* --------------------------------------------------------------------------------------------------------- */
ledstatus[6] &= ~(1<<8); // clear pause led

ledstatus[5] &= ~(1<<0); // clear WC led
ledstatus[6] &= ~(1<<11); // clear CA led
ledstatus[6] &= ~(1<<10); // clear Break led
/* --------------------------------------------------------------------------------------------------------- */
        break;                                          /* end case IOT */
        }                                               /* end switch opcode */

/* ------------------------------------------------------------------------- */
skip:	;	// goto label
/* ------------------------------------------------------------------------- */

    }                                                   /* end while */

/* Simulation halted */

saved_PC = IF | (PC & 07777);                           /* save copies */
saved_DF = DF & 070000;
saved_LAC = LAC & 017777;
saved_MQ = MQ & 07777;
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
return reason;
}                                                       /* end sim_instr */

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int_req = (int_req & ~INT_ION) | INT_NO_CIF_PENDING;
saved_DF = IB = saved_PC & 070000;
UF = UB = gtf = emode = 0;
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r)
    pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Set PC for boot (PC<14:12> will typically be 0) */

void cpu_set_bootpc (int32 pc)
{
saved_PC = pc;                                          /* set PC, IF */
saved_DF = IB = pc & 070000;                            /* set IB, DF */
return;
}
/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
if (vptr != NULL)
    *vptr = M[addr] & 07777;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
M[addr] = val & 07777;
return SCPE_OK;
}

/* Memory size change */

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 mc = 0;
uint32 i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i++)
    mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
    return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE; i++)
    M[i] = 0;
return SCPE_OK;
}

/* Change device number for a device */

t_stat set_dev (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newdev;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
newdev = get_uint (cptr, 8, DEV_MAX - 1, &r);           /* get new */
if ((r != SCPE_OK) || (newdev == dibp->dev))
    return r;
dibp->dev = newdev;                                     /* store */
return SCPE_OK;
}

/* Show device number for a device */

t_stat show_dev (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
DIB *dibp;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
fprintf (st, "devno=%02o", dibp->dev);
if (dibp->num > 1)
    fprintf (st, "-%2o", dibp->dev + dibp->num - 1);
return SCPE_OK;
}

/* CPU device handler - should never get here! */

int32 bad_dev (int32 IR, int32 AC)
{
return (SCPE_IERR << IOT_V_REASON) | AC;                /* broken! */
}

/* Build device dispatch table */

t_bool build_dev_tab (void)
{
DEVICE *dptr;
DIB *dibp;
uint32 i, j;
static const uint8 std_dev[] = {
    000, 010, 020, 021, 022, 023, 024, 025, 026, 027
    };

for (i = 0; i < DEV_MAX; i++)                           /* clr table */
    dev_tab[i] = NULL;
for (i = 0; i < ((uint32) sizeof (std_dev)); i++)       /* std entries */
    dev_tab[std_dev[i]] = &bad_dev;
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* add devices */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* enabled? */
        for (j = 0; j < dibp->num; j++) {               /* loop thru disp */
            if (dibp->dsp[j]) {                         /* any dispatch? */
                if (dev_tab[dibp->dev + j]) {           /* already filled? */
                    printf ("%s device number conflict at %02o\n",
                            sim_dname (dptr), dibp->dev + j);
                    if (sim_log)
                        fprintf (sim_log, "%s device number conflict at %02o\n",
                                 sim_dname (dptr), dibp->dev + j);
                     return TRUE;
                    }
                dev_tab[dibp->dev + j] = dibp->dsp[j];  /* fill */
                }                                       /* end if dsp */
            }                                           /* end for j */
        }                                               /* end if enb */
    }                                                   /* end for i */
return FALSE;
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++)
        hst[i].pc = 0;
    hst_p = 0;
    return SCPE_OK;
    }
lnt = (int32) get_uint (cptr, 10, HIST_MAX, &r);
if ((r != SCPE_OK) || (lnt && (lnt < HIST_MIN)))
    return SCPE_ARG;
hst_p = 0;
if (hst_lnt) {
    free (hst);
    hst_lnt = 0;
    hst = NULL;
    }
if (lnt) {
    hst = (InstHistory *) calloc (lnt, sizeof (InstHistory));
    if (hst == NULL)
        return SCPE_MEM;
    hst_lnt = lnt;
    }
return SCPE_OK;
}

/* Show history */

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 l, k, di, lnt;
char *cptr = (char *) desc;
t_stat r;
t_value sim_eval;
InstHistory *h;
extern t_stat fprint_sym (FILE *ofile, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw);

if (hst_lnt == 0)                                       /* enabled? */
    return SCPE_NOFNC;
if (cptr) {
    lnt = (int32) get_uint (cptr, 10, hst_lnt, &r);
    if ((r != SCPE_OK) || (lnt == 0))
        return SCPE_ARG;
    }
else lnt = hst_lnt;
di = hst_p - lnt;                                       /* work forward */
if (di < 0)
    di = di + hst_lnt;
fprintf (st, "PC     L AC    MQ    ea     IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->pc & HIST_PC) {                              /* instruction? */
        l = (h->lac >> 12) & 1;                         /* link */
        fprintf (st, "%05o  %o %04o  %04o  ", h->pc & ADDRMASK, l, h->lac & 07777, h->mq);
        if (h->ir < 06000)
            fprintf (st, "%05o  ", h->ea);
        else fprintf (st, "       ");
        sim_eval = h->ir;
        if ((fprint_sym (st, h->pc & ADDRMASK, &sim_eval, &cpu_unit, SWMASK ('M'))) > 0)
            fprintf (st, "(undefined) %04o", h->ir);
        if (h->ir < 04000)
            fprintf (st, "  [%04o]", h->opnd);
        fputc ('\n', st);                               /* end line */
        }                                               /* end else instruction */
    }                                                   /* end for */
return SCPE_OK;
}



/* ------------------------------------------------------------------------------------ */
uint32 tempLeds=0;
void setleds(uint32 sPC, uint32 sMA, uint16 sMB, int32 sLAC, int32 sMQ, int32 sIF, int32 sDF)
{

	ledstatus[0] = (uint32) sPC;
	ledstatus[1] = (uint32) sMA;
	ledstatus[2] = (uint32) sMB;
	ledstatus[3] = (uint32) sLAC;
	ledstatus[4] = (uint32) sMQ;

	// instruction leds: decode instruction in memory (could also be found in IR)
//this *should* be found in IR, methinks. Just from memory is possibly problematic

//	tempLeds = ledstatus[5] & 12; // preserve value of fetch/execute handled in main loop
	tempLeds = ledstatus[5] & 13; // preserve value of fetch/execute/WC handled in main loop

	switch((M[sMA] & 0xE00) >> 9)
	{
		case 0:	tempLeds+=(1<<11); break;		// 000 AND
		case 1:	tempLeds+=(1<<10); break;		// 001 TAD
		case 2:	tempLeds+=(1<<9); break;		// 010 DCA
		case 3:	tempLeds+=(1<<8); break;		// 011 ISZ
		case 4:	tempLeds+=(1<<7); break;		// 100 JMS
		case 5:	tempLeds+=(1<<6); break;		// 101 JMP
		case 6:	tempLeds+=(1<<5); break;		// 110 IOT
		case 7: tempLeds+=(1<<4); break;		// 111-0 and 111-1 OPR group 1 & 2
		default: printf("instruction error in multiplexer");	// debug only, remove
	}

	if ( ((M[sMA] & 0xE00) >> 9) <= 5)	// <=5: all memory reference instructions
		if ((M[sMA] & 0x100) != 0)	// if fourth bit is set, this is indirect addressing, so...
		tempLeds += (1<<1);		// ...light defer
		
	ledstatus[5]=tempLeds;

//	tempLeds = 0; //ledstatus[6]; // want to preserve value of some leds set in main loop
	tempLeds = ledstatus[6] & 0xd00; // want to preserve value of CA/break/pause set in main loop

							// CAddr led - handled in main loop
							// Break led - handled in main loop
	tempLeds |= ((int_req & INT_ION)==0?0:1)<<9;	// ION led
							// Pause led - handled in main loop
	if (swStop==0)	tempLeds |= (1<<7);		// RUN led
	ledstatus[6]=tempLeds;

	// DF & IF in simh live in the 3 bits of octal digit #5...
	tempLeds = (uint32) (sDF>>3); // shift down from oct digit 5 to HW cols 1-3 (SW col[3-5])
	tempLeds += (uint32) (sIF>>6); // shift down from oct digit 5 to HW cols 4-6 (SW col[9-11]
	// Link
	tempLeds += (uint32) ((sLAC & 010000)>>7); // shift down from bit 12 to bit 8

	ledstatus[7]=tempLeds;

}
/* ------------------------------------------------------------------------------------ */

//--- PiDP add -------------------------------------------------------------

int mountUSBStickFile(int devNo, char *devCode, char *sPath)
{
	char	sUSBPath[CBUFSIZE];		// will be "/media/usb0" etc
	char	sFoundFile[CBUFSIZE];		// path & name of file that is found
	char	fileExtension[4];		// will be ".RX" etc
	FILE 	*fp;
	DIR 	*pDir;
	struct 	dirent *pDirent;
	int 	i,j, alreadyMounted;

	fileExtension[0]='.';			// extension starts with a .
	strncpy(&fileExtension[1], devCode, 2);	// extension is PT, RX, RL etc
	fileExtension[2]=0;			// don't want device number
	sFoundFile[0]=0;			// empty string, no file found yet

	// if mounting another image to a device, clear the current file from the mountlist:
	mountedFiles[devNo][0]=0x00;

	for (i=0;i<8;i++)				// search all 8 USB mount points
	{
		sprintf(sUSBPath,"/media/usb%d",i);	// usb sticks are numbered 0..7
//printf("1- %s\r\n", sUSBPath);
		pDir = opendir(sUSBPath);
		if (pDir==NULL) 			// that means usbmount not installed?
		{	printf("\r\nCannot open usb%d directory\r\n", i); return 1;	}

		while ((pDirent = readdir(pDir)) != NULL) // search all files in directory
		{
			if (strstr(pDirent->d_name,fileExtension))
			{
				sprintf(sPath, "%s/%s", sUSBPath, pDirent->d_name);
				alreadyMounted=0;
				for (j=0;j<7;j++)
				{
					if (strncmp(mountedFiles[j],sPath, CBUFSIZE)==0)
						alreadyMounted=1;
//printf("   >%s %d\r\n", mountedFiles[j], strncmp(mountedFiles[j],sPath, CBUFSIZE));

				}
				if (alreadyMounted==0)
				{
					strcpy(sFoundFile, sPath);
//printf("2-%s\r\n", sFoundFile);
					break;		// break out of while loop
				}
			}
		}
		closedir (pDir);

		if (sFoundFile[0]!=0)
			break;
	}

//printf("3-%s\r\n", sFoundFile);

	if (sFoundFile[0]==0x00)			// no file found, exit
	{	printf("\r\nNo unmounted %s file found\r\n", devCode);	return 1;	}

	fp = fopen(sFoundFile, "r");			// check file is OK
	if (fp==NULL) 
	{	printf("\r\nError opening file %s\r\n", sFoundFile);	return 1;	}
	fclose (fp);


	sprintf(sPath,"%s %s", devCode, sFoundFile);	// print cmd string
//	printf("\r\nMounting %s\r\n", sPath);

	if(attach_cmd ((int32) 0, xcptr)==SCPE_OK)		// issue simh attach command
	{	strcpy(mountedFiles[devNo], sFoundFile);		// add file to mount list
printf("\r\nMounted %s %s\r\n", devCode, mountedFiles[devNo]);
	}
	else
	{	printf("\r\nSimH error mounting %s\r\n", devCode);	return 1;	}

	return 0;
}

