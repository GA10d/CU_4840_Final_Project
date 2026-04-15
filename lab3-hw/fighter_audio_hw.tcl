# TCL File for fighter_audio_wm8731
# Generated for the Lab 3 hardware integration flow

package require -exact qsys 21.1

set_module_property DESCRIPTION "WM8731 audio playback peripheral for DE1-SoC"
set_module_property NAME fighter_audio_wm8731
set_module_property VERSION 1.0
set_module_property INTERNAL false
set_module_property OPAQUE_ADDRESS_MAP true
set_module_property AUTHOR "OpenAI Codex"
set_module_property DISPLAY_NAME "WM8731 Audio Peripheral"
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE true
set_module_property ANALYZE_HDL AUTO
set_module_property REPORT_TO_TALKBACK false
set_module_property ALLOW_GREYBOX_GENERATION false
set_module_property REPORT_HIERARCHY false

add_fileset QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL fighter_audio_wm8731
set_fileset_property QUARTUS_SYNTH ENABLE_RELATIVE_INCLUDE_PATHS false
set_fileset_property QUARTUS_SYNTH ENABLE_FILE_OVERWRITE_MODE false
add_fileset_file fighter_audio.sv SYSTEM_VERILOG PATH fighter_audio.sv TOP_LEVEL_FILE

add_interface clock clock end
set_interface_property clock clockRate 0
set_interface_property clock ENABLED true
set_interface_property clock EXPORT_OF ""
set_interface_property clock PORT_NAME_MAP ""
set_interface_property clock CMSIS_SVD_VARIABLES ""
set_interface_property clock SVD_ADDRESS_GROUP ""
add_interface_port clock clk clk Input 1

add_interface reset reset end
set_interface_property reset associatedClock clock
set_interface_property reset synchronousEdges DEASSERT
set_interface_property reset ENABLED true
set_interface_property reset EXPORT_OF ""
set_interface_property reset PORT_NAME_MAP ""
set_interface_property reset CMSIS_SVD_VARIABLES ""
set_interface_property reset SVD_ADDRESS_GROUP ""
add_interface_port reset reset_n reset_n Input 1

add_interface avs avalon end
set_interface_property avs addressUnits WORDS
set_interface_property avs associatedClock clock
set_interface_property avs associatedReset reset
set_interface_property avs bitsPerSymbol 8
set_interface_property avs burstOnBurstBoundariesOnly false
set_interface_property avs burstcountUnits WORDS
set_interface_property avs explicitAddressSpan 0
set_interface_property avs holdTime 0
set_interface_property avs linewrapBursts false
set_interface_property avs maximumPendingReadTransactions 0
set_interface_property avs maximumPendingWriteTransactions 0
set_interface_property avs readLatency 0
set_interface_property avs readWaitTime 1
set_interface_property avs setupTime 0
set_interface_property avs timingUnits Cycles
set_interface_property avs writeWaitTime 0
set_interface_property avs ENABLED true
set_interface_property avs EXPORT_OF ""
set_interface_property avs PORT_NAME_MAP ""
set_interface_property avs CMSIS_SVD_VARIABLES ""
set_interface_property avs SVD_ADDRESS_GROUP ""
add_interface_port avs avs_chipselect chipselect Input 1
add_interface_port avs avs_read read Input 1
add_interface_port avs avs_write write Input 1
add_interface_port avs avs_address address Input 2
add_interface_port avs avs_writedata writedata Input 32
add_interface_port avs avs_readdata readdata Output 32
set_interface_assignment avs embeddedsw.configuration.isFlash 0
set_interface_assignment avs embeddedsw.configuration.isMemoryDevice 0
set_interface_assignment avs embeddedsw.configuration.isNonVolatileStorage 0
set_interface_assignment avs embeddedsw.configuration.isPrintableDevice 0

add_interface audio conduit end
set_interface_property audio associatedClock clock
set_interface_property audio ENABLED true
set_interface_property audio EXPORT_OF ""
set_interface_property audio PORT_NAME_MAP ""
set_interface_property audio CMSIS_SVD_VARIABLES ""
set_interface_property audio SVD_ADDRESS_GROUP ""
add_interface_port audio aud_xck xck Output 1
add_interface_port audio aud_bclk bclk Output 1
add_interface_port audio aud_daclrck daclrck Output 1
add_interface_port audio aud_adclrck adclrck Output 1
add_interface_port audio aud_dacdat dacdat Output 1
add_interface_port audio aud_adcdat adcdat Input 1

add_interface fpga_i2c conduit end
set_interface_property fpga_i2c associatedClock clock
set_interface_property fpga_i2c ENABLED true
set_interface_property fpga_i2c EXPORT_OF ""
set_interface_property fpga_i2c PORT_NAME_MAP ""
set_interface_property fpga_i2c CMSIS_SVD_VARIABLES ""
set_interface_property fpga_i2c SVD_ADDRESS_GROUP ""
add_interface_port fpga_i2c fpga_i2c_sclk sclk Bidir 1
add_interface_port fpga_i2c fpga_i2c_sdat sdat Bidir 1

add_interface status conduit end
set_interface_property status associatedClock clock
set_interface_property status ENABLED true
set_interface_property status EXPORT_OF ""
set_interface_property status PORT_NAME_MAP ""
set_interface_property status CMSIS_SVD_VARIABLES ""
set_interface_property status SVD_ADDRESS_GROUP ""
add_interface_port status codec_init_done init_done Output 1
add_interface_port status codec_init_error init_error Output 1
