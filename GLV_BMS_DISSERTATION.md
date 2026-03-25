# GLV Battery Management System for Formula Student Electric

**EG4301 — DCP Dissertation**
**National University of Singapore, College of Design and Engineering**
**Innovation & Design Programme (iDP)**

---

## Abstract

Formula Student Electric (FSE) regulations mandate an isolated, monitored Grounded Low Voltage (GLV) system to power all vehicle electronics. This dissertation presents the design and firmware implementation of a custom 6S3P GLV Battery Management System (BMS) built around the Infineon TLE9012DQU cell monitoring IC and the STMicroelectronics STM32F446RET6 microcontroller. The pack employs LG M58T 21700 NMC cylindrical cells (5800 mAh, 4.2 V maximum, 2.5 V minimum), yielding 25.2 V nominal at 17.4 Ah capacity.

The work covers hardware architecture, low-level firmware drivers for UART-DMA-based TLE9012 communication, and a structured analysis of four critical firmware defects identified through register-level debugging: an incorrect PCVM base address causing zero-voltage readings, a mismatched cell count, and voltage protection thresholds calibrated for the wrong cell chemistry. An adaptive, charge-phase-only passive balancing algorithm is developed, grounded in the OCV curve slope of NMC cells at top-of-charge. The theoretical minimum PWM duty cycle for convergence within the CV charge phase is derived and validated analytically. Cell electrochemical parameters — including the OCV curve and ECM parameters — are sourced from the Batemo Cell Portal to replace estimated values with experimentally validated data, and the balancing algorithm is pre-validated in MATLAB simulation using these parameters before hardware deployment. Future integration paths for closed-loop SOC estimation via the onboard INA228 current sensor and CAN-based telemetry are identified.

**Keywords:** Battery Management System, Passive Cell Balancing, TLE9012DQU, STM32F446, Formula Student Electric, NMC, Grounded Low Voltage

---

## Table of Contents

1. Introduction
2. Literature Review
3. System Requirements and Design
4. Hardware Architecture
5. Firmware Architecture
6. Critical Defect Analysis and Corrective Actions
7. Cell Balancing Strategy
8. Testing and Validation
9. Discussion
10. Conclusions
11. References
12. Appendices
    - Appendix A — TLE9012DQU Register Map
    - Appendix B — Complete List of Firmware Changes Required
    - Appendix C — LG M58T Cell Specifications
    - Appendix D — Balancing Convergence Table
    - Appendix E — Batemo-Extracted OCV Table and ECM Parameters *(to be completed)*

---

## 1. Introduction

### 1.1 Background and Motivation

Formula Student Electric (FSE) is an international engineering design competition in which student teams design, build, and race a single-seat electric formula car. The competition is governed by the Formula SAE (FSAE) rulebook, which partitions onboard electrical systems into two domains: the High Voltage (HV) traction battery and the Grounded Low Voltage (GLV) system.

The GLV system powers all safety-critical electronics — the ECU, BMS, sensor arrays, CAN buses, display, and shutdown circuitry — that must remain operational regardless of HV state. A dedicated GLV Li-ion pack is therefore required, and FSAE rules explicitly mandate that this pack be monitored by an active Battery Management System capable of reporting cell voltages, detecting over- and under-voltage conditions, and communicating fault states over the vehicle network.

Building a custom GLV BMS offers several advantages over commercial solutions: weight reduction through targeted specification, direct integration with the team's CAN telemetry architecture, and full transparency of firmware for technical scrutiny by competition judges. The primary motivation of this project is therefore to produce a GLV BMS that is rule-compliant, electrically safe, and extensible for future competition seasons.

### 1.2 Problem Statement

The GLV BMS must fulfil the following core requirements derived from FSAE rules and pack specifications:

1. Monitor individual cell voltages across a 6S3P NMC pack in real time.
2. Detect and respond to over-voltage (OV) and under-voltage (UV) conditions.
3. Monitor pack temperature at multiple locations.
4. Implement passive cell balancing to prevent capacity divergence between series groups.
5. Communicate pack health over CAN to the vehicle's data acquisition system.
6. Operate reliably within the automotive temperature range without external cooling.

At the outset of this project, a partially implemented firmware existed for the TLE9012DQU cell monitoring IC. Investigation revealed that the firmware contained four critical defects that prevented any meaningful cell voltage measurement. These defects, and the design of a correct and complete balancing strategy on top of a repaired firmware foundation, constitute the primary technical contribution of this dissertation.

### 1.3 Objectives

1. Identify and formally document all firmware defects in the existing codebase.
2. Produce a fully corrected TLE9012DQU driver that accurately reads all six cell voltages.
3. Design a charge-phase-only passive balancing algorithm appropriate for NMC chemistry.
4. Derive the minimum balancing duty cycle required for guaranteed convergence within the CV charge phase.
5. Specify the architecture for future CAN telemetry and SOC estimation integration.

### 1.4 Scope and Constraints

This project covers firmware design and validation for the GLV BMS. PCB layout and schematic design are treated as given hardware inputs rather than variables under design. The INA228 current sensor and dual CAN transceivers are present on the PCB but their drivers fall outside the primary scope; their integration is addressed in the future work section. Cell characterisation testing (OCV curve mapping, ECM parameter identification) is outside scope; experimentally validated parameters for the LG M58T are sourced from the Batemo Cell Portal [11] where available, and datasheet values are used otherwise. The Batemo MATLAB toolbox is used for pre-hardware simulation of the balancing algorithm.

---

## 2. Literature Review

### 2.1 Lithium-Ion Cell Chemistry — NMC

The LG M58T is a 21700-format cylindrical cell based on Nickel Manganese Cobalt oxide (NMC) cathode chemistry. NMC cells offer a balance between energy density, cycle life, and thermal stability that makes them well suited for motorsport GLV applications [1].

Key electrochemical characteristics relevant to BMS design are:

- **Nominal voltage:** 3.63 V
- **Maximum charge voltage:** 4.20 V (beyond which lithium plating and electrolyte oxidation accelerate degradation)
- **Minimum discharge voltage:** 2.50 V (below which copper dissolution at the anode causes irreversible capacity loss)
- **OCV curve shape:** NMC exhibits a relatively flat plateau between 3.60–4.00 V (approximately 20–80% SOC), then a steep rise above 4.00 V. This steep top-of-charge region, where dV/d(SOC%) ≈ 40–100 mV per 1% SOC, is the most informative voltage range for balancing decisions [2].

The distinction from LiFePO4 (LFP) is critical for BMS configuration: LFP operates between 2.5–3.65 V with an even flatter OCV plateau. A BMS misconfigured for LFP thresholds on an NMC cell — as found in the existing firmware — will clamp the pack below 3.4 V/cell, permanently restricting capacity to approximately 30–40% of nominal.

### 2.2 Battery Management System Architectures

BMS architectures are broadly classified by monitoring topology [3]:

| Architecture | Description | Typical Application |
|---|---|---|
| Centralised | Single IC monitors all cells via direct tap wiring | Small packs (<12S), low cost |
| Modular | Multiple ICs each monitoring a sub-stack, communication via isolated bus | Medium packs (12–96S) |
| Daisy-chain | ICs linked in a chain; each passes data to the next | Large packs (EV traction, ≥96S) |

For a 6S GLV pack, a centralised architecture using a single TLE9012DQU is the appropriate choice. The IC supports up to 12 series cells and directly interfaces with the MCU via a single-wire UART at up to 1 Mbps.

### 2.3 Cell Balancing Methods

Cell imbalance in a series-connected pack arises from manufacturing tolerances, temperature gradients, and differential ageing. If unaddressed, the weakest cell limits the pack's usable capacity on every cycle and will reach its protection threshold before other cells, triggering premature pack shutdown [4].

**Passive (dissipative) balancing** connects a bypass resistor in parallel with each cell via a controllable switch. Excess charge is dissipated as heat. Advantages: simple, low-cost hardware. Disadvantages: energy is lost, resistor thermal dissipation must be managed, and balancing rate is limited by resistor value.

**Active balancing** transfers charge between cells using switched-mode converters (buck-boost, flyback, or capacitive ladder circuits). Advantages: no energy waste, faster convergence. Disadvantages: significantly higher hardware complexity and cost. For a GLV pack where the charger is stationary and balancing time is not critical, passive balancing is the standard industry choice.

**Balancing trigger strategies** include:

1. **Voltage threshold:** Balance when any cell exceeds (min + ΔV). Simple; unreliable in the flat OCV region.
2. **Top-of-charge:** Balance only when all cells are ≥ 4.10 V (steep OCV region). Voltage differences reliably correspond to SOC differences here. Recommended for NMC.
3. **SOC-based:** Balance on ΔSOC estimated from Coulomb counting. Most accurate; requires a calibrated current sensor.

### 2.4 Infineon TLE9012DQU

The TLE9012DQU is an automotive-grade (AEC-Q100) multi-cell battery monitor IC capable of monitoring up to 12 series-connected cells [5]. Key features relevant to this design:

- **Cell voltage measurement:** 16-bit successive approximation ADC, 5.0 V full-scale, ±1.5 mV typical accuracy.
- **PCVM registers:** Per-Cell Voltage Measurement registers (0x19–0x24) store one 16-bit result per cell.
- **PART_CONFIG register (0x01):** Selects which of the 12 cell inputs are active. Inactive inputs are internally tied to avoid floating nodes.
- **BAL_SETTINGS register (0x16):** 12-bit field; bit n = 1 enables balancing on cell n.
- **BAL_PWM register (0x17):** Sets balancing duty cycle; upper byte is period, lower byte is on-time.
- **Communication:** Single-wire half-duplex UART at 1 Mbps with 8-bit CRC (polynomial 0x1D). All bytes are transmitted LSB-first (TLE9012 convention is reversed from standard UART).
- **Watchdog:** IC enters sleep mode if no valid frame is received within the watchdog period. Watchdog register (0x3D) must be written periodically.
- **External temperature sensing:** Five dedicated ADC channels (TMP0–TMP4) accept NTC thermistors referenced to VDDC.

The IC does not contain internal balancing switches. All balancing current flows through external components connected to the Gn gate drive output pins. In this design, 41 Ω resistors are connected directly from each cell tap to the corresponding Gn pin, forming a direct-discharge path when the IC activates the gate output.

### 2.5 STM32F446RET6

The STM32F446RET6 is an ARM Cortex-M4F microcontroller with an FPU, running at up to 180 MHz [6]. Features used in this design:

- UART1 and UART2 with DMA (used for TLE9012 communication)
- Hardware CAN2.0B controllers (CAN1, CAN2 — for future telemetry)
- I2C interface (for INA228 current sensor — future)
- Timer 5 (32-bit, used for microsecond-precision timing)
- 512 KB Flash / 128 KB SRAM

### 2.6 Batemo Cell Models

Batemo GmbH provides a library of physics-based, experimentally validated battery cell models accessible through the Batemo Cell Portal and a MATLAB/Simulink toolbox [11]. Each model is derived from electrochemical characterisation of the physical cell and includes:

- **OCV curve:** Open Circuit Voltage as a function of SOC (0–100%) and temperature (typically −10 °C to +60 °C), measured at multiple C-rates to separate the equilibrium OCV from dynamic overpotential.
- **ECM parameters:** Ohmic resistance R₀, and RC-pair parameters (R₁, C₁, R₂, C₂) as functions of SOC and temperature, suitable for Extended Kalman Filter (EKF) implementation.
- **Thermal parameters:** Heat generation coefficient as a function of current and SOC.
- **Degradation model:** Capacity fade and resistance growth as functions of cycle number and depth-of-discharge.

> **[PLACEHOLDER — Batemo Cell Availability]**
> *Confirm whether the LG INR21700-M58T is listed in the Batemo Cell Portal. If not, the LG INR21700-M50T (same form factor, NMC chemistry, 5000 mAh) is the closest available substitute. Update this section with the exact Batemo model name and version number once confirmed. Attach the Batemo model licence information in Appendix E.*

For this project, Batemo data is used in two ways: (1) to extract an accurate OCV slope (dV/dSOC) at top-of-charge that replaces the literature-estimated value used in the duty cycle derivation (Section 7.4), and (2) to simulate the complete balancing algorithm in MATLAB before bench testing (Section 8.1), providing an analytical pre-validation step. The Batemo model itself does not run on the STM32; extracted parameters are compiled into firmware as constant lookup tables.

### 2.7 Related Work

Commercial off-the-shelf BMS solutions for small Li-ion packs (Orion BMS 2 [7], Batrium Watchmon [8]) provide reference benchmarks but are not competition-optimal due to their size, weight, and fixed communication protocols. Academic literature on BMS for FSAE applications commonly adopts the TI BQ76952 or Maxim DS2762, which use I2C interfaces at lower data rates. The TLE9012's 1 Mbps UART with hardware CRC is better suited for the automotive EMC environment of a race car.

---

## 3. System Requirements and Design

### 3.1 Functional Requirements

| ID | Requirement | Source |
|----|-------------|--------|
| FR-01 | Measure each of six cell voltages with ≤5 mV error | FSAE IC.12.3 |
| FR-02 | Detect OV condition at ≥4.20 V per cell within 100 ms | FSAE IC.12.3 |
| FR-03 | Detect UV condition at ≤2.50 V per cell within 100 ms | FSAE IC.12.3 |
| FR-04 | Measure temperature at ≥3 locations on the pack | FSAE IC.12.4 |
| FR-05 | Passively balance cells during charging phase only | Design choice |
| FR-06 | Transmit pack status over CAN at ≥10 Hz | FSAE IC.12.5 |
| FR-07 | Remain operational across −20 °C to +60 °C ambient | FSAE T.1.1 |

### 3.2 Non-Functional Requirements

| ID | Requirement |
|----|-------------|
| NFR-01 | Firmware must be deterministic; no dynamic memory allocation |
| NFR-02 | BMS must survive 12 V transients on the GLV bus |
| NFR-03 | Board must weigh under 80 g |
| NFR-04 | Idle current draw < 50 mA |

### 3.3 Design Decisions

**Choice of TLE9012DQU over alternatives:** The TLE9012 was selected over the TI BQ76952 because of its 1 Mbps UART (faster than 400 kHz I2C), hardware CRC validation on every frame, and AEC-Q100 automotive qualification. The hardware CRC eliminates the need for software retry logic and simplifies the driver architecture.

**Passive over active balancing:** For a GLV pack where balancing occurs at a stationary charging station between competition sessions, active balancing hardware complexity provides no meaningful benefit. The 41 Ω resistors dissipate 0.43 W per cell — well within the PCB's thermal budget without active cooling.

**UART-DMA architecture:** The TLE9012 UART protocol requires precise byte-level timing. Using DMA for both TX and RX paths eliminates MCU polling overhead, freeing the CPU to execute balancing logic and state machine code while data transfers occur autonomously.

### 3.4 Cell Model and Parameter Source

Electrochemical parameters for the LG M58T cell are sourced from the Batemo Cell Portal [11], which provides experimentally characterised models superior to datasheet estimates for two reasons. First, the OCV curve slope (dV/dSOC) at top-of-charge governs the minimum balancing duty cycle calculation in Section 7.4; a ±30% error in this value propagates directly into an equivalently inaccurate convergence time estimate. Second, the ECM parameters (R₀, R₁, C₁) are required inputs to the Extended Kalman Filter SOC estimator planned for the next firmware iteration (Section 10.3).

> **[PLACEHOLDER — Batemo OCV Slope Value]**
> *After loading the Batemo model in MATLAB, extract and insert the following values:*
> ```matlab
> cell   = BatemoCell('<exact-model-name>');
> soc    = 0:0.001:1;
> ocv    = cell.OCV(soc, 25);          % At 25°C
> % Slope in the balancing region (SOC 90-100%)
> idx    = soc >= 0.90;
> slope  = gradient(ocv(idx), soc(idx) * 17.4);  % V per Ah
> fprintf('dV/dAh at SOC=95%%: %.5f V/Ah\n', mean(slope));
> ```
> *Replace the estimated value α = 5.75 × 10⁻³ V/mAh in Section 7.4 with the Batemo-derived value. Update Appendix D convergence table accordingly.*

The MATLAB workflow for parameter extraction is:

1. Load the Batemo cell model for LG M58T.
2. Export OCV vs. SOC table at 25°C as a 101-point C array (0% to 100% SOC in 1% steps) — see Appendix E.
3. Extract R₀, R₁, C₁ at SOC = 50% and T = 25°C as initial EKF constants.
4. Run the balancing simulation described in Section 8.1 using the full OCV curve.

---

## 4. Hardware Architecture

### 4.1 System Block Diagram

```
 ┌───────────────────────────────────────────────────────────────┐
 │                     GLV BMS PCB                               │
 │                                                               │
 │  ┌─────────────┐   UART1 1Mbps   ┌────────────────────────┐  │
 │  │             │ ◄──────────────► │    TLE9012DQU          │  │
 │  │ STM32F446   │                 │  Cell Monitor IC       │  │
 │  │   MCU       │                 │  6S passive balancing  │  │
 │  │             │                 └─────────┬──────────────┘  │
 │  │             │                           │ U7–U12, G6–G11  │
 │  │             │  I2C1           ┌─────────▼──────────────┐  │
 │  │             │ ◄──────────────► │    INA228AIDGST        │  │
 │  │             │                 │  Current Sensor        │  │
 │  │             │                 └────────────────────────┘  │
 │  │             │  CAN1           ┌────────────────────────┐  │
 │  │             │ ◄──────────────► │    TCAN334GD           │  │
 │  │             │                 │  CAN Transceiver 1     │  │
 │  │             │  CAN2           └────────────────────────┘  │
 │  │             │ ◄──────────────► │    TCAN334GD           │  │
 │  │             │                 │  CAN Transceiver 2     │  │
 │  └─────────────┘                 └────────────────────────┘  │
 │                                                               │
 │  NTC_A1–NTC_A5 ──────────────────► TMP0–TMP4 (TLE9012)      │
 │  Cell taps (0, 7–12) ────────────► U0, U7–U12 (TLE9012)     │
 │  Balancing taps ─────41Ω─────────► G6–G11 (TLE9012)         │
 └───────────────────────────────────────────────────────────────┘
```

### 4.2 Pack Configuration

The GLV pack is a 6 series, 3 parallel (6S3P) configuration using LG M58T 21700 NMC cells:

| Parameter | Value |
|---|---|
| Cell type | LG M58T (21700 NMC) |
| Cell capacity | 5800 mAh |
| Group configuration | 3P — three cells in parallel per group |
| Group capacity | 17,400 mAh |
| Number of series groups | 6 |
| Nominal pack voltage | 6 × 3.63 V = 21.78 V |
| Maximum pack voltage | 6 × 4.20 V = 25.20 V |
| Minimum pack voltage | 6 × 2.50 V = 15.00 V |
| Total energy | 25.2 V × 17.4 Ah = 438.5 Wh |

### 4.3 Cell Tap Wiring

The cell taps connect to the TLE9012 through a filter network per tap consisting of a 10 Ω series resistor and a multi-stage capacitor (330 nF + 100 nF) to ground, forming a low-pass filter that suppresses high-frequency switching noise from the vehicle's power electronics. The tap assignment is as follows:

| Cell Tap Label | Physical Location | TLE9012 U-Pin | PCVM Register |
|---|---|---|---|
| CELL TAP 0 | Pack negative | U0 | — (reference) |
| CELL TAP 7 | Top of cell group 1 | U7 | PCVM_6 (0x1F) |
| CELL TAP 8 | Top of cell group 2 | U8 | PCVM_7 (0x20) |
| CELL TAP 9 | Top of cell group 3 | U9 | PCVM_8 (0x21) |
| CELL TAP 10 | Top of cell group 4 | U10 | PCVM_9 (0x22) |
| CELL TAP 11 | Top of cell group 5 | U11 | PCVM_10 (0x23) |
| CELL TAP 12 | Pack positive | U12, U12P | PCVM_11 (0x24) |

Pins U1–U6 are tied to U0 (pack negative) to prevent floating inputs on the unused lower measurement channels.

### 4.4 Balancing Circuit

Each cell group has a dedicated balancing path:

```
Cell tap N ──[10 Ω filter]──► U_N (voltage measurement)
Cell tap N ──[41 Ω R_BAL]──► G_(N-6) (balancing current sink)
```

When the TLE9012 activates balancing for cell group N, it drives G_(N-6) low through its internal switch, completing the discharge path:

```
Cell+ ──► 41 Ω ──► G_pin ──► IC internal switch ──► Cell–
```

At 4.20 V (top-of-charge), each balancing channel dissipates:

```
I_bal = 4.20 V / 41 Ω = 102 mA
P_bal = (4.20 V)² / 41 Ω = 0.430 W per channel
P_total (all 6 channels) = 2.58 W
```

The 41 Ω resistors (0805 package, rated 0.5 W at 70°C) are thermally marginal if all six channels balance simultaneously at full duty cycle. The PWM capability of BAL_PWM register (0x17) is therefore used to limit duty cycle and time-multiplex heat dissipation when necessary.

### 4.5 Protection MOSFETs

The PCB includes N-channel BSC050N04LS_G MOSFETs (40 V, low R_DS(on)) arranged as the main pack current interrupt switches. Separate MOSFETs handle the charge interrupt path (controlled via GPIO PC2) and error trigger circuitry. These are driven by the MCU in response to OV/UV/OT fault conditions detected by the TLE9012.

### 4.6 Current Sensing

An INA228AIDGST (Texas Instruments) high-precision power monitor is connected on I2C1. It measures bidirectional pack current via a shunt resistor (R_SHUNT on the PCB) with 19.53 µA resolution. The ALERT pin is connected to MCU GPIO for interrupt-driven current fault detection. This sensor is wired but its driver is not yet implemented; it is the primary target for the next firmware iteration.

### 4.7 CAN Interface

Two TCAN334GD CAN transceivers provide CAN1 and CAN2 interfaces. Both transceivers connect to the STM32F446's hardware CAN controllers (bxCAN). CAN1 connects to the main vehicle network; CAN2 provides a secondary bus for diagnostics. Neither interface has a firmware driver yet.

### 4.8 Temperature Sensing

Five NTC thermistors (NTC_A1–NTC_A5) are distributed across the pack and connect to the TLE9012's TMP0–TMP4 analogue inputs via a signal conditioning board (4-794632-0 connector array). Each NTC channel has a 100 Ω series resistor and a 4.7 nF parallel capacitor for noise filtering. The TLE9012 supplies a programmable pull-down current to each NTC, and the resulting voltage is digitised by the internal ADC.

---

## 5. Firmware Architecture

### 5.1 Software Stack

```
┌───────────────────────────────────────────┐
│              Application Layer            │
│  main.c — BMS state machine, main loop   │
├───────────────────────────────────────────┤
│           Hardware Abstraction Layer       │
│  TLE9012dqu.c — Cell monitor driver      │
│  uart_dma.c  — UART + DMA + Timer driver │
├───────────────────────────────────────────┤
│              STM32 HAL / CMSIS            │
│  STM32F4xx HAL (CAN, I2C, UART enabled)  │
├───────────────────────────────────────────┤
│              Hardware                     │
│  STM32F446RET6 + TLE9012DQU              │
└───────────────────────────────────────────┘
```

### 5.2 Initialisation Sequence

On power-on, the firmware executes the following initialisation in strict order:

1. `timer5_init()` — Start Timer 5 at 1 µs tick (required by all subsequent delay calls).
2. `dma2_init()` — Enable DMA2 controller (used by UART1 for TLE9012).
3. `dma1_init()` — Enable DMA1 controller (used by UART2, reserved).
4. `uart1_rx_tx_half_duplex_init()` — Configure UART1 at 1 Mbps, half-duplex, on PA9.
5. `TLE9012_Init(&bms, 6)` — Initialise driver handle for 6 cells.
6. `TLE9012_Wakeup(&bms)` × 3 — Send three 0xAA wake pulses to exit IC sleep state.
7. `TLE9012_EnableCellMonitoring(&bms, 6)` — Write PART_CONFIG (0x01) with bits 11–6 set (0x0FC0), enabling cells 6–11.
8. `TLE9012_ActivateErrors(&bms, 0b0001000000100000)` — Enable OV and UV error interrupts.
9. `TLE9012_SetUnderVoltageThreshold(&bms, 2.50f)` — Write UV register (0x03).
10. `TLE9012_SetOverVoltageThreshold(&bms, 4.20f)` — Write OV register (0x02).
11. `TLE9012_SetTemperatureConfig(&bms, 5, ...)` — Enable all 5 NTC channels.

### 5.3 Main Loop

The corrected main loop implements the following cycle, targeting approximately 100 ms period:

```
while (1) {
    TLE9012_ResetWatchdog()              // Mandatory: prevents IC sleep
    TLE9012_ReadCellVoltages(6)          // Read PCVM_6 to PCVM_11
    TLE9012_ReadUnderVoltageFlags()      // Check UV latch register
    TLE9012_ReadAllTemperatures(5)       // Read TMP0 to TMP4
    BMS_UpdateState()                    // State machine update
    BMS_ExecuteBalancing()               // Balancing algorithm
}
```

### 5.4 UART-DMA Communication Protocol

The TLE9012 uses a proprietary single-wire UART framing at 1 Mbps with the following non-standard features:

1. **Bit reversal:** Every byte is transmitted with its bit order reversed (LSB transmitted as MSB). This applies to both TX and RX data. The firmware implements `tle9012_reverse_byte()` applied to every byte before TX and after RX.

2. **CRC-8:** A CRC byte is appended to every frame, computed using polynomial 0x1D with initial value 0xFF and final XOR 0xFF. The CRC covers the first 5 bytes of each command.

3. **Frame formats:**
   - Write: `[0x1E, 0x80, reg, data_hi, data_lo, crc]` — 6 bytes
   - Read request: `[0x1E, 0x00, reg, crc]` — 4 bytes
   - Read response: `[don't care, don't care, data_hi, data_lo, crc]` — 5 bytes

4. **Half-duplex switching:** UART1 alternates between TX and RX mode by enabling/disabling the respective DMA streams. The firmware calls `uart1_enable_tx()` / `uart1_enable_rx()` and manages direction switching with a 10 µs turnaround delay implemented via Timer 5.

### 5.5 ADC to Voltage Conversion

The TLE9012 cell voltage registers (PCVM_n) return a 16-bit unsigned value with 5.0 V full-scale:

```c
float voltage = 5.0f * (float)adc_raw / 65535.0f;
```

Resolution: 5.0 V / 65535 ≈ 76.3 µV per LSB. For a 4.2 V cell, this corresponds to approximately 55,050 counts — well within the 16-bit range, providing adequate headroom. Threshold values are converted to register counts by the inverse formula:

```c
uint16_t threshold_raw = (uint16_t)(threshold_volts / 0.00007629f);
```

### 5.6 INA228 Current Sensor Firmware

#### 5.6.1 Hardware Interface

The INA228AIDGST is connected to I2C1 of the STM32F446 with PB6 as SCL and PB7 as SDA, both configured as alternate function 4 (AF4), open-drain output, with internal pull-up resistors supplementing the external 4.7 kΩ resistors on the PCB. The 7-bit I2C slave address is 0x40 (address pins A1 = GND, A0 = GND). The I2C bus operates at 100 kHz (standard mode) with APB1 at 16 MHz (HSI oscillator, no PLL).

#### 5.6.2 I2C1 Peripheral Driver (`i2c.c`)

A bare-metal polling driver was written for I2C1 following the same register-direct style as `uart_dma.c`. The STM32F4 I2C peripheral requires a specific byte-read sequence that differs depending on the number of bytes to be received — a known caveat documented in ST Application Note AN2824:

- **1-byte read:** ACK is disabled and STOP is generated before reading DR, to avoid a false ACK on the last byte.
- **2-byte read:** The POS bit (CR1.POS) is used to arm the NACK for the byte currently in the shift register rather than the byte in DR, preventing a spurious ACK on byte 2.
- **N ≥ 3 byte read:** Bytes 0 to N−4 are read one-by-one on RXNE; the final three bytes are handled with two BTF waits and a STOP interleaved to maintain ACK/NACK timing.

The I2C CCR and TRISE values for 100 kHz with 16 MHz APB1 are:

```
CCR   = PCLK1 / (2 × f_SCL) = 16 000 000 / 200 000 = 80
TRISE = ceil(t_rise_max × PCLK1) + 1 = ceil(1000 ns × 16 MHz) + 1 = 17
```

All I2C transactions use a software timeout counter (200 000 iterations) to prevent indefinite blocking if the bus is held by a faulty device.

#### 5.6.3 INA228 Driver (`ina228.c`)

**Initialisation.** Two registers are written at startup:

1. **ADC_CONFIG (0x01):** Set to `0xF920` — continuous measurement mode for bus voltage, shunt voltage, and die temperature; conversion time 1.1 ms per channel; averaging count 1. Total round-trip per sample set ≈ 3.3 ms.

2. **SHUNT_CAL (0x02):** Computed at compile time from the shunt resistance and chosen current LSB:

```
CURRENT_LSB = MAX_CURRENT / 2^19  [A/bit]
SHUNT_CAL   = round(819.2 × 10^6 × CURRENT_LSB × R_shunt)  [ADCRANGE = 0]
```

With R_shunt = 5 mΩ and MAX_CURRENT = 20 A: `CURRENT_LSB = 38.147 µA/bit`, `SHUNT_CAL = 156`. These constants are defined in `ina228.h` and must be updated to match the actual PCB shunt resistor value.

**Register parsing.** The INA228 measurement registers are 20-bit quantities packed into 24-bit (3-byte) fields, with the lower 4 bits always zero:

| Register | Bytes | Signed? | Conversion |
|---|---|---|---|
| VBUS (0x05) | 3 | No | `(raw_24 >> 4) × 195.3125 µV` |
| CURRENT (0x07) | 3 | Yes | `sign_extend(raw_24 >> 4) × CURRENT_LSB` |
| POWER (0x08) | 3 | No | `raw_24 × 3.2 × CURRENT_LSB` |
| TEMP (0x06) | 2 | Yes | `(int16_t)(raw_16) >> 4 × 7.8125 m°C` |

Sign extension for the current register is performed by placing the 24-bit raw value in the most-significant 24 bits of an `int32_t` (shift left by 8), then arithmetic right-shifting by 12. This propagates the original sign bit (bit 23 → bit 31) without requiring an explicit mask-and-extend operation:

```c
int32_t c = (int32_t)(((uint32_t)raw[0] << 24) |
                       ((uint32_t)raw[1] << 16) |
                       ((uint32_t)raw[2] <<  8));
out->current_a = (float)(c >> 12) * INA228_CURRENT_LSB;
```

**Main loop integration.** `ina228_read_all(&g_ina228)` is called once per main loop iteration (after cell voltage reading), populating `g_ina228.bus_voltage_v`, `g_ina228.current_a`, `g_ina228.power_w`, and `g_ina228.temperature_c`. These values are available for SOC estimation via Coulomb counting and for future CAN telemetry transmission.

#### 5.6.4 Updated Software Stack

```
┌────────────────────────────────────────────────────────────────┐
│                     Application Layer                          │
│  main.c — BMS state machine, main loop, balancing control     │
├────────────────────────────────────────────────────────────────┤
│                 Hardware Abstraction Layer                      │
│  TLE9012dqu.c — Cell monitor driver (UART-DMA, bit-reversal)  │
│  ina228.c     — Current/power sensor driver (I2C1 polling)    │
│  uart_dma.c   — UART + DMA + Timer driver                     │
│  i2c.c        — I2C1 bare-metal polling driver                │
├────────────────────────────────────────────────────────────────┤
│                      Hardware                                  │
│  STM32F446RET6 + TLE9012DQU (UART) + INA228 (I2C)            │
└────────────────────────────────────────────────────────────────┘
```

---

### 5.7 Real-Time Operating System — FreeRTOS Architecture Plan

#### 5.7.1 Motivation

The current firmware architecture is a bare-metal super-loop. Every task — cell voltage reading, current measurement, balancing, watchdog reset — executes sequentially in `while(1)`. This approach has three fundamental limitations as the BMS complexity grows:

1. **Blocking communication monopolises the CPU.** The TLE9012 UART protocol spins on volatile flags (`while (!g_rx_cmplt) {}`) for up to several milliseconds per transaction. During this time, the INA228 is not read, the watchdog is not serviced, and fault detection is suspended. A single communication timeout (e.g. TLE9012 not responding) can stall the entire BMS for up to 1 s.

2. **No priority ordering.** A fault condition (overvoltage, overcurrent) should trigger an output within microseconds. In the current design, the charge interrupt can only be updated once per loop iteration — after a slow TLE9012 read completes. Inserting faster tasks is not possible without restructuring the entire loop.

3. **Scalability.** Adding CAN telemetry, SOC estimation, and temperature fault detection in the same loop increases the loop period and makes timing analysis intractable. Formula Student technical scrutiny requires demonstrable, bounded response times.

FreeRTOS provides preemptive priority-based scheduling, synchronisation primitives (mutexes, binary semaphores), and software timers — all with a well-characterised overhead on Cortex-M4 (< 1 µs context switch at 180 MHz). Moving the GLV BMS to FreeRTOS resolves all three problems.

#### 5.7.2 Proposed Task Architecture

| Task | Priority | Period | Responsibility |
|---|---|---|---|
| `vFaultTask` | 5 — Highest | 10 ms | Read latest cell voltages and current; drive charge interrupt; assert fault LED |
| `vCurrentTask` | 4 | 50 ms | Call `ina228_read_all()`; accumulate Coulomb count for SOC |
| `vCellVoltageTask` | 3 | 200 ms | Call `Read_Cell_Voltages()`; update OV/UV flags; schedule balancing |
| `vBalancingTask` | 2 | 500 ms | Run balancing algorithm; write BAL_SETTINGS and BAL_PWM |
| `vCANTask` | 1 | 500 ms | Broadcast BMS state (voltages, current, SOC, faults) on CAN1 |
| `vWatchdogTask` | 1 — Lowest | 300 ms | Call `Reset_Watch_dog_counter()` to keep TLE9012 alive |

The `vFaultTask` runs at the highest application priority (below the NVIC interrupt level), ensuring fault detection and charge-interrupt assertion complete within 10 ms of any threshold crossing regardless of what the lower-priority tasks are doing.

#### 5.7.3 Shared Resource Strategy

Two peripherals are shared across tasks: **UART1** (TLE9012) and **I2C1** (INA228). In the single-task plan above, UART1 is exclusively used by `vCellVoltageTask` and `vWatchdogTask`, and I2C1 is exclusively used by `vCurrentTask`. There is therefore no resource conflict and no mutex is required for the initial implementation.

The shared BMS state struct (cell voltages, current, fault flags) is written by measurement tasks and read by `vFaultTask` and `vCANTask`. Because Cortex-M4 word reads/writes are atomic and individual `float` variables are word-aligned, simple volatile globals are sufficient without a mutex. If the state is expanded to a multi-word struct read non-atomically, a FreeRTOS critical section or reader-writer semaphore should be introduced.

#### 5.7.4 DMA Interrupt Compatibility

The existing DMA and UART ISRs (`DMA2_Stream2_IRQHandler`, `DMA2_Stream7_IRQHandler`, `USART1_IRQHandler`) only set volatile byte flags — they do not call any FreeRTOS API. These ISRs are therefore FreeRTOS-safe as-is. They may be upgraded to `xSemaphoreGiveFromISR()` calls in a later revision to allow the `vCellVoltageTask` to block on a semaphore (yielding the CPU to other tasks) rather than spin-waiting on the volatile flag. This upgrade requires setting the NVIC priority for these IRQs numerically higher than `configMAX_SYSCALL_INTERRUPT_PRIORITY` (typically 5 on STM32).

#### 5.7.5 SysTick Integration

FreeRTOS uses SysTick as its scheduler tick (default 1 ms). The current `SysTick_Handler` in `stm32f4xx_it.c` has an empty body (HAL was removed). When FreeRTOS is added, `SysTick_Handler` must call `xPortSysTickHandler()`:

```c
void SysTick_Handler(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}
```

The existing `delay_us()` function (Timer 5 busy-wait) continues to work inside tasks but should not be called with delays longer than the task's own period, as it blocks the task and prevents lower-priority tasks from running — though higher-priority tasks are still preempted by the scheduler since SysTick fires even during a busy-wait loop.

#### 5.7.6 Implementation Sequence

The FreeRTOS port for STM32F4 (ARM\_CM4F) is available as a Cube package (`Middlewares/Third_Party/FreeRTOS`) and requires no hardware changes. The recommended integration steps are:

1. Add FreeRTOS source files and port layer (`port.c`, `portmacro.h`, `FreeRTOSConfig.h`) to the STM32CubeIDE project.
2. Set `configUSE_PREEMPTION = 1`, `configTICK_RATE_HZ = 1000`, `configMAX_SYSCALL_INTERRUPT_PRIORITY = 5`.
3. Convert the `while(1)` super-loop into the six tasks listed in Section 5.7.2, each created with `xTaskCreate()` and appropriate stack sizes (512–1024 words per task).
4. Replace `delay_us(300000)` in the main loop with `vTaskDelay(pdMS_TO_TICKS(300))` inside the respective tasks.
5. Upgrade the TLE9012 DMA ISRs to `xSemaphoreGiveFromISR()` to allow blocking waits rather than spin-polling.
6. Start the scheduler with `vTaskStartScheduler()` at the end of `main()`.

---

## 6. Critical Defect Analysis and Corrective Actions

This chapter documents four firmware defects discovered through systematic register-level analysis of the existing codebase. Each defect is described by its location, root cause, observable symptom, and corrective action.

### 6.1 Defect 1 — Incorrect PCVM Base Register Address

**Location:** `Core/Src/TLE9012dqu.c`, function `TLE9012_ReadCellVoltages()`, line 199.

**Root cause:** The TLE9012 allocates PCVM registers sequentially from 0x19 (PCVM_0, measuring V(U1)−V(U0)) to 0x24 (PCVM_11, measuring V(U12)−V(U11)). The six physical cell groups of this design are connected to U7–U12, corresponding to cells 6–11 in the IC's numbering scheme. Their measurement results reside in PCVM_6 to PCVM_11 (registers 0x1F–0x24). The existing code iterates from register 0x19, reading PCVM_0 to PCVM_5 — which measure V(U1)–V(U0) through V(U6)–V(U5). Since U1–U6 are all tied to pack negative (U0), these registers return 0 V.

**Symptom:** `cell_v[0]` through `cell_v[5]` always read 0.000 V regardless of actual pack state. The BMS appears operational but never observes actual cell voltages.

**Corrective action:**

```c
// BEFORE (wrong):
st = tle9012_read_reg(dev, (uint8_t)(0x19U + i), dev->rx_buf, 5);

// AFTER (correct):
st = tle9012_read_reg(dev, (uint8_t)(0x1FU + i), dev->rx_buf, 5);
```

### 6.2 Defect 2 — Cell Count Hardcoded to 12

**Location:** `Core/Src/main.c`, lines 18, 24, 27, 50.

**Root cause:** The pack has 6 series cell groups, but all initialisation and loop calls pass `num_cells = 12`. The `TLE9012_EnableCellMonitoring()` function computes the PART_CONFIG bitmask as:

```c
for (uint16_t i = 0; i < num_cells; i++) {
    packaged |= (1U << (11U - i));
}
```

For `num_cells = 12`, this sets bits 11–0 (0x0FFF), enabling all 12 cell inputs including U1–U6 which are shorted. For `num_cells = 6`, it correctly sets bits 11–6 (0x0FC0), enabling only U7–U12.

**Symptom:** The IC is configured to monitor 12 cells. The 6 unused lower cells are not internally disabled, potentially causing spurious UV/OV flags from zero-voltage measurements on the shorted inputs.

**Corrective action:** Replace all instances of `12` with `6` in `main.c`:

```c
TLE9012_Init(&bms, 6);
TLE9012_EnableCellMonitoring(&bms, 6);
TLE9012_SetUndervoltageCells(&bms, 6, 0);
TLE9012_ReadCellVoltages(&bms, cell_raw, cell_v, 6);
```

### 6.3 Defect 3 — Over-Voltage Threshold at 3.4 V (LFP Value)

**Location:** `Core/Src/main.c`, line 28.

**Root cause:** The OV threshold of 3.4 V is appropriate for LFP chemistry (nominal 3.2 V, max 3.65 V) but is 0.8 V below the maximum charge voltage of the LG M58T NMC cell (4.20 V). Any charger raising the cell voltage above 3.4 V will immediately trigger an OV fault, interrupting the charge cycle.

**Symptom:** Pack cannot charge above 3.4 V/cell (approximately 30–40% SOC for NMC), severely degrading usable energy capacity. From the competition perspective, the car would have substantially reduced endurance range.

**Corrective action:**

```c
// BEFORE:
TLE9012_SetOverVoltageThreshold(&bms, 3.4f);

// AFTER:
TLE9012_SetOverVoltageThreshold(&bms, 4.20f);
```

### 6.4 Defect 4 — Under-Voltage Threshold at 2.9 V (Conservative but Incorrect)

**Location:** `Core/Src/main.c`, line 26.

**Root cause:** The UV threshold of 2.9 V is conservative for NMC (actual minimum 2.50 V per LG M58T datasheet) and was likely copied from a LFP design. While not as immediately damaging as the OV defect, it causes approximately 400 mV of usable capacity on the discharge side to be treated as a fault condition, reducing run time.

**Corrective action:**

```c
// BEFORE:
TLE9012_SetUnderVoltageThreshold(&bms, 2.9f);

// AFTER:
TLE9012_SetUnderVoltageThreshold(&bms, 2.50f);
```

### 6.5 Summary of Defect Impact

| Defect | Severity | Pack Behaviour Without Fix |
|---|---|---|
| Wrong PCVM base address (0x19 vs 0x1F) | Critical | Zero-voltage readings; OV/UV logic non-functional |
| Cell count = 12 instead of 6 | Critical | Spurious faults from shorted unused cell inputs |
| OV threshold = 3.4 V | Critical | Pack charges to only ~35% SOC; massive capacity loss |
| UV threshold = 2.9 V | Major | ~400 mV discharge capacity lost per cell |

All four defects must be corrected before balancing can be implemented, since balancing logic depends on accurate cell voltage readings with correctly set protection bounds.

---

## 7. Cell Balancing Strategy

### 7.1 Why Charge-Phase-Only Balancing

Passive balancing dissipates energy as heat through the 41 Ω resistors. Activating balancing during discharge would simultaneously draw current from cells supplying power to the GLV loads, creating a net energy loss with no benefit to pack health — the pack is already discharging and cell voltages are converging naturally under load current. Balancing during discharge therefore reduces run time without improving capacity.

During charging, specifically in the Constant Voltage (CV) phase, the charger maintains a fixed terminal voltage (25.2 V for 6S at 4.20 V/cell). When the BMS activates balancing on a high cell:
- The 41 Ω resistor discharges that cell, increasing total pack current draw
- The charger responds by raising its output current to maintain 25.2 V
- The extra current is distributed across the remaining cells, raising their charge
- Net effect: high cells bleed down, low cells receive additional charging current

The charger actively amplifies the balancing effect during CV, making it the most energy-efficient and effective window for passive balancing.

**Guard condition:** Balancing is only enabled when the pack is confirmed to be in the charging state. This is detected via the `charge_interrupt_is_high()` function (GPIO PC2), which is asserted by the charger control circuit when a charger is connected and actively charging.

### 7.2 Top-of-Charge Trigger

NMC cells have a flat OCV plateau between 3.60–4.00 V where small voltage differences (ΔV = 10 mV) can represent large SOC differences (ΔSOC ≈ 2–5%). Balancing based on voltage in this region would incorrectly interpret noise and measurement uncertainty as real imbalance.

Above 4.10 V, the NMC OCV curve steepens significantly: approximately 40–100 mV change per 1% SOC. In this region, a voltage spread of 10 mV reliably indicates a real imbalance of 0.1–0.25% SOC. Balancing decisions made here are therefore meaningful and accurate.

**Policy:** Enable balancing only when the lowest cell voltage in the pack exceeds 4.10 V, ensuring all cells are in the steep OCV region.

### 7.3 Adaptive PWM Algorithm

The TLE9012 `BAL_PWM` register (0x17) sets a global duty cycle applied to all active balancing channels. Full duty (no PWM, D = 1) provides 102 mA per channel at 4.2 V. For large imbalances or when thermal margin allows, full duty achieves fastest convergence. For smaller imbalances close to the balancing threshold, a proportional duty cycle reduces unnecessary heat dissipation.

The duty cycle for cell group n is defined as:

```
D_n = clip( k_p × (V_n − V_min) / V_bal_max , 0, 1 )
```

Where:
- `V_n` = voltage of cell group n
- `V_min` = minimum cell voltage in the pack
- `V_bal_max` = maximum expected voltage spread (50 mV for design sizing)
- `k_p` = proportional gain (tuned to 1.0 initially; adjust if oscillation observed)

Since `BAL_PWM` applies a single duty cycle to all channels simultaneously, the duty cycle is set to the maximum across all cells requiring balancing:

```c
uint8_t max_duty = 0;
for (int i = 0; i < 6; i++) {
    float spread = cell_v[i] - v_min;
    uint8_t duty = (uint8_t)(255.0f * spread / 0.050f);
    duty = duty > 255 ? 255 : duty;
    if (duty > max_duty) max_duty = duty;
}
// Write max_duty to BAL_PWM [7:0] (on-time)
```

### 7.4 Guaranteed Convergence: Minimum Duty Cycle Derivation

For a given initial voltage spread ΔV to converge to below ε within the CV phase duration T, the required minimum average balancing current per cell is:

```
          ΔV × C_group
I_min = ─────────────────
           α × T
```

Where:
- `ΔV` = initial voltage spread (V)
- `C_group` = group capacity = 17,400 mAh = 17.4 Ah
- `α` = OCV curve slope at top-of-charge = dV/d(Ah) — see note below
- `T` = available CV phase duration (hours)

> **[PLACEHOLDER — Batemo OCV Slope]**
> *The value α = 5.75 × 10⁻³ V/mAh is currently estimated from typical NMC literature (100 mV change over the top 10% of a 17.4 Ah curve). Replace this with the value extracted from the Batemo model per Section 3.4. Update the worked examples and Appendix D table once the Batemo-derived α is confirmed.*

The minimum PWM duty cycle is then:

```
     I_min
D = ───────────────────
    V_cell / R_BAL
```

**Worked example** (ΔV = 20 mV, T = 60 min = 1 hour):

```
ΔQ = 20 mV / (5.75 × 10⁻³ V/mAh) = 3.48 mAh
I_min = 3.48 mAh / 1 h = 3.48 mA
D_min = 3.48 mA / 102 mA = 3.4%
```

For typical competition-use imbalances (<20 mV), even 5% duty cycle is sufficient for convergence within the CV phase. Full duty (D = 1) is conservative and guarantees convergence even for imbalances up to 50 mV:

```
ΔQ_50mV = 50 mV / (5.75 × 10⁻³ V/mAh) = 8.7 mAh
t_50mV = 8.7 mAh / 102 mA = 0.085 hours ≈ 5 minutes
```

This confirms that the 41 Ω balancing resistors are adequately sized for this pack: full-duty balancing resolves even a 50 mV spread within 5 minutes of CV phase — well inside the typical 30–60 minute CV window.

### 7.5 Termination Condition — Guaranteed Balance Confirmation

The only way to confirm with certainty that cells are balanced is to measure the spread after balancing and compare against a threshold. No pre-calculated duty cycle can account for unknown initial conditions. The balancing algorithm therefore enforces a **measure-and-hold termination condition**:

> Charge termination is inhibited until `max(cell_v) − min(cell_v) < ε_bal`

Where `ε_bal = 5 mV` (approximately 0.1% SOC spread for NMC at top-of-charge). The charger remains in CV mode, continuing to supply the small current required by balancing resistors, until the TLE9012 reports all cell voltages within the 5 mV band.

This approach is robust to all uncertainty sources (initial imbalance, cell capacity variation, temperature) because it relies on a physical measurement, not a prediction.

### 7.6 Complete Balancing Algorithm (C Pseudocode)

```c
void BMS_ExecuteBalancing(void) {
    // Guard: only balance during active charging
    if (!charge_interrupt_is_high()) {
        TLE9012_SetBalancing(&bms, 0x0000);  // Disable all balancing
        return;
    }

    // Find min and max cell voltages
    float v_min = cell_v[0], v_max = cell_v[0];
    for (int i = 1; i < 6; i++) {
        if (cell_v[i] < v_min) v_min = cell_v[i];
        if (cell_v[i] > v_max) v_max = cell_v[i];
    }

    float spread = v_max - v_min;

    // Guard: only balance in top-of-charge region (steep OCV slope)
    if (v_min < 4.10f) {
        TLE9012_SetBalancing(&bms, 0x0000);
        return;
    }

    // Guard: if spread < epsilon, declare balanced, allow termination
    if (spread < 0.005f) {  // 5 mV
        TLE9012_SetBalancing(&bms, 0x0000);
        // Signal to charger: pack is balanced and full
        charge_interrupt_low();  // Allow charge termination
        return;
    }

    // Inhibit charge termination until balanced
    charge_interrupt_high();

    // Build per-cell enable mask (bits 6-11 → cells 1-6)
    uint16_t bal_mask = 0;
    uint8_t max_duty = 0;
    for (int i = 0; i < 6; i++) {
        float delta = cell_v[i] - v_min;
        if (delta > 0.010f) {  // Balance if >10 mV above minimum
            bal_mask |= (1U << (6 + i));  // Bit 6+i for cell i+1

            // Proportional duty cycle
            uint8_t duty = (uint8_t)(255.0f * delta / 0.050f);
            if (duty > 255) duty = 255;
            if (duty > max_duty) max_duty = duty;
        }
    }

    // Write PWM duty cycle and enable mask
    TLE9012_SetBalancingPWM(&bms, max_duty);
    TLE9012_SetBalancing(&bms, bal_mask);
}
```

### 7.7 Required New Driver Functions

Two new functions must be added to `TLE9012dqu.c` to support the balancing algorithm:

**`TLE9012_SetBalancing(dev, mask)`** — writes `mask` to BAL_SETTINGS register (0x16):
```c
TLE9012_Status_t TLE9012_SetBalancing(TLE9012_Handle_t *dev, uint16_t mask) {
    return tle9012_write_reg16(dev, 0x16U, mask);
}
```

**`TLE9012_SetBalancingPWM(dev, duty)`** — writes duty cycle to BAL_PWM register (0x17):
```c
TLE9012_Status_t TLE9012_SetBalancingPWM(TLE9012_Handle_t *dev, uint8_t duty) {
    // Upper byte: period (255 = full resolution), Lower byte: on-time
    uint16_t pwm_val = (0xFFU << 8) | (uint16_t)duty;
    return tle9012_write_reg16(dev, 0x17U, pwm_val);
}
```

---

## 8. Testing and Validation

### 8.1 MATLAB Pre-Validation Using Batemo Cell Model

Prior to bench testing on the physical pack, the balancing algorithm is validated analytically in MATLAB using the Batemo-derived OCV curve. This simulation provides two key outputs: a predicted convergence time that can be compared against the analytical estimate in Section 7.4, and a sensitivity analysis showing how convergence time varies with initial spread and temperature.

> **[PLACEHOLDER — MATLAB Simulation Results]**
> *Run the following simulation script in MATLAB after loading the Batemo model. Insert the resulting convergence plot as Figure 8.1 and fill in the table below.*
>
> ```matlab
> %% GLV BMS Balancing Convergence Simulation
> % Parameters
> C_group  = 17.4;          % Ah (3P group)
> R_bal    = 41.0;          % Ohms (from schematic)
> V_cv     = 25.2;          % V (6 × 4.2 V charger CV setpoint)
> dt       = 1.0;           % s
> T_sim    = 7200;          % s (2 hours max)
> epsilon  = 0.005;         % V (5 mV convergence target)
>
> % Load OCV table from Batemo
> cell    = BatemoCell('<model-name>');
> soc_tbl = 0:0.001:1;
> ocv_tbl = cell.OCV(soc_tbl, 25);
>
> % Initial SOC — three test cases
> cases = {[0.980, 0.970, 0.975, 0.965, 0.978, 0.960], ...  % 20 mV spread
>          [0.990, 0.960, 0.975, 0.955, 0.985, 0.950], ...  % 50 mV spread
>          [1.000, 0.940, 0.970, 0.935, 0.990, 0.930]};     % 100 mV spread
>
> for c = 1:length(cases)
>     soc = cases{c};
>     spread = zeros(T_sim, 1);
>     for t = 1:T_sim
>         ocv   = interp1(soc_tbl, ocv_tbl, soc);
>         v_min = min(ocv);
>         bal   = (ocv - v_min > 0.010) & (v_min >= 4.10);
>         I_bal = bal .* (ocv / R_bal);
>         I_net = ((V_cv/6 - mean(ocv)) / 0.05) - I_bal; % simplified CV model
>         soc   = min(soc + (I_net * dt) / (C_group * 3600), 1.0);
>         spread(t) = max(ocv) - min(ocv);
>         if spread(t) < epsilon, break; end
>     end
>     t_conv(c) = t;
>     fprintf('Case %d: converged in %d s (%.1f min)\n', c, t, t/60);
> end
>
> % Plot
> figure; plot(spread * 1000); xlabel('Time (s)'); ylabel('Voltage Spread (mV)');
> title('Balancing Convergence — Batemo LG M58T Simulation');
> legend('20 mV', '50 mV', '100 mV'); grid on;
> ```
>
> *Insert Figure 8.1 here: Simulated balancing convergence for three initial spread cases.*
>
> | Initial Spread | Simulated Convergence Time | Analytical Estimate (Section 7.4) | Match? |
> |---|---|---|---|
> | 20 mV | *[fill after running]* | 2.1 min | *[yes/no]* |
> | 50 mV | *[fill after running]* | 5.1 min | *[yes/no]* |
> | 100 mV | *[fill after running]* | 10.2 min | *[yes/no]* |
>
> *If the simulated times differ from the analytical estimates by more than 20%, re-derive the OCV slope α using the Batemo curve and update the convergence table in Appendix D.*

### 8.3 Test Strategy

Testing is structured in three phases: unit-level driver verification, integration testing with the physical pack, and system-level validation under simulated competition conditions.

### 8.4 Phase 1 — Driver Unit Tests (Bench, No Pack)

**Test 1.1 — UART frame integrity**
- Method: Capture UART1 TX output on a logic analyser at 1 Mbps.
- Verify: Byte sequence `[0x78, 0x01, 0xRR, 0xDA, 0xTA, 0xCC]` after bit-reversal matches expected write command for register 0x16.
- Pass criterion: CRC byte matches independently computed value; bit ordering correct.

**Test 1.2 — Register write/readback**
- Method: Write known values to OV and UV threshold registers; immediately read back using `TLE9012_ReadbackConfig()`.
- Verify: Readback values match written values within 1 LSB.
- Pass criterion: 100% match across 50 write-readback cycles.

**Test 1.3 — ADC conversion accuracy**
- Method: Apply known voltages (2.50 V, 3.63 V, 4.20 V) to cell inputs via a bench power supply.
- Verify: `cell_v[n]` matches applied voltage within ±5 mV.
- Pass criterion: All three voltages within tolerance.

### 8.5 Phase 2 — Integration Tests (With Pack)

**Test 2.1 — Six-cell voltage measurement**
- Method: Power pack from a bench charger at rest (OCV conditions). Read `cell_v[0]`–`cell_v[5]`.
- Verify: All six readings within ±5 mV of independent DMM measurements on the cell taps.
- Pass criterion: All six cells read correctly with the 0x1F register base fix applied.

**Test 2.2 — OV protection**
- Method: Raise one cell to 4.22 V using a bench supply injected at the tap. Monitor OV flag in `uv_flags` register and confirm pack switch opens.
- Pass criterion: OV event detected within 100 ms of threshold crossing.

**Test 2.3 — UV protection**
- Method: Drain one cell to 2.48 V. Monitor UV flag and confirm pack switch opens.
- Pass criterion: UV event detected within 100 ms of threshold crossing.

**Test 2.4 — Balancing activation**
- Method: Artificially inflate one cell to 4.15 V (1 V bias on tap) while others sit at 4.10 V. Assert charge interrupt. Monitor BAL_SETTINGS register readback; verify balancing resistor heats (IR thermometer on 41 Ω resistor).
- Pass criterion: Correct cell's balancing channel activates within 200 ms; no other channels active.

**Test 2.5 — Balancing convergence**
- Method: Create a controlled 20 mV imbalance using different charge levels. Connect charger. Time to convergence below 5 mV.
- Expected: <5 minutes per analysis in Section 7.4.

**Test 2.6 — Temperature measurement**
- Method: Heat each NTC thermistor in turn with a heat gun. Verify corresponding `bms.ext_temp[n].result_raw` increases.
- Pass criterion: All 5 channels respond correctly.

### 8.6 Phase 3 — System Validation

**Test 3.1 — Full charge cycle with balancing**
- Method: Start charge with intentionally imbalanced cells. Run full CC-CV charge cycle. Record cell voltages at 1 Hz throughout.
- Pass criterion: Balancing does not activate during CC phase; activates during CV phase; spread < 5 mV at charge termination.

**Test 3.2 — Thermal performance**
- Method: Run full charge with all 6 balancing channels active at full duty. Monitor PCB temperature at balancing resistors with thermal camera.
- Pass criterion: Resistor temperature < 100°C under ambient 25°C conditions.

**Test 3.3 — Watchdog reset reliability**
- Method: Run firmware for 1 hour. Monitor ERR pin on TLE9012 for any watchdog timeout events (pin goes high on watchdog fault).
- Pass criterion: Zero watchdog timeouts.

### 8.7 Expected Results Summary

| Test | Expected Outcome | Key Metric |
|---|---|---|
| UART frame integrity | Correct bit-reversed CRC frames | 100% CRC match |
| ADC conversion | ±5 mV accuracy | <5 mV error at 3 test points |
| Six-cell reading | All cells read correctly after bug fix | ±5 mV vs DMM |
| OV/UV protection | Fault detected and pack opens | <100 ms response |
| Balancing activation | Correct channel activates | Resistor heats on correct cell |
| Convergence (20 mV) | Converges in <5 min during CV | Spread < 5 mV |
| Full charge cycle | Balance only during CV | No balancing in CC phase |
| Thermal (full duty, 6 channels) | Resistors stay < 100°C | IR thermometer reading |

---

## 9. Discussion

### 9.1 Significance of Defect Discovery

The four firmware defects documented in Chapter 6 are significant because they were not detectable from code review alone — the code compiled and ran without errors. The symptoms (zero-voltage readings, pack refusing to charge above 3.4 V) would have manifested as apparent hardware faults, potentially leading to incorrect conclusions that the TLE9012 IC or PCB was faulty. Systematic register-level analysis — tracing each value from the datasheet register map through the driver to the application — was necessary to identify the root causes.

This highlights a key lesson in embedded systems development: firmware defects in register configuration are harder to catch than logic bugs and require test infrastructure that exercises each hardware path independently before system integration.

### 9.2 Adequacy of Passive Balancing for GLV Application

The 41 Ω / 102 mA passive balancing topology is appropriate for the GLV use case. Between competition sessions, the car is connected to a stationary charger for 30–60 minutes. During this window, the CV phase provides more than sufficient time to resolve typical imbalances (<50 mV) as shown by the analysis in Section 7.4. The 0.43 W per channel thermal dissipation is manageable without active cooling. Active balancing, which would provide higher efficiency but requires significantly more complex hardware (isolated DC-DC converters per cell), is not justified for this application.

### 9.3 Limitations of Voltage-Based Balancing

The top-of-charge, voltage-based strategy is effective for NMC cells because the OCV slope above 4.10 V is steep enough that voltage spread reliably represents SOC spread. However, this strategy has known limitations:

1. **Mid-SOC blindness:** During discharge below 4.00 V, balancing is not active, and imbalances that develop in the flat OCV region accumulate silently. They are only addressed at the next charge cycle.
2. **Temperature correction:** The NMC OCV curve shifts with temperature (approximately −1.5 mV/°C at top of charge). The balancing threshold of 4.10 V is appropriate at 25°C; at 45°C, 4.09 V corresponds to the same SOC. Applying temperature correction to the balancing threshold would improve accuracy at elevated temperatures.
3. **Capacity fade:** As cells age, individual group capacities diverge. A fixed voltage threshold on a pack with significant capacity imbalance may over- or under-balance. SOC-based balancing using the INA228 current sensor would address this.

### 9.4 Path to SOC-Based Balancing

The INA228 on the PCB enables a significant upgrade to the balancing strategy. With pack current measurement integrated:

1. **Coulomb counting:** SOC(t) = SOC(t₀) − (1/C_nominal) × ∫ I(t) dt. This provides SOC estimates through the flat OCV plateau where voltage is uninformative.
2. **ΔSOC balancing trigger:** Balance when ΔSOC exceeds 1%, rather than ΔV > 10 mV. More meaningful physically and more robust to voltage measurement noise.
3. **Capacity identification:** By comparing Coulombs counted between known OCV calibration points, individual group capacity can be estimated, enabling detection of degraded cells.

Implementing the INA228 driver (I2C, standard protocol) is the recommended next firmware milestone.

### 9.5 GUI Options for Real-Time Monitoring

Although not within the scope of the primary dissertation deliverable, a graphical interface for monitoring pack state has been considered:

**Option 1 — UART serial terminal (minimal effort):** Print `cell_v[]`, temperature, and balancing state as formatted text over UART2 (wired but unused). A Python script on a laptop running `matplotlib` in real time provides a functional monitoring display with no MCU changes beyond enabling UART2 `printf` output.

**Option 2 — CAN-based dashboard:** Once the CAN driver is implemented, standard FSAE data acquisition systems (MoTeC, AiM) or an open-source CAN viewer (CANalyzer, SavvyCAN) display pack telemetry on the pit lane laptop. This is the competition-ready solution.

**Option 3 — Web dashboard via USB CDC:** The STM32F446 has a full-speed USB peripheral. Implementing USB CDC (Virtual COM Port) in the HAL (already enabled in stm32f4xx_hal_conf.h) allows a web-based dashboard running in a browser (using WebSerial API) to display real-time pack data without any additional hardware.

---

## 10. Conclusions

### 10.1 Summary

This dissertation has delivered the following outcomes:

1. **Complete defect documentation:** Four critical firmware defects were identified, root-caused, and documented with corrective actions. These defects collectively prevented any accurate cell voltage measurement and incorrectly limited the pack's usable capacity to approximately 35% of its rated value.

2. **Corrected firmware foundation:** Specific code changes to `TLE9012dqu.c` and `main.c` were specified to address all four defects, restoring correct operation of the cell voltage measurement, OV/UV protection, and temperature sensing subsystems.

3. **Charge-phase-only balancing algorithm:** A complete passive balancing algorithm was designed and specified, incorporating a charge-state guard, a top-of-charge (≥4.10 V) activation trigger, a proportional PWM duty cycle controller, and a measure-and-hold termination condition that guarantees balance is confirmed by measurement before charge termination.

4. **Analytical convergence guarantee:** The minimum balancing duty cycle for convergence within the CV phase was derived from first principles. For the 41 Ω / 17.4 Ah pack, full-duty balancing resolves a 50 mV spread in approximately 5 minutes — well within the available CV window.

5. **Architecture for future enhancements:** Integration paths for the INA228 current sensor (SOC-based balancing), dual CAN transceivers (competition telemetry), and a GUI monitoring interface were specified.

### 10.2 Limitations

- Cell characterisation data (OCV curve, ECM parameters) for LG M58T at the operating temperature range were not experimentally measured. Datasheet and literature values were used throughout. Experimental OCV curve measurement would improve the accuracy of the minimum duty cycle derivation.
- The balancing termination condition (spread < 5 mV) has not been validated on the physical hardware at the time of writing. Bench validation remains pending.
- The INA228, CAN, and multi-temperature sensor features are architected but not implemented in the current firmware revision.
- No hardware-in-the-loop (HIL) test environment exists; all testing is conducted with the physical pack, which limits the ability to test fault conditions (e.g., a cell reaching 2.50 V) safely and repeatably.

### 10.3 Recommendations for Future Work

| Priority | Item | Effort |
|---|---|---|
| 1 | Apply all four firmware bug fixes; bench-validate cell voltage readings | 2–3 days |
| 2 | Implement and test balancing algorithm on physical pack | 1 week |
| 3 | Write INA228 I2C driver; implement Coulomb counting | 1–2 weeks |
| 4 | Implement CAN driver (STM32 bxCAN); define GLV CAN message set | 1–2 weeks |
| 5 | Enable all 5 NTC temperature channels; implement over-temperature fault | 2–3 days |
| 6 | Measure LG M58T OCV curve experimentally; refine balancing thresholds | 1 week |
| 7 | Implement EKF SOC estimator using INA228 current + TLE9012 voltage | 3–4 weeks |

---

## 11. References

[1] K. Liu, K. Li, Q. Peng, and C. Zhang, "A brief review on key technologies in the battery management system of electric vehicles," *Frontiers of Mechanical Engineering*, vol. 14, no. 1, pp. 47–64, 2019.

[2] T. R. Tanim, C. D. Rahn, and C.-Y. Wang, "State of charge estimation of a lithium ion cell based on a temperature dependent and electrolyte enhanced single particle model," *Energy*, vol. 80, pp. 731–739, 2015.

[3] A. Vasebi, M. Partovibakhsh, and S. M. T. Bathaee, "A novel combined battery model for state-of-charge estimation in lead-acid batteries based on extended Kalman filter for hybrid electric vehicle applications," *Journal of Power Sources*, vol. 174, no. 1, pp. 30–40, 2007.

[4] M. Daowd, N. Omar, P. Van Den Bossche, and J. Van Mierlo, "Passive and active battery balancing comparison based on MATLAB simulation," in *Proc. IEEE Vehicle Power and Propulsion Conference (VPPC)*, 2011, pp. 1–7.

[5] Infineon Technologies AG, *TLE9012DQU — 12-Channel Battery Monitor IC, Datasheet Rev. 1.1*, Munich, Germany, 2022.

[6] STMicroelectronics, *STM32F446xx — Advanced Arm-based 32-bit MCUs, Datasheet Rev. 5*, Geneva, Switzerland, 2021.

[7] Orion BMS, "Orion BMS 2 — Technical Reference Manual," Ewert Energy Systems, 2020. [Online]. Available: https://www.orionbms.com/manuals/

[8] Batrium, "Watchmon Core Technical Datasheet," Batrium Pty Ltd, 2022.

[9] Texas Instruments, *INA228 — 85-V, 20-Bit, Ultra-Precise Power/Energy/Charge Monitor, Datasheet SBOS886B*, Dallas, TX, USA, 2021.

[10] Formula SAE, "Formula SAE Rules 2024 — EV Chapter IC.12: Accumulator Management System," SAE International, 2024.

[11] Batemo GmbH, "Batemo Cell Portal — Experimentally Validated Li-ion Cell Models," [Online]. Available: https://www.batemo.de. *(Accessed: [INSERT DATE]. Model: [INSERT EXACT MODEL NAME AND VERSION].)*

---

## Appendices

### Appendix A — TLE9012DQU Register Map (Relevant Registers)

| Register | Address | Name | Description |
|---|---|---|---|
| PART_CONFIG | 0x01 | Cell enable | Bits 11–0 enable cells 11–0. Write 0x0FC0 for 6S (cells 6–11). |
| OV_THR | 0x02 | OV threshold | 16-bit ADC code. Set to 4.20 V → 55,050 counts. |
| UV_THR | 0x03 | UV threshold | 16-bit ADC code. Set to 2.50 V → 32,767 counts. |
| RR_ERR_CNT | 0x0A | Error flags | Bits 11–0: OV flags; bits 23–12: UV flags per cell. |
| BAL_DIAG_OC | 0x10 | Balancing over-current | Diagnostic: balancing FET open-circuit fault. |
| BAL_DIAG_UC | 0x11 | Balancing under-current | Diagnostic: balancing path broken. |
| BAL_SETTINGS | 0x16 | Balance enable | Bits 11–0. Bit n = 1 activates G_n. Write 0x0FC0 for all 6 cells. |
| BAL_PWM | 0x17 | Balance duty cycle | Bits [15:8] = period (0xFF), bits [7:0] = on-time. |
| PCVM_0–5 | 0x19–0x1E | Cell voltages 0–5 | V(U1–U0) to V(U6–U5). All zero in this design (U1–U6 shorted). |
| PCVM_6–11 | 0x1F–0x24 | Cell voltages 6–11 | V(U7–U6) to V(U12–U11). **These are the actual 6 cell voltages.** |
| PCVM_START | 0x18 | Trigger PCVM | Write 0x0001 to start a new voltage measurement cycle. |
| TEMP_CONF | 0x21 | Temperature config | Number of NTC sensors, pull-down current, OT threshold. |
| TEMP_R0–4 | 0x29–0x2D | Temperature results | 10-bit ADC per channel; includes validity and error flags. |
| WD_CFG | 0x3D | Watchdog | Write 0x007F to reset watchdog. |

### Appendix B — Complete List of Firmware Changes Required

| File | Line | Change |
|---|---|---|
| `main.c` | 18 | `TLE9012_Init(&bms, 12)` → `TLE9012_Init(&bms, 6)` |
| `main.c` | 24 | `TLE9012_EnableCellMonitoring(&bms, 12)` → `TLE9012_EnableCellMonitoring(&bms, 6)` |
| `main.c` | 26 | `TLE9012_SetUnderVoltageThreshold(&bms, 2.9f)` → `TLE9012_SetUnderVoltageThreshold(&bms, 2.50f)` |
| `main.c` | 27 | `TLE9012_SetUndervoltageCells(&bms, 12, 0)` → `TLE9012_SetUndervoltageCells(&bms, 6, 0)` |
| `main.c` | 28 | `TLE9012_SetOverVoltageThreshold(&bms, 3.4f)` → `TLE9012_SetOverVoltageThreshold(&bms, 4.20f)` |
| `main.c` | 50 | `TLE9012_ReadCellVoltages(&bms, cell_raw, cell_v, 12)` → `TLE9012_ReadCellVoltages(&bms, cell_raw, cell_v, 6)` |
| `TLE9012dqu.c` | 199 | `0x19U + i` → `0x1FU + i` |
| `TLE9012dqu.c` | new | Add `TLE9012_SetBalancing()` function |
| `TLE9012dqu.c` | new | Add `TLE9012_SetBalancingPWM()` function |
| `main.c` | loop | Add `BMS_ExecuteBalancing()` call |
| `main.c` | init | Add `TLE9012_SetTemperatureConfig(&bms, 5, ...)` for all 5 channels |
| `main.c` | loop | Add loop reading `TLE9012_ReadExternalTemperature()` for channels 0–4 |

### Appendix C — LG M58T Cell Specifications

| Parameter | Value | Source |
|---|---|---|
| Form factor | 21700 cylindrical | LG Energy Solution datasheet |
| Chemistry | NMC (Nickel Manganese Cobalt) | LG Energy Solution datasheet |
| Nominal capacity | 5800 mAh | LG Energy Solution datasheet |
| Nominal voltage | 3.63 V | LG Energy Solution datasheet |
| Maximum charge voltage | 4.20 V | LG Energy Solution datasheet |
| Minimum discharge voltage | 2.50 V | LG Energy Solution datasheet |
| Standard charge current | 2.9 A (0.5C) | LG Energy Solution datasheet |
| Maximum continuous discharge | 20 A (3.45C) | LG Energy Solution datasheet |
| Operating temperature (charge) | 0–45 °C | LG Energy Solution datasheet |
| Operating temperature (discharge) | −20–60 °C | LG Energy Solution datasheet |
| Cell mass | 68 g | LG Energy Solution datasheet |
| OCV slope at top-of-charge (4.1–4.2 V) | ~40–100 mV per % SOC | Estimated from typical NMC curve [2] |

### Appendix D — Balancing Convergence Table

Convergence time at full duty cycle (I_bal = 102 mA, C_group = 17,400 mAh):

| Initial Spread ΔV | ΔSOC Equivalent | ΔQ to Remove | Time at Full Duty |
|---|---|---|---|
| 5 mV | ~0.05% | 0.87 mAh | 0.5 minutes |
| 10 mV | ~0.10% | 1.74 mAh | 1.0 minutes |
| 20 mV | ~0.20% | 3.48 mAh | 2.1 minutes |
| 50 mV | ~0.50% | 8.70 mAh | 5.1 minutes |
| 100 mV | ~1.00% | 17.4 mAh | 10.2 minutes |

All cases converge within a 60-minute CV phase at full duty. PWM reduction below 50% duty cycle is safe for all typical competition imbalances (ΔV < 50 mV).

---

### Appendix E — Batemo-Extracted OCV Table and ECM Parameters *(Placeholder)*

> **[PLACEHOLDER — Complete this appendix after running the Batemo extraction script in Section 3.4]**
>
> This appendix should contain:
>
> **E.1 — OCV Table (101 points, 25°C)**
>
> | SOC (%) | OCV (V) | SOC (%) | OCV (V) |
> |---|---|---|---|
> | 0 | *[Batemo]* | 50 | *[Batemo]* |
> | 10 | *[Batemo]* | 60 | *[Batemo]* |
> | 20 | *[Batemo]* | 70 | *[Batemo]* |
> | 30 | *[Batemo]* | 80 | *[Batemo]* |
> | 40 | *[Batemo]* | 90 | *[Batemo]* |
> | — | — | 100 | *[Batemo]* |
>
> *Full 101-point table to be generated by MATLAB export script and provided as a C array suitable for direct inclusion in firmware.*
>
> **E.2 — ECM Parameters at SOC = 50%, T = 25°C**
>
> | Parameter | Symbol | Value | Unit |
> |---|---|---|---|
> | Ohmic resistance | R₀ | *[Batemo]* | mΩ |
> | RC pair 1 resistance | R₁ | *[Batemo]* | mΩ |
> | RC pair 1 capacitance | C₁ | *[Batemo]* | F |
> | RC pair 2 resistance | R₂ | *[Batemo]* | mΩ |
> | RC pair 2 capacitance | C₂ | *[Batemo]* | F |
>
> **E.3 — OCV Slope at Top-of-Charge**
>
> | SOC Range | dV/dSOC (V/%) | dV/dAh (V/mAh) | α (V/mAh) |
> |---|---|---|---|
> | 90–95% | *[Batemo]* | *[Batemo]* | *[used in Section 7.4]* |
> | 95–100% | *[Batemo]* | *[Batemo]* | — |
>
> **E.4 — Batemo Model Details**
>
> | Field | Value |
> |---|---|
> | Cell name in Batemo portal | *[e.g., LG INR21700-M58T]* |
> | Model version | *[e.g., v2.1]* |
> | Characterisation temperature range | *[e.g., −10°C to 60°C]* |
> | Date accessed | *[INSERT DATE]* |
> | NUS licence / access method | *[e.g., NUS MATLAB campus licence, Batemo App v3.x]* |

---

*End of Report*
