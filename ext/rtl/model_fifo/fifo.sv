/*
* Copyright (c) 2022 Barcelona Supercomputing Center
* All rights reserved.
*
* The license below extends only to copyright in the software and shall
* not be construed as granting a license to any other intellectual
* property including but not limited to intellectual property relating
* to a hardware implementation of the functionality of the software
* licensed hereunder.  You may use the software subject to the license
* terms below provided that you ensure that this notice is replicated
* unmodified and in its entirety in all distributions of the software,
* modified or unmodified, in source code or in binary form.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met: redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer;
* redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution;
* neither the name of the copyright holders nor the names of its
* contributors may be used to endorse or promote products derived from
* this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
* A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
* OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Authors: Guillem Lopez Paradis
*/

module fifo
	
	#(
		parameter DATA_WIDTH = 64, // Bits of data
		parameter ADDR_WIDTH = 5
	)

	(
		input logic								clk,
		input logic								rst,
		input logic		[DATA_WIDTH-1:0] 		data_input,
		input logic								read_enable_input,
		input logic								write_enable_input,
		output logic	[DATA_WIDTH-1:0]		data_output,
		output logic							empty,
		output logic							full
	);

	parameter FIFO_SIZE  = (1 << ADDR_WIDTH);

	// Internal Variables
	
	reg [DATA_WIDTH-1:0] fifo_mem [0:FIFO_SIZE-1]; 

	reg	[ADDR_WIDTH-1:0] head_ptr;	// read pointer
	reg	[ADDR_WIDTH-1:0] tail_ptr;	// write pointer
	reg [ADDR_WIDTH:0]   fifo_cnt;	// counter of elements

	// Internal read and write enable
	wire read_enable, write_enable;

	// Logic disable when full
	assign read_enable = read_enable_input && !empty;
	assign write_enable = write_enable_input && !full;


	// Logic empty and full


	assign full = (fifo_cnt == (FIFO_SIZE));
	assign empty = (fifo_cnt == 0);

	// Logic keep track elements in fifo
	always @(posedge clk, posedge rst)
	begin
		
		if (rst) begin
			fifo_cnt <= 0;
		
		end	else if (read_enable && !write_enable) begin
			fifo_cnt <= fifo_cnt - 1;
		
		end else if (!read_enable && write_enable) begin
			fifo_cnt <= fifo_cnt + 1;
		
		end else if (read_enable && write_enable) begin
			fifo_cnt <= fifo_cnt;
		
		end else begin
			fifo_cnt <= fifo_cnt;
		end 
	end

	// Logic Read element pointer 
	always @(posedge clk, posedge rst) 
	begin
		
		if (rst) begin
			head_ptr <= 0;

		end else if (read_enable && !empty) begin
			head_ptr <= head_ptr + 1;

		end else begin
			head_ptr <= head_ptr;
		end // end else
	end

	// Logic Write element pointer
	always @(posedge clk, posedge rst) 
	begin
		
		if (rst) begin
			tail_ptr <= 0;

		end else if (write_enable && !full) begin
			tail_ptr <= tail_ptr + 1;

		end else begin
			tail_ptr <= tail_ptr;
		end // end else
	end

	// Logic Write element fifo
	always @(posedge clk) 
	begin
		
		if (!full && write_enable) begin
			fifo_mem[tail_ptr] <= data_input;

		end else begin
			fifo_mem[tail_ptr] <= fifo_mem[tail_ptr];

		end 
	end	

	// Logic Read element fifo
	always @(posedge clk) 
	begin
		
		if (!empty && read_enable) begin
			data_output <= fifo_mem[head_ptr];

		end else begin
			data_output <= data_output;

		end 
	end

endmodule // fifo









