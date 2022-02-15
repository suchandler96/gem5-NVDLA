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









