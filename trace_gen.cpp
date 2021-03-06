/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2016 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
/*
 * Instruction/Memory Trace Generation Tool
 * 
 * This tool collects an trace of instructions that access memory
 * by filling a buffer.  When the buffer overflows,the callback writes all
 * of the collected records to a file.
 *
 * Traces include the instruction address (in hex) and any of the below qualifiers if
 * they so apply:
 * R <hex-addr>: this instruction includes a memory location as a source operand; is followed by load address
 * W <hex-addr>: this instruction includes a memory location as a destination operand; is followed by store address.
 * T: this instruction is a branch that in this instance was evaluated as Taken
 * N: this instruction is a branch that in this instance was evaluated as Not Taken
 * J: this instruction is an unconditional Jump
 * A: this instruction is a Call
 * E: this instruction is a Return
 * C: this instruction is a cache invalidate instruction
 * P: this instruction is a TLB invalidate instruction
 *
 * Below is an example snippet of trace that would be generated with this tool:
 * 7fa133ea0b06 R 7fa10ddf69e8
 * 7fa133ea0b0b W fa626264
 * 7fa133ea0b0f R fa639427
 * 7fa133ea0b15
 * 7fa133ea0b18
 * 7fa133ea0b1c N
 * 7fa133ea0b1e
 * 7fa133ea0b22 N
 * 7fa133ea0b28 W fa626264
 * 7fa133ea0b2c
 * 7fa133ea0b2e T
 * 7fa133ea0a90
 * 7fa133ea0a93 R fa639428
 * 7fa133ea0a99
 * 7fa133ea0a9c
 * 7fa133ea0a9f
 * 7fa133ea0aa3 N
 * 7fa133ea0aa9
 * 7fa133ea0aad N
 * 7fa133ea0ab3 R 7fa10ddf69e8
 *
 * This tool does a similar task as memtrace.cpp, but it uses the buffering api.
 */


#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <stddef.h>
#include <fcntl.h>

#include <sys/time.h>
#include <iomanip>

#include "pin.H"
#include "portability.H"
using namespace std;


/*
 * Knobs for tool
 */

/*
 * Name of the output file
 */
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "trace_gen.out", "output file");

/*
 * Emit the address trace to the output file
 */
KNOB<BOOL> KnobEmitTrace(KNOB_MODE_WRITEONCE, "pintool", "emit", "0", "emit a trace in the output file");

KNOB<BOOL> KnobSMARTS(KNOB_MODE_WRITEONCE, "pintool", "smarts", "0", "do SMARTS interval tracing");

KNOB<UINT64> KnobWarmupIns(KNOB_MODE_WRITEONCE, "pintool", "warmup_ins", "10000", "number of warmup instructions in SMARTS tracing");

KNOB<UINT64> KnobInsInterval(KNOB_MODE_WRITEONCE, "pintool", "ins_interval", "20000", "size of interval between traces in SMARTS tracing");

KNOB<UINT64> KnobNumIntervals(KNOB_MODE_WRITEONCE, "pintool", "num_intervals", "10", "number of snippet to collect in SMARTS tracing");

KNOB<UINT64> KnobSnippetSize(KNOB_MODE_WRITEONCE, "pintool", "snippet_size", "1000", "snippet size to collect in SMARTS tracing");

/* Struct for holding memory references.
 */
struct MEMREF
{
    ADDRINT pc;
    ADDRINT ea0;
    ADDRINT ea1;
    char ev;
    char b1;
    char b2;
    char b3;
    char t1;
};
// num elements around 131000


BUFFER_ID bufId;

TLS_KEY mlog_key;


#define NUM_BUF_PAGES 1024


/*
 * MLOG - thread specific data that is not handled by the buffering API.
 */
class MLOG
{
  public:
    MLOG(THREADID tid);
    ~MLOG();

    VOID DumpBufferToFile( struct MEMREF * reference, UINT64 numElements, THREADID tid );
    bool EmitBuffer(void);
    void PrintEmitHeader(void);
    void SetBufferTime(void);
    void IncNumIns(UINT64 numElements);
    void OpenFile(void);

  private:
    ofstream _ofile;
    UINT64 num_ins;
    bool SMARTS;
    UINT64 warmup_ins;
    UINT64 ins_interval;
    UINT64 num_intervals;
    UINT64 snippet_size;
    struct timeval buffer_tv;  
    bool tracing; 
    int fd;
    THREADID tid;
};


void MLOG::OpenFile(void)
{

        if ( ! _ofile.is_open()) {
        string filename = KnobOutputFile.Value() + "." + decstr(getpid_portable()) + "." + decstr(tid);
        _ofile.open(filename.c_str());
        if ( ! _ofile )
        {
            cerr << "Error: could not open output file. " << filename << endl;
            exit(1);
        }
        _ofile << hex;

        }
}

MLOG::MLOG(THREADID tid)
{
    if (KnobEmitTrace && tid == 24)
    {
            
        num_ins = 0;
        SMARTS = KnobSMARTS;
        warmup_ins = KnobWarmupIns.Value();
        ins_interval = KnobInsInterval.Value();
        num_intervals = KnobNumIntervals.Value();
        snippet_size = KnobSnippetSize.Value();
        SetBufferTime();
        tracing = false;
        this->tid = tid;
    }
}


MLOG::~MLOG()
{
    if (KnobEmitTrace)
    {
        _ofile.close();
    }
}


VOID MLOG::DumpBufferToFile( struct MEMREF * reference, UINT64 numElements, THREADID tid )
{
   
    for(UINT64 i=0; i<numElements; i++, reference++)
    {
        _ofile << reference->pc;
        if (reference->ev != 0)
            _ofile << " " << reference->ev;
        if (reference->ea0 != reference->pc)
            _ofile << " R " << reference->ea0;
        if (reference->t1 == 'W')
            _ofile << " W " << reference->ea1;
        else if (reference->t1 == 'R')
            _ofile << " R " << reference->ea1;
        _ofile << endl;

    }
}

bool MLOG::EmitBuffer(void)
{
  if (!SMARTS) {
      if (!tracing) {
          OpenFile();
          PrintEmitHeader();
      }
      tracing = true;
      return true;
  }
  if (num_ins < warmup_ins) {
        tracing = false;
        return false;
    }
    UINT64 num_ins_less_warmup = num_ins - warmup_ins;
    UINT64 temp = num_ins_less_warmup / ins_interval;
    if (temp >= num_intervals) {
        tracing = false;
        return false;
    }
    if (num_ins_less_warmup < ((ins_interval * temp) + snippet_size)) {
        if (!tracing) {
            OpenFile();
            PrintEmitHeader();
        }
        tracing = true;
        return true;
    } else {
        tracing = false;
        return false;
    }
}

void MLOG::PrintEmitHeader(void)
{
    _ofile << "time: " << dec << buffer_tv.tv_sec << "." << setw(6) << buffer_tv.tv_usec << hex << endl;
    _ofile << "instructions: " << dec << num_ins << hex << endl;
}

void MLOG::SetBufferTime(void)
{
    struct timezone tz1;
    gettimeofday(&buffer_tv, &tz1);
}

void MLOG::IncNumIns(UINT64 numElements)
{
  num_ins += numElements;
}

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    cerr << "This tool demonstrates the basic use of the buffering API." << endl << endl;

    cerr << KNOB_BASE::StringKnobSummary() << endl;

    cerr << "size of ADDRINT: " << sizeof(ADDRINT) << endl;

    return -1;
}


/*
 * Insert code to write data to a thread-specific buffer for instructions
 * that access memory.
 */
VOID Trace(TRACE trace, VOID *v)
{
    // Insert a call to record the effective address.
    for(BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl=BBL_Next(bbl))
    {
        for(INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins=INS_Next(ins))
        {
            if (INS_Opcode(ins) == XED_ICLASS_INVD ||
                INS_Opcode(ins) == XED_ICLASS_WBINVD)
            {
                INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_INST_PTR, offsetof(struct MEMREF, pc),
                                     IARG_UINT32, 'C', offsetof(struct MEMREF, ev),
                                     (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea0),
                                     (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD2_EA :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYWRITE_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea1),
                                     IARG_UINT32, (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? 'R' :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? 'W' : 0, offsetof(struct MEMREF, t1),
                                     IARG_END);
            }
            else if (INS_Opcode(ins) == XED_ICLASS_INVLPG ||
              INS_Opcode(ins) == XED_ICLASS_INVPCID)
            {
                INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_INST_PTR, offsetof(struct MEMREF, pc),
                                     IARG_UINT32, 'P', offsetof(struct MEMREF, ev),
                                     (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea0),
                                     (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD2_EA :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYWRITE_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea1),
                                     IARG_UINT32, (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? 'R' :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? 'W' : 0, offsetof(struct MEMREF, t1),
                                     IARG_END);

            }
            else if (INS_Category(ins) == XED_CATEGORY_COND_BR)
            {
              
                INS_InsertFillBuffer(ins, IPOINT_TAKEN_BRANCH, bufId,
                                    IARG_INST_PTR, offsetof(struct MEMREF, pc),
                                    IARG_UINT32, 'T', offsetof(struct MEMREF, ev),
                                    (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea0),
                                     (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD2_EA :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYWRITE_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea1),
                                     IARG_UINT32, (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? 'R' :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? 'W' : 0, offsetof(struct MEMREF, t1),
                                    IARG_END);
                INS_InsertFillBuffer(ins, IPOINT_AFTER, bufId,
                                    IARG_INST_PTR, offsetof(struct MEMREF, pc),
                                    IARG_UINT32, 'N', offsetof(struct MEMREF, ev),
                                    (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea0),
                                     (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD2_EA :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYWRITE_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea1),
                                     IARG_UINT32, (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? 'R' :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? 'W' : 0, offsetof(struct MEMREF, t1),
                                    IARG_END);
            }
            else if (INS_IsCall(ins))
            {
              INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_INST_PTR, offsetof(struct MEMREF, pc),
                                     IARG_UINT32, 'A', offsetof(struct MEMREF, ev),
                                     (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea0),
                                     (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD2_EA :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYWRITE_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea1),
                                     IARG_UINT32, (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? 'R' :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? 'W' : 0, offsetof(struct MEMREF, t1),
                                     IARG_END);
            }
            else if (INS_IsRet(ins))
            {
              INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_INST_PTR, offsetof(struct MEMREF, pc),
                                     IARG_UINT32, 'E', offsetof(struct MEMREF, ev),
                                     (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea0),
                                     (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD2_EA :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYWRITE_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea1),
                                     IARG_UINT32, (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? 'R' :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? 'W' : 0, offsetof(struct MEMREF, t1),
                                     IARG_END);
            }
            else if (INS_IsBranch(ins))
            {
                INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_INST_PTR, offsetof(struct MEMREF, pc),
                                     IARG_UINT32, 'J', offsetof(struct MEMREF, ev),
                                     (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea0),
                                     (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD2_EA :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYWRITE_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea1),
                                     IARG_UINT32, (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? 'R' :
                                        (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? 'W' : 0, offsetof(struct MEMREF, t1),
                                     IARG_END);
            }
            else
            {
                INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_INST_PTR, offsetof(struct MEMREF, pc),
                                     IARG_UINT32, 0, offsetof(struct MEMREF, ev),
                                     (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD_EA : IARG_INST_PTR, offsetof(struct MEMREF, ea0),
                                     (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYREAD2_EA :
                                        ((INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? IARG_MEMORYWRITE_EA : IARG_INST_PTR), offsetof(struct MEMREF, ea1),
                                     IARG_UINT32, (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins)) ? 'R' :
                                        ((INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) ? 'W' : 0), offsetof(struct MEMREF, t1),
                                     IARG_END);

            }
        }
    }
}


/**************************************************************************
 *
 *  Callback Routines
 *
 **************************************************************************/

/*!
 * Called when a buffer fills up, or the thread exits, so we can process it or pass it off
 * as we see fit.
 * @param[in] id		buffer handle
 * @param[in] tid		id of owning thread
 * @param[in] ctxt		application context
 * @param[in] buf		actual pointer to buffer
 * @param[in] numElements	number of records
 * @param[in] v			callback value
 * @return  A pointer to the buffer to resume filling.
 */
VOID * BufferFull(BUFFER_ID id, THREADID tid, const CONTEXT *ctxt, VOID *buf,
                  UINT64 numElements, VOID *v)
{
    if ( (! KnobEmitTrace) || (tid != 24) )
        return buf;

    struct MEMREF * reference=(struct MEMREF*)buf;

    MLOG * mlog = static_cast<MLOG*>( PIN_GetThreadData( mlog_key, tid ) );

    if (mlog->EmitBuffer())
        mlog->DumpBufferToFile( reference, numElements, tid );

    mlog->SetBufferTime();
    mlog->IncNumIns(numElements);

    return buf;
}



VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    // There is a new MLOG for every thread.  Opens the output file.
    MLOG * mlog = new MLOG(tid);

    // A thread will need to look up its MLOG, so save pointer in TLS
    PIN_SetThreadData(mlog_key, mlog, tid);


}


VOID ThreadFini(THREADID tid, const CONTEXT *ctxt, INT32 code, VOID *v)
{
    MLOG * mlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, tid));

    delete mlog;

    PIN_SetThreadData(mlog_key, 0, tid);
}


/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }
    //cerr << ZLIB_VERSION << endl;
    
    // Initialize the memory reference buffer
    bufId = PIN_DefineTraceBuffer(sizeof(struct MEMREF), NUM_BUF_PAGES,
                                  BufferFull, 0);

    if(bufId == BUFFER_ID_INVALID)
    {
        cerr << "Error: could not allocate initial buffer" << endl;
        return 1;
    }

    // Initialize thread-specific data not handled by buffering api.
    mlog_key = PIN_CreateThreadDataKey(0);
   
    // add an instrumentation function
    TRACE_AddInstrumentFunction(Trace, 0);

    // add callbacks
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);

    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}


