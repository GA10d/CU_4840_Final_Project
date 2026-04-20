[jy3557@micro21 DE1_SOC_Linux_Audio]$ cd /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio
[jy3557@micro21 DE1_SOC_Linux_Audio]$ export PATH="$QUARTUS_ROOTDIR/sopc_builder/bin:$PATH"
[jy3557@micro21 DE1_SOC_Linux_Audio]$ export PATH="$QUARTUS_ROOTDIR/sopc_builder/bin:$PATH"
[jy3557@micro21 DE1_SOC_Linux_Audio]$ which qsys-generate
/tools/intel/intelFPGA/21.1/quartus/sopc_builder/bin/qsys-generate
[jy3557@micro21 DE1_SOC_Linux_Audio]$ qsys-generate soc_system.qsys \
>   --synthesis=VERILOG \
>   --output-directory=./soc_system \
>   --search-path="../cores/i2s,$" \
>   --part=5CSEMA5F31C6 \
>   --clear-output-directory

2026.04.20.16:42:00 Info: Saving generation log to /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/soc_system/soc_system_generation.rpt
2026.04.20.16:42:00 Info: Starting: <b>Create HDL design files for synthesis</b>
2026.04.20.16:42:00 Info: qsys-generate /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/soc_system.qsys --synthesis=VERILOG --output-directory=/homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/soc_system/synthesis --family="Cyclone V" --part=5CSEMA5F31C6
2026.04.20.16:42:00 Info: Loading DE1_SOC_Linux_Audio/soc_system.qsys
2026.04.20.16:42:00 Info: Reading input file
2026.04.20.16:42:00 Info: Adding audio_pll [altera_pll 14.1]
2026.04.20.16:42:00 Warning: audio_pll: Used altera_pll <b>21.1</b> (instead of 14.1)
2026.04.20.16:42:00 Info: Parameterizing module audio_pll
2026.04.20.16:42:00 Info: Adding clk_0 [clock_source 14.1]
2026.04.20.16:42:00 Warning: clk_0: Used clock_source <b>21.1</b> (instead of 14.1)
2026.04.20.16:42:00 Info: Parameterizing module clk_0
2026.04.20.16:42:00 Info: Adding clock_bridge_0 [altera_clock_bridge 14.1]
2026.04.20.16:42:00 Warning: clock_bridge_0: Used altera_clock_bridge <b>21.1</b> (instead of 14.1)
2026.04.20.16:42:00 Info: Parameterizing module clock_bridge_0
2026.04.20.16:42:00 Info: Adding clock_bridge_44 [altera_clock_bridge 14.1]
2026.04.20.16:42:00 Warning: clock_bridge_44: Used altera_clock_bridge <b>21.1</b> (instead of 14.1)
2026.04.20.16:42:00 Info: Parameterizing module clock_bridge_44
2026.04.20.16:42:00 Info: Adding clock_bridge_48 [altera_clock_bridge 14.1]
2026.04.20.16:42:00 Warning: clock_bridge_48: Used altera_clock_bridge <b>21.1</b> (instead of 14.1)
2026.04.20.16:42:00 Info: Parameterizing module clock_bridge_48
2026.04.20.16:42:00 Info: Adding hps_0 [altera_hps 14.1]
2026.04.20.16:42:00 Warning: hps_0: Used altera_hps <b>21.1</b> (instead of 14.1)
2026.04.20.16:42:00 Info: Parameterizing module hps_0
2026.04.20.16:42:00 Info: Adding i2s_clkctrl_apb_0 [i2s_clkctrl_api 1.7]
2026.04.20.16:42:00 Info: Parameterizing module i2s_clkctrl_apb_0
2026.04.20.16:42:00 Info: Adding i2s_output_apb_0 [i2s_output_apb 1.4]
2026.04.20.16:42:00 Info: Parameterizing module i2s_output_apb_0
2026.04.20.16:42:00 Info: Adding sysid_qsys_0 [altera_avalon_sysid_qsys 14.1]
2026.04.20.16:42:00 Warning: sysid_qsys_0: Used altera_avalon_sysid_qsys <b>21.1</b> (instead of 14.1)
2026.04.20.16:42:00 Info: Parameterizing module sysid_qsys_0
2026.04.20.16:42:00 Info: Building connections
2026.04.20.16:42:00 Info: Parameterizing connections
2026.04.20.16:42:00 Info: Validating
2026.04.20.16:42:03 Info: Done reading input file
2026.04.20.16:42:04 Info: soc_system.audio_pll: The legal reference clock frequency is 5.0 MHz..800.0 MHz
2026.04.20.16:42:04 Warning: soc_system.audio_pll: Able to implement PLL - Actual settings differ from Requested settings
2026.04.20.16:42:04 Info: soc_system.hps_0: HPS Main PLL counter settings: n = 0  m = 73
2026.04.20.16:42:04 Info: soc_system.hps_0: HPS peripherial PLL counter settings: n = 0  m = 39
2026.04.20.16:42:04 Warning: soc_system.hps_0: <b>"Configuration/HPS-to-FPGA user 0 clock frequency" (desired_cfg_clk_mhz)</b> requested 100.0 MHz, but only achieved 97.368421 MHz
2026.04.20.16:42:04 Warning: soc_system.hps_0: <b>"QSPI clock frequency" (desired_qspi_clk_mhz)</b> requested 400.0 MHz, but only achieved 370.0 MHz
2026.04.20.16:42:04 Warning: soc_system.hps_0: 1 or more output clock frequencies cannot be achieved precisely, consider revising desired output clock frequencies.
2026.04.20.16:42:04 Info: soc_system.sysid_qsys_0: System ID is not assigned automatically. Edit the System ID parameter to provide a unique ID
2026.04.20.16:42:04 Info: soc_system.sysid_qsys_0: Time stamp will be automatically updated when this component is generated.
2026.04.20.16:42:16 Info: soc_system: Generating <b>soc_system</b> "<b>soc_system</b>" for QUARTUS_SYNTH
2026.04.20.16:42:19 Warning: hps_0.f2h_irq0: Cannot connect clock for <b>irq_mapper.sender</b>
2026.04.20.16:42:19 Warning: hps_0.f2h_irq0: Cannot connect reset for <b>irq_mapper.sender</b>
2026.04.20.16:42:19 Warning: hps_0.f2h_irq1: Cannot connect clock for <b>irq_mapper_001.sender</b>
2026.04.20.16:42:19 Warning: hps_0.f2h_irq1: Cannot connect reset for <b>irq_mapper_001.sender</b>
2026.04.20.16:42:20 Info: audio_pll: "<b>soc_system</b>" instantiated <b>altera_pll</b> "<b>audio_pll</b>"
2026.04.20.16:42:20 Info: hps_0: "Running  for module: hps_0"
2026.04.20.16:42:20 Info: hps_0: HPS Main PLL counter settings: n = 0  m = 73
2026.04.20.16:42:21 Info: hps_0: HPS peripherial PLL counter settings: n = 0  m = 39
2026.04.20.16:42:21 Warning: hps_0: <b>"Configuration/HPS-to-FPGA user 0 clock frequency" (desired_cfg_clk_mhz)</b> requested 100.0 MHz, but only achieved 97.368421 MHz
2026.04.20.16:42:21 Warning: hps_0: <b>"QSPI clock frequency" (desired_qspi_clk_mhz)</b> requested 400.0 MHz, but only achieved 370.0 MHz
2026.04.20.16:42:21 Warning: hps_0: 1 or more output clock frequencies cannot be achieved precisely, consider revising desired output clock frequencies.
2026.04.20.16:42:21 Info: hps_0: "<b>soc_system</b>" instantiated <b>altera_hps</b> "<b>hps_0</b>"
2026.04.20.16:42:21 Info: i2s_clkctrl_apb_0: "<b>soc_system</b>" instantiated <b>i2s_clkctrl_api</b> "<b>i2s_clkctrl_apb_0</b>"
2026.04.20.16:42:21 Info: i2s_output_apb_0: "<b>soc_system</b>" instantiated <b>i2s_output_apb</b> "<b>i2s_output_apb_0</b>"
2026.04.20.16:42:21 Info: sysid_qsys_0: "<b>soc_system</b>" instantiated <b>altera_avalon_sysid_qsys</b> "<b>sysid_qsys_0</b>"
2026.04.20.16:42:21 Info: avalon_st_adapter: Inserting error_adapter: error_adapter_0
2026.04.20.16:42:21 Info: mm_interconnect_0: "<b>soc_system</b>" instantiated <b>altera_mm_interconnect</b> "<b>mm_interconnect_0</b>"
2026.04.20.16:42:21 Info: irq_mapper: "<b>soc_system</b>" instantiated <b>altera_irq_mapper</b> "<b>irq_mapper</b>"
2026.04.20.16:42:21 Info: rst_controller: "<b>soc_system</b>" instantiated <b>altera_reset_controller</b> "<b>rst_controller</b>"
2026.04.20.16:42:21 Info: fpga_interfaces: "<b>hps_0</b>" instantiated <b>altera_interface_generator</b> "<b>fpga_interfaces</b>"
2026.04.20.16:42:21 Info: hps_io: "<b>hps_0</b>" instantiated <b>altera_hps_io</b> "<b>hps_io</b>"
2026.04.20.16:42:21 Info: i2s_output_apb_0_apb_slave_translator: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_apb_translator</b> "<b>i2s_output_apb_0_apb_slave_translator</b>"
2026.04.20.16:42:22 Info: sysid_qsys_0_control_slave_translator: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_slave_translator</b> "<b>sysid_qsys_0_control_slave_translator</b>"
2026.04.20.16:42:22 Info: hps_0_h2f_lw_axi_master_agent: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_axi_master_ni</b> "<b>hps_0_h2f_lw_axi_master_agent</b>"
2026.04.20.16:42:22 Info: i2s_output_apb_0_apb_slave_agent: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_apb_slave_agent</b> "<b>i2s_output_apb_0_apb_slave_agent</b>"
2026.04.20.16:42:22 Info: sysid_qsys_0_control_slave_agent: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_slave_agent</b> "<b>sysid_qsys_0_control_slave_agent</b>"
2026.04.20.16:42:22 Info: sysid_qsys_0_control_slave_agent_rsp_fifo: "<b>mm_interconnect_0</b>" instantiated <b>altera_avalon_sc_fifo</b> "<b>sysid_qsys_0_control_slave_agent_rsp_fifo</b>"
2026.04.20.16:42:22 Info: router: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_router</b> "<b>router</b>"
2026.04.20.16:42:22 Info: router_002: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_router</b> "<b>router_002</b>"
2026.04.20.16:42:22 Info: hps_0_h2f_lw_axi_master_wr_limiter: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_traffic_limiter</b> "<b>hps_0_h2f_lw_axi_master_wr_limiter</b>"
2026.04.20.16:42:22 Info: Reusing file <b>/homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/soc_system/synthesis/submodules/altera_avalon_sc_fifo.v</b>
2026.04.20.16:42:22 Info: i2s_output_apb_0_apb_slave_burst_adapter: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_burst_adapter</b> "<b>i2s_output_apb_0_apb_slave_burst_adapter</b>"
2026.04.20.16:42:22 Info: Reusing file <b>/homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/soc_system/synthesis/submodules/altera_merlin_address_alignment.sv</b>
2026.04.20.16:42:22 Info: Reusing file <b>/homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/soc_system/synthesis/submodules/altera_avalon_st_pipeline_base.v</b>
2026.04.20.16:42:22 Info: cmd_demux: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_demultiplexer</b> "<b>cmd_demux</b>"
2026.04.20.16:42:22 Info: cmd_mux: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_multiplexer</b> "<b>cmd_mux</b>"
2026.04.20.16:42:22 Info: rsp_demux: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_demultiplexer</b> "<b>rsp_demux</b>"
2026.04.20.16:42:22 Info: rsp_mux: "<b>mm_interconnect_0</b>" instantiated <b>altera_merlin_multiplexer</b> "<b>rsp_mux</b>"
2026.04.20.16:42:22 Info: Reusing file <b>/homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/soc_system/synthesis/submodules/altera_merlin_arbitrator.sv</b>
2026.04.20.16:42:22 Info: avalon_st_adapter: "<b>mm_interconnect_0</b>" instantiated <b>altera_avalon_st_adapter</b> "<b>avalon_st_adapter</b>"
2026.04.20.16:42:31 Info: border: "<b>hps_io</b>" instantiated <b>altera_interface_generator</b> "<b>border</b>"
2026.04.20.16:42:32 Info: error_adapter_0: "<b>avalon_st_adapter</b>" instantiated <b>error_adapter</b> "<b>error_adapter_0</b>"
2026.04.20.16:42:32 Info: soc_system: Done "<b>soc_system</b>" with 28 modules, 88 files
2026.04.20.16:42:32 Info: qsys-generate succeeded.
2026.04.20.16:42:32 Info: Finished: <b>Create HDL design files for synthesis</b>
[jy3557@micro21 DE1_SOC_Linux_Audio]$ 
[jy3557@micro21 DE1_SOC_Linux_Audio]$ find soc_system -maxdepth 3 -type f | head
soc_system/soc_system_generation.rpt
soc_system/synthesis/soc_system_hps_0_hps.svd
soc_system/synthesis/soc_system.regmap
soc_system/synthesis/soc_system.v
soc_system/synthesis/submodules/soc_system_audio_pll.v
soc_system/synthesis/submodules/soc_system_audio_pll.qip
soc_system/synthesis/submodules/soc_system_hps_0.v
soc_system/synthesis/submodules/i2s_clkctrl_apb.v
soc_system/synthesis/submodules/i2s_output_apb.v
soc_system/synthesis/submodules/capture_fifo.qip
[jy3557@micro21 DE1_SOC_Linux_Audio]$ quartus_sh --flow compile DE1_SOC_Linux_Audio
Info: *******************************************************************
Info: Running Quartus Prime Shell
    Info: Version 21.1.0 Build 842 10/21/2021 SJ Lite Edition
    Info: Copyright (C) 2021  Intel Corporation. All rights reserved.
    Info: Your use of Intel Corporation's design tools, logic functions 
    Info: and other software and tools, and any partner logic 
    Info: functions, and any output files from any of the foregoing 
    Info: (including device programming or simulation files), and any 
    Info: associated documentation or information are expressly subject 
    Info: to the terms and conditions of the Intel Program License 
    Info: Subscription Agreement, the Intel Quartus Prime License Agreement,
    Info: the Intel FPGA IP License Agreement, or other applicable license
    Info: agreement, including, without limitation, that your use is for
    Info: the sole purpose of programming logic devices manufactured by
    Info: Intel and sold by Intel or its authorized distributors.  Please
    Info: refer to the applicable agreement for further details, at
    Info: https://fpgasoftware.intel.com/eula.
    Info: Processing started: Mon Apr 20 16:43:07 2026
Info: Command: quartus_sh --flow compile DE1_SOC_Linux_Audio
Info: Quartus(args): compile DE1_SOC_Linux_Audio
Info: Project Name = /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio
Info: Revision Name = DE1_SOC_Linux_Audio
Info: *******************************************************************
Info: Running Quartus Prime Analysis & Synthesis
    Info: Version 21.1.0 Build 842 10/21/2021 SJ Lite Edition
    Info: Processing started: Mon Apr 20 16:43:09 2026
Info: Command: quartus_map --read_settings_files=on --write_settings_files=off DE1_SOC_Linux_Audio -c DE1_SOC_Linux_Audio
Warning (18236): Number of processors has not been specified which may cause overloading on shared machines.  Set the global assignment NUM_PARALLEL_PROCESSORS in your QSF to an appropriate value for best performance.
Info (20030): Parallel compilation is enabled and will use 12 of the 12 processors detected
Info (12248): Elaborating Platform Designer system entity "soc_system.qsys"
Info (12250): 2026.04.20.16:43:22 Progress: Loading DE1_SOC_Linux_Audio/soc_system.qsys
Info (12250): 2026.04.20.16:43:22 Progress: Reading input file
Info (12250): 2026.04.20.16:43:22 Progress: Adding audio_pll [altera_pll 14.1]
Warning (12251): Audio_pll: Used altera_pll 21.1 (instead of 14.1)
Info (12250): 2026.04.20.16:43:24 Progress: Parameterizing module audio_pll
Info (12250): 2026.04.20.16:43:24 Progress: Adding clk_0 [clock_source 14.1]
Warning (12251): Clk_0: Used clock_source 21.1 (instead of 14.1)
Info (12250): 2026.04.20.16:43:24 Progress: Parameterizing module clk_0
Info (12250): 2026.04.20.16:43:24 Progress: Adding clock_bridge_0 [altera_clock_bridge 14.1]
Warning (12251): Clock_bridge_0: Used altera_clock_bridge 21.1 (instead of 14.1)
Info (12250): 2026.04.20.16:43:24 Progress: Parameterizing module clock_bridge_0
Info (12250): 2026.04.20.16:43:24 Progress: Adding clock_bridge_44 [altera_clock_bridge 14.1]
Warning (12251): Clock_bridge_44: Used altera_clock_bridge 21.1 (instead of 14.1)
Info (12250): 2026.04.20.16:43:24 Progress: Parameterizing module clock_bridge_44
Info (12250): 2026.04.20.16:43:24 Progress: Adding clock_bridge_48 [altera_clock_bridge 14.1]
Warning (12251): Clock_bridge_48: Used altera_clock_bridge 21.1 (instead of 14.1)
Info (12250): 2026.04.20.16:43:24 Progress: Parameterizing module clock_bridge_48
Info (12250): 2026.04.20.16:43:24 Progress: Adding hps_0 [altera_hps 14.1]
Warning (12251): Hps_0: Used altera_hps 21.1 (instead of 14.1)
Info (12250): 2026.04.20.16:43:24 Progress: Parameterizing module hps_0
Info (12250): 2026.04.20.16:43:24 Progress: Adding i2s_clkctrl_apb_0 [i2s_clkctrl_api 1.7]
Warning (12251): I2s_clkctrl_apb_0: Component type i2s_clkctrl_api is not in the library
Info (12250): 2026.04.20.16:43:24 Progress: Parameterizing module i2s_clkctrl_apb_0
Info (12250): 2026.04.20.16:43:24 Progress: Adding i2s_output_apb_0 [i2s_output_apb 1.4]
Warning (12251): I2s_output_apb_0: Component type i2s_output_apb is not in the library
Info (12250): 2026.04.20.16:43:24 Progress: Parameterizing module i2s_output_apb_0
Info (12250): 2026.04.20.16:43:24 Progress: Adding sysid_qsys_0 [altera_avalon_sysid_qsys 14.1]
Warning (12251): Sysid_qsys_0: Used altera_avalon_sysid_qsys 21.1 (instead of 14.1)
Info (12250): 2026.04.20.16:43:24 Progress: Parameterizing module sysid_qsys_0
Info (12250): 2026.04.20.16:43:24 Progress: Building connections
Info (12250): 2026.04.20.16:43:24 Progress: Parameterizing connections
Info (12250): 2026.04.20.16:43:24 Progress: Validating
Info (12250): 2026.04.20.16:43:31 Progress: Done reading input file
Info (12250): Soc_system.audio_pll: The legal reference clock frequency is 5.0 MHz..800.0 MHz
Warning (12251): Soc_system.audio_pll: Able to implement PLL - Actual settings differ from Requested settings
Info (12250): Soc_system.hps_0: HPS Main PLL counter settings: n = 0  m = 73
Info (12250): Soc_system.hps_0: HPS peripherial PLL counter settings: n = 0  m = 39
Warning (12251): Soc_system.hps_0: "Configuration/HPS-to-FPGA user 0 clock frequency" (desired_cfg_clk_mhz) requested 100.0 MHz, but only achieved 97.368421 MHz
Warning (12251): Soc_system.hps_0: "QSPI clock frequency" (desired_qspi_clk_mhz) requested 400.0 MHz, but only achieved 370.0 MHz
Warning (12251): Soc_system.hps_0: 1 or more output clock frequencies cannot be achieved precisely, consider revising desired output clock frequencies.
Error (12252): Soc_system.i2s_clkctrl_apb_0: Component i2s_clkctrl_api 1.7 not found or could not be instantiated
Error (12252): Soc_system.i2s_output_apb_0: Component i2s_output_apb 1.4 not found or could not be instantiated
Info (12250): Soc_system.sysid_qsys_0: System ID is not assigned automatically. Edit the System ID parameter to provide a unique ID
Info (12250): Soc_system.sysid_qsys_0: Time stamp will be automatically updated when this component is generated.
Info (12250): Soc_system: Generating soc_system "soc_system" for QUARTUS_SYNTH
Info (12250): Exception in thread "main" java.lang.NullPointerException
Info (12250):     at com.altera.sopcmodel.transforms.mm.MerlinSystem.attachBorderModule(MerlinSystem.java:460)
Info (12250):     at com.altera.sopcmodel.transforms.mm.MerlinSystem.recreateBorder(MerlinSystem.java:450)
Info (12250):     at com.altera.sopcmodel.transforms.mm.MerlinSystem.recreateBorder(MerlinSystem.java:427)
Info (12250):     at com.altera.sopcmodel.transforms.avalon.InitialInterconnectTransform.cloneConnectionPoint(InitialInterconnectTransform.java:161)
Info (12250):     at com.altera.sopcmodel.transforms.avalon.InitialInterconnectTransform.cloneSlaves(InitialInterconnectTransform.java:153)
Info (12250):     at com.altera.sopcmodel.transforms.avalon.InitialInterconnectTransform.doExecute(InitialInterconnectTransform.java:96)
Info (12250):     at com.altera.sopcmodel.transforms.SopcTransformStep.execute(SopcTransformStep.java:66)
Info (12250):     at com.altera.sopcmodel.transforms.SopcTransformList.doExecute(SopcTransformList.java:112)
Info (12250):     at com.altera.sopcmodel.transforms.SopcTransformStep.execute(SopcTransformStep.java:66)
Info (12250):     at com.altera.sopcmodel.transforms.mm.MMTransform.doExecute(MMTransform.java:106)
Info (12250):     at com.altera.sopcmodel.transforms.SopcTransformStep.execute(SopcTransformStep.java:66)
Info (12250):     at com.altera.sopcmodel.transforms.SopcTransformList.doExecute(SopcTransformList.java:112)
Info (12250):     at com.altera.sopcmodel.transforms.SopcTransformStep.execute(SopcTransformStep.java:66)
Info (12250):     at com.altera.sopcmodel.transforms.avalon.AvalonTransform.doExecute(AvalonTransform.java:45)
Info (12250):     at com.altera.sopcmodel.transforms.SopcTransformStep.execute(SopcTransformStep.java:66)
Info (12250):     at com.altera.sopcmodel.ensemble.EnsembleUtils.doTransform(EnsembleUtils.java:1357)
Info (12250):     at com.altera.sopc.generator.EnsembleGenerationFileSet2.attemptTransform(EnsembleGenerationFileSet2.java:90)
Info (12250):     at com.altera.sopc.generator.EnsembleGenerationFileSet2.generate(EnsembleGenerationFileSet2.java:51)
Info (12250):     at com.altera.sopc.generator.FileSet2.generate(FileSet2.java:150)
Info (12250):     at com.altera.sopc.generator.Sellafield.generate(Sellafield.java:366)
Info (12250):     at com.altera.sopcmodel.sbtools.sbgenerate.SbGenerate.performGeneration(SbGenerate.java:521)
Info (12250):     at com.altera.sopcmodel.sbtools.sbgenerate.SbGenerate.act(SbGenerate.java:467)
Info (12250):     at com.altera.utilities.AltCmdLineToolBase.runTheTool(AltCmdLineToolBase.java:718)
Info (12250):     at com.altera.sopcmodel.sbtools.sbgenerate.SbGenerate.main(SbGenerate.java:981)
Info (12249): Finished elaborating Platform Designer system entity "soc_system.qsys"
Info (12021): Found 3 design units, including 3 entities, in source file /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/i2s_clkctrl_apb.v
    Info (12023): Found entity 1: i2s_clkctrl_apb File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/i2s_clkctrl_apb.v Line: 1
    Info (12023): Found entity 2: audio_clock_generator File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/i2s_clkctrl_apb.v Line: 115
    Info (12023): Found entity 3: clk_divider File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/i2s_clkctrl_apb.v Line: 158
Info (12021): Found 1 design units, including 1 entities, in source file /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/capture_fifo.v
    Info (12023): Found entity 1: capture_fifo File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/capture_fifo.v Line: 40
Info (12021): Found 1 design units, including 1 entities, in source file /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/i2s_shift_in.v
    Info (12023): Found entity 1: i2s_shift_in File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/i2s_shift_in.v Line: 7
Info (12021): Found 1 design units, including 1 entities, in source file /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/i2s_shift_out.v
    Info (12023): Found entity 1: i2s_shift_out File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/i2s_shift_out.v Line: 9
Info (12021): Found 1 design units, including 1 entities, in source file /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/i2s_output_apb.v
    Info (12023): Found entity 1: i2s_output_apb File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/i2s_output_apb.v Line: 1
Warning (10275): Verilog HDL Module Instantiation warning at DE1_SOC_Linux_Audio.v(330): ignored dangling comma in List of Port Connections File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 330
Info (12021): Found 1 design units, including 1 entities, in source file DE1_SOC_Linux_Audio.v
    Info (12023): Found entity 1: DE1_SOC_Linux_Audio File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 1
Info (12021): Found 1 design units, including 1 entities, in source file /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/playback_fifo.v
    Info (12023): Found entity 1: playback_fifo File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/playback_fifo.v Line: 40
Warning (10236): Verilog HDL Implicit Net warning at i2s_clkctrl_apb.v(146): created implicit net for "lrclk" File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/cores/i2s/i2s_clkctrl_apb.v Line: 146
Warning (10236): Verilog HDL Implicit Net warning at DE1_SOC_Linux_Audio.v(407): created implicit net for "i2s_playback_fifo_ack" File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 407
Warning (10236): Verilog HDL Implicit Net warning at DE1_SOC_Linux_Audio.v(419): created implicit net for "i2s_capture_fifo_write" File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 419
Info (12127): Elaborating entity "DE1_SOC_Linux_Audio" for the top level hierarchy
Warning (10034): Output port "DRAM_ADDR" at DE1_SOC_Linux_Audio.v(23) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 23
Warning (10034): Output port "DRAM_BA" at DE1_SOC_Linux_Audio.v(24) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 24
Warning (10034): Output port "HEX0" at DE1_SOC_Linux_Audio.v(43) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 43
Warning (10034): Output port "HEX1" at DE1_SOC_Linux_Audio.v(44) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 44
Warning (10034): Output port "HEX2" at DE1_SOC_Linux_Audio.v(45) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 45
Warning (10034): Output port "HEX3" at DE1_SOC_Linux_Audio.v(46) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 46
Warning (10034): Output port "HEX4" at DE1_SOC_Linux_Audio.v(47) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 47
Warning (10034): Output port "HEX5" at DE1_SOC_Linux_Audio.v(48) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 48
Warning (10034): Output port "LEDR" at DE1_SOC_Linux_Audio.v(58) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 58
Warning (10034): Output port "VGA_B" at DE1_SOC_Linux_Audio.v(77) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 77
Warning (10034): Output port "VGA_G" at DE1_SOC_Linux_Audio.v(80) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 80
Warning (10034): Output port "VGA_R" at DE1_SOC_Linux_Audio.v(82) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 82
Warning (10034): Output port "ADC_DIN" at DE1_SOC_Linux_Audio.v(4) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 4
Warning (10034): Output port "ADC_SCLK" at DE1_SOC_Linux_Audio.v(6) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 6
Warning (10034): Output port "DRAM_CAS_N" at DE1_SOC_Linux_Audio.v(25) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 25
Warning (10034): Output port "DRAM_CKE" at DE1_SOC_Linux_Audio.v(26) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 26
Warning (10034): Output port "DRAM_CLK" at DE1_SOC_Linux_Audio.v(27) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 27
Warning (10034): Output port "DRAM_CS_N" at DE1_SOC_Linux_Audio.v(28) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 28
Warning (10034): Output port "DRAM_LDQM" at DE1_SOC_Linux_Audio.v(30) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 30
Warning (10034): Output port "DRAM_RAS_N" at DE1_SOC_Linux_Audio.v(31) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 31
Warning (10034): Output port "DRAM_UDQM" at DE1_SOC_Linux_Audio.v(32) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 32
Warning (10034): Output port "DRAM_WE_N" at DE1_SOC_Linux_Audio.v(33) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 33
Warning (10034): Output port "FAN_CTRL" at DE1_SOC_Linux_Audio.v(36) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 36
Warning (10034): Output port "FPGA_I2C_SCLK" at DE1_SOC_Linux_Audio.v(39) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 39
Warning (10034): Output port "IRDA_TXD" at DE1_SOC_Linux_Audio.v(52) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 52
Warning (10034): Output port "TD_RESET_N" at DE1_SOC_Linux_Audio.v(73) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 73
Warning (10034): Output port "VGA_BLANK_N" at DE1_SOC_Linux_Audio.v(78) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 78
Warning (10034): Output port "VGA_CLK" at DE1_SOC_Linux_Audio.v(79) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 79
Warning (10034): Output port "VGA_HS" at DE1_SOC_Linux_Audio.v(81) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 81
Warning (10034): Output port "VGA_SYNC_N" at DE1_SOC_Linux_Audio.v(83) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 83
Warning (10034): Output port "VGA_VS" at DE1_SOC_Linux_Audio.v(84) has no driver File: /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio.v Line: 84
Error (12153): Can't elaborate top-level user hierarchy
Error: Quartus Prime Analysis & Synthesis was unsuccessful. 3 errors, 49 warnings
    Error: Peak virtual memory: 681 megabytes
    Error: Processing ended: Mon Apr 20 16:43:37 2026
    Error: Elapsed time: 00:00:28
    Error: Total CPU time (on all processors): 00:00:36
Error (293001): Quartus Prime Full Compilation was unsuccessful. 5 errors, 49 warnings
Error: Flow compile (for project /homes/user/stud/fall25/jy3557/Desktop/Embedded_System/feature_sounds/sound/DE1_SOC_Linux_Audio/DE1_SOC_Linux_Audio) was not successful
Error: ERROR: Error(s) found while running an executable. See report file(s) for error message(s). Message log indicates which executable was run last.

Error (23031): Evaluation of Tcl script /tools/intel/intelFPGA/21.1/quartus/common/tcl/internal/qsh_flow.tcl unsuccessful
Error: Quartus Prime Shell was unsuccessful. 12 errors, 49 warnings
    Error: Peak virtual memory: 762 megabytes
    Error: Processing ended: Mon Apr 20 16:43:38 2026
    Error: Elapsed time: 00:00:31
    Error: Total CPU time (on all processors): 00:00:37


