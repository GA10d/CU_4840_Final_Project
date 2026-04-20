PROJECT = DE1_SOC_Linux_Audio
QSYS_SYSTEM = soc_system

QSYS = $(QSYS_SYSTEM).qsys
SOPCINFO = $(QSYS_SYSTEM).sopcinfo
QIP = $(QSYS_SYSTEM)/synthesis/$(QSYS_SYSTEM).qip
HPS_PIN_TCL = $(QSYS_SYSTEM)/synthesis/submodules/hps_sdram_p0_pin_assignments.tcl

QPF = $(PROJECT).qpf
QSF = $(PROJECT).qsf
SDC = $(PROJECT).sdc

SOF = output_files/$(PROJECT).sof
SOF_FALLBACK = $(PROJECT).sof
RBF = $(QSYS_SYSTEM).rbf

MANUAL_DTS = ../socfpga_cyclone5_DE1-SoC.dts
DTB = $(notdir $(MANUAL_DTS:.dts=.dtb))

.PHONY: all qsys project quartus sof rbf dtb clean

all: quartus

qsys: $(SOPCINFO)

$(SOPCINFO) $(QIP) $(HPS_PIN_TCL): $(QSYS)
	rm -rf $(SOPCINFO) $(QSYS_SYSTEM)/
	qsys-generate $(QSYS) --synthesis=VERILOG

project: $(QPF) $(QSF) $(SDC) $(HPS_PIN_TCL)
	quartus_map $(PROJECT)
	quartus_sta -t $(HPS_PIN_TCL) $(PROJECT)

sof: $(SOF)
quartus: $(SOF)

$(SOF): $(QPF) $(QSF) $(SDC) $(QIP) $(HPS_PIN_TCL)
	quartus_sh --flow compile $(PROJECT)
	if [ ! -f "$@" ] && [ -f "$(SOF_FALLBACK)" ]; then \
		mkdir -p $(dir $@); \
		cp "$(SOF_FALLBACK)" "$@"; \
	fi

rbf: $(RBF)

$(RBF): $(SOF)
	sof_path="$(SOF)"; \
	if [ ! -f "$$sof_path" ] && [ -f "$(SOF_FALLBACK)" ]; then \
		sof_path="$(SOF_FALLBACK)"; \
	fi; \
	quartus_cpf -c "$$sof_path" "$@"

dtb: $(DTB)

$(DTB): $(MANUAL_DTS)
	dtc -I dts -O dtb -o "$@" "$<"

clean:
	rm -rf db incremental_db output_files $(SOPCINFO) $(QSYS_SYSTEM)/ $(RBF) $(DTB)
