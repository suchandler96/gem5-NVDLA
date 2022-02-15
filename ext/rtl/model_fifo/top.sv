module top(
	input clk,
	input rst,
	input [64-1:0] data_input,
	input read_enable,
	input write_enable,
	output [64-1:0] data_output,
	output empty,
	output full
);

fifo #(
	.DATA_WIDTH(64), // Bits of data
	.ADDR_WIDTH(3))

	fifo(	clk,
			rst,
			data_input,
			read_enable,
			write_enable,
			data_output,
			empty,
			full
			);
endmodule
