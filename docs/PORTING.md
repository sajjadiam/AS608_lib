# Platform integration

[Port contract](../src/as608_port.h) · [Recovery](ERROR_HANDLING.md)

The core is ISO C11 and platform-independent. The generic port contract
is independent of the optional, single-instance STM32F103 test adapter
provided under `examples/stm32f103/`. The historical
STM32F103C8T6 configuration was USART1/USART2, 57600 baud, 8N2; verify
the actual module's voltage levels, pinout, supply, baud and framing
against its board documentation before connecting hardware. Processor
datasheet ratings alone do not establish breakout-module ratings.

## Required callbacks

| Callback | Positive result | Zero | Negative |
| --- | --- | --- | --- |
| try_write | Exact accepted prefix, <= requested count | No space/progress | Latched I/O fault |
| try_read | Exact consumed prefix, <= requested count | No data | Latched I/O fault |
| tx_idle | Exactly 1: physical completion | Still pending | Latched I/O fault |
| clock_ms | uint32_t monotonic milliseconds (all values valid) | Valid tick | Not applicable |

Provide all four callbacks. The driver copies the descriptor, not the
objects referenced by user. Serialize all driver calls on the same
context; do not re-enter from a callback or ISR. Return promptly without
waiting on queues, UART, locks, DMA or incoming data.

## Suggested UART adapter

```text
Application task -> try_write -> owned TX queue -> IRQ/DMA -> USART
Application task <- try_read  <- owned RX ring  <- IRQ/DMA <- USART
                            shared latched error state
```

TX: copy accepted bytes into port-owned storage before returning or
transfer them synchronously to hardware. Do not give DMA a pointer
borrowed from the driver. Queue bytes preserve exact order, including
across frames. RX: consume a prefix from the ring; do not start DMA into
the borrowed output pointer. Keep reception active while transmitting.

Use a concurrency strategy appropriate to the MCU and execution contexts:
short interrupt-masked critical sections or a correctly designed
single-producer/single-consumer ring with proven publication ordering.
Volatile alone is insufficient. For DMA, use coherent producer snapshots
and account for wrap and overwritten data. On cached MCUs additionally
handle cache maintenance and DMA memory visibility. These are design
requirements, not a verified ring implementation.

## Physical transmission completion

```text
tx_idle = no error AND queue empty AND no outstanding DMA/IT AND USART TC
```

Return a negative fault before considering idle. TXE only indicates an
empty data register; DMA transfer-complete does not guarantee the last
stop bit has left the USART. A consistent snapshot must not race with
an ISR that starts another transfer. Reference: ST RM0008 Rev.21,
USART transmitter pp. 792–793 and DMA transmission section.

## Scheduling and buffer sizing

For baud B and F bits per serial character (start, data, parity, stop),
maximum continuous byte rate is B/F. For 57600 baud and 8N2, F=11,
rate approximately 5236.36 bytes/s. A 267-byte maximum frame takes
approximately 50.99 ms on the wire, excluding pauses. These are ideal
line-rate calculations, not measured MCU performance.

For a ring with usable capacity C, require

```text
C >= ceil((B/F) * T_blackout) + M
```

where T_blackout is the maximum time in seconds with no RX consumption
and M covers already occupied bytes, burst/interrupt jitter and other
implementation margins. Example: 20 ms continuous reception at 57600
8N2 adds 105 bytes (rounded up), before margin. A ring with 128 slots
and a reserved empty slot has only 127 usable bytes. No fixed ring size
is universally safe. Sustained service throughput must also exceed the
incoming rate with margin; otherwise any finite ring can eventually
overflow. Header and body consume in separate polls. Measure scheduling
jitter, actual accepted bytes per poll and ISR/DMA latency.

Size the whole-job deadline for TX+RX wire time, sensor processing,
inter-packet delays, queueing and scheduler latency. Clock must continue
during jobs, including low-power modes. Do not reset it during recovery.
Observe deadlines before an entire uint32_t wrap can hide time.

## Errors and recovery

Latch framing, parity, noise, overrun, DMA and ring overflow errors.
Report them on the next I/O callback, regardless of direction. Do not
return zero to hide a lost byte. The core does not continuously monitor
the port while idle. See ERROR_HANDLING.md for recovery sequencing.

## Bring-up acceptance criteria

- Verify voltage and interface settings; use an isolated test module.
- Confirm ordered partial TX/RX, zero progress and bounded callbacks.
- Measure last-stop-bit completion versus reported tx_idle.
- Inject RX overflow, UART error and abort with pending DMA; observe
  fault propagation and no stale bytes after physical recovery.
- Validate VfyPwd/ReadSysPara and then non-destructive commands.
- Verify download ACK profile before download or composite write.
- Demonstrate separate contexts and independent USART sessions under load.

## STM32F103 Blue Pill reference test port

[`as608_port_stm32f103.c`](../examples/stm32f103/as608_port_stm32f103.c)
is a deliberately single-instance LL/CMSIS adapter for hardware validation,
not a production UART framework. It is not part of the core CMake target.
The application owns GPIO, RCC, baud/framing, NVIC and physical recovery.
It uses `HAL_GetTick()` only for the timebase and does not use HAL UART I/O.
The default USART is USART1; change `AS608_TEST_UART` at compile time if
needed and route the matching IRQ. The private RX/TX rings have 511/127
usable bytes respectively. At 57600 8N2, a previously empty RX ring
provides approximately 97.6 ms of ideal buffering, not an ISR latency
allowance. UART hardware still requires timely service of each character.

Copy the C file into an STM32CubeIDE application and add the core src
directory to include paths. Configure asynchronous full-duplex 8-bit
no-parity USART (stop bits and baud verified against the sensor), no
DMA, no hardware flow control. HAL initialization may configure the
peripheral, but do not use HAL UART I/O or HAL_UART_IRQHandler on it.
The application must establish a fresh physical link and ensure the
peripheral interrupt sources and NVIC IRQ are disabled before init.
Declare the four functions in the application's existing header:

```c
bool as608_test_port_init(as608_port_t *port);
void as608_test_port_stop(void);
uint32_t as608_test_port_get_errors(void);
void as608_test_port_irq_handler(void);
```

For example, after UART configuration and sensor recovery (USART1):

```c
static as608_t sensor;
as608_config_t config = {
    .device_address = UINT32_C(0xFFFFFFFF),
    .timeout_ms = 5000U
};

if (!as608_test_port_init(&config.port)) {
    Error_Handler();
}
if (as608_init(&sensor, &config) != AS608_STATUS_OK) {
    as608_test_port_stop();
    Error_Handler();
}
NVIC_ClearPendingIRQ(USART1_IRQn);
NVIC_EnableIRQ(USART1_IRQn);
```

In the application's existing IRQ handler, call
`as608_test_port_irq_handler()` instead of HAL UART dispatch. Start
a non-destructive command and poll the core from one serialized
thread; do not call core APIs or port init/stop from the ISR.

For recovery: logically abort an active core job if necessary, stop
the port, disable its NVIC IRQ, reset/reconfigure the USART, recover
the sensor according to its documentation and establish a fresh link.
Only then reinitialize the port, confirm core recovery where required,
and re-enable NVIC. A port stop alone does not stop the hardware
shifter or establish a new sensor epoch. Error bits are raw USART SR
PE/FE/NE/ORE plus bit 16 for software RX overflow.

The ARM workflow compiles this adapter as a separate object using
STM32CubeF1 v1.8.6 with pinned CMSIS/device and HAL driver revisions.
This verifies compilation with actual vendor headers, not a bootable
firmware link, execution timing, electrical compatibility, or hardware
behavior. No target port or timing guarantee is certified by this
document; validate on the actual application and sensor.
