# Task for researcher

Research the Waveshare ESP32-S3-Touch-LCD-7 board (800x480, 7 inch, ESP32-S3-WROOM-1-N16R8) at a hardware-pin level, for porting an Arduino_GFX-based ESP32-S3 firmware to it.

Find from the Waveshare wiki (https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7), its schematic PDF, and Waveshare's demo code on GitHub (waveshareteam/ESP32-S3-Touch-LCD-7 repo):

1. RGB LCD panel: exact pin numbers for DE, VSYNC, HSYNC, PCLK, and all 16 RGB data pins; pixel clock, hsync/vsync polarities and timing used in demos; which Arduino_GFX class the demo uses (Arduino_ESP32RGBPanel?); backlight pin and whether it's PWM or via an IO expander.
2. Touch controller: model (GT911?), I2C pins, I2C address, interrupt/reset pins (possibly via an IO expander like CH422G — say so if so).
3. IO expander: does the board have a CH422G or similar? Which functions (backlight, reset, etc.) go through it?
4. Audio: does it have an audio codec/amplifier? Which chip and pins (I2S)?
5. IMU/accelerometer: any? 
6. Flash size, PSRAM size and mode (OPI?), USB: native USB-OTG on GPIO19/20 or a USB-UART bridge (CH343P?)?
7. Battery connector / power management?
8. Anything else unusual (SD card slot, CAN, RS485, RTC, GPIO availability).

Answer in English with concrete pin numbers and cite which source each group came from.

---
Update progress at: /Users/jima/Code/agent-buddy/.pi-subagents/artifacts/progress/82d529ca-fc16-42bf-9f38-b9340ae57d92/progress.md

---
**Output:**
Write your findings to exactly this path: /tmp/lcd7-research.md
This path is authoritative for this run.
Ignore any other output filename or output path mentioned elsewhere, including output destinations in the base agent prompt, system prompt, or task instructions.

## Acceptance Contract
Acceptance level: attested
Completion is not accepted from prose alone. End with a structured acceptance report.

Criteria:
- criterion-1: Return concrete findings with file paths and severity when applicable

Required evidence: review-findings, residual-risks

Finish with a fenced JSON block tagged `acceptance-report` in this shape:
Use empty arrays when no items apply; array fields contain strings unless object entries are shown.
`criteriaSatisfied[].status` must be exactly one of: satisfied, not-satisfied, not-applicable.
`commandsRun[].result` must be exactly one of: passed, failed, not-run.
```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "specific proof"
    }
  ],
  "changedFiles": [
    "src/file.ts"
  ],
  "testsAddedOrUpdated": [
    "test/file.test.ts"
  ],
  "commandsRun": [
    {
      "command": "command",
      "result": "passed",
      "summary": "short result"
    }
  ],
  "validationOutput": [
    "validation output or concise summary"
  ],
  "residualRisks": [
    "none"
  ],
  "noStagedFiles": true,
  "diffSummary": "short description of the diff",
  "reviewFindings": [
    "blocker: file.ts:12 - issue found, or no blockers"
  ],
  "manualNotes": "anything else the parent should know"
}
```