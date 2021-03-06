# trace_gen
Trace Generation for Memory and Front-end simulation.
Instruction/Memory Trace Generation Tool
This tool collects an trace of instructions that access memory by filling a buffer.  When the buffer overflows,the callback writes all of the collected records to a file.

Traces include the instruction address (in hex) and any of the below qualifiers if they so apply:
 * R \<hex-addr\>: this instruction includes a memory location as a source operand; is followed by load address
 * W \<hex-addr\>: this instruction includes a memory location as a destination operand; is followed by store address.
 * T: this instruction is a branch that in this instance was evaluated as Taken
 * N: this instruction is a branch that in this instance was evaluated as Not Taken
 * J: this instruction is an unconditional Jump
 * A: this instruction is a Call
 * E: this instruction is a Return
 * C: this instruction is a cache invalidate instruction
 * P: this instruction is a TLB invalidate instruction

With just the above information, one can simulate the flow of instructions through a processor while accurately simulating cache activity and latency. It provides all the information necessary for a Runahead Prefetch simulation. Namely control flow changes, and all memory requests, both instruction and data.

Below is an example snippet of trace that would be generated with this tool:
```
7fa133ea0b06 R 7fa10ddf69e8
7fa133ea0b0b W fa626264
7fa133ea0b0f R fa639427
7fa133ea0b15
7fa133ea0b18
7fa133ea0b1c N
7fa133ea0b1e
7fa133ea0b22 N
7fa133ea0b28 W fa626264
7fa133ea0b2c
7fa133ea0b2e T
7fa133ea0a90
7fa133ea0a93 R fa639428
7fa133ea0a99
7fa133ea0a9c
7fa133ea0a9f
7fa133ea0aa3 N
7fa133ea0aa9
7fa133ea0aad N
7fa133ea0ab3 R 7fa10ddf69e8
```

# Dependencies
This PinTool requires an installation of the Pin binary instrumentation program. This can be found [here](https://software.intel.com/en-us/articles/pintool-downloads).
* Note: this PinTool had only be tested on a linux distribution of Pin v3.0.

# Install
* Navigate to Pin's root directory and cd into source/tools
* clone this repo
* navigate into the repo directory and execute:
```
$ mkdir obj-intel64
$ make obj-intel64/trace_gen.so
```

# Run
From the pintool's root directory execute the following:
```
$ ../../../pin -t obj-intel64/trace_gen.so -emit 1 -smarts 1 -warmup_ins <number_of_warmup_instructions> -ins_interval <instruction_interval> -num_intervals <number of intervals> -snippet_size <number_of_instructions_per_snippet> -- <command line for program under test>
```
Below is an explanation of the available command line options:
* -o: file in which the trace is dumped (default: trace_gen.out)
* -emit: enables logging the trace in a trace (default: false)
* -smarts: enables special smarts based tracing (default: false). Without smarts based tracing, the entire program is logged. smarts tracing works in conjunction with the below command line options:
 * -warmup_ins: number of instructions to ignore before beginning the trace
 * -ins_interval: number of instructions between the start of each SMART snippet
 * -num_intervals: number of SMART snippets to log.
 * -snippet_size: number of instructions to log per SMART snippet
