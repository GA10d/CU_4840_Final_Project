package require -exact qsys 16.1

set_module_property DESCRIPTION "MMIO-driven VGA renderer for the fighter game"
set_module_property NAME fighter_vga
set_module_property VERSION 1.0
set_module_property INTERNAL false
set_module_property OPAQUE_ADDRESS_MAP true
set_module_property AUTHOR ""
set_module_property DISPLAY_NAME fighterVga
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE true
set_module_property REPORT_TO_TALKBACK false
set_module_property ALLOW_GREYBOX_GENERATION false
set_module_property REPORT_HIERARCHY false

add_fileset QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL fighter_vga_renderer
set_fileset_property QUARTUS_SYNTH ENABLE_RELATIVE_INCLUDE_PATHS false
set_fileset_property QUARTUS_SYNTH ENABLE_FILE_OVERWRITE_MODE false
add_fileset_file fighter_vga_renderer.sv SYSTEM_VERILOG PATH fighter_vga_renderer.sv TOP_LEVEL_FILE

set_module_assignment embeddedsw.dts.group video
set_module_assignment embeddedsw.dts.name fighter_vga
set_module_assignment embeddedsw.dts.vendor csee4840

add_interface clock clock end
set_interface_property clock clockRate 0
set_interface_property clock ENABLED true
add_interface_port clock clk_50 clk Input 1

add_interface reset reset end
set_interface_property reset associatedClock clock
set_interface_property reset synchronousEdges DEASSERT
set_interface_property reset ENABLED true
add_interface_port reset reset_n reset_n Input 1

add_interface avalon_slave_0 avalon end
set_interface_property avalon_slave_0 addressUnits WORDS
set_interface_property avalon_slave_0 associatedClock clock
set_interface_property avalon_slave_0 associatedReset reset
set_interface_property avalon_slave_0 bitsPerSymbol 8
set_interface_property avalon_slave_0 burstOnBurstBoundariesOnly false
set_interface_property avalon_slave_0 burstcountUnits WORDS
set_interface_property avalon_slave_0 explicitAddressSpan 128
set_interface_property avalon_slave_0 holdTime 0
set_interface_property avalon_slave_0 linewrapBursts false
set_interface_property avalon_slave_0 maximumPendingReadTransactions 0
set_interface_property avalon_slave_0 maximumPendingWriteTransactions 0
set_interface_property avalon_slave_0 readLatency 0
set_interface_property avalon_slave_0 readWaitTime 1
set_interface_property avalon_slave_0 setupTime 0
set_interface_property avalon_slave_0 timingUnits Cycles
set_interface_property avalon_slave_0 writeWaitTime 0
set_interface_property avalon_slave_0 ENABLED true

add_interface_port avalon_slave_0 avs_chipselect chipselect Input 1
add_interface_port avalon_slave_0 avs_read read Input 1
add_interface_port avalon_slave_0 avs_write write Input 1
add_interface_port avalon_slave_0 avs_address address Input 5
add_interface_port avalon_slave_0 avs_writedata writedata Input 32
add_interface_port avalon_slave_0 avs_readdata readdata Output 32
set_interface_assignment avalon_slave_0 embeddedsw.configuration.isFlash 0
set_interface_assignment avalon_slave_0 embeddedsw.configuration.isMemoryDevice 0
set_interface_assignment avalon_slave_0 embeddedsw.configuration.isNonVolatileStorage 0
set_interface_assignment avalon_slave_0 embeddedsw.configuration.isPrintableDevice 0

add_interface vga conduit end
set_interface_property vga associatedClock clock
set_interface_property vga associatedReset reset
set_interface_property vga ENABLED true

add_interface_port vga vga_r       vga_r       Output 8
add_interface_port vga vga_g       vga_g       Output 8
add_interface_port vga vga_b       vga_b       Output 8
add_interface_port vga vga_hs      vga_hs      Output 1
add_interface_port vga vga_vs      vga_vs      Output 1
add_interface_port vga vga_clk     vga_clk     Output 1
add_interface_port vga vga_blank_n vga_blank_n Output 1
add_interface_port vga vga_sync_n  vga_sync_n  Output 1
