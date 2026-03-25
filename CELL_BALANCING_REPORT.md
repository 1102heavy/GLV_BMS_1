# Cell Balancing Report — GLV BMS Firmware (6S3P Pack)
## TLE9012DQU on STM32F446RE · LG M58T (21700 NMC) Cells

---

## 1. Executive Summary

Analysis of both the firmware and hardware schematic reveals **three pre-existing critical bugs** that must be fixed before any balancing logic can function correctly, plus the complete absence of any balancing implementation. The hardware is fully wired for passive balancing via 41 Ω discharge resistors driven by the TLE9012DQU's G6–G11 gate pins, but the firmware does not use the `BAL_SETTINGS` register (0x16) at all. This report documents the hardware topology confirmed from the schematic, the critical firmware bugs, the corrected voltage thresholds for the LG M58T cell, and three concrete balancing strategies to implement.

---

## 2. Cell Chemistry — LG M58T (21700 NMC)

The LG M58T is a **21700-format Li-NMC cylindrical cell**, not LiFePO4. All voltage thresholds in the current firmware are configured for the wrong chemistry.

| Parameter | LG M58T Specification | Current Firmware Value | Status |
|---|---|---|---|
| Maximum charge voltage | **4.20 V** | 3.4 V (OV threshold) | **CRITICAL BUG — pack cannot charge past 3.4 V** |
| Minimum discharge voltage | **2.50 V** | 2.9 V (UV threshold) | **Wrong — cutting off 400 mV of usable capacity** |
| Nominal voltage | 3.63 V | — | — |
| Capacity (single cell) | 5800 mAh | — | — |
| Pack configuration | 6S3P | — | 18 cells total |
| Pack nominal voltage | ~21.8 V | — | — |
| Pack full charge voltage | 25.2 V | — | — |

With the current OV threshold at 3.4 V, the BMS will fault and cut charging at only **~81% SOC**. This must be corrected immediately. The corrected thresholds are:

```c
TLE9012_SetUnderVoltageThreshold(&bms, 2.5f);   // LG M58T min: 2.5 V
TLE9012_SetOverVoltageThreshold(&bms,  4.2f);   // LG M58T max: 4.2 V
```

---

## 3. Hardware Topology from Schematic

### 3.1 Cell Tap Mapping

The pack cells are **not** connected to U0–U5 as previously assumed. The schematic shows:

```
CELL TAP 0  ──► U0  (pack negative / absolute reference)
CELL TAP 7  ──► U7  (top of Cell 1)
CELL TAP 8  ──► U8  (top of Cell 2)
CELL TAP 9  ──► U9  (top of Cell 3)
CELL TAP 10 ──► U10 (top of Cell 4)
CELL TAP 11 ──► U11 (top of Cell 5)
CELL TAP 12 ──► U12 (top of Cell 6 = pack positive)
CELL TAP 12 ──► U12P (upper reference for IC power)
```

Pins U1–U6 are shorted to U0 (CELL TAP 0 / pack negative) so that the IC sees zero volts across cells 0–5. **The 6 real cell voltages are measured by the IC as cells 6–11**, i.e., using PCVM registers 0x1F–0x24, not 0x19–0x1E.

### 3.2 Cell Filter Network (per cell, from schematic)

Each cell tap feeds into the IC through an RC filter:

```
CELL TAP N ──[ 10 Ω ]──┬──► U_N pin (voltage measurement)
                        │
                       ═╪═  330 nF + 100 nF (to GND)
```

Additionally, CELL TAP 12 feeds U12P through a separate filter (R16: 10 Ω + R17: 5.1 Ω, C23: 1 µF, C24: 100 nF).

### 3.3 Balancing Circuit (per cell, from schematic)

Each of the 6 cells has a dedicated **41 Ω balancing resistor** connecting the cell tap node to the corresponding gate drive output pin of the TLE9012DQU:

```
CELL TAP 7  ──[ 41 Ω R39 ]──► G6   (balances Cell 1)
CELL TAP 8  ──[ 41 Ω R37 ]──► G7   (balances Cell 2)
CELL TAP 9  ──[ 41 Ω R34 ]──► G8   (balances Cell 3)
CELL TAP 10 ──[ 41 Ω R31 ]──► G9   (balances Cell 4)
CELL TAP 11 ──[ 41 Ω R26 ]──► G10  (balances Cell 5)
CELL TAP 12 ──[ 41 Ω R23 ]──► G11  (balances Cell 6)
```

When the IC activates balancing for a cell (by asserting the Gn pin), current flows from the cell tap through the 41 Ω resistor, dissipating the cell's energy as heat and reducing its voltage.

**Balancing current at full charge (4.2 V):**
```
I_bal = 4.2 V / 41 Ω ≈ 102 mA per cell
P_bal = 4.2² / 41  ≈ 0.43 W per resistor
```

**Balancing current at nominal (3.63 V):**
```
I_bal = 3.63 V / 41 Ω ≈ 88 mA
P_bal = 3.63² / 41  ≈ 0.32 W
```

If all 6 cells balance simultaneously: total power = ~2.6 W at full charge.

### 3.4 Main Pack Switch MOSFETs

The schematic shows **N-channel BSC050N04LS_G MOSFETs** (Q1–Q4 in SuperSO8 package) forming the main pack discharge path between "Battery Negative" and "Pack Negative". These are separate from the balancing circuit and are not relevant to the balancing implementation.

### 3.5 Additional Hardware (Not Yet in Firmware)

The schematic reveals peripherals that the current firmware does not use:

| Component | Interface | Function |
|---|---|---|
| INA228AIDGST | I2C (I2C_SCL/SDA) | High-precision pack current + power monitor (ALERT pin wired) |
| TCAN334GD ×2 | CAN1, CAN2 | Dual CAN transceivers for external comms (telemetry, charger) |
| NTC_A1–A5 | TMP0–TMP4 (TLE9012) | 5 NTC temperature sensors across the pack |
| ERB-RD5R00X ×4 | Series in each tap | Cell tap protection fuses (F2–F5) |
| CHARGE_INTERRUPT (PC2) | GPIO | External charger control signal (defined in `uart_dma.h` but unused in `main.c`) |

---

## 4. Critical Firmware Bugs (Must Fix First)

### Bug 1 — PCVM Register Offset (CRITICAL — Wrong cells being read)

**Location:** [TLE9012dqu.c:199](Core/Src/TLE9012dqu.c#L199)

```c
// Current (WRONG — reads PCVM_0 to PCVM_5, which are all 0 V from shorted U1–U6):
st = tle9012_read_reg(dev, (uint8_t)(0x19U + i), dev->rx_buf, 5);

// Correct (reads PCVM_6 to PCVM_11, the actual cell voltages):
st = tle9012_read_reg(dev, (uint8_t)(0x1FU + i), dev->rx_buf, 5);
```

The TLE9012DQU register map for cell voltages:
```
0x19 = PCVM_0  → V(U1) - V(U0)  ← currently reading this (all zeros)
...
0x1F = PCVM_6  → V(U7) - V(U6)  ← Cell 1 of the pack (CELL TAP 7 - 0)
0x20 = PCVM_7  → V(U8) - V(U7)  ← Cell 2
0x21 = PCVM_8  → Cell 3
0x22 = PCVM_9  → Cell 4
0x23 = PCVM_10 → Cell 5
0x24 = PCVM_11 → Cell 6
```

### Bug 2 — Cell Count Set to 12 Instead of 6

**Location:** [main.c:18–34](Core/Src/main.c#L18)

All `12` references must become `6`. With 12, the PART_CONFIG register (0x01) enables cells 0–11. The correct value for this hardware is bits 11–6 only (`0x0FC0`), which `TLE9012_EnableCellMonitoring(6)` correctly generates (it packs from bit 11 downward for `num_cells` bits).

```c
// Fix all four occurrences:
TLE9012_Init(&bms, 6);
TLE9012_EnableCellMonitoring(&bms, 6);     // sets PART_CONFIG = 0x0FC0
TLE9012_ReadCellVoltages(&bms, cell_raw, cell_v, 6);
TLE9012_SetUndervoltageCells(&bms, 6, 0);
```

### Bug 3 — Voltage Thresholds Wrong for LG M58T Chemistry

**Location:** [main.c:26–28](Core/Src/main.c#L26)

```c
// Current (wrong — tuned for LiFePO4):
TLE9012_SetUnderVoltageThreshold(&bms, 2.9f);
TLE9012_SetOverVoltageThreshold(&bms,  3.4f);

// Correct for LG M58T (NMC):
TLE9012_SetUnderVoltageThreshold(&bms, 2.5f);
TLE9012_SetOverVoltageThreshold(&bms,  4.2f);
```

**Impact of bug:** The OV threshold at 3.4 V causes the BMS to fault during every charge cycle before reaching useful SOC. At 3.4 V/cell the pack has only ~30–40% SOC for NMC chemistry.

---

## 5. BAL_SETTINGS Register Bit Mapping for This Hardware

Because the pack cells occupy IC cell positions 6–11, the `BAL_SETTINGS` register (0x16) bits map as follows:

```
BAL_SETTINGS bit 6  → G6  → balances Cell 1 (CELL TAP 7 – CELL TAP 0)
BAL_SETTINGS bit 7  → G7  → balances Cell 2 (CELL TAP 8 – CELL TAP 7)
BAL_SETTINGS bit 8  → G8  → balances Cell 3
BAL_SETTINGS bit 9  → G9  → balances Cell 4
BAL_SETTINGS bit 10 → G10 → balances Cell 5
BAL_SETTINGS bit 11 → G11 → balances Cell 6 (CELL TAP 12 – CELL TAP 11)
```

To balance all 6 cells simultaneously: write `0x0FC0` to register 0x16.
To balance Cell 1 only: write `0x0040`.
To balance Cell 6 only: write `0x0800`.

The `bal_mask` bit position for cell `i` (0-indexed from Cell 1):
```c
bal_mask |= (1U << (6U + i));   // bit 6 = Cell 1, bit 11 = Cell 6
```

> Note: This differs from the previous version of this report, which incorrectly assumed cells occupied positions 0–5 (G0–G5).

---

## 6. TLE9012DQU Balancing Hardware Capabilities

### 6.1 Passive Balancing Only

The TLE9012DQU uses external passive balancing exclusively — no active (energy-transfer) topology. This hardware confirms that via the 41 Ω per-cell resistors. Energy is dissipated as heat.

### 6.2 Automatic Measurement Pause (PBOFF)

The IC automatically pauses balancing during all PCVM/SCVM/BVM measurement windows to prevent the 102 mA discharge load from corrupting the ADC reading. This requires no firmware action — `TLE9012_ReadCellVoltages()` can be called while balancing is active.

### 6.3 Watchdog Safety

If the MCU stops calling `TLE9012_ResetWatchdog()` (register 0x3D), the IC enters sleep mode and disables all balancing automatically. This is already present in the `while(1)` loop.

### 6.4 BAL_SETTINGS Register (0x16)

One register write enables or disables balancing per-cell independently. Any combination of cells may be balanced simultaneously.

### 6.5 BAL_PWM Register (0x17)

System-wide PWM duty cycle for all active balancing channels:
```
Bits [15:8] = period (in tBAL_PWM_LSB units)
Bits  [7:0] = on-time
Duty cycle  = on_time / period
```

At 50% duty cycle with 41 Ω: average I_bal ≈ 51 mA, P ≈ 0.21 W/cell.

### 6.6 Balancing Diagnostics (0x10, 0x11)

- `BAL_DIAG_OC` (0x10): overcurrent flag per cell → shorted balancing resistor
- `BAL_DIAG_UC` (0x11): undercurrent flag per cell → open circuit (broken resistor or wiring)

---

## 7. When to Balance: Charge-Phase Only

**Balancing must only run during charging, never during discharge.** During discharge, balancing burns energy from cells that should be powering the load — every milliwatt dissipated in a 41 Ω resistor is capacity lost from the pack. The only correct time to balance is when a charger is actively supplying energy, so the cost of diverting a little of it to level the cells is negligible.

### 7.1 Charge State Detection

The board provides two mechanisms to detect whether the pack is charging:

**Option A — `CHARGE_INTERRUPT` pin (PC2) — available now, no new code**

From the schematic, PC2 is driven HIGH by the STM32 to enable the charger. The function `charge_interrupt_is_high()` is already declared in [uart_dma.h:33](Core/Inc/uart_dma.h#L33). If the firmware only asserts this pin when a charger is connected and charging is authorised, then reading it back is a reliable charge-state signal — no extra driver required.

```c
// Gate all balancing on the charging signal:
if (charge_interrupt_is_high()) {
    // ... run balancing logic ...
} else {
    TLE9012_SetBalancing(&bms, 0x0000U);  // disable all balancing immediately
}
```

**Option B — INA228AIDGST current sensor (I2C) — most accurate, requires driver**

The INA228 on the schematic directly measures pack current. Positive current = charging, negative = discharging. Once an I2C driver is written for it, the sign of the measured current becomes the definitive charge-state signal. This is the long-term correct approach.

**Option C — Voltage trend guard (no extra hardware or pins)**

As a software-only fallback: if the average cell voltage has been rising over the last N samples, the pack is charging. Simple but adds latency before balancing starts.

**Recommended now:** Use Option A (`charge_interrupt_is_high()`). It requires zero new code beyond what the balancing strategies already need.

---

## 8. Balancing Strategies for 6S3P LG M58T

All strategies below use the corrected thresholds (4.2 V max, 2.5 V min) and include the charge-phase guard.

---

### Strategy 1 — Fixed Delta, Charge-Only (Simplest)

**Concept:** While charging, balance any cell more than 10 mV above the pack minimum. Disable balancing the moment charging stops.

**New driver function needed (add to [TLE9012dqu.c](Core/Src/TLE9012dqu.c) and [TLE9012dqu.h](Core/Inc/TLE9012dqu.h)):**

```c
TLE9012_Status_t TLE9012_SetBalancing(TLE9012_Handle_t *dev, uint16_t bal_mask)
{
    if (dev == NULL) return TLE9012_ERR_NULL;
    return tle9012_write_reg16(dev, 0x16, bal_mask & 0x0FFFU);
}
```

**Logic to add in `main.c` inside `while(1)` (after `TLE9012_ReadCellVoltages`):**

```c
#define BALANCE_DELTA_V   0.010f   // 10 mV hysteresis band
#define NUM_CELLS         6

if (charge_interrupt_is_high()) {
    float v_min = cell_v[0];
    for (int i = 1; i < NUM_CELLS; i++) {
        if (cell_v[i] < v_min) v_min = cell_v[i];
    }

    uint16_t bal_mask = 0U;
    for (int i = 0; i < NUM_CELLS; i++) {
        if (cell_v[i] > v_min + BALANCE_DELTA_V) {
            bal_mask |= (1U << (6U + i));  // bits 6–11 for cells 1–6
        }
    }
    TLE9012_SetBalancing(&bms, bal_mask);
} else {
    TLE9012_SetBalancing(&bms, 0x0000U);  // not charging — disable all balancing
}
```

**Advantages:** Simple. No wasted energy during discharge. No state machine.

**Limitations:** Balances at any SOC during charging. For NMC, mid-charge voltage differences are not a reliable SOC indicator (flat plateau region), so this can balance unnecessarily early in the charge cycle.

---

### Strategy 2 — Top-of-Charge, Charge-Only (Recommended for LG M58T)

**Concept:** Only balance when the pack is both charging AND cells are near full charge (≥ 4.10 V). For NMC chemistry the voltage curve steepens sharply above ~80% SOC, making cell-to-cell voltage differences a reliable indicator of actual SOC imbalance only in this region. Balancing here maximises capacity utilisation by ensuring every cell reaches 4.20 V at end-of-charge.

```
Why 4.10 V? Below this, the NMC curve is nearly flat. A 20 mV difference at 3.8 V
could mean almost nothing in SOC terms. At 4.10 V the same 20 mV = meaningful
SOC divergence that warrants corrective action.
```

**Thresholds:**
```c
#define V_BAL_START     4.10f    // enter balancing above this per-cell voltage
#define V_BAL_STOP      4.05f    // exit balancing below this (hysteresis)
#define BALANCE_DELTA_V 0.010f   // minimum spread to trigger balancing
#define NUM_CELLS       6
```

**Full logic for `main.c` inside `while(1)`:**

```c
static uint8_t bal_active[NUM_CELLS] = {0};  // must be static — persists across iterations

if (charge_interrupt_is_high()) {
    float v_min = cell_v[0];
    for (int i = 1; i < NUM_CELLS; i++) {
        if (cell_v[i] < v_min) v_min = cell_v[i];
    }

    uint16_t bal_mask = 0U;
    for (int i = 0; i < NUM_CELLS; i++) {
        if (!bal_active[i]) {
            // Arm: cell is in top-of-charge zone AND above pack minimum
            if (cell_v[i] >= V_BAL_START && cell_v[i] > v_min + BALANCE_DELTA_V) {
                bal_active[i] = 1U;
            }
        } else {
            // Disarm: cell has been discharged to stop threshold OR has converged
            if (cell_v[i] < V_BAL_STOP || cell_v[i] <= v_min + BALANCE_DELTA_V) {
                bal_active[i] = 0U;
            }
        }

        if (bal_active[i]) {
            bal_mask |= (1U << (6U + i));
        }
    }
    TLE9012_SetBalancing(&bms, bal_mask);

} else {
    // Not charging — disable balancing and reset state
    TLE9012_SetBalancing(&bms, 0x0000U);
    for (int i = 0; i < NUM_CELLS; i++) bal_active[i] = 0U;
}
```

**State transitions per cell:**
```
IDLE ──► BALANCING  when: charging=1  AND  cell_v ≥ 4.10 V  AND  cell_v > V_min + 10 mV
BALANCING ──► IDLE  when: charging=0  OR   cell_v < 4.05 V  OR   cell_v ≤ V_min + 10 mV
```

**Why reset `bal_active` when charging stops:** If the charger is disconnected mid-balance (e.g., cable pulled), the state flags would wrongly re-arm the moment charging resumes, potentially balancing at a voltage where the cell happens to be ≥ 4.10 V for reasons other than imbalance. Resetting on charger disconnect ensures a clean evaluation on the next charge cycle.

**Advantages:** Zero energy wasted during discharge. Balances only where voltage differences are meaningful for NMC. Standard industry approach for Li-ion BMS. Ensures every cell hits 4.20 V before the charge terminates.

**Limitations:** Balancing only corrects imbalance at end-of-charge. For very diverged packs (>100 mV spread), convergence may take multiple charge cycles.

---

### Strategy 3 — PWM-Modulated Balancing (Thermal Management)

**Concept:** Use register 0x17 to reduce average balancing current via PWM, lowering the steady-state power dissipation on the 41 Ω resistors. Since all 6 resistors could simultaneously dissipate ~2.6 W at full charge, PWM reduces total board heating.

**New driver function:**

```c
TLE9012_Status_t TLE9012_SetBalancingPWM(TLE9012_Handle_t *dev,
                                          uint8_t period,
                                          uint8_t on_time)
{
    if (dev == NULL) return TLE9012_ERR_NULL;
    uint16_t data = ((uint16_t)period << 8) | on_time;
    return tle9012_write_reg16(dev, 0x17, data);
}
```

**Call once in init, before the main loop:**
```c
TLE9012_SetBalancingPWM(&bms, 10, 5);   // 50% duty cycle → ~51 mA, ~0.21 W/cell
```

**Power at different duty cycles with 41 Ω (at 4.2 V):**

| Duty Cycle | I_avg | P per cell | P total (6 cells) |
|---|---|---|---|
| 100% (no PWM) | 102 mA | 0.43 W | 2.6 W |
| 75% | 76 mA | 0.32 W | 1.9 W |
| 50% | 51 mA | 0.21 W | 1.3 W |
| 25% | 26 mA | 0.11 W | 0.6 W |

**Recommended:** Use 50% duty cycle as a starting point. If the board runs cool during bench testing with 100% duty, PWM is not needed.

---

## 8. Required Code Changes — Complete Summary

### 8.1 In [TLE9012dqu.c:199](Core/Src/TLE9012dqu.c#L199) — Fix PCVM Register Offset

```c
// CHANGE THIS LINE:
st = tle9012_read_reg(dev, (uint8_t)(0x19U + i), dev->rx_buf, 5);

// TO:
st = tle9012_read_reg(dev, (uint8_t)(0x1FU + i), dev->rx_buf, 5);
```

### 8.2 In [main.c](Core/Src/main.c) — Fix Cell Count and Voltage Thresholds

```c
// Change all 12 → 6, and fix thresholds:
TLE9012_Init(&bms, 6);
TLE9012_Wakeup(&bms);
TLE9012_Wakeup(&bms);
TLE9012_Wakeup(&bms);
TLE9012_EnableCellMonitoring(&bms, 6);
TLE9012_ActivateErrors(&bms, 0b0001000000100000);
TLE9012_SetUnderVoltageThreshold(&bms, 2.5f);    // LG M58T min
TLE9012_SetUndervoltageCells(&bms, 6, 0);
TLE9012_SetOverVoltageThreshold(&bms, 4.2f);     // LG M58T max
```

And in the main loop:
```c
TLE9012_ReadCellVoltages(&bms, cell_raw, cell_v, 6);
```

### 8.3 Add to [TLE9012dqu.h](Core/Inc/TLE9012dqu.h)

```c
TLE9012_Status_t TLE9012_SetBalancing(TLE9012_Handle_t *dev, uint16_t bal_mask);
TLE9012_Status_t TLE9012_SetBalancingPWM(TLE9012_Handle_t *dev, uint8_t period, uint8_t on_time);
TLE9012_Status_t TLE9012_ReadBalancingDiag(TLE9012_Handle_t *dev,
                                            uint16_t *oc_flags,
                                            uint16_t *uc_flags);
```

### 8.4 Add to [TLE9012dqu.c](Core/Src/TLE9012dqu.c)

```c
TLE9012_Status_t TLE9012_SetBalancing(TLE9012_Handle_t *dev, uint16_t bal_mask)
{
    if (dev == NULL) return TLE9012_ERR_NULL;
    return tle9012_write_reg16(dev, 0x16, bal_mask & 0x0FFFU);
}

TLE9012_Status_t TLE9012_SetBalancingPWM(TLE9012_Handle_t *dev, uint8_t period, uint8_t on_time)
{
    if (dev == NULL) return TLE9012_ERR_NULL;
    return tle9012_write_reg16(dev, 0x17, ((uint16_t)period << 8) | on_time);
}

TLE9012_Status_t TLE9012_ReadBalancingDiag(TLE9012_Handle_t *dev,
                                            uint16_t *oc_flags,
                                            uint16_t *uc_flags)
{
    if (dev == NULL || oc_flags == NULL || uc_flags == NULL) return TLE9012_ERR_PARAM;
    TLE9012_Status_t st;

    st = tle9012_read_reg(dev, 0x10, dev->rx_buf, 5);
    if (st != TLE9012_OK) return st;
    *oc_flags = ((uint16_t)dev->rx_buf[2] << 8) | dev->rx_buf[3];

    st = tle9012_read_reg(dev, 0x11, dev->rx_buf, 5);
    if (st != TLE9012_OK) return st;
    *uc_flags = ((uint16_t)dev->rx_buf[2] << 8) | dev->rx_buf[3];

    return TLE9012_OK;
}
```

---

## 9. Safety Considerations

| Risk | Schematic Evidence | Mitigation |
|---|---|---|
| Wrong cells being monitored | U7–U12 used, not U0–U5 | Fix register offset to 0x1F (Bug 1) |
| Pack cannot fully charge | OV threshold 3.4 V, cell max 4.2 V | Fix OV to 4.2 V (Bug 3) |
| Usable capacity wasted | UV threshold 2.9 V, cell min 2.5 V | Fix UV to 2.5 V (Bug 3) |
| Balancing during discharge wastes energy | No charge-state guard in firmware | Gate all balancing on `charge_interrupt_is_high()` |
| Resistor over-dissipation | 41 Ω, 0.43 W at 4.2 V, 2.6 W total | Use PWM at 50% if board runs hot during charge |
| Balancing all 6 cells simultaneously | 6 × 0.43 W = 2.6 W continuous | PWM or stagger; only occurs during charging anyway |
| Open/shorted balancing path | Fuses F2–F5 on cell taps | Add `TLE9012_ReadBalancingDiag()` call after each balance cycle |
| MCU hang mid-balancing | Watchdog at reg 0x3D already in loop | IC disables balancing on watchdog expiry — already safe |
| Floating cell inputs (U1–U6) | Shorted to U0 at CELL TAP 0 | Fix cell count to 6; PART_CONFIG 0x0FC0 disables cells 0–5 |
| Charger disconnected mid-balance | CHARGE_INTERRUPT is output, not latched | Reset `bal_active[]` flags whenever `charge_interrupt_is_high()` is false |

---

## 10. Recommended Implementation Order

1. **Fix Bug 1** — change `0x19U + i` to `0x1FU + i` in [TLE9012dqu.c:199](Core/Src/TLE9012dqu.c#L199). Without this, every voltage reading is wrong.
2. **Fix Bug 2** — change all `12` to `6` in [main.c](Core/Src/main.c).
3. **Fix Bug 3** — correct UV to 2.5 V, OV to 4.2 V in [main.c](Core/Src/main.c).
4. **Bench test** — verify `cell_v[0..5]` now reads real cell voltages (~3.5–4.2 V with cells installed).
5. **Add `TLE9012_SetBalancing()`** — one register write; add to driver.
6. **Implement Strategy 1** (fixed delta, charge-only) — connect a charger and verify the G6–G11 hardware paths (41 Ω resistors) warm up correctly per cell. Confirm that disconnecting the charger immediately zeroes the balancing mask.
7. **Upgrade to Strategy 2** (top-of-charge, charge-only) — this is the correct long-term implementation for LG M58T.
8. **Add balancing diagnostics** — read BAL_DIAG_OC and BAL_DIAG_UC after each balancing write to catch open/short faults on the 6 balancing paths.
9. **If PCB runs hot during charging** — add `TLE9012_SetBalancingPWM()` at 50% duty cycle.
10. **Future: add INA228 driver** — replace `charge_interrupt_is_high()` with a current sign check for a more robust charge-state signal independent of firmware intent.

---

## 11. Register Quick Reference

| Register | Address | Description | Firmware Status |
|---|---|---|---|
| `PART_CONFIG` | 0x01 | Cell enable bits [11:0] | Written — but with wrong count (12 vs 6) |
| `OL_OV_THR` | 0x02 | OV threshold | Written — **wrong value (3.4 V)** |
| `OL_UV_THR` | 0x03 | UV threshold | Written — **wrong value (2.9 V)** |
| `TEMP_CONF` | 0x04 | Temperature sensor config | Not called |
| `RR_ERR_CNT` | 0x0A | Error / UV flags | Read — correct |
| `UV_OV_RESET` | 0x0C | UV latch reset | Written |
| **`BAL_DIAG_OC`** | **0x10** | **Balancing OC diagnostic** | **Not implemented** |
| **`BAL_DIAG_UC`** | **0x11** | **Balancing UC diagnostic** | **Not implemented** |
| **`BAL_SETTINGS`** | **0x16** | **Per-cell balancing enable** | **Not implemented** |
| **`BAL_PWM`** | **0x17** | **Balancing PWM period/duty** | **Not implemented** |
| `MEAS_CTRL` | 0x18 | Start PCVM | Written — correct |
| `PCVM_0`–`PCVM_5` | 0x19–0x1E | Cells 0–5 (all 0 V, shorted) | Read — **WRONG REGISTERS** |
| **`PCVM_6`–`PCVM_11`** | **0x1F–0x24** | **Actual pack cells 1–6** | **Not read (bug)** |
| `EXT_TEMP_0`–`4` | 0x29–0x2D | External NTC results | Only ch2 read; 4 channels unused |
| `ICVID` | 0x36 | Config readback | Available |
| `WD_CFG` | 0x3D | Watchdog reset | Implemented — correct |

Entries in **bold** are either not implemented or contain critical bugs.

---

---

## 12. Guaranteed Balance at End-of-Charge: Math and Algorithm

### 12.1 Why No Fixed PWM Can Guarantee Balance

A pre-calculated duty cycle cannot guarantee balance in all conditions because the initial voltage spread ΔV is unknown before charging starts. A pack that has sat idle for weeks may have 5 mV spread; one that has been cycled unevenly may have 80 mV. Any fixed PWM that handles the worst case will be unnecessarily aggressive for the typical case, wasting energy as heat.

The only provably correct guarantee is a **feedback termination condition**: measure the spread after balancing, and do not terminate the charge until the spread is below a defined threshold ε.

### 12.2 Physics of CV-Phase Balancing

During the CV phase, the charger holds the total pack voltage at **25.2 V** (4.20 V × 6). When the TLE9012 activates balancing on a high cell:

```
High cell ──[ 41 Ω ]──► G_n (IC sink)
                            │
                            ▼
                     ~102 mA discharged from high cell
                            │
                     Charger sees increased load
                            │
                     Charger raises current to hold 25.2 V
                            │
                     Extra current goes to remaining cells
                            ▼
              High cell voltage ↓   Other cells voltage ↑
                         Spread converges
```

The charger actively cooperates with the balancing process during CV. No energy is wasted from the pack — the charger supplies it. This is why the CV phase is the correct and efficient window.

### 12.3 Minimum Balancing Current Formula

To reduce a voltage spread ΔV to zero within available time T, the required average balancing current is:

```
               ΔV × C_group
I_bal_min = ─────────────────        [mA, with T in hours, C in mAh]
               α × 100 × T
```

**Variables for this hardware:**

| Variable | Value | Source |
|---|---|---|
| `R_BAL` | 41 Ω | Schematic (R23, R26, R31, R34, R37, R39) |
| `I_bal_max` | 4.2 V / 41 Ω = **102 mA** | Hardware maximum, full duty |
| `C_group` | 3 × 5800 mAh = **17,400 mAh** | LG M58T spec × 3P |
| `α` (OCV slope at 4.10–4.20 V) | **~50 mV per 1% SOC** | LG M58T NMC chemistry |

The NMC OCV curve is steep at the top of charge — 50 mV per 1% SOC is a conservative estimate. The actual slope can be measured by slowly charging a single cell and recording V vs. Q near 4.2 V.

**Required BAL_PWM duty cycle:**

```
     I_bal_min        ΔV × C_group
D = ──────────  =  ────────────────────────
     I_bal_max      α × 100 × T × I_bal_max
```

### 12.4 Worked Examples (LG M58T, 60-minute CV phase, I_bal_max = 102 mA)

| Spread ΔV | ΔSOC | Charge to move ΔQ | I_min needed | Duty cycle D | Balance time at D=1 |
|---|---|---|---|---|---|
| 5 mV | 0.1% | 17.4 mAh | 17.4 mA | **17%** | ~10 min |
| 10 mV | 0.2% | 34.8 mAh | 34.8 mA | **34%** | ~20 min |
| 20 mV | 0.4% | 69.6 mAh | 69.6 mA | **68%** | ~41 min |
| 50 mV | 1.0% | 174 mAh | 174 mA | >100% → **D=1** | ~102 min (extend CV) |
| 100 mV | 2.0% | 348 mAh | 348 mA | >100% → **D=1** | ~204 min (extend CV) |

**Observations:**
- For well-maintained packs with ΔV < 20 mV: balancing converges within a standard CV phase at ~70% duty or less.
- For ΔV > 50 mV: run at 100% duty (no PWM) and extend the CV phase until convergence is measured.
- For ΔV > 100 mV: the pack has a significant capacity mismatch. Multiple charge cycles will be needed. Consider investigating cell health.

### 12.5 The Adaptive PWM Algorithm (Proportional to Spread)

Rather than a fixed duty cycle, scale it proportionally to the measured spread. This maximises balancing speed when spread is large, and reduces heat generation as the pack converges:

```c
#define ALPHA_MV_PER_SOC_PCT  50.0f   // OCV slope (mV / % SOC) — measure from cell datasheet
#define C_GROUP_MAH           17400.0f // 3P group capacity (mAh)
#define R_BAL                 41.0f   // balancing resistor (Ω)
#define T_CV_HOURS            1.0f    // available CV phase time (hours) — conservative
#define V_BAL_FULL            4.2f    // cell voltage during CV (V)

// Call this once per balance iteration, after reading cell voltages
uint8_t compute_bal_pwm_duty(float delta_v_mv)
{
    // I_bal_max at current cell voltage
    float i_bal_max_ma = (V_BAL_FULL * 1000.0f) / R_BAL;  // mA

    // Minimum current to converge within T_CV_HOURS
    float i_min_ma = (delta_v_mv * C_GROUP_MAH) /
                     (ALPHA_MV_PER_SOC_PCT * 100.0f * T_CV_HOURS);

    float duty = i_min_ma / i_bal_max_ma;

    if (duty >= 1.0f) return 255U;         // full on (map to BAL_PWM max)
    if (duty <= 0.0f) return 0U;
    return (uint8_t)(duty * 255.0f);       // scale to 8-bit PWM register
}
```

### 12.6 The Closed-Loop Termination Condition (Guaranteed Balance)

The only way to confirm balance **without doubt** is to measure it. Add this condition to the charge termination logic:

```c
#define BALANCE_CONFIRMED_MV  5.0f   // declare balanced when max spread < 5 mV
                                     // 5 mV ≈ 0.1% SOC spread for NMC — negligible

// Inside the charge management loop:
float v_max = cell_v[0], v_min = cell_v[0];
for (int i = 1; i < 6; i++) {
    if (cell_v[i] > v_max) v_max = cell_v[i];
    if (cell_v[i] < v_min) v_min = cell_v[i];
}
float spread_mv = (v_max - v_min) * 1000.0f;

uint8_t balance_confirmed = (spread_mv < BALANCE_CONFIRMED_MV);

// Charge termination is only valid when:
//   (a) charger current has tapered (normal CC-CV termination), AND
//   (b) cell spread is confirmed below ε
if (charger_current_below_cutoff && balance_confirmed) {
    disable_charge_interrupt();   // signal charger to stop
}
// If charger taper happens before balance: stay in CV, keep balancing
// The LG M58T can safely hold at 4.2V for an additional 30-60 min
```

**Charge termination state machine:**

```
CHARGING_CC ──► CHARGING_CV         when: any cell hits 4.20 V
CHARGING_CV ──► BALANCING_EXTENSION when: charger tapers BUT spread > 5 mV
BALANCING_EXTENSION ──► DONE        when: spread < 5 mV  (confirmed balanced)
CHARGING_CV ──► DONE                when: charger tapers AND spread < 5 mV already
```

### 12.7 Summary of Recommended Settings for LG M58T

| Parameter | Value | Rationale |
|---|---|---|
| Start balancing | ≥ 4.10 V per cell (charging) | Steep NMC region; SOC differences are meaningful |
| Stop balancing | ≤ 4.05 V per cell OR spread < 5 mV | Hysteresis + convergence confirmation |
| PWM duty | Proportional to spread (Section 12.5), or fixed 100% | Full duty handles worst-case in ~102 min |
| Charge termination ε | 5 mV | ~0.1% SOC; negligible capacity loss |
| Max CV extension | 60 min past normal taper | Safe for LG M58T at 4.2 V |
| OCV slope α (verify) | ~50 mV / % SOC at 4.15–4.20 V | Measure from cell OCV curve for accuracy |

> **Note on α:** The 50 mV/% SOC figure is an estimate for NMC chemistry. For maximum accuracy, measure the OCV curve of an LG M58T by slow-charging (C/20) and recording voltage vs. cumulative capacity in the 4.10–4.20 V range. The actual slope directly sets the precision of the I_min calculation.

---

*Report updated 2026-03-17 · Schematic revision 2026-03-14 · LG M58T (21700 NMC) · GLV_BMS_1 firmware · STM32F446RE + TLE9012DQU*
