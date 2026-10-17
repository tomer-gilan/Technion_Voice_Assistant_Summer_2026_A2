<div dir="rtl">

# איך הקוד בנוי - עוזר קולי על ESP32

קבוצה A2, קורס IoT, קיץ 2026. המסמך הזה מסביר את הקוד: מה כל חלק עושה, איך הלוח עובר בין מצבים, ואיך מרכז הבקרה יודע מה קורה בלוח. איך זה עובד מבחוץ מוסבר ב-[README](../README.md).

## התמונה הגדולה

<div dir="ltr">

```mermaid
flowchart LR
  MIC["INMP441 mic"] -- "I2S, 24 kHz" --> ESP["ESP32<br/>test/test.ino"]
  ESP -- "I2S, 8 kHz mu-law decoded" --> AMP["MAX98357A amp + speaker"]
  ESP -- "SPI" --> SD["SD card<br/>replies + voice commands"]
  ESP <-- "TLS WebSocket over Wi-Fi<br/>audio in, audio + tool calls out" --> AI["OpenAI Realtime API"]
  ESP <-- "USB serial, 115200<br/>@@ status lines + commands" --> GUI["Control center<br/>tools/reply_audio_app.html"]
  ESP --> LED["Onboard LED"]
```

</div>

ה-ESP32 לא מבין דיבור בעצמו. הוא מקליט, שולח, מנגן ומבצע. כל ההבנה קורית אצל OpenAI, שמחזיר אודיו ולפעמים גם בקשה להפעיל "כלי" בלוח.

## המודולים

כל מודול הוא זוג קבצים `xxx_utils.h/.cpp` בתיקייה `test/`, עם namespace משלו. הקובץ `test.ino` מחבר את כולם: הוא מחזיק את מצב השיחה ומחליט מה לעשות בכל רגע.

| מודול | מה הוא עושה |
|---|---|
| `test.ino` | הלולאה הראשית, מצב השיחה, הטיפול באירועים של OpenAI והפעלת הכלים |
| `wifi_utils` | התחברות ל-Wi-Fi לפי `config.h` |
| `openai_realtime_utils` | החיבור המאובטח ל-OpenAI, אימות, והתחברות מחדש כשהחיבור נופל |
| `realtime_json_utils` | סריקה מהירה של הודעות גדולות מהשרת בלי לפענח את כל ה-JSON |
| `i2s_audio_utils` | המיקרופון והרמקול (I2S), ה-ring buffer של הניגון ופענוח mu-law |
| `mic_capture_utils` | משימה נפרדת (FreeRTOS) שקוראת את המיקרופון בלי הפסקה |
| `vad_utils` | גלאי הקול: מחליט מתי מישהו התחיל ומתי סיים לדבר |
| `hands_free_utils` | מצב Hands-free: מקשיב לבד כשהלוח פנוי ושולח רק דיבור אמיתי |
| `board_state_utils` | ה-LED והכלי `set_board_state` |
| `light_sensor_utils` | חיישן האור (עוד מדומה) והכלי `get_time_of_day` |
| `device_controls_utils` | פקודות עוצמה, השתקה, צפצוף ו-LED מהמחשב, והכלי `set_volume` |
| `clock_timer_utils` | הכלים `get_current_time` ו-`set_timer`, הצלצול של הטיימר, וההודעה בקול כשהוא נגמר |
| `greeting_utils` | משפט פתיחה אקראי כשהחיבור מוכן |
| `settings_store_utils` | שמירת הגדרות בזיכרון הקבוע (NVS), כך שהן נשארות אחרי כיבוי |
| `sd_card_utils` | חיבור כרטיס ה-SD ובדיקה שהוא באמת כותב וקורא |
| `recording_folder_utils` | שמות הקבצים והתיקיות של ההקלטות |
| `reply_recorder_utils` | שומר כל תשובה של העוזר ל-SD בזמן שהיא מגיעה |
| `voice_command_recorder_utils` | שומר את מה שהמיקרופון שמע בכל פקודה |
| `sd_file_transfer_utils` | פקודות LIST / GET / DELETE: מרכז הבקרה מוריד הקלטות מהלוח |
| `status_report_utils` | שורות ה-`@@` למרכז הבקרה |
| `measurement_utils` | מדידות זמן (עלייה ותגובה) לכל פקודה |
| `heap_diagnostics_utils` | מדידות זיכרון בכל שלב |
| `log_utils` | מאקרו לוג עם רמות (`LOG_INFO`, `LOG_ERROR`, ...) |

## מה קורה בלולאה הראשית

הפונקציה `loop()` רצה כל הזמן ולא נתקעת באף שלב. בכל סיבוב היא:

1. מקבלת הודעות מ-OpenAI (אלא אם ה-buffer של הרמקול כמעט מלא, ואז עוצרת קצת כדי שהניגון יתפוס).
2. בודקת אם הוקלד משהו ב-Serial (מהמחשב או ממרכז הבקרה).
3. שולחת ל-OpenAI את חתיכות המיקרופון שמוכנות.
4. בודקת אם הניגון הסתיים, ואם עבר יותר מדי זמן בלי תשובה.
5. נותנת ל-Hands-free להחליט אם להתחיל להקליט, ולטיימר אם לצלצל.
6. מדווחת למרכז הבקרה אם המצב השתנה.

## המצבים של העוזר

הלוח מדווח כל שינוי מצב בשורה `@@STATE`. מרכז הבקרה צובע לפיה את העיגול הגדול.

<div dir="ltr">

```mermaid
stateDiagram-v2
  [*] --> booting
  booting --> ready: Wi-Fi, clock and OpenAI OK
  booting --> halted: a startup check failed
  ready --> listening: Talk / LISTEN
  ready --> armed: Hands-free on
  armed --> listening: speech detected
  armed --> ready: Hands-free off
  listening --> processing: 3 s recorded or speech ended
  processing --> tool: model calls a tool
  tool --> processing: tool result sent back
  processing --> speaking: reply audio arrives
  speaking --> saving: last audio played
  saving --> ready
  saving --> armed: Hands-free on
  ready --> reconnecting: connection lost
  reconnecting --> ready
```

</div>

| מצב | מה הלוח עושה |
|---|---|
| `booting` | בודק את החומרה ומתחבר. כל בדיקה מדווחת בשורת `@@STAGE` |
| `ready` | מחכה ל-Talk |
| `armed` | Hands-free דלוק: מקשיב לרעש ומחכה לדיבור |
| `listening` | מקליט ושולח ל-OpenAI |
| `processing` | ההקלטה נגמרה, מחכה לתשובה |
| `tool` | מבצע כלי (למשל מדליק LED). הפרטים אומרים איזה |
| `speaking` | מנגן את התשובה. הפרטים `greeting` או `timer` כשהוא מדבר בלי שאלו אותו |
| `saving` | מסיים לשמור את התשובה ל-SD |
| `reconnecting` | החיבור ל-OpenAI נפל ומתחבר מחדש |
| `halted` | נעצר בגלל תקלה (למשל אין מספיק זיכרון). צריך Reset |

## הפרוטוקול בין הלוח למרכז הבקרה

הלוח מדפיס ל-Serial שני סוגי שורות: לוג רגיל לבני אדם, ושורות שמתחילות ב-`@@` בשביל מרכז הבקרה. כך מרכז הבקרה לא צריך לנחש מתוך הלוג. כל שורה היא מילה אחרי `@@` ואז הפרטים, מופרדים ברווחים. הפקודה `STATUS` מדפיסה מחדש את כל המצב, וכך מרכז הבקרה מתעדכן גם אם התחבר באמצע.

### שורות סטטוס (מהלוח)

| שורה | דוגמה | מה היא אומרת |
|---|---|---|
| `@@STAGE` | `@@STAGE wifi ok 172.20.10.4` | תוצאה של בדיקת עלייה: sd, mic, speaker, wifi, time, openai |
| `@@STATE` | `@@STATE tool set_volume` | המצב של העוזר (הטבלה למעלה) |
| `@@HEARD` | `@@HEARD Turn on the light.` | מה שהמשתמש אמר, בטקסט |
| `@@SAID` | `@@SAID Done, the light is on.` | מה שהעוזר ענה, בטקסט |
| `@@RESULT` | `@@RESULT pass 4` | האם הפקודה ה-4 הצליחה, ואם לא, למה |
| `@@MIC_LEVEL` | `@@MIC_LEVEL -32` | עוצמת המיקרופון בזמן הקלטה (dBFS), 10 פעמים בשנייה |
| `@@WIFI` | `@@WIFI -58` | עוצמת ה-Wi-Fi |
| `@@LED` | `@@LED on 100 voice` | מצב ה-LED, הבהירות, ומי שינה אותו: `voice` (קול), `gui` (מרכז הבקרה) או `status` (עלייה) |
| `@@VOLUME` | `@@VOLUME 30 unmuted` | עוצמת הרמקול והאם מושתק |
| `@@BEEP` | `@@BEEP played` | צפצוף הבדיקה נוגן, או `busy` |
| `@@LIGHT` | `@@LIGHT 4000 day 1000 3000` | קריאת חיישן האור, חלק היום והספים |
| `@@TIMER` | `@@TIMER 45 60 check the oven` | 45 מתוך 60 שניות נשארו, ועל מה התזכורת (אם יש). גם `off`, ו-`done` כשהוא מצלצל |
| `@@VAD` | `@@VAD on 3 900` | Hands-free דלוק או כבוי, רגישות (1-5) וכמה מילישניות שקט מסיימות משפט |
| `@@VAD_LEVEL` | `@@VAD_LEVEL -48 -55 -40` | הרעש עכשיו, רעש הרקע והסף, בזמן ש-Hands-free מקשיב |
| `@@VAD_EVENT` | `@@VAD_EVENT discarded short 180` | דיבור התחיל (`started`), נשלח (`sent`), נזרק כי לא נשמע כמו דיבור (`discarded short\|unvoiced\|steady`), או השהיה אחרי הרבה רעש (`paused`) |
| `@@TIMING` | `@@TIMING n=4 result=pass ...` | הזמנים של פקודה אחת, לגרף ולטבלת המדידות |
| `@@BOOT_TIMING` | `@@BOOT_TIMING ... total_ms=6120` | כמה זמן לקח כל שלב בעלייה |
| `@@HEAP` | `@@HEAP free=139900 largest=110580 ...` | הזיכרון הפנוי אחרי כל שלב |

### שורות של העברת קבצים (מהלוח)

`@@LIST_BEGIN`, `@@FILE <name> <size>`, `@@LIST_END <count>` עונות ל-`LIST`. ‏`@@GET_BEGIN <name> <size>`, ואחריה הבייטים עצמם, ואז `@@GET_END <name> <crc32>` עונות ל-`GET`. מרכז הבקרה בודק את ה-CRC כדי לדעת שהקובץ הגיע שלם. ‏`@@SAVED` ו-`@@DELETED` אומרות שהקלטה נוספה או נמחקה. ‏`@@BUSY` אומרת שהלוח באמצע שיחה ולא יכול לשלוח קובץ עכשיו.

### פקודות (מהמחשב ללוח)

אפשר להקליד אותן גם ב-Serial Monitor של Arduino. מרכז הבקרה שולח את אותן פקודות.

| פקודה | מה היא עושה |
|---|---|
| `LISTEN` | מקליט 3 שניות ושולח (כמו הכפתור Talk) |
| `STATUS` | מדפיס מחדש את כל שורות הסטטוס |
| `VOLUME <0-100>`, `MUTE on\|off`, `BEEP` | הרמקול |
| `LED on\|off`, `LED_BRIGHTNESS <0-100>` | ה-LED |
| `LIGHT <0-6142>`, `LIGHT_BANDS <evening> <day>` | חיישן האור המדומה והספים שלו |
| `VAD on\|off`, `VAD_SENSITIVITY <1-5>`, `VAD_HANGOVER <600-1500>` | Hands-free |
| `LIST`, `GET <name>`, `DELETE <name>`, `DELETE_ALL [replies\|commands]` | ההקלטות ב-SD |

## כלי חדש בשלושה צעדים

ככה הוספנו את `set_volume`, `get_current_time` ו-`set_timer`:

1. מוסיפים את הכלי לרשימת הכלים בהודעת ההגדרות של הסשן (`kSessionUpdateMessage` ב-`test.ino`): שם, תיאור באנגלית למודל, וסכמת JSON של הפרמטרים.
2. כותבים פונקציה במודול המתאים שקוראת את הפרמטרים, מבצעת, מדפיסה שורת `@@` ושולחת ל-OpenAI את התוצאה (`function_call_output`) ובקשה לענות (`response.create`).
3. מוסיפים את שם הכלי ל-dispatch ב-`test.ino`. אם צריך, מלמדים את מרכז הבקרה את שורת ה-`@@` החדשה.

## זיכרון

ל-ESP32-WROOM-32 שלנו אין PSRAM. בהדלקה פנויים כ-215KB, וכשהסשן מוכן נשארים רק כ-38KB, כי החיבור המוצפן וה-WebSocket לבדם לוקחים כ-51KB. לכן לא שומרים הקלטה שלמה בזיכרון אלא שולחים כל 100ms מיד, מבקשים מ-OpenAI תשובה ב-mu-law ‏8kHz (קטנה פי 6), ומודדים את הזיכרון אחרי כל שלב (`@@HEAP`). הפירוט והמספרים ב-[README](../README.md), בחלק של המדידות.

</div>
