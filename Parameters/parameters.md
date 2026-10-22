<div dir="rtl">

# פרמטרים והגדרות

יש שלושה סוגים של פרמטרים: הגדרות שמשנים ממרכז הבקרה תוך כדי עבודה (בלי לקמפל), פרמטרים קבועים בקוד (`ESP32/parameters.h`), וסודות (`ESP32/SECRETS.h`).

## 1. הגדרות שמשנים ממרכז הבקרה, בלי קימפול

כל אלה נשמרות ב-flash של הלוח (NVS) ונשארות אחרי Reset או כיבוי. אפשר לשנות אותן גם בפקודה ב-Serial.

| הגדרה | איפה במרכז הבקרה | פקודה | טווח | ברירת מחדל |
|---|---|---|---|---|
| רשת Wi-Fi (שם וסיסמה) | Device controls ← Wi-Fi network | `WIFI_SSID <name>`, ואז `WIFI_PASSWORD <password>` | 2.4GHz בלבד, סיסמה 8-64 תווים או בלי | הרשת מ-`SECRETS.h` |
| חזרה לרשת ברירת המחדל | Wi-Fi network ← Back to default | `WIFI_FORGET` | | |
| עוצמת הרמקול | Speaker volume | `VOLUME <0-100>` | 0-100% (בקול: 10-100%) | 100% |
| השתקה | Mute | `MUTE on\|off` | | כבוי |
| בהירות ה-LED | Brightness | `LED_BRIGHTNESS <0-100>` | 0-100% | 100% |
| ספי חיישן האור (ערב, יום) | Evening from / Day from ← Apply thresholds | `LIGHT_BANDS <evening> <day>` | 0-6142 | 1000 / 3000 |
| Hands-free | המתג ליד Talk | `VAD on\|off` | | כבוי |
| רגישות ה-Hands-free | Sensitivity | `VAD_SENSITIVITY <1-5>` | 1 (רק קול חזק) עד 5 | 3 |
| שקט שמסיים משפט | Stop after silence | `VAD_HANGOVER <600-1500>` | 0.6-1.5 שניות | 1 שנייה |
| כפתור BOOT ככפתור Talk | On the board itself | `BUTTON on\|off` | | **כבוי** |
| צלילי מצב (offline / online / שאלה שנכשלה) | On the board itself | `SOUNDS on\|off` | | **כבוי** |

הגדרות זמניות, שלא נשמרות: מצב ה-LED (`LED on|off`, הלוח תמיד עולה עם LED כבוי), וערך חיישן האור המדומה (`LIGHT <0-6142>`, רק כשאין LDR מחובר).

## 2. פרמטרים קבועים בקוד: `ESP32/parameters.h`

שינוי שלהם דורש קימפול וצריבה. כולם מרוכזים בקובץ אחד, וליד כל אחד הערה שמסבירה מה הוא עושה.

| פרמטר | ערך | מה הוא קובע |
|---|---|---|
| `I2S_BCLK`, `I2S_LRC`, `I2S_DOUT`, `I2S_MIC_SERIAL_DATA` | 26, 25, 22, 21 | פיני ה-I2S (משותפים למיקרופון ולמגבר) |
| `SD_CS`, `SD_SCLK`, `SD_MISO`, `SD_MOSI` | 5, 18, 19, 23 | פיני כרטיס ה-SD |
| `LED_PIN`, `LED_ACTIVE_HIGH` | 2, 1 | ה-LED שהעוזר שולט בו |
| `TALK_BUTTON_PIN` | 0 | כפתור BOOT |
| `LIGHT_SENSOR_PIN` | (בהערה) | מוגדר: הלוח קורא LDR אמיתי. לא מוגדר: חיישן מדומה. רק פיני ADC1 (‏32-39) |
| `OPENAI_MODEL` | `gpt-realtime-2.1-mini` | המודל |
| `TIMEZONE_UTC_OFFSET_SEC` / `TIMEZONE_POSIX` | UTC+2 / (בהערה) | אזור הזמן. עם `TIMEZONE_POSIX` גם שעון קיץ |
| `kSerialBaudRate` | 115200 | חייב להתאים למרכז הבקרה |
| `kManualRecordDurationSec` | 3 | כמה זמן מקליט Talk / LISTEN / BOOT |
| `kMicSoftwareGain` | 2 | הגברה של המיקרופון לפני השליחה (x4 גרם לקיטום) |
| `kHandsFreeCooldownMs` | 600 | אחרי שהלוח מדבר, המיקרופון מחכה (כדי לא לשמוע את עצמו) |
| `kReplyTimeoutMs` | 15000 | שקט מהשרת שאחריו שאלה נכשלת |
| `kWifiBootWaitMs` / `kTimeSyncBootWaitMs` | 20000 / 15000 | כמה זמן ההדלקה מחכה ל-Wi-Fi ולשעון לפני שממשיכה בלעדיהם |
| `kOpenAiAttemptsBeforeOffline` | 5 | ניסיונות חיבור ברצף עד מצב offline |
| `kOfflineRetryIntervalMs` | 30000 | כל כמה זמן מנסים שוב במצב offline |
| `kHeartbeatIntervalMs`, `kHeartbeatPongTimeoutMs`, `kHeartbeatMissedPongs` | 15000, 10000, 2 | ping ל-OpenAI כל 15 שניות; אם 2 תשובות (pong) ברצף לא חוזרות תוך 10 שניות, החיבור נסגר ומתחבר מחדש (מזהה חיבור "תקוע" כשיש Wi-Fi בלי אינטרנט). 0 מכבה |
| `kDefaultVolumePercent`, `kDefaultLedBrightnessPercent` | 100, 100 | ברירות מחדל עד שמשנים ושומרים |
| `kDefaultEveningLightMin`, `kDefaultDayLightMin` | 1000, 3000 | ספי האור ההתחלתיים |
| `kMinVoiceVolumePercent`, `kVoiceVolumeStepPercent` | 10, 20 | העוצמה הנמוכה ביותר בקול, וכמה "louder" מגביר |
| `kMaxTimerSeconds` | 3600 | הטיימר הארוך ביותר |
| `kMaxKeptRecordings` | 5 | כמה פקודות וכמה תשובות נשמרות ב-SD |
| `kSdSpiFrequencyHz` | 4MHz | מהירות ה-SPI של כרטיס ה-SD |
| `kButtonSampleMs`, `kButtonDebounceSamples` | 5, 6 | ה-debounce של כפתור BOOT (‏30ms) |

### קבועים פנימיים שכדאי להכיר

אלה נשארו בתוך המודולים, כי הם חלק מהאלגוריתם ושינוי שלהם דורש הבנה של הקוד סביבם:

| קבוע | איפה | ערך |
|---|---|---|
| קצב הדגימה של המיקרופון | `i2s_audio_utils.cpp`, `vad_utils.h` | 24kHz (המינימום של OpenAI ל-PCM) |
| גודל חתיכת שליחה | `ESP32.ino` (`kChunkSamples`) | 2400 דגימות = 100ms |
| מאגר הניגון | `i2s_audio_utils.cpp` (`kRingBufferBytes`) | 32KB (כ-4 שניות mu-law) |
| מאגר DMA של ה-I2S | `i2s_audio_utils.cpp` | 12 x 300 דגימות = 150ms |
| ה-pre-roll של Hands-free | `vad_utils.h` (`kPreRollMs`) | 500ms |
| משפט ארוך ביותר ב-Hands-free | `vad_utils.h` (`kMaxUtteranceMs`) | 10 שניות |
| הוראות למודל ורשימת הכלים | `ESP32.ino` (`kSessionUpdateMessage`) | |

## 3. סודות: `ESP32/SECRETS.h`

| סוד | מה |
|---|---|
| `WIFI_SSID`, `WIFI_PASSWORD` | רשת ברירת המחדל (אפשר להחליף ממרכז הבקרה) |
| `OPENAI_API_KEY` | מפתח ל-OpenAI Realtime API |

הקובץ ב-`.gitignore`. בגיט נמצאת רק התבנית `ESP32/SECRETS.example.h`: מעתיקים אותה ל-`SECRETS.h` וממלאים. בלי `SECRETS.h` הקומפילציה נעצרת עם הודעה שמסבירה מה לעשות.

</div>
