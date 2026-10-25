# Calibration Instructions

Group A2, IoT course, Summer 2026. Three things in the system need calibration: the light sensor, the Hands-free sensitivity, and the microphone level. The first two are done from the control center and are saved on the board.

## 1. The light sensor (LDR)

**In the submitted system the LDR is not connected.** As the course guide instructs, the sensor value is simulated from the control center (the Light sensor slider), and the thresholds below are calibrated against the simulated value. The first part of this section (wiring and checking) is for the case where the sensor is connected.

**Wiring (only if you connect a real LDR):** an LDR and a 10kΩ resistor in series between 3.3V and GND. The midpoint connects to GPIO 34 (or to any ADC1 pin in the range 32-39, because ADC2 does not work while Wi-Fi is on).

**Check before calibrating:** upload `Unit Tests/09_light_sensor`. Cover the sensor, then shine a phone flashlight on it.
- A higher number when there is more light: the wiring is correct.
- The number goes up when you cover it: swap the LDR and the resistor.
- The range between darkness and the flashlight should be at least 1500 (out of 4095). If it is smaller, try a different resistor (4.7kΩ or 22kΩ).

**Enabling it in the firmware (only with a real LDR):** in `ESP32/parameters.h`, uncomment the line `#define LIGHT_SENSOR_PIN 34`, and upload. From then on the board reads the sensor every second, and the slider in the control center only displays the value.

**Calibrating the thresholds (without compiling):**
1. With "day" lighting in the room, write down the value in Device controls → Light sensor (`reads`). With no LDR connected, move the slider to a value that represents day.
2. Darken the room partly (evening lighting) and write it down again, then in the dark. With no LDR, move the slider accordingly.
3. Set `Evening from` between night and evening, and `Day from` between evening and day, and click **Apply thresholds**.
4. Ask `Is it day or night?` in each of the conditions and check the answer.

The thresholds are saved in flash. The scale in the firmware is 0-6142, that is, the ADC reading (0-4095) times 1.5.

## 2. Hands-free: sensitivity and silence

Two sliders, next to the Hands-free switch:

| Slider | What it sets | When to change it |
|---|---|---|
| Sensitivity (1-5) | How loud you need to speak relative to the background noise | The board does not hear quiet speech: raise it. Noises in the room set it off: lower it |
| Stop after silence (0.6-1.5 seconds) | How much silence ends a sentence | The sentence is cut off at a short pause: lengthen it. It waits too long after you have finished: shorten it |

**How to calibrate in the room:**
1. Turn on Hands-free and do not speak for 10 seconds. The level meter shows the background noise and the threshold.
2. Say a normal sentence from where the user will stand. It should start recording from the first word.
3. Knock on the table and clap. This should not start a recording. If it does, lower the sensitivity by one step.
4. Say a sentence with a pause in the middle. If it is cut off, lengthen Stop after silence.

**Calibrating without the board:** the tool `tools/vad_replay.cpp` runs the same detector on WAV recordings on the computer (for example recordings downloaded from the SD card), and shows where it would start and stop at each sensitivity. The instructions are at the top of the file.

## 3. The microphone level

The microphone gain is fixed in the code: `kMicSoftwareGain = 2` in `ESP32/parameters.h`. At x4, normal speech clipped.

**Check:** while recording, the level meter in the control center (`@@MIC_LEVEL`) should reach about -20 dBFS for normal speech from half a meter. In our measurements the median was -19 dBFS, and clipping appeared in 1 question of 26. If there is a lot of clipping (the value is close to 0), lower it to 1. If everything is weaker than -35, raise it to 3. The control center gets the gain from the board in the `@@LIMITS` line (`mic_gain`), so playing the recordings on the computer sounds like what OpenAI heard without updating anything in the page.
