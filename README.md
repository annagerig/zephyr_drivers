# zephyr_drivers

Zephyr drivers for the VT1 project (bridge vibration analysis on the
STM32U3). A west module, pulled in by the VT1 manifest (`firmware/west.yml`).

## Drivers

### STM32U3 HSP hardware FFT (`drivers/hsp`)

Real FFT on the HSP (Signal Processing Engine) of the STM32U3, as a Zephyr
device driver in the style of the in-tree STM32 drivers.

- API: `<vt1/drivers/hsp.h>`, `hsp_rfft_f32(dev, in, out, log2n)`, output in
  the CMSIS packed layout (`out[0]` DC, `out[1]` Nyquist, then re/im pairs).
- Devicetree: compatible `st,stm32u3-hsp` (binding in `dts/bindings/dsp/`).
  The SoC DTS has no HSP node yet, so the board overlay adds it:
  ```dts
  &{/soc} {
  	hsp1: hsp@4002c000 {
  		compatible = "st,stm32u3-hsp";
  		reg = <0x4002c000 0x400>;
  		clocks = <&rcc STM32_CLOCK(AHB1, 4)>;
  		interrupts = <77 0>;
  		status = "okay";
  	};
  };
  ```
- Kconfig: `CONFIG_HSP_STM32` (off by default, because the driver clocks and
  boots the HSP at startup), `CONFIG_HSP_STM32_INIT_PRIORITY`, log level.
  It selects `USE_STM32_HAL_HSP`, so hal_stm32 builds `stm32u3xx_hal_hsp.c`.
- Init: clock on, `HAL_HSP_Init`, load ST's firmware, boot, read the BRAM
  base and size from `HAL_HSP_GetCapabilities` (BRAM-AB at 0x200A0000,
  16 KB, so n <= 4096).
- RFFT: copy into BRAM, direct accelerator command (TYPE_2 output),
  synchronous wait, copy back.
- Requires Range 1 (HCLK > 48 MHz); the NUCLEO-U3C5ZI-Q runs at 96 MHz.

Verified on the NUCLEO-U3C5ZI-Q: output matches CMSIS-DSP `arm_rfft_fast_f32`
to ~1e-7 relative (not bit-identical), 1.1 to 1.8 times faster for n = 64..4096.

## Licenses

Own code: Apache-2.0. `drivers/hsp/st/` (HSP firmware microcode and command
definitions, FW 1.1.7) is from STMicroelectronics' STM32CubeU3 package under
ST's license terms.
