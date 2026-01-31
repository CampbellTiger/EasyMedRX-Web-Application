################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

CG_TOOL_ROOT := C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-armllvm_3.2.2.LTS

GEN_OPTS__FLAG := 
GEN_CMDS__FLAG := 

ORDERED_OBJS += \
"./add_func.o" \
"./syscfg/ti_drivers_config.o" \
"./syscfg/ti_net_config.o" \
"./syscfg/ti_sysbios_config.o" \
"./syscfg/ti_drivers_net_wifi_config.o" \
"./drive_gpio.o" \
"./httpget.o" \
"./httpsget.o" \
"./main_tirtos.o" \
"./platform.o" \
"../cc32xxs_tirtos7.cmd" \
$(GEN_CMDS__FLAG) \
-Wl,-lti_utils_build_linker.cmd.genlibs \
-Wl,-l"ti/devices/cc32xx/driverlib/ticlang/Release/driverlib.a" \
-Wl,-llibc.a \

-include ../makefile.init

RM := DEL /F
RMDIR := RMDIR /S/Q

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir_vars.mk
-include subdir_rules.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C55_DEPS)),)
-include $(C55_DEPS)
endif
ifneq ($(strip $(C_UPPER_DEPS)),)
-include $(C_UPPER_DEPS)
endif
ifneq ($(strip $(S67_DEPS)),)
-include $(S67_DEPS)
endif
ifneq ($(strip $(S62_DEPS)),)
-include $(S62_DEPS)
endif
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(OPT_DEPS)),)
-include $(OPT_DEPS)
endif
ifneq ($(strip $(C??_DEPS)),)
-include $(C??_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(S??_DEPS)),)
-include $(S??_DEPS)
endif
ifneq ($(strip $(C64_DEPS)),)
-include $(C64_DEPS)
endif
ifneq ($(strip $(CXX_DEPS)),)
-include $(CXX_DEPS)
endif
ifneq ($(strip $(S64_DEPS)),)
-include $(S64_DEPS)
endif
ifneq ($(strip $(INO_DEPS)),)
-include $(INO_DEPS)
endif
ifneq ($(strip $(CLA_DEPS)),)
-include $(CLA_DEPS)
endif
ifneq ($(strip $(S55_DEPS)),)
-include $(S55_DEPS)
endif
ifneq ($(strip $(SV7A_DEPS)),)
-include $(SV7A_DEPS)
endif
ifneq ($(strip $(C62_DEPS)),)
-include $(C62_DEPS)
endif
ifneq ($(strip $(C67_DEPS)),)
-include $(C67_DEPS)
endif
ifneq ($(strip $(PDE_DEPS)),)
-include $(PDE_DEPS)
endif
ifneq ($(strip $(K_DEPS)),)
-include $(K_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
ifneq ($(strip $(CC_DEPS)),)
-include $(CC_DEPS)
endif
ifneq ($(strip $(C++_DEPS)),)
-include $(C++_DEPS)
endif
ifneq ($(strip $(C43_DEPS)),)
-include $(C43_DEPS)
endif
ifneq ($(strip $(S43_DEPS)),)
-include $(S43_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(CPP_DEPS)),)
-include $(CPP_DEPS)
endif
ifneq ($(strip $(SA_DEPS)),)
-include $(SA_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
EXE_OUTPUTS += \
CC3220S_LAUNCHXL_REV1.out 

EXE_OUTPUTS__QUOTED += \
"CC3220S_LAUNCHXL_REV1.out" 

CUSTOM_TOOL_OUTPUTS_756764557 += \
CC3220S_LAUNCHXL_REV1.bin 

CUSTOM_TOOL_OUTPUTS_756764557__QUOTED += \
"CC3220S_LAUNCHXL_REV1.bin" 

CUSTOM_TOOL_OUTPUTS_299299689 += \
syscfg/CC3220S_LAUNCHXL_REV1.sli 

CUSTOM_TOOL_OUTPUTS_299299689__QUOTED += \
"syscfg\CC3220S_LAUNCHXL_REV1.sli" 


# All Target
all: $(OBJS) $(CMD_SRCS) $(GEN_CMDS)
	@$(MAKE) --no-print-directory -Onone "CC3220S_LAUNCHXL_REV1.out" secondary-outputs

# Tool invocations
CC3220S_LAUNCHXL_REV1.out: $(OBJS) $(CMD_SRCS) $(GEN_CMDS)
	@echo 'Building target: "$@"'
	@echo 'Invoking: Arm Linker'
	"C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-armllvm_3.2.2.LTS/bin/tiarmclang.exe" -mcpu=cortex-m4 -mfloat-abi=soft -mfpu=none -mlittle-endian -mthumb -Oz -DDeviceFamily_CC3220 -gdwarf-3 -march=armv7e-m -Wl,-m"CC3220S_LAUNCHXL_REV1.map" -Wl,-i"C:/ti/simplelink_cc32xx_sdk_7_10_00_13/source/ti/net/http/lib" -Wl,-i"C:/ti/simplelink_cc32xx_sdk_7_10_00_13/source/ti/net/lib" -Wl,-i"C:/ti/simplelink_cc32xx_sdk_7_10_00_13/source/ti/drivers/net/wifi/slnetif" -Wl,-i"C:/ti/simplelink_cc32xx_sdk_7_10_00_13/source" -Wl,-i"C:/ti/simplelink_cc32xx_sdk_7_10_00_13/source/ti/drivers/net/wifi" -Wl,-i"C:/ti/simplelink_cc32xx_sdk_7_10_00_13/kernel/tirtos7/packages" -Wl,-i"C:/Users/gwyli/workspace_v12/CC3220S_LAUNCHXL_REV1/MCU+Image/syscfg" -Wl,-i"C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-armllvm_3.2.2.LTS/lib" -Wl,--diag_wrap=off -Wl,--display_error_number -Wl,--warn_sections -Wl,--xml_link_info="CC3220S_LAUNCHXL_REV1_linkInfo.xml" -Wl,--rom_model -o "CC3220S_LAUNCHXL_REV1.out" $(ORDERED_OBJS)
	@echo 'Finished building target: "$@"'
	@echo ' '

CC3220S_LAUNCHXL_REV1.bin: $(EXE_OUTPUTS) CC3220S_LAUNCHXL_REV1.out
	@echo 'Building secondary target: "$@"'
	@echo 'Invoking: TI Arm ObjCopy'
	"C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-armllvm_3.2.2.LTS/bin/tiarmobjcopy" -O binary --only-section .text --only-section .const --only-section .cinit --only-section .rodata --only-section .resetVecs "CC3220S_LAUNCHXL_REV1.out" "CC3220S_LAUNCHXL_REV1.bin"
	@echo 'Finished building secondary target: "$@"'
	@echo ' '

syscfg/CC3220S_LAUNCHXL_REV1.sli: $(EXE_OUTPUTS) CC3220S_LAUNCHXL_REV1.bin syscfg/ti_drivers_net_wifi_config.json
	@echo 'Building secondary target: "$@"'
	@echo 'Invoking: Image Creator'
	"C:/ti/simplelink_cc32xx_sdk_7_10_00_13/source/ti/drivers/net/imagecreator/bin/SLImageCreator.exe" syscfg create_image --sdk_path "C:/ti/simplelink_cc32xx_sdk_7_10_00_13" --json "C:/Users/gwyli/workspace_v12/CC3220S_LAUNCHXL_REV1/MCU+Image/syscfg/ti_drivers_net_wifi_config.json" --file "C:/Users/gwyli/workspace_v12/CC3220S_LAUNCHXL_REV1/MCU+Image/syscfg/CC3220S_LAUNCHXL_REV1.sli" --mcu "C:/Users/gwyli/workspace_v12/CC3220S_LAUNCHXL_REV1/MCU+Image/CC3220S_LAUNCHXL_REV1.bin"
	@echo 'Finished building secondary target: "$@"'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(CUSTOM_TOOL_OUTPUTS_299299689__QUOTED)$(GEN_MISC_FILES__QUOTED)$(GEN_FILES__QUOTED)$(EXE_OUTPUTS__QUOTED)$(CUSTOM_TOOL_OUTPUTS_756764557__QUOTED)
	-$(RM) "add_func.o" "syscfg\ti_drivers_config.o" "syscfg\ti_net_config.o" "syscfg\ti_sysbios_config.o" "syscfg\ti_drivers_net_wifi_config.o" "drive_gpio.o" "httpget.o" "httpsget.o" "main_tirtos.o" "platform.o" 
	-$(RM) "add_func.d" "syscfg\ti_drivers_config.d" "syscfg\ti_net_config.d" "syscfg\ti_sysbios_config.d" "syscfg\ti_drivers_net_wifi_config.d" "drive_gpio.d" "httpget.d" "httpsget.d" "main_tirtos.d" "platform.d" 
	-$(RMDIR) $(GEN_MISC_DIRS__QUOTED)
	-@echo 'Finished clean'
	-@echo ' '

secondary-outputs: $(CUSTOM_TOOL_OUTPUTS_756764557) $(CUSTOM_TOOL_OUTPUTS_299299689)

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets

